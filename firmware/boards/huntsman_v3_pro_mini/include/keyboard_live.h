#ifndef KEYBOARD_LIVE_H
#define KEYBOARD_LIVE_H
#include <stdbool.h>
void keyboard_live_init(void);
void keyboard_live_control_bind(void);
void keyboard_live_service(void);
void keyboard_live_usb_reset(void);
bool keyboard_live_command(const char *line);
#endif
