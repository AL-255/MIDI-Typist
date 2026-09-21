/* Synthetic emulator-only memory map, deliberately not an AT32 flash image. */
ENTRY(m1_lighting_init)
MEMORY {
    CODE (rx) : ORIGIN = 0x10000000, LENGTH = 256K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
SECTIONS {
    .text : { KEEP(*(.test_exports)) *(.text*) *(.rodata*) } > CODE
    .data : { *(.data*) } > RAM
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) }
}
