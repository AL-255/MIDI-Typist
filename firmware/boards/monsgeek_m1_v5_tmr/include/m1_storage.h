#ifndef MIDI_TYPIST_M1_STORAGE_H
#define MIDI_TYPIST_M1_STORAGE_H
#include <stdbool.h>
#include <stdint.h>

/* ID2949 boot erases 70 x 0x800 bytes starting at 0x08005000. Custom
 * profiles occupy its last two pages, not any stock settings/calibration.
 * Factory-bootloader application updates erase BOTH custom profiles. */
#define M1_APPLICATION_START 0x08005000u
#define M1_APPLICATION_VECTOR 0x08005200u
#define M1_APPLICATION_END 0x08028000u
#define M1_STORAGE_PAGE_BYTES 2048u
#define M1_STORAGE_SLOT_A 0x08027000u
#define M1_STORAGE_SLOT_B 0x08027800u
#define M1_FLASH_SIZE_REGISTER 0x1ffff7e0u
#define M1_FLASH_SIZE_KIB 256u
#define M1_STORAGE_SRAM_START 0x20000000u
#define M1_STORAGE_SRAM_END 0x20018000u

typedef enum {
    M1_STORAGE_OK=0, M1_STORAGE_ARGUMENT=0x31001, M1_STORAGE_CONTEXT,
    M1_STORAGE_UNSAFE, M1_STORAGE_GEOMETRY, M1_STORAGE_LINK,
    M1_STORAGE_BUSY, M1_STORAGE_CONTROLLER, M1_STORAGE_RECORD,
    M1_STORAGE_UNLOCK, M1_STORAGE_ERASE, M1_STORAGE_PROGRAM,
    M1_STORAGE_VERIFY, M1_STORAGE_RESUME
} m1_storage_result_t;

/* Privileged foreground, no concurrent flash owner. Buffers are complete
 * pages in main SRAM. Reads never unlock or change controller state. */
uint32_t m1_storage_read(unsigned slot,uint8_t *page);
/* platform_safe is explicit proof from the owner: adequate supply, neutral
 * outputs drained, acquisition and transports quiesced. Hardware DMA/ADC/
 * timer/SPI guards are additional checks, not a replacement for that proof.
 * No arbitrary addresses, reset, option bytes, mass erase or implicit retry.
 * Completed errors relock and preserve IRQ/VTOR. Busy after the finite SDK
 * poll budget cannot safely return to flash: stop permanently in SRAM with
 * IRQs masked. An NMI/HardFault during the transaction uses the same trap.
 * The linker must place .m1_storage_ram in SRAM and define the actual flash
 * load-image end as __m1_application_flash_end__, <= M1_STORAGE_SLOT_A. */
uint32_t m1_storage_write(unsigned slot,const uint8_t *page,bool platform_safe);
uint32_t m1_storage_erase(unsigned slot,bool platform_safe);
bool m1_storage_fatal(void);
#endif
