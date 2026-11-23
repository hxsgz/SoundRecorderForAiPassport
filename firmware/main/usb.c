#include "rec.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"

static bool ready;
static uint32_t active_at;
static bool send_bytes(const void *data, size_t len) {
    const char *p = data;
    while (len) {
        int n = usb_serial_jtag_write_bytes(p, len, pdMS_TO_TICKS(600));
        if (n <= 0)
            return false;
        len -= n;
        p += n;
    }
    return true;
}
static bool line(const char *s) { return send_bytes(s, strlen(s)) && send_bytes("\n", 1); }
static bool listing(const char *id, size_t size, void *unused) {
    char s[72];
    snprintf(s, sizeof(s), "CLIP %s %u", id, (unsigned)size);
    return line(s);
}
static void transfer(const char *id, char *ack) {
    char digest[65], p[PATH_LEN], s[110];
    size_t size = 0;
    if (!digest_clip(id, digest, &size)) {
        line("ERR READ");
        return;
    }
    clip_path(p, id);
    if (ack) {
        if (strcmp(digest, ack) != 0) {
            line("ERR CHECKSUM");
            return;
        }
        if (unlink(p) != 0) {
            line("ERR DELETE");
            return;
        }
        screen_clips_changed();
        refresh_space();
        if (!storage_full && strcmp(fault, "FULL - USB export") == 0)
            fault = "";
        line("OK");
        return;
    }
    FILE *f = fopen(p, "rb");
    if (!f) {
        line("ERR READ");
        return;
    }
    snprintf(s, sizeof(s), "DATA %u %s", (unsigned)size, digest);
    bool ok = line(s);
    uint8_t buf[1024];
    size_t left = size;
    while (ok && left) {
        size_t want = left < sizeof(buf) ? left : sizeof(buf);
        ok = fread(buf, 1, want, f) == want && send_bytes(buf, want);
        left -= want;
    }
    fclose(f);
    if (ok)
        line("END");
}
static void command(char *s) {
    char reply[160], *save = NULL, *op = strtok_r(s, " ", &save);
    if (!op)
        return;
    char *arg = strtok_r(NULL, " ", &save), *hash = strtok_r(NULL, " ", &save);
    if (!strcmp(op, "PPR1")) {
        snprintf(reply, sizeof(reply), "PASSPORT1 %s %s %lu %s", device_id, boot_id,
                 (unsigned long)uptime_ms(), mounted ? "READY" : "UNMOUNTED");
        line(reply);
        return;
    }
    if (!strcmp(op, "BEGIN")) {
        stop_playback();
        close_recording();
        exporting = true;
        active_at = uptime_ms();
        line("OK");
        return;
    }
    if (!exporting) {
        line("ERR SESSION");
        return;
    }
    active_at = uptime_ms();
    if (!strcmp(op, "END")) {
        retry_storage();
        exporting = false;
        line("OK");
    } else if (!strcmp(op, "CLOCK") && arg) {
        char *end;
        unsigned long seconds = strtoul(arg, &end, 10);
        line(*arg && !*end && seconds < 86400 && sync_clock(seconds) ? "OK" : "ERR CLOCK");
    } else if (!strcmp(op, "INIT") && arg && strcmp(arg, device_id) == 0 && !mounted) {
        line(initialize_clips() ? "OK" : "ERR STORAGE");
    } else if (!mounted)
        line("ERR UNMOUNTED");
    else if (!strcmp(op, "LIST"))
        line(visit_clips(listing, NULL) ? "END" : "ERR LIST");
    else if (!strcmp(op, "GET") && arg && valid_id(arg))
        transfer(arg, NULL);
    else if (!strcmp(op, "ACK") && arg && valid_id(arg) && hash && strlen(hash) == 64)
        transfer(arg, hash);
    else
        line("ERR COMMAND");
    active_at = uptime_ms();
}
void usb_init(void) {
    usb_serial_jtag_driver_config_t cfg = {.rx_buffer_size = 512, .tx_buffer_size = 2048};
    ready = usb_serial_jtag_driver_install(&cfg) == ESP_OK;
    if (!ready) {
        fault = "USB ERROR - restart";
        screen_warning();
    }
}
void usb_poll(void) {
    static char cmd[128];
    static size_t used;
    static bool discard;
    if (!ready)
        return;
    if (exporting && uptime_ms() - active_at > 15000)
        exporting = false;
    char buf[128];
    int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), 0);
    for (int i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n') {
            cmd[used] = 0;
            if (!discard)
                command(cmd);
            used = 0;
            discard = false;
        } else if (c != '\r' && !discard) {
            if (used + 1 >= sizeof(cmd) || c < 32 || c > 126)
                discard = true;
            else
                cmd[used++] = c;
        }
    }
}
