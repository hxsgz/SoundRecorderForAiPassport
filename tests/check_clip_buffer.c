#include "../firmware/main/clip_buffer.h"
#include <assert.h>

static unsigned saved[1600], count;
static unsigned buffered[RESUME_FRAMES], buffered_count;
static unsigned fail_at = 1600;
static clip_buffer_t buf;
static unsigned next_frame;

static bool collect(int16_t *pcm, bool defer) {
    unsigned id = pcm[0];
    for (unsigned j = 0; j < WB_FRAME; j++) assert(pcm[j] == (int16_t)(id + j));
    if (defer) {
        assert(buffered_count < RESUME_FRAMES);
        buffered[buffered_count++] = id;
        return true;
    }
    for (unsigned i = 0; i < buffered_count; i++) saved[count++] = buffered[i];
    buffered_count = 0;
    if (count >= fail_at) return false;
    assert(count < sizeof(saved) / sizeof(*saved));
    saved[count++] = id;
    return true;
}

static bool feed_candidate(bool voice, bool possible) {
    int16_t frame[WB_FRAME];
    for (unsigned j = 0; j < WB_FRAME; j++) frame[j] = (int16_t)(next_frame + j);
    next_frame++;
    return append_audio(&buf, frame, voice, possible, collect);
}

static bool feed(bool voice) { return feed_candidate(voice, false); }

static void check_gap(unsigned gap, unsigned lead, unsigned tail) {
    unsigned start = next_frame, before = count;
    for (unsigned i = 0; i < gap; i++) assert(feed(false));
    assert(buf.quiet_frames == gap);
    assert(feed(true) && buf.quiet_frames == 0);
    assert(count == before + lead + tail + 1);
    for (unsigned j = 0; j < lead; j++) assert(saved[before++] == start + j);
    for (unsigned j = gap - tail; j < gap; j++) assert(saved[before++] == start + j);
    assert(saved[before] == start + gap);
}

int main(void) {
    assert(RESUME_FRAMES == 500 && feed(true));
    check_gap(0, 0, 0);
    check_gap(3, 3, 0);
    check_gap(30, 30, 0);
    check_gap(31, 31, 0);
    check_gap(59, 59, 0);
    check_gap(60, 60, 0);
    check_gap(61, 30, 30);
    check_gap(100, 30, 30);
    check_gap(400, 30, 30);
    check_gap(499, 30, 30);
    // An uncertain syllable half-way through a two-second gap protects both halves.
    unsigned first_id = next_frame, initial = count;
    for (unsigned i = 0; i < 100; i++) assert(feed_candidate(false, i == 49));
    assert(buf.quiet_frames == 100 && feed(true));
    assert(count == initial + 101);
    for (unsigned i = 0; i < 101; i++) assert(saved[initial+i] == first_id+i);
    unsigned before = count;
    for (unsigned i = 1; i <= RESUME_FRAMES; i++) {
        assert(feed_candidate(false, true));
        assert(buf.quiet_frames == i);
    }
    assert(count == before + 4);
    assert(buffered_count == RESUME_FRAMES - CLIP_TAIL_FRAMES);
    buf.quiet_frames = buf.splice_gap = buffered_count = 0;
    check_gap(1, 1, 0);
    for (unsigned i = 0; i < 100; i++) assert(feed(false));
    fail_at = count + buffered_count + 2;
    assert(!feed(true) && count == fail_at);
    puts("PASS natural gaps through 1.20 s, 1.22/2/8/9.98 s cuts, weak syllable protection, 10 s close and write failure");
    return 0;
}
