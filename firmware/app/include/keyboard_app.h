#ifndef MIDI_TYPIST_KEYBOARD_APP_H
#define MIDI_TYPIST_KEYBOARD_APP_H
#include "keyboard_raw.h"
#include "keyboard_midi.h"
#include "keyboard_menu.h"
#include "keyboard_calibration.h"

typedef struct {
    /* Synchronous, single-owner callbacks. Only the board owns storage
     * addresses, erase geometry, current schema and bootloader boundaries. */
    bool (*load_calibration)(uint8_t profile,uint8_t count,uint16_t *lo,uint16_t *hi);
    bool (*save_calibration)(const keyboard_calibration_t *cal);
    bool (*clear_profile)(void);
    void (*reset_sensors)(uint8_t profile);
    void (*log)(const char *message);
} keyboard_app_ops_t;
typedef bool (*keyboard_send_fn)(const keyboard_report_t *report);
typedef struct {
    /* Separate allocations permit MCU-specific RAM placement without adding
     * section attributes or device headers to shared application code. */
    keyboard_raw_t *raw;
    keyboard_midi_t *midi;
    keyboard_menu_t *menu;
    keyboard_calibration_t *cal;
    const keyboard_app_ops_t *ops;
    keyboard_report_t sent;
    uint32_t last_frame,last_report;
    bool sent_valid,loaded,reset_pending,frame_valid;
} keyboard_app_t;

/* All methods run serially from one cooperative loop or one RTOS owner task.
 * ISRs publish acquired frames/completions to the board; never mutate app state.
 * Send callbacks return true only after taking an immutable copy/ownership. */
void keyboard_app_init(keyboard_app_t *app,keyboard_raw_t *raw,keyboard_midi_t *midi,
                       keyboard_menu_t *menu,keyboard_calibration_t *cal,
                       const keyboard_app_ops_t *ops);
void keyboard_app_invalidate(keyboard_app_t *app,uint32_t now);
bool keyboard_app_calibrate(keyboard_app_t *app,uint32_t now,bool healthy);
/* Explicitly erase custom profile/settings (Fn+R or cfg clean), then apply
 * defaults once every key is released. Retention across firmware reflashes is
 * a board/updater contract, not a guarantee made by the shared application. */
bool keyboard_app_reset_profile(keyboard_app_t *app);
void keyboard_app_frame(keyboard_app_t *app,const uint16_t *samples,uint8_t count,
                        uint8_t profile,uint16_t *lo,uint16_t *hi,bool valid,uint32_t now);
void keyboard_app_service(keyboard_app_t *app,uint32_t now,bool healthy,
                          keyboard_send_fn keyboard_send,midi_send_fn midi_send);
void keyboard_app_lights(keyboard_app_t *app,const uint16_t *lo,const uint16_t *hi,
                         uint8_t *frame,uint32_t now);
/* Common newline-stripped cfg commands. Framing, transport and presentation
 * are board/host concerns; operation validation and safety live here.
 * A recognized but malformed cfg command is consumed without changing ACK. */
bool keyboard_app_command(keyboard_app_t *app,const char *line,uint32_t now,
                          bool healthy,uint32_t *ack,uint8_t *result);
#endif
