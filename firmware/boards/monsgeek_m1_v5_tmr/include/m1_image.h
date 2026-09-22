#ifndef MIDI_TYPIST_M1_IMAGE_H
#define MIDI_TYPIST_M1_IMAGE_H
#include <stdint.h>
/* Factory ID2949 bootloader's application identity check, not vendor code.
 * Exactly 14 ASCII bytes, without NUL. No boot flag/factory record is in the ELF. */
#define M1_APPLICATION_IDENTITY "AT32F405 8KMKB"
#define M1_APPLICATION_IDENTITY_BYTES 14u
#define M1_VECTOR_WORDS 128u
/* Factory IAP metadata, not executable bootloader code. The experimental
 * image arms this before peripheral startup and deliberately leaves it set:
 * the next reset enters IAP and erases the application/custom profile slots. */
#define M1_RECOVERY_FLAG_ADDRESS 0x08004800u
#define M1_RECOVERY_FLAG_VALUE 0x55aa55aau
typedef enum {
    M1_MAIN_RESET, M1_MAIN_CLOCK, M1_MAIN_COLD, M1_MAIN_RUNNING,
    M1_MAIN_GEOMETRY_FAULT, M1_MAIN_CLOCK_FAULT, M1_MAIN_TIME_FAULT,
    M1_MAIN_BOOT_FAULT, M1_MAIN_SOURCE_FAULT, M1_MAIN_DEVICE_FAULT,
    M1_MAIN_EXCEPTION, M1_MAIN_RECOVERY_FAULT
} m1_main_state_t;
enum { M1_DEVICE_SCAN=1u, M1_DEVICE_LIGHT=2u, M1_DEVICE_TRANSPORT=4u,
       M1_DEVICE_STORAGE=8u, M1_DEVICE_RADIO=16u, M1_DEVICE_POWER=32u };
/* Debugger-visible diagnostics; not a host protocol or persistent record. */
extern volatile m1_main_state_t m1_main_state;
extern volatile uint32_t m1_main_detail;
void m1_main(void) __attribute__((noreturn));
#endif
