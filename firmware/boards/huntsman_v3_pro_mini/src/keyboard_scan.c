#include "defaults.h"
#include "keyboard_scan.h"
#include "huntsman_layout.h"
#include <string.h>

static bool fixed_threshold(uint8_t profile, uint8_t key)
{
    return key == 0x1eu || key == 0x50u || key == 0x3bu ||
        (profile == 1u ? key == 0x28u || key == 0x29u :
         profile == 2u ? key == 0x1bu || key == 0x1cu : key == 0x29u || key == 0x2au);
}

static bool editor_control(uint8_t profile, uint8_t key)
{
    if ((key >= 2u && key <= 11u) || key == 0x4fu || key == 0x53u ||
        key == 0x54u || key == 0x59u) return true;
    return profile <= 2u ? key == 0x39u || key == 0x3eu || key == 0x40u || key == 0x81u :
                          key == 0x19u || key == 0x26u || key == 0x27u || key == 0x28u;
}

void keyboard_scan_thresholds(const keyboard_config_t *s, uint8_t key, optical_key_config_t *c)
{
    const bool preview = s->mode == KEY_CONFIG_ACTUATION && !editor_control(s->profile, key);
    const uint8_t level = preview ? s->actuation : s->saved_actuation;
    const uint16_t act = g_actuation_levels[level >= 1u && level <= 10u ? level : DEFAULT_ACTUATION_LEVEL];
    unsigned press = act >> 8u;
    unsigned release = preview ? (press > OPTICAL_PREVIEW_RELEASE_GAP_LEVEL ? press - OPTICAL_PREVIEW_RELEASE_GAP_LEVEL : 1u) :
                       (act <= OPTICAL_RELEASE_MIN_Q16 + OPTICAL_RELEASE_GAP_Q16 ? 1u : (act - OPTICAL_RELEASE_GAP_Q16) >> 8u);
    if (fixed_threshold(s->profile, key)) { press = OPTICAL_FIXED_PRESS_LEVEL; release = OPTICAL_FIXED_RELEASE_LEVEL; }
    if (press < OPTICAL_MIN_LEVEL) press = OPTICAL_MIN_LEVEL;
    if (press > OPTICAL_MAX_PRESS_LEVEL) press = OPTICAL_MAX_PRESS_LEVEL;
    if (release < OPTICAL_MIN_LEVEL) release = OPTICAL_MIN_LEVEL;
    if (press < release + OPTICAL_RELEASE_GAP_LEVEL) release = press == OPTICAL_MIN_LEVEL ? 1u : press - OPTICAL_RELEASE_GAP_LEVEL;
    unsigned delta = keyboard_config_rapid_q16(s) >> 8u;
    if (s->mode != KEY_CONFIG_RAPID && delta < OPTICAL_MIN_LEVEL) delta = OPTICAL_MIN_LEVEL;
    bool rapid = !fixed_threshold(s->profile, key);
    switch (key)
    {
        case 0x4b: case 0x4c: case 0x51: case 0x55: case 0x56: rapid = false; break;
        default: break;
    }
    if (s->profile <= 2u ? key == 0x39u || key == 0x3eu || key == 0x40u || key == 0x81u :
                          key == 0x39u || key == 0x3eu || key == 0x40u) rapid = false;
    *c = (optical_key_config_t){press, release, rapid && s->rapid_enabled, 0u, delta, delta, OPTICAL_RAPID_WAIT_FRAMES, OPTICAL_RAPID_WAIT_FRAMES};
}

void keyboard_scan_init(keyboard_scan_t *s, uint8_t profile)
{
    memset(s, 0, sizeof(*s));
    keyboard_engine_init(&s->engine, profile);
    s->count = profile == 3u ? 65u : profile == 2u ? 62u : profile == 1u ? 61u : 0u;
    for (unsigned i = 0; i < s->count; ++i) { s->lower[i] = OPTICAL_FALLBACK_LOWER_RAW; s->upper[i] = OPTICAL_FALLBACK_UPPER_RAW; }
}

void keyboard_scan_frame(keyboard_scan_t *s, const uint16_t *raw,
                         const uint8_t *lower, const uint8_t *upper, keyboard_scan_event_t event)
{
    if (!s->count) return;
    s->valid = true;
    for (unsigned i = 0; i < s->count; ++i)
    {
        s->raw[i] = raw[i];
        /* Production uses ((raw - 1) & 0xf000) == 0: includes 4096. */
        if (raw[i] == 0u || raw[i] > 4096u) s->valid = false;
        else if (!s->ready && s->settling < OPTICAL_SETTLING_FRAMES) s->sums[i] += raw[i];
    }
    if (!s->ready)
    {
        /* Production settling divides valid-sample sums by 128 frames,
         * not by each key's valid count. No keystrokes during settling. */
        if (s->settling < OPTICAL_SETTLING_FRAMES) { ++s->settling; return; }
        for (unsigned i = 0; i < s->count; ++i)
        {
            const uint16_t lo = lower[i * 3u] | (uint16_t)lower[i * 3u + 1u] << 8u;
            const uint16_t hi = upper[i * 3u] | (uint16_t)upper[i * 3u + 1u] << 8u;
            /* First the normal rebuild, then the settled rebuild. If the
             * second pair fails validation production keeps the first. */
            const bool first = optical_key_calibrate(lo, hi, 0u, false, &s->lower[i], &s->upper[i]);
            const bool second = optical_key_calibrate(lo, hi, s->sums[i] / OPTICAL_SETTLING_FRAMES, true,
                                                       &s->lower[i], &s->upper[i]);
            if (first || second) ++s->calibrated;
        }
        s->ready = true;
        s->valid = false; /* require a processed frame before host arming */
        return;
    }
    /* Fail closed for an incomplete/invalid frame. The main loop turns host
     * reporting off; SysEx can still inspect the raw values. */
    if (!s->valid) return;
    const uint8_t profile = s->engine.config.profile;
    for (unsigned pos = 0; pos < KEYBOARD_GRID_SIZE; ++pos)
    {
        const keyboard_grid_cell_t *cell = &g_keyboard_grid[pos];
        const uint8_t i = profile == 1u ? cell->ansi : profile == 2u ? cell->iso : cell->jis;
        if (i >= s->count) continue;
        const uint8_t key = keyboard_key_for_sensor(profile, i);
        optical_key_config_t config;
        keyboard_scan_thresholds(&s->engine.config, key, &config);
        uint8_t level = optical_key_level(s->lower[i], s->upper[i], raw[i]);
        if (level < OPTICAL_ZERO_LEVEL) level = 0u;
        s->levels[i] = level;
        const int change = optical_key_update(&s->keys[i], &config, level);
        if (change)
        {
            (void)keyboard_engine_event(&s->engine, key, change > 0);
            if (event) event(key, change > 0, level);
        }
    }
}

bool keyboard_scan_neutral(const keyboard_scan_t *s)
{
    if (!s->ready || !s->valid) return false;
    for (unsigned i = 0; i < s->count; ++i)
    {
        optical_key_config_t config;
        keyboard_scan_thresholds(&s->engine.config, keyboard_key_for_sensor(s->engine.config.profile, i), &config);
        if (s->keys[i].pressed || s->levels[i] >= config.release) return false;
    }
    return s->engine.config.mode == KEY_CONFIG_NORMAL && !s->engine.config.fn;
}
