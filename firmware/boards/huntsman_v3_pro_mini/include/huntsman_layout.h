#ifndef HUNTSMAN_LAYOUT_H
#define HUNTSMAN_LAYOUT_H

#include "keyboard_layout.h"
#include <stddef.h>

#define KEYBOARD_GRID_SIZE 72u
#define KEYBOARD_ACTION_COUNT 135u
#define KEY_ID_FN 0x3bu
#define KEY_ID_TAB 0x10u
#define KEY_ID_CAPS 0x1eu
#define KEY_ID_ESC 0x6eu

/* IDs are production physical/logical IDs, NOT USB HID usages. */
typedef struct {
    uint8_t position, key, ansi, iso, jis;
} keyboard_grid_cell_t;

extern const keyboard_grid_cell_t g_keyboard_grid[KEYBOARD_GRID_SIZE];
extern const keyboard_action_t g_keyboard_base[KEYBOARD_ACTION_COUNT];
extern const keyboard_action_t g_keyboard_fn_ansi[KEYBOARD_ACTION_COUNT];
extern const keyboard_action_t g_keyboard_fn_iso[KEYBOARD_ACTION_COUNT];
extern const keyboard_action_t g_keyboard_fn_jis[KEYBOARD_ACTION_COUNT];
extern const uint16_t g_actuation_levels[11];
extern const uint16_t g_rapid_levels[11];

/* 1 = ANSI/61, 2 = ISO/62, 3 = JIS/65, as returned in A2 metadata byte 7. */
uint8_t keyboard_key_for_sensor(uint8_t profile, uint8_t sensor);
const keyboard_action_t *keyboard_action(uint8_t profile, uint8_t key, uint8_t fn);

#endif
