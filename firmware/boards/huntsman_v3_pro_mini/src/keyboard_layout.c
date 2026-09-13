#include "huntsman_layout.h"

#include <stddef.h>

uint8_t keyboard_key_for_sensor(uint8_t profile, uint8_t sensor)
{
    if (profile < 1u || profile > 3u || sensor >= (profile == 3u ? 65u : 60u + profile))
        return 0u;
    for (unsigned i = 0; i < KEYBOARD_GRID_SIZE; ++i)
    {
        const keyboard_grid_cell_t *cell = &g_keyboard_grid[i];
        const uint8_t index = profile == 1u ? cell->ansi : profile == 2u ? cell->iso : cell->jis;
        if (index == sensor)
        {
            /* Production 0x2000cbf0 patches positions 52/53 for ANSI/ISO. */
            if (profile != 3u && i == 52u) return KEY_ID_FN;
            if (profile != 3u && i == 53u) return 0x3eu;
            return cell->key;
        }
    }
    return 0u;
}

const keyboard_action_t *keyboard_action(uint8_t profile, uint8_t key, uint8_t fn)
{
    if (profile < 1u || profile > 3u || key == 0u) return NULL;
    const keyboard_action_t *layer = g_keyboard_base;
    if (fn) layer = profile == 1u ? g_keyboard_fn_ansi :
                    profile == 2u ? g_keyboard_fn_iso : g_keyboard_fn_jis;
    for (unsigned i = 0; i < KEYBOARD_ACTION_COUNT; ++i)
        if (layer[i].key == key) return &layer[i];
    return NULL;
}
