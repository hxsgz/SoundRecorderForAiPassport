#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { SCREEN_OFF, SCREEN_ART, SCREEN_LIST, SCREEN_CONFIRM } screen_mode_t;
typedef struct {
    bool connected, sample;
    uint32_t since;
} cable_filter_t;

static inline bool cable_update(cable_filter_t *c, bool input, uint32_t now) {
    if (input != c->sample) {
        c->sample = input;
        c->since = now;
    }
    if (c->connected == input || now - c->since < (input ? 250u : 2500u))
        return false;
    c->connected = input;
    return true;
}

static inline screen_mode_t idle_screen(bool plugged) {
    return plugged ? SCREEN_ART : SCREEN_OFF;
}
