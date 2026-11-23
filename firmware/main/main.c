#include "rec.h"
#include "gate.h"
#include "clip_buffer.h"
#include "record_gain.h"
#include "bsp_audio.h"
#include "speex/speex.h"
#include "bcg729/decoder.h"
#include "esp_mac.h"
#include "esp_pm.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

char boot_id[18], device_id[13];
const char *fault = "";
bool recording, playing, exporting, movie_paused;
static FILE *out, *input;
static char recording_id[ID_LEN + 1];

static void *encoder, *wb_decoder;
static SpeexBits bits;
static char bit_buffer[WB_PACKET + 2];
static bcg729DecoderChannelContextStruct *decoder;
static gate_t gate;
static clip_buffer_t audio_buffer;
// Hold ambiguous audio until confirmed speech returns. The 10-second
// deadline bounds this queue even when every frame looks vaguely speech-like.
static char deferred[RESUME_FRAMES][WB_PACKET];
static unsigned deferred_count;
static unsigned head, filled, frames;
static uint32_t resume_after;
static unsigned discard_after_pause;
static int record_gain_q8 = RECORD_GAIN_Q8;
static bool audio_ok;
static bool write_blocked;
uint32_t uptime_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

size_t clip_display_bytes(const char *id, size_t stored) {
    return recording && !strcmp(id, recording_id) ? frames * WB_PACKET : stored;
}

static void failure(const char *message) {
    fault = message;
    screen_warning();
}
static void write_failure(const char *message) {
    write_blocked=true;
    failure(message);
}
void retry_storage(void) {
    refresh_space();
    if (write_blocked && mounted && !storage_full) {
        write_blocked=false;
        fault="";
    }
}
void close_recording(void) {
    if (out) {
        if (fclose(out) != 0)
            write_failure("WRITE ERROR - export");
        out = NULL;
    }
    if (encoder) {
        speex_encoder_destroy(encoder);
        encoder = NULL;
    }
    if (recording)
        screen_clips_changed();
    recording = false;
    audio_buffer.quiet_frames = 0;
    audio_buffer.splice_gap = 0;
    deferred_count = 0;
    frames = 0;
    filled = 0;
    gate.votes = 0;
}
void stop_playback(void) {
    if (input) {
        fclose(input);
        input = NULL;
    }
    if (wb_decoder) {
        speex_decoder_destroy(wb_decoder);
        wb_decoder = NULL;
    }
    if (decoder) {
        closeBcg729DecoderChannel(decoder);
        decoder = NULL;
    }
    if (playing)
        resume_after = uptime_ms() + 240;
    playing = false;
    filled = 0;
    gate.votes = 0;
}
bool toggle_movie_pause(void) {
    movie_paused = !movie_paused;
    stop_playback();
    close_recording();
    memset(&audio_buffer, 0, sizeof(audio_buffer));
    memset(&gate, 0, sizeof(gate));
    head = 0;
    // Drop buffered I2S samples from the pause even if USB export delayed
    // the next read. Six DMA descriptors at 240 samples fit within 320 ms.
    discard_after_pause = movie_paused ? 0 : 16;
    nvs_handle_t h;
    if (nvs_open("recorder", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_u8(h, "movie", movie_paused ? 1 : 0) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}
void play_clip(const char *id) {
    if (!audio_ok || exporting || !valid_id(id))
        return;
    close_recording();
    stop_playback();
    char p[PATH_LEN];
    clip_path(p, id);
    input = fopen(p, "rb");
    if (wideband_id(id))
        wb_decoder = speex_decoder_init(&speex_wb_mode);
    else
        decoder = initBcg729DecoderChannel();
    if (!input || (!decoder && !wb_decoder)) {
        stop_playback();
        failure("PLAY ERROR");
        return;
    }
    playing = true;
}
static void playback_frame(void) {
    char bytes[WB_PACKET];
    int16_t pcm[WB_FRAME];
    size_t packet = wb_decoder ? WB_PACKET : PACKET;
    size_t length = fread(bytes, 1, packet, input);
    if (length != packet) {
        if (length || ferror(input))
            failure("READ ERROR");
        stop_playback();
        return;
    }
    size_t samples;
    if (wb_decoder) {
        speex_bits_set_bit_buffer(&bits, bytes, packet);
        if (speex_decode_int(wb_decoder, &bits, pcm) < 0) {
            stop_playback();
            failure("DECODE ERROR");
            return;
        }
        samples = WB_FRAME;
    } else {
        int16_t narrow[FRAME];
        bcg729Decoder(decoder, (uint8_t *)bytes, PACKET, 0, 0, 0, narrow);
        for (int i = 0; i < FRAME; i++)
            pcm[i * 2] = pcm[i * 2 + 1] = narrow[i];
        samples = FRAME * 2;
    }
    if (bsp_audio_write(pcm, samples * sizeof(*pcm)) != ESP_OK) {
        stop_playback();
        failure("SPEAKER ERROR");
    }
}
static bool save_frame(int16_t *pcm, bool defer) {
    char data[WB_PACKET];
    speex_bits_init_buffer(&bits, bit_buffer, sizeof(bit_buffer));
    apply_record_gain(&record_gain_q8, pcm, WB_FRAME);
    int encoded = speex_encode_int(encoder, pcm, &bits);
    bool ok = encoded == 1 && speex_bits_nbytes(&bits) == WB_PACKET &&
              speex_bits_write(&bits, data, sizeof(data)) == WB_PACKET;
    if (ok && defer && deferred_count < RESUME_FRAMES) {
        memcpy(deferred[deferred_count++], data, sizeof(data));
        return true;
    }
    unsigned previous = frames;
    if (ok && !defer && deferred_count) {
        size_t n = fwrite(deferred, WB_PACKET, deferred_count, out);
        frames += n;
        ok = n == deferred_count;
        deferred_count = 0;
    }
    ok = ok && !defer && fwrite(data, sizeof(data), 1, out) == 1;
    if (ok) frames++;
    if (ok && frames / 50 != previous / 50) {
        ok = fflush(out) == 0 && fsync(fileno(out)) == 0;
        refresh_space();
    }
    if (!ok || storage_full) {
        close_recording();
        refresh_space();
        write_failure(storage_full ? "FULL - USB export" : "WRITE ERROR - export");
        return false;
    }
    return true;
}
static void begin_clip(void) {
    uint32_t now = uptime_ms(), lead = filled * 20;
    char id[ID_LEN + 1], path[PATH_LEN];
    snprintf(id, sizeof(id), "%sw%08lx", boot_id, (unsigned long)(now > lead ? now - lead : 0));
    clip_path(path, id);
    // Exclusive creation protects earlier recordings even if a boot identifier is repeated.
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    out = fd < 0 ? NULL : fdopen(fd, "wb");
    if (fd >= 0 && !out)
        close(fd);
    encoder = speex_encoder_init(&speex_wb_mode);
    if (encoder) {
        int quality = 5, complexity = 2;
        speex_encoder_ctl(encoder, SPEEX_SET_QUALITY, &quality);
        speex_encoder_ctl(encoder, SPEEX_SET_COMPLEXITY, &complexity);
    }
    if (!out || !encoder) {
        close_recording();
        if (fd >= 0)
            unlink(path);
        refresh_space();
        write_failure("CANNOT SAVE - USB");
        return;
    }
    record_gain_q8 = RECORD_GAIN_Q8;
    strcpy(recording_id, id);
    recording = true;
    screen_clips_changed();
    unsigned count = filled;
    for (unsigned i = 0; i < count; i++)
        if (!save_frame(audio_buffer.preroll[(head + PRE_FRAMES - count + i) % PRE_FRAMES], false))
            return;
    filled = 0;
}
static void capture_frame(void) {
    int16_t pcm[WB_FRAME];
    if (bsp_audio_read(pcm, sizeof(pcm)) != ESP_OK) {
        close_recording();
        audio_ok = false;
        failure("MIC ERROR - restart");
        return;
    }
    if (discard_after_pause) {
        discard_after_pause--;
        return;
    }
    if ((int32_t)(uptime_ms() - resume_after) < 0 || !mounted || storage_full || write_blocked)
        return;
    bool start = false, continuing = false, possible = false;
    for (int half = 0; half < 2; half++) {
        int16_t narrow[FRAME];
        for (int i = 0; i < FRAME; i++) {
            int index = half * FRAME * 2 + 2 * i;
            narrow[i] = ((int)pcm[index] + pcm[index + 1]) / 2;
        }
        start |= gate_frame(&gate, narrow);
        continuing |= gate.continuing;
        possible |= gate.possible_speech;
    }
    if (recording) {
        if (append_audio(&audio_buffer, pcm, continuing, possible, save_frame) &&
            audio_buffer.quiet_frames == RESUME_FRAMES) close_recording();
    } else {
        memcpy(audio_buffer.preroll[head], pcm, sizeof(pcm));
        head = (head + 1) % PRE_FRAMES;
        if (filled < PRE_FRAMES)
            filled++;
        if (start)
            begin_clip();
    }
}
static void identity(void) {
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    for (int i = 0; i < 6; i++)
        snprintf(device_id + i * 2, 3, "%02x", mac[i]);
    uint32_t count = 0;
    nvs_handle_t h;
    if (nvs_flash_init() == ESP_OK && nvs_open("recorder", NVS_READWRITE, &h) == ESP_OK) {
        uint8_t paused = 0;
        if (nvs_get_u8(h, "movie", &paused) == ESP_OK) movie_paused = paused == 1;
        nvs_get_u32(h, "boot", &count);
        nvs_set_u32(h, "boot", ++count);
        nvs_commit(h);
        nvs_close(h);
    }
    snprintf(boot_id, sizeof(boot_id), "%08lx-%08lx", (unsigned long)count,
             (unsigned long)esp_random());
}
void app_main(void) {
    const esp_pm_config_t power = {
        .max_freq_mhz = 160, .min_freq_mhz = 80, .light_sleep_enable = false};
    esp_pm_configure(&power);
    identity();
    mount_clips();
    screen_init();
    usb_init();
    audio_ok = bsp_audio_init() == ESP_OK && bsp_audio_set_format(WB_RATE, 16, 1) == ESP_OK;
    if (audio_ok)
        bsp_audio_set_volume(80);
    else
        failure("MIC ERROR - restart");
    if (!mounted || storage_full)
        screen_warning();
    for (;;) {
        usb_poll();
        screen_poll();
        if (exporting || !audio_ok || (movie_paused && !playing))
            vTaskDelay(pdMS_TO_TICKS(10));
        else if (playing)
            playback_frame();
        else
            capture_frame();
    }
}
