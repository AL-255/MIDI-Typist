#include "defaults.h"
#include "keyboard_menu.h"
#include "keyboard_midi.h"
#include <assert.h>

/* Link only the real initializers: no board, SDK, flash or device access. */
int main(void)
{
    keyboard_raw_t raw;
    keyboard_midi_t midi;
    keyboard_menu_t menu;
    keyboard_raw_init(&raw);
    keyboard_midi_init(&midi);
    keyboard_menu_init(&menu);
    for (unsigned i=0; i<RAW_KEY_COUNT; ++i) {
        assert(raw.press[i]==RAW_DEFAULT_PRESS);
        assert(raw.release[i]==RAW_DEFAULT_RELEASE);
    }
    assert(raw.enabled==DEFAULT_KEYBOARD_ENABLED);
    assert(raw.engine.config.saved_actuation==DEFAULT_ACTUATION_LEVEL);
    assert(raw.engine.config.saved_rapid==DEFAULT_RAPID_LEVEL);
    assert(raw.engine.config.rapid_enabled==DEFAULT_RAPID_ENABLED);
    assert(raw.engine.config.locked==DEFAULT_PROFILE_LOCKED);
    assert(midi.mode==DEFAULT_MIDI_MODE && midi.janko==DEFAULT_MIDI_JANKO);
    assert(midi.lower_muted==DEFAULT_MIDI_LOWER_MUTED);
    assert(midi.music.root==DEFAULT_MIDI_ROOT && midi.music.scale==DEFAULT_MIDI_SCALE);
    assert(midi.octave==DEFAULT_MIDI_OCTAVE);
    assert(midi.velocity_start==DEFAULT_MIDI_VELOCITY_START);
    assert(menu.brightness==DEFAULT_BRIGHTNESS_LEVEL);
    const unsigned char steps[]=DEFAULT_BRIGHTNESS_STEPS;
    assert(keyboard_menu_brightness(&menu)==steps[DEFAULT_BRIGHTNESS_LEVEL]);
    assert(keyboard_raw_press_level(1)==RAW_BOTTOM_OUT);
    assert(keyboard_raw_press_level(10)==RAW_DEFAULT_RELEASE-1);
    return 0;
}
