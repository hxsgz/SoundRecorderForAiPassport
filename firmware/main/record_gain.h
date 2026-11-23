#pragma once
#include <stdint.h>
#include <stddef.h>

#define RECORD_GAIN_Q8 768

// Called on the encoder input only. Use the peak of this 20 ms block to
// reduce gain before overflow; recover toward the normal gain more slowly.
static inline void apply_record_gain(int *gain_q8, int16_t *pcm, size_t count) {
    int peak = 0;
    for (size_t i = 0; i < count; i++) {
        int x = pcm[i];
        int magnitude = x < 0 ? -x : x;
        if (magnitude > peak) peak = magnitude;
    }
    int limit = peak > 10000 ? (30000 * 256) / peak : RECORD_GAIN_Q8;
    int gain = *gain_q8;
    if (gain > limit) gain = limit;
    else gain += (limit - gain + 15) / 16;
    *gain_q8 = gain;
    for (size_t i = 0; i < count; i++)
        pcm[i] = (int16_t)((int32_t)pcm[i] * gain / 256);
}
