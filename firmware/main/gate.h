#pragma once
#include "rec.h"
#include "webrtc/common_audio/vad/vad_core.h"

typedef struct {
    VadInstT model;
    unsigned votes;
    unsigned follow_votes;
    unsigned last_period;
    unsigned consonant_window;
    bool continuing;
    bool possible_speech;
    unsigned warmup;
    unsigned floor_q8;
    int16_t pitch_history[96];
    int16_t previous_sample;
    unsigned levels[16];
    unsigned level_index;
    bool ready;
} gate_t;

bool gate_frame(gate_t *gate, const int16_t pcm[FRAME]);
