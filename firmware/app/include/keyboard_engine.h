#ifndef KEYBOARD_ENGINE_H
#define KEYBOARD_ENGINE_H

#include "keyboard.h"
#include "keyboard_config.h"

typedef struct {
    keyboard_config_t config;
    keyboard_report_t report;
    uint8_t pressed[256], fn_at_press[256], modifiers[256], usages[256];
} keyboard_engine_t;

void keyboard_engine_init(keyboard_engine_t *engine, uint8_t profile);
/* Events use recovered production key IDs, not scan indices or HID usages. */
bool keyboard_engine_event(keyboard_engine_t *engine, uint8_t key, bool down);
/* Application keyboard overrides; recovered editor/reference path is unchanged. */
bool keyboard_application_event(keyboard_engine_t *engine, uint8_t key, bool down);
/* Overrides only a normal base-layer output, never physical/Fn routing. */
bool keyboard_application_mapped_event(keyboard_engine_t *engine,uint8_t key,bool down,uint8_t usage);
uint8_t keyboard_shortcut_usage(uint8_t profile, uint8_t key);
void keyboard_engine_release_all(keyboard_engine_t *engine);

#endif
