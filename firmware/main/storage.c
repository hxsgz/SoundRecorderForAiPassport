#include "rec.h"
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_littlefs.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"

bool mounted, storage_full;
size_t available;
static esp_vfs_littlefs_conf_t fs = {
    .base_path = CLIP_DIR, .partition_label = "clips", .format_if_mount_failed = false};
bool valid_id(const char *s) {
    if (strlen(s) != ID_LEN)
        return false;
    for (int i = 0; i < ID_LEN; i++) {
        if (i == 8 || i == 17) {
            if (s[i] != '-' && !(i == 17 && wideband_id(s)))
                return false;
        } else if (!isdigit((unsigned char)s[i]) && !(s[i] >= 'a' && s[i] <= 'f'))
            return false;
    }
    return true;
}
void clip_path(char path[PATH_LEN], const char *id) {
    snprintf(path, PATH_LEN, CLIP_DIR "/%s", id);
}
void refresh_space(void) {
    size_t total, used;
    if (!mounted || esp_littlefs_info("clips", &total, &used) != ESP_OK) {
        available = 0;
        storage_full = true;
        return;
    }
    available = total > used ? total - used : 0;
    storage_full = available < 24576;
}
bool initialize_clips(void) {
    const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x83,"clips");
    if (mounted || !p || p->address!=0x300000 || p->size!=0x500000)
        return false;
    if (esp_littlefs_format("clips") != ESP_OK)
        return false;
    mounted = esp_vfs_littlefs_register(&fs) == ESP_OK;
    refresh_space();
    return mounted;
}
bool mount_clips(void) {
    mounted = esp_vfs_littlefs_register(&fs) == ESP_OK;
    if (!mounted) {
        // Only provision a completely erased, correctly bounded recorder partition automatically.
        const esp_partition_t *p =
            esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "clips");
        if (!p || p->address != 0x300000 || p->size != 0x500000)
            return false;
        uint8_t buf[512];
        for (size_t off = 0; off < p->size; off += sizeof(buf)) {
            if (esp_partition_read(p, off, buf, sizeof(buf)) != ESP_OK)
                return false;
            for (size_t i = 0; i < sizeof(buf); i++)
                if (buf[i] != 0xff)
                    return false;
        }
        return initialize_clips();
    }
    refresh_space();
    return true;
}
bool visit_clips(bool (*visit)(const char *, size_t, void *), void *arg) {
    DIR *d = opendir(CLIP_DIR);
    if (!d)
        return false;
    bool ok = true;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!valid_id(e->d_name))
            continue;
        char p[PATH_LEN];
        struct stat st;
        clip_path(p, e->d_name);
        if (stat(p, &st) != 0 || !visit(e->d_name, st.st_size, arg)) {
            ok = false;
            break;
        }
    }
    closedir(d);
    return ok;
}
bool digest_clip(const char *id, char hash[65], size_t *size) {
    if (!valid_id(id))
        return false;
    char p[PATH_LEN];
    clip_path(p, id);
    FILE *f = fopen(p, "rb");
    if (!f)
        return false;
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    bool ok = mbedtls_sha256_starts(&ctx, 0) == 0;
    uint8_t buf[512], sum[32];
    size_t n;
    *size = 0;
    while (ok && (n = fread(buf, 1, sizeof(buf), f)) > 0) {
        ok = mbedtls_sha256_update(&ctx, buf, n) == 0;
        *size += n;
    }
    ok = ok && !ferror(f) && mbedtls_sha256_finish(&ctx, sum) == 0;
    fclose(f);
    mbedtls_sha256_free(&ctx);
    if (ok)
        for (int i = 0; i < 32; i++)
            snprintf(hash + 2 * i, 3, "%02x", sum[i]);
    return ok;
}

// Re-open between batches: deleting entries can invalidate directory offsets.
bool clear_recordings(void) {
    if (!mounted || exporting)
        return false;
    stop_playback();
    close_recording();
    bool ok = true;
    for (;;) {
        DIR *dir = opendir(CLIP_DIR);
        if (!dir) { ok = false; break; }
        char batch[8][ID_LEN + 1];
        unsigned count = 0;
        errno = 0;
        struct dirent *entry;
        while (count < 8 && (entry = readdir(dir))) {
            if (valid_id(entry->d_name))
                strcpy(batch[count++], entry->d_name);
        }
        ok = errno == 0;
        closedir(dir);
        for (unsigned i = 0; ok && i < count; i++) {
            char path[PATH_LEN];
            clip_path(path, batch[i]);
            ok = unlink(path) == 0;
        }
        if (!ok || !count)
            break;
    }
    retry_storage();
    screen_clips_changed();
    return ok;
}
