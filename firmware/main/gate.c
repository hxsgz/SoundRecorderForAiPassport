#include "gate.h"
#include <stdlib.h>
#include <string.h>

// Mean absolute PCM level, relative to the local DC offset.
static unsigned frame_level(const int16_t pcm[FRAME]) {
    int sum = 0;
    for (unsigned i = 0; i < FRAME; i++)
        sum += pcm[i];
    int mean = sum / FRAME;
    unsigned level = 0;
    for (unsigned i = 0; i < FRAME; i++)
        level += abs(pcm[i] - mean);
    return level / FRAME;
}

// Require periodic voicing somewhere in an utterance. The pre-roll retains
// initial consonants, and the recorder tail bridges unvoiced syllables.
static unsigned voice_period(gate_t *gate, const int16_t pcm[FRAME], bool evaluate) {
    int16_t *h = gate->pitch_history;
    memmove(h, h + 40, 56 * sizeof(*h));
    int mean = 0;
    for (int i = 0; i < 40; i++) {
        int sample = ((int)pcm[2 * i] + pcm[2 * i + 1]) / 64;
        // Reduce low-frequency contact noise before checking periodicity.
        h[56 + i] = (4 * sample - 3 * gate->previous_sample) / 4;
        gate->previous_sample = sample;
        mean += h[56 + i];
    }
    if (!evaluate)
        return 0;
    int sum_current = mean;
    mean /= 40;
    int power = 0;
    for (int i = 56; i < 96; i++) {
        int a = h[i] - mean;
        power += a * a;
    }
    if (!power)
        return 0;
    unsigned best_lag = 0;
    int best_score = 44;
    for (int lag = 9; lag <= 56; lag++) {
        int corr = 0, other = 0, sum = 0;
        for (int i = 56; i < 96; i++) {
            int b = h[i - lag];
            corr += (h[i] - mean) * b;
            other += b * b;
            sum += b;
        }
        corr -= (int)((int64_t)(sum_current - mean * 40) * sum / 40);
        other -= (int)((int64_t)sum * sum / 40);
        if (corr <= 0 || other <= 0) continue;
        int64_t energy = (int64_t)power * other;
        int64_t correlation = (int64_t)corr * corr;
        if (correlation * 100 > energy * best_score) {
            best_lag = lag;
            best_score = (int)(correlation * 100 / energy);
        }
    }
    return best_lag;
}

static bool near_period(unsigned a, unsigned b) {
    return a && b && abs((int)a - (int)b) <= (int)(b / 6 + 1);
}

static bool changing_level(gate_t *gate, unsigned level) {
    gate->levels[gate->level_index++ % 16] = level;
    unsigned low = gate->levels[0], high = low;
    for (int i = 1; i < 16; i++) {
        if (gate->levels[i] < low)
            low = gate->levels[i];
        if (gate->levels[i] > high)
            high = gate->levels[i];
    }
    return high > low + low / 2;
}

bool gate_frame(gate_t *gate, const int16_t pcm[FRAME]) {
    if (!gate->ready) {
        WebRtcVad_InitCore(&gate->model);
        WebRtcVad_set_mode_core(&gate->model, 2);
        gate->ready = true;
    }
    // Only a new positive classification can sustain the recorder's own tail.
    bool voiced = WebRtcVad_CalcVad8khz(&gate->model, pcm, FRAME) == 1;
    unsigned level = frame_level(pcm);
    // Weak VAD evidence protects audio inside a clip without opening one
    // or restarting the ten-second deadline.
    gate->possible_speech = voiced && level >= 45;
    bool changing = changing_level(gate, level);
    unsigned period = voice_period(gate, pcm, voiced && level >= 65);
    unsigned last = gate->last_period;
    bool stable = near_period(period, last) || near_period(period, last * 2) ||
                  near_period(period * 2, last);
    gate->last_period = period;
    if (!voiced || !period) {
        int delta = (int)(level * 256) - (int)gate->floor_q8;
        gate->floor_q8 += delta / (delta > 0 ? 40 : 16);
    }
    if (gate->warmup < 16) {
        gate->warmup++;
        return false;
    }
    unsigned floor = gate->floor_q8 / 256;
    bool audible = level >= 110 && level >= floor + floor / 2;
    bool clear_enough = voiced && level >= 65 && level >= floor + floor / 5;
    if (stable && changing && clear_enough) {
        if (gate->follow_votes < 10) gate->follow_votes++;
    } else if (gate->follow_votes) gate->follow_votes--;
    if (gate->follow_votes >= 7) gate->consonant_window = 22;
    else if (gate->consonant_window) gate->consonant_window--;
    gate->continuing = clear_enough && gate->consonant_window;
    if (voiced && audible && stable && changing) {
        if (gate->votes < 12)
            gate->votes++;
    } else if (gate->votes) {
        gate->votes--;
    }
    return gate->votes >= 9;
}
