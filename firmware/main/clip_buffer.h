#pragma once
#include "rec.h"
#include <string.h>

#define RESUME_FRAMES (10 * WB_RATE / WB_FRAME)
#define PAUSE_EDGE_FRAMES 30  // 600 ms of context on each side of a cut
#define CLIP_TAIL_FRAMES 4    // 80 ms at the end of a file

// Pre-roll and pending PCM are used at different times. Only samples kept
// in the output timeline enter Speex, so its predictor stays in sync on joins.
typedef struct {
    union {
        int16_t preroll[PRE_FRAMES][WB_FRAME];
        int16_t pending[PAUSE_EDGE_FRAMES][WB_FRAME];
    };
    unsigned quiet_frames;
    unsigned splice_gap;
} clip_buffer_t;

static inline bool append_audio(clip_buffer_t *buf, int16_t pcm[WB_FRAME], bool speech,
                                bool possible_speech, bool (*save)(int16_t *, bool)) {
    buf->quiet_frames = speech ? 0 : buf->quiet_frames + 1;
    bool defer = buf->quiet_frames > CLIP_TAIL_FRAMES;
    if (!speech && !possible_speech) {
        if (++buf->splice_gap <= PAUSE_EDGE_FRAMES) return save(pcm, defer);
        unsigned slot = (buf->splice_gap - PAUSE_EDGE_FRAMES - 1) % PAUSE_EDGE_FRAMES;
        memcpy(buf->pending[slot], pcm, sizeof(buf->pending[slot]));
        return true;
    }
    unsigned first = PAUSE_EDGE_FRAMES;
    if (buf->splice_gap > 2 * PAUSE_EDGE_FRAMES) first = buf->splice_gap - PAUSE_EDGE_FRAMES;
    for (unsigned i = first; i < buf->splice_gap; i++) {
        unsigned slot = (i - PAUSE_EDGE_FRAMES) % PAUSE_EDGE_FRAMES;
        if (!save(buf->pending[slot], defer)) return false;
    }
    buf->splice_gap = 0;
    return save(pcm, defer);
}
