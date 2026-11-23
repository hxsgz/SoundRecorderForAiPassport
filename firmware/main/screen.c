#include "screen_view.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "esp_lcd_panel_ops.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>

static QueueHandle_t keys;
static screen_mode_t mode = SCREEN_OFF;
static cable_filter_t cable;
static uint32_t touched, last_paint, battery_at;
static bool ready, battery_ok, pending_warning, clips_changed = true, low_notified;
static bool manually_blank;
static int battery_level = -1;
static const char *notice;
static char chosen[ID_LEN + 1];
static clip_row_t rows[ROW_COUNT], newer[ROW_COUNT - 1], older[ROW_COUNT - 1], current;
static unsigned count, above_count;

void screen_clips_changed(void) { clips_changed = true; }
void screen_warning(void) { pending_warning = true; }
static void key_cb(bsp_btn_t key, bsp_btn_ev_t ev, void *user) {
    if (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG) return;
    unsigned v = key + (ev == BSP_BTN_LONG ? 8 : 0);
    xQueueSend(keys, &v, 0);
}
static bool find_newest(const char *id, size_t size, void *unused) {
    if (strcmp(id, chosen) > 0) strcpy(chosen, id);
    return true;
}
static void keep_neighbor(clip_row_t *side, const char *id, size_t bytes, bool above) {
    for (unsigned i = 0; i < ROW_COUNT - 1; i++) {
        int order = strcmp(id, side[i].id);
        if (!*side[i].id || (above ? order < 0 : order > 0)) {
            memmove(side + i + 1, side + i, (ROW_COUNT - 2 - i) * sizeof(*side));
            strcpy(side[i].id, id);
            side[i].bytes = bytes;
            break;
        }
    }
}
static bool neighbors(const char *id, size_t size, void *unused) {
    count++;
    int order = strcmp(id, chosen);
    if (order == 0) {
        strcpy(current.id, id);
        current.bytes = size;
    } else {
        if (order > 0) above_count++;
        keep_neighbor(order > 0 ? newer : older, id, size, order > 0);
    }
    return true;
}
static void select_item(bool newest) {
    if (newest) {
        chosen[0] = 0;
        if (mounted) visit_clips(find_newest, NULL);
    }
    memset(newer, 0, sizeof(newer));
    memset(older, 0, sizeof(older));
    memset(rows, 0, sizeof(rows));
    memset(&current, 0, sizeof(current));
    count = above_count = 0;
    clips_changed = false;
    if (mounted) visit_clips(neighbors, NULL);
    if (!current.id[0]) {
        if (!newest && count) select_item(true);
        else chosen[0] = 0;
        return;
    }
    unsigned below_count = count - above_count - 1;
    unsigned above = below_count < 2 ? 4 - below_count : 2;
    if (above > above_count) above = above_count;
    for (unsigned i = 0; i < above; i++) rows[i] = newer[above - 1 - i];
    rows[above] = current;
    for (unsigned i = above + 1; i < ROW_COUNT; i++) rows[i] = older[i - above - 1];
}
static void paint(void) {
    if (clips_changed) select_item(!*chosen);
    bool alert = !mounted || *fault || storage_full || available < 300000 || (battery_level >= 0 && battery_level < 15);
    const char *status = notice ? notice : !mounted ? "存储未就绪" : *fault ? fault :
                         storage_full ? "已满，请导出" : exporting ? "正在导出" :
                         playing ? "正在播放" : movie_paused ? "观影 · 已停录" : recording ? "正在录音" :
                         available < 300000 ? "空间不足" : alert ? "电量低" : "聆听中";
    unsigned first = 0;
    while (first < ROW_COUNT && strcmp(rows[first].id, chosen)) first++;
    unsigned top_number = count ? count - above_count + first : 0;
    view_list(rows, chosen, count, top_number, status, alert, recording);
    view_battery(battery_level);
}
static bool enter(screen_mode_t next, uint32_t now) {
    if (mode == next) return true;
    if (!view_switch(next)) return false;
    if (next == SCREEN_OFF) {
        bsp_display_backlight(0);
        esp_lcd_panel_disp_on_off(bsp_display_panel(), false);
    } else {
        if (next == SCREEN_LIST) {
            notice = NULL;
            select_item(true);
            paint();
        } else if (next == SCREEN_CONFIRM) {
            view_battery(battery_level);
        }
        if (mode == SCREEN_OFF)
            esp_lcd_panel_disp_on_off(bsp_display_panel(), true);
        bsp_display_backlight(next == SCREEN_ART ? 20 : 25);
    }
    mode = next;
    touched = last_paint = now;
    return true;
}
static void handle_key(unsigned key, uint32_t now) {
    if (key == 8 + BSP_BTN_UP) {
        manually_blank = enter(SCREEN_OFF, now);
        return;
    }
    manually_blank = false;
    if (key == 8 + BSP_BTN_DOWN && mode != SCREEN_CONFIRM) {
        bool saved = toggle_movie_pause();
        enter(SCREEN_LIST, now);
        notice = saved ? (movie_paused ? "观影 · 已停录" : "已恢复监听") :
                         (movie_paused ? "本次已停录" : "本次已恢复");
        touched = now;
        paint();
        return;
    }
    if (mode == SCREEN_OFF) { enter(SCREEN_ART, now); return; }
    if (mode == SCREEN_ART) { enter(SCREEN_LIST, now); return; }
    if (mode == SCREEN_CONFIRM) {
        if (key != BSP_BTN_UP && key != BSP_BTN_DOWN) return;
        if (key == BSP_BTN_UP && now - touched < 250) return;
        bool clear = key == BSP_BTN_UP && now - touched < 5000 && !exporting;
        if (!enter(SCREEN_LIST, now)) return;
        if (clear) {
            bool ok = clear_recordings();
            notice = ok ? "录音已清空" : "清除失败";
            select_item(true);
            paint();
        }
        return;
    }
    touched = now;
    last_paint = now - 600;
    notice = NULL;
    if (key == 8 + BSP_BTN_OK) {
        if (!mounted || exporting) { notice = "存储忙"; return; }
        stop_playback();
        if (!enter(SCREEN_CONFIRM, now)) notice = "界面不可用";
    } else if (key == BSP_BTN_OK && *chosen) {
        if (playing) stop_playback();
        else play_clip(chosen);
    } else if (key == BSP_BTN_UP || key == BSP_BTN_DOWN) {
        if (clips_changed) select_item(false);
        const char *id = key == BSP_BTN_UP ? newer[0].id : older[0].id;
        if (*id) { strcpy(chosen, id); select_item(false); }
    }
}
void screen_init(void) {
    keys = xQueueCreate(8, sizeof(unsigned));
    if (!keys || bsp_button_init(key_cb, NULL) != ESP_OK) fault = "KEY ERROR";
    battery_ok = bsp_battery_init() == ESP_OK;
    if (battery_ok) battery_level = bsp_battery_soc();
    if (bsp_display_init() != ESP_OK) return;
    bsp_display_backlight(0);
    if (!bsp_lvgl_init() || !bsp_lvgl_lock(300)) return;
    ready = view_init();
    bsp_lvgl_unlock();
    if (ready) enter(SCREEN_ART, uptime_ms());
}
void screen_poll(void) {
    if (!ready) return;
    uint32_t now = uptime_ms();
    if (battery_ok && now - battery_at >= 30000) {
        battery_at = now;
        int old = battery_level;
        battery_level = bsp_battery_soc();
        if (old >= 15 && battery_level >= 0 && battery_level < 15) screen_warning();
        if (mode == SCREEN_CONFIRM) view_battery(battery_level);
    }
    bool low = mounted && available < 300000;
    if (low && !low_notified) screen_warning();
    low_notified = low;
    if (cable_update(&cable, usb_serial_jtag_is_connected(), now)) {
        if (!cable.connected) manually_blank = false;
        if (cable.connected && !manually_blank && mode != SCREEN_CONFIRM) enter(SCREEN_ART, now);
        else if (!cable.connected && mode == SCREEN_ART) enter(SCREEN_OFF, now);
    }
    if (mode == SCREEN_CONFIRM && (exporting || now - touched >= 5000))
        enter(idle_screen(cable.connected), now);
    if (pending_warning) {
        pending_warning = false;
        if (!cable.connected && !manually_blank && mode == SCREEN_OFF) enter(SCREEN_LIST, now);
        if (mode == SCREEN_LIST) last_paint = now - 600;
    }
    unsigned key;
    while (keys && xQueueReceive(keys, &key, 0) == pdTRUE) handle_key(key, now);
    if (mode == SCREEN_LIST && now - touched >= 20000) enter(idle_screen(cable.connected), now);
    else if (mode == SCREEN_ART && !cable.connected && now - touched >= 5000) enter(SCREEN_OFF, now);
    else if (mode == SCREEN_LIST && now - last_paint >= 600) { paint(); last_paint = now; }
}
