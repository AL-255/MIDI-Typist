#ifndef MIDI_TYPIST_FUN60_FLASH_H
#define MIDI_TYPIST_FUN60_FLASH_H
#include <stddef.h>
#include <stdint.h>
/* Slot-only API. No arbitrary address/bootloader/factory write primitive.
 * Main context only; caller must invalidate active captures before mutation.
 * A write erases one whole 2 KiB slot, programs aligned words, then verifies.
 * Zero is success; any write failure latches until restart (no automatic retry).
 * The startup/linker MUST copy .ramfunc and the official flash driver to RAM. */
uint32_t fun60_flash_read(unsigned slot,uint8_t *out,size_t length);
uint32_t fun60_flash_write(unsigned slot,const uint8_t *data,size_t length);
uint32_t fun60_flash_erase(unsigned slot);
enum { FUN60_FLASH_ARGUMENT=0x30001, FUN60_FLASH_VERIFY, FUN60_FLASH_LATCHED };
#endif
