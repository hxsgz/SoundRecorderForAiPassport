#include "rec.h"
#include "nvs.h"
#include <stdlib.h>
#include <string.h>

// Retain local time-of-day at boot for the most recent synced sessions.
// Unsynced sessions keep their explicit uptime label.
typedef struct { char boot[18]; uint32_t second; } clock_entry_t;
static clock_entry_t entries[24];
static bool loaded;

static void load(void) {
    if (loaded) return;
    loaded = true;
    nvs_handle_t h;
    if (nvs_open("recorder", NVS_READONLY, &h) == ESP_OK) {
        size_t size = sizeof(entries);
        if (nvs_get_blob(h, "clock_v1", entries, &size) != ESP_OK || size != sizeof(entries))
            memset(entries, 0, sizeof(entries));
        nvs_close(h);
    }
}

bool sync_clock(unsigned second) {
    if (second >= 86400) return false;
    load();
    uint32_t base = (second + 86400 - (uptime_ms() / 1000) % 86400) % 86400;
    bool same = !strncmp(entries[0].boot, boot_id, 17);
    if (same && (base + 86402 - entries[0].second) % 86400 <= 4)
        return true;
    clock_entry_t next[24];
    memcpy(next, entries, sizeof(next));
    if (!same) memmove(next + 1, entries, sizeof(entries) - sizeof(*entries));
    memcpy(next[0].boot, boot_id, sizeof(boot_id));
    next[0].second = base;
    nvs_handle_t h;
    if (nvs_open("recorder", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, "clock_v1", next, sizeof(next)) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (ok) memcpy(entries, next, sizeof(entries));
    return ok;
}

bool clip_time_label(const char *id, char *text, size_t cap) {
    uint32_t seconds = strtoul(id + 18, NULL, 16) / 1000;
    load();
    for (unsigned i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        if (!strncmp(entries[i].boot, id, 17)) {
            seconds = (seconds + entries[i].second) % 86400;
            snprintf(text, cap, "%02u:%02u:%02u", (unsigned)(seconds / 3600),
                     (unsigned)(seconds / 60 % 60), (unsigned)(seconds % 60));
            return true;
        }
    }
    return false;
}
