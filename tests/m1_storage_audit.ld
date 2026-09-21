/* Synthetic audit, NOT an application image or an ownership proof on a device.
 * The fake image end tests the real board reservation contract. */
ENTRY(m1_storage_read)
MEMORY {
    CODE (rx) : ORIGIN = 0x10000000, LENGTH = 256K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
__m1_application_flash_end__ = 0x08026000;
SECTIONS {
    INCLUDE firmware/boards/monsgeek_m1_v5_tmr/linker/storage_ram.ld
    .text : { KEEP(*(.test_exports)) *(.text*) *(.rodata*) } > CODE
    .data : { *(.data*) } > RAM
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) }
}
