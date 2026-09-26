#ifndef SYNTHETIC_BOARD_H
#define SYNTHETIC_BOARD_H
#include <stdint.h>
enum { SYN_PROFILE=42, SYN_COUNT=104 };
enum { SYN_FN, SYN_TAB, SYN_ENTER, SYN_SPACE, SYN_SHIFT, SYN_S, SYN_E,
       SYN_J, SYN_H, SYN_P, SYN_T, SYN_C, SYN_K, SYN_L, SYN_ESC, SYN_CAPS,
       SYN_LCTRL, SYN_LWIN, SYN_LALT, SYN_RALT, SYN_RCTRL, SYN_BACKSLASH };
void synthetic_board_init(void);
void synthetic_rate(uint32_t hz);
#endif
