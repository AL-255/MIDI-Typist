#ifndef KEYBOARD_MENU_H
#define KEYBOARD_MENU_H
#include "keyboard_raw.h"
#include "keyboard_lighting.h"
#include "keyboard_text.h"
#include "midi_music.h"

enum { MENU_NONE, MENU_CALIBRATION, MENU_TRIGGER, MENU_MODE, MENU_LIGHT_DOWN, MENU_LIGHT_UP,
       MENU_RAPID, MENU_RESET, MENU_LOWER, MENU_KEY, MENU_SCALE, MENU_JANKO,
       MENU_VELOCITY, MENU_VELOCITY_SET, MENU_SELECT_KEY, MENU_SELECT_SCALE };
#define MENU_OPTION_COUNT MENU_VELOCITY

typedef struct {
    uint32_t bar_at, pending_revision;
    uint8_t profile, keys[RAW_KEY_COUNT], fn, tab, c, enter, k, l, caps, r, y, n, s, e, shift;
    uint8_t option_sensors[MENU_OPTION_COUNT];
    uint16_t previous;
    uint8_t brightness, bar, pending, pending_sensor;
    uint8_t music_page, choice_sensor, selection;
    bool choice_ready;
    bool velocity_page;
    bool press_page;
    bool brightness_session;
    bool reset_confirmation, confirmation_ready;
    keyboard_text_t text;
} keyboard_menu_t;

void keyboard_menu_init(keyboard_menu_t *s);
uint8_t keyboard_menu_frame(keyboard_menu_t *s, keyboard_raw_t *raw,
                         const uint16_t *lower, const uint16_t *upper,
                         const keyboard_config_t *before, uint32_t now, bool calibration, bool lower_muted,
                         const midi_music_config_t *music, uint8_t velocity_start);
void keyboard_menu_lights(keyboard_menu_t *s, const keyboard_raw_t *raw,
                          const uint16_t *lower, const uint16_t *upper,
                          uint8_t *frame, uint32_t now, bool midi, bool calibration,
                          bool janko);
uint8_t keyboard_menu_brightness(const keyboard_menu_t *s);
/* MIDI-mode trigger point: the raw press threshold currently in effect is
 * echoed back so the page opens on the nearest selection. */
uint8_t keyboard_menu_control(uint8_t profile, uint8_t key);
void keyboard_menu_cancel(keyboard_menu_t *s);
bool keyboard_menu_thresholds(keyboard_raw_t *raw, const uint16_t *lower,
                              const uint16_t *upper);
#endif
