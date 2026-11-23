#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define CLIP_DIR "/clips"
#define ID_LEN 26
#define PATH_LEN 52
#define RATE 8000
#define FRAME 80
#define PACKET 10
#define WB_FRAME 320
#define WB_PACKET 42
#define WB_RATE 16000
#define PRE_FRAMES 24
static inline bool wideband_id(const char *id) { return id[17] == 'w'; }
bool clear_recordings(void);
size_t clip_display_bytes(const char *id, size_t stored);
void screen_clips_changed(void);
extern char boot_id[18], device_id[13];
extern const char *fault;
extern bool mounted, storage_full, playing, recording, exporting, movie_paused;
bool toggle_movie_pause(void);
extern size_t available;
uint32_t uptime_ms(void);
bool valid_id(const char *id);
void clip_path(char path[PATH_LEN], const char *id);
bool mount_clips(void);
bool initialize_clips(void);
void refresh_space(void);
void retry_storage(void);
bool visit_clips(bool (*visit)(const char *, size_t, void *), void *arg);
bool digest_clip(const char *id, char hash[65], size_t *size);
void close_recording(void);
void stop_playback(void);
void play_clip(const char *id);
bool sync_clock(unsigned second);
bool clip_time_label(const char *id, char *text, size_t cap);
void usb_init(void);
void usb_poll(void);
void screen_init(void);
void screen_poll(void);
void screen_warning(void);
