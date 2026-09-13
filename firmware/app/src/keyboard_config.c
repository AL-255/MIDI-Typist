#include "defaults.h"
#include "keyboard_config.h"

#include <string.h>
#include "keyboard_layout.h"

static uint8_t valid_level(uint8_t value, uint8_t fallback)
{
    return value >= 1u && value <= 10u ? value : fallback;
}

void keyboard_config_init(keyboard_config_t *state, uint8_t profile)
{
    memset(state, 0, sizeof(*state));
    state->profile = profile;
    state->actuation = state->saved_actuation = DEFAULT_ACTUATION_LEVEL;
    state->rapid = state->saved_rapid = DEFAULT_RAPID_LEVEL;
    state->rapid_enabled = DEFAULT_RAPID_ENABLED;
    state->locked = DEFAULT_PROFILE_LOCKED;
}

static void commit(keyboard_config_t *state)
{
    if (!state->dirty) return;
    if (state->mode == KEY_CONFIG_ACTUATION) state->saved_actuation = state->actuation;
    if (state->mode == KEY_CONFIG_RAPID) state->saved_rapid = state->rapid;
    state->dirty = 0u;
    ++state->revision;
    /* The application storage adapter observes this committed state and saves
     * it in the custom tail pages; never write the original profile area. */
}

bool keyboard_config_event(keyboard_config_t *state, uint8_t key, bool down, bool fn_at_press)
{
    const keyboard_layout_t *layout=keyboard_layout(state->profile);
    if(!layout) return false;
    /* Production action 0x11/1 -> 0x200141f8, including during an editor. */
    if (key == layout->fn)
    {
        state->fn = down;
        return true;
    }
    if (state->mode == KEY_CONFIG_NORMAL)
    {
        const keyboard_action_t *action = keyboard_action(state->profile, key, fn_at_press);
        if (action == NULL || action->type != 0x11u ||
            (action->arg0 != 0x70u && action->arg0 != 0x71u)) return false;
        /* Entry is action based; both the latched layer and live FN matter.
         * Production 0x2000f41c: press only, FN live, no locked profile. */
        if (down && state->fn && !state->locked)
        {
            state->mode = action->arg0 == 0x70u ? KEY_CONFIG_ACTUATION : KEY_CONFIG_RAPID;
            if (state->mode == KEY_CONFIG_ACTUATION)
                state->actuation = valid_level(state->saved_actuation, DEFAULT_ACTUATION_LEVEL);
            else
                state->rapid = valid_level(state->saved_rapid, DEFAULT_RAPID_LEVEL);
            state->dirty = 0u;
        }
        return true;
    }

    /* Production 0x200134fc consumes ordinary key events while editing.
     * Releasing FN does not leave the editor. Number-row keys select 1..10. */
    if (!down) return true;
    const uint8_t digit=keyboard_editor_digit(state->profile,key);
    if (digit)
    {
        if (state->mode == KEY_CONFIG_ACTUATION) state->actuation = digit;
        else state->rapid = digit;
        state->dirty = 1u;
        return true;
    }
    if (key == layout->escape)
    {
        commit(state);
        state->mode = KEY_CONFIG_NORMAL;
        return true;
    }
    if (key == layout->tab)
    {
        if (!fn_at_press) return true;
        const uint8_t previous = state->mode;
        commit(state);
        state->mode = previous == KEY_CONFIG_ACTUATION ? KEY_CONFIG_NORMAL : KEY_CONFIG_ACTUATION;
        if (state->mode) state->actuation = valid_level(state->saved_actuation, DEFAULT_ACTUATION_LEVEL);
        return true;
    }
    if (key == layout->caps)
    {
        if (!fn_at_press)
        {
            if (state->mode == KEY_CONFIG_RAPID && !state->locked)
            {
                state->rapid_enabled ^= 1u;
                state->saved_rapid = state->rapid;
                ++state->revision;
            }
            return true;
        }
        if (state->locked) return true;
        const uint8_t previous = state->mode;
        commit(state);
        state->mode = previous == KEY_CONFIG_RAPID ? KEY_CONFIG_NORMAL : KEY_CONFIG_RAPID;
        if (state->mode) state->rapid = valid_level(state->saved_rapid, DEFAULT_RAPID_LEVEL);
        return true;
    }

    const int direction=keyboard_editor_step(state->profile,key);
    if (direction)
    {
        uint8_t *level = state->mode == KEY_CONFIG_ACTUATION ? &state->actuation : &state->rapid;
        if (direction > 0 && *level < 10u) ++*level;
        if (direction < 0 && *level > 1u) --*level;
        state->dirty = 1u; /* The original marks dirty even at a bound. */
    }
    return true;
}

uint16_t keyboard_config_actuation_q16(const keyboard_config_t *state)
{
    const keyboard_layout_t *layout=keyboard_layout(state->profile);
    if(!layout) return 0;
    return layout->actuation_levels[valid_level(state->mode == KEY_CONFIG_ACTUATION ?
                                        state->actuation : state->saved_actuation, DEFAULT_ACTUATION_LEVEL)];
}

uint16_t keyboard_config_release_q16(const keyboard_config_t *state)
{
    const uint16_t press = keyboard_config_actuation_q16(state);
    return press <= OPTICAL_RELEASE_MIN_Q16 + OPTICAL_RELEASE_GAP_Q16 ?
           OPTICAL_RELEASE_MIN_Q16 : press - OPTICAL_RELEASE_GAP_Q16;
}

uint16_t keyboard_config_rapid_q16(const keyboard_config_t *state)
{
    const keyboard_layout_t *layout=keyboard_layout(state->profile);
    if(!layout) return 0;
    return layout->rapid_levels[valid_level(state->mode == KEY_CONFIG_RAPID ? state->rapid : state->saved_rapid, DEFAULT_RAPID_LEVEL)];
}
