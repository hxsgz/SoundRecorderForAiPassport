#include "screen_view.h"
#include "bsp_display.h"
#include <string.h>
LV_FONT_DECLARE(passport_small);
LV_FONT_DECLARE(passport_large);
#define PAPER 0xf6f3ec
#define PANEL 0xe8e9e0
#define INK 0x193d37
#define MUTED 0x45645d
#define SELECTED 0x24534b
#define ORANGE 0xc56a43
#define WHITE 0xfffcf3
static lv_obj_t *page, *picture, *prompt, *total_label, *battery, *status_label, *indicator, *free_label, *empty;
static lv_obj_t *boxes[ROW_COUNT], *names[ROW_COUNT], *lengths[ROW_COUNT];
extern const uint8_t charging_start[] asm("_binary_charging_rgb565_start");
static const lv_image_dsc_t wallpaper = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
               .w = 240, .h = 320, .stride = 480},
    .data = charging_start, .data_size = 240 * 320 * 2,
};

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, int r, unsigned fill) {
    lv_obj_t *o = lv_obj_create(parent);
    if (!o) return NULL;
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, r, 0);
    return o;
}
static lv_obj_t *label(lv_obj_t *parent, int x, int y, int w, lv_text_align_t align,
                       const lv_font_t *font, unsigned color, const char *text) {
    lv_obj_t *o = lv_label_create(parent);
    if (!o) return NULL;
    lv_obj_remove_style_all(o);
    lv_obj_set_width(o, w);
    lv_obj_set_height(o, LV_SIZE_CONTENT);
    lv_obj_set_pos(o, x, y);
    lv_label_set_long_mode(o, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(o, align, 0);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_label_set_text(o, text);
    return o;
}
bool view_init(void) {
    lv_obj_t *root = lv_screen_active();
    lv_obj_remove_style_all(root);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    page = box(root, 0, 0, 240, 320, 0, PAPER);
    if (!page) return false;
    total_label = label(page, 13, 13, 59, LV_TEXT_ALIGN_LEFT, &passport_small, MUTED, "");
    if (!label(page, 66, 9, 108, LV_TEXT_ALIGN_CENTER, &passport_large, INK, "随声记")) return false;
    box(page, 12, 41, 216, 1, 0, 0xcbd1c7);
    indicator = box(page, 14, 54, 6, 6, 3, MUTED);
    status_label = label(page, 25, 47, 147, LV_TEXT_ALIGN_LEFT, &passport_small, MUTED, "");
    free_label = label(page, 167, 47, 61, LV_TEXT_ALIGN_RIGHT, &passport_small, MUTED, "");
    for (int i = 0; i < ROW_COUNT; i++) {
        boxes[i] = box(page, 10, 69 + i * 44, 220, 39, 8, PANEL);
        if (!boxes[i]) return false;
        names[i] = label(boxes[i], 11, 7, 134, LV_TEXT_ALIGN_LEFT, &passport_large, INK, "");
        lengths[i] = label(boxes[i], 143, 10, 65, LV_TEXT_ALIGN_RIGHT, &passport_small, MUTED, "");
        if (!names[i] || !lengths[i]) return false;
    }
    empty = label(page, 15, 166, 210, LV_TEXT_ALIGN_CENTER, &passport_small, MUTED, "暂无录音");
    box(page, 12, 292, 216, 1, 0, 0xcbd1c7);
    if (!label(page, 0, 299, 240, LV_TEXT_ALIGN_CENTER, &passport_small, INK,
               "长按+息屏   长按−观影   长按确认清空")) return false;
    picture = lv_image_create(root);
    if (!picture) return false;
    lv_image_set_src(picture, &wallpaper);
    lv_obj_align(picture, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(picture, LV_OBJ_FLAG_HIDDEN);
    prompt = box(root, 0, 0, 240, 320, 0, 0x141820);
    if (!prompt ||
        !label(prompt, 24, 73, 210, LV_TEXT_ALIGN_LEFT, &passport_large, WHITE, "清空全部录音？") ||
        !label(prompt, 24, 122, 210, LV_TEXT_ALIGN_LEFT, &passport_small, 0xc4caca, "清除后无法撤销") ||
        !label(prompt, 24, 184, 195, LV_TEXT_ALIGN_LEFT, &passport_large, 0xf39775, "+   清除") ||
        !label(prompt, 24, 231, 195, LV_TEXT_ALIGN_LEFT, &passport_large, WHITE, "−   保留")) return false;
    lv_obj_add_flag(prompt, LV_OBJ_FLAG_HIDDEN);
    battery = label(root, 174, 13, 53, LV_TEXT_ALIGN_RIGHT, &passport_small, MUTED, "--%");
    return total_label && battery && status_label && free_label && indicator && empty;
}
static void hidden(lv_obj_t *o, bool hide) {
    if (hide) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}
bool view_switch(screen_mode_t mode) {
    if (!bsp_lvgl_lock(100)) return false;
    hidden(picture, mode != SCREEN_ART);
    hidden(prompt, mode != SCREEN_CONFIRM);
    hidden(page, mode != SCREEN_LIST);
    hidden(battery, mode == SCREEN_ART || mode == SCREEN_OFF);
    lv_obj_set_style_text_color(battery, lv_color_hex(mode == SCREEN_CONFIRM ? WHITE : MUTED), 0);
    bsp_lvgl_unlock();
    return true;
}
static void set_text(lv_obj_t *o, const char *s) {
    if (strcmp(lv_label_get_text(o), s)) lv_label_set_text(o, s);
}
void view_battery(int level) {
    if (!bsp_lvgl_lock(60)) return;
    char s[12] = "--%";
    if (level >= 0 && level <= 100) snprintf(s, sizeof(s), "%d%%", level);
    set_text(battery, s);
    bsp_lvgl_unlock();
}
void view_list(const clip_row_t rows[ROW_COUNT], const char *chosen, unsigned count, unsigned top_number,
               const char *status, bool alert, bool active) {
    if (!bsp_lvgl_lock(80)) return;
    char s[48];
    snprintf(s, sizeof(s), "%u 段", count);
    set_text(total_label, s);
    set_text(status_label, status);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(alert || active ? ORANGE : MUTED), 0);
    snprintf(s, sizeof(s), "%u KiB", (unsigned)(available / 1024));
    set_text(free_label, s);
    hidden(empty, count != 0);
    for (int i = 0; i < ROW_COUNT; i++) {
        const char *id = rows[i].id;
        hidden(boxes[i], !*id);
        if (!*id) continue;
        bool selected = !strcmp(id, chosen);
        lv_obj_set_style_bg_color(boxes[i], lv_color_hex(selected ? SELECTED : PANEL), 0);
        lv_obj_set_style_text_color(names[i], lv_color_hex(selected ? WHITE : INK), 0);
        lv_obj_set_style_text_color(lengths[i], lv_color_hex(selected ? WHITE : MUTED), 0);
        if (!clip_time_label(id, s, sizeof(s)))
            snprintf(s, sizeof(s), "录音 %02u", top_number - (unsigned)i);
        set_text(names[i], s);
        unsigned ds = clip_display_bytes(id, rows[i].bytes) / (wideband_id(id) ? 210 : 100);
        snprintf(s, sizeof(s), "%u:%02u.%u", ds / 600, (ds / 10) % 60, ds % 10);
        set_text(lengths[i], s);
    }
    bsp_lvgl_unlock();
}
