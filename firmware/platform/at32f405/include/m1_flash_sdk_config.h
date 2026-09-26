#ifndef MIDI_TYPIST_M1_FLASH_SDK_CONFIG_H
#define MIDI_TYPIST_M1_FLASH_SDK_CONFIG_H
/* Forced include for the pinned SDK flash translation unit only. Preserve
 * vendor source/functions; replace its billion-iteration erase budget with
 * the board's finite polling policy. These are loop counts, not milliseconds. */
#include "defaults.h"
#include "at32f402_405_flash.h"
#undef ERASE_TIMEOUT
#undef PROGRAMMING_TIMEOUT
#define ERASE_TIMEOUT M1_FLASH_ERASE_WAIT_LOOPS
#define PROGRAMMING_TIMEOUT M1_FLASH_PROGRAM_WAIT_LOOPS
#endif
