#pragma once
#include "rec.h"
#include "screen_idle.h"
#include "lvgl.h"
#define ROW_COUNT 5
typedef struct {char id[ID_LEN + 1]; size_t bytes;} clip_row_t;
bool view_init(void);
bool view_switch(screen_mode_t mode);
void view_battery(int level);
void view_list(const clip_row_t rows[ROW_COUNT], const char *chosen, unsigned count, unsigned top_number,
               const char *status, bool alert, bool active);
