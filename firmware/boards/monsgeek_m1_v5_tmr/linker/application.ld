/* Application-only experimental image, with early reset-to-IAP recovery.
 * No bootloader/boot flag, factory settings or custom profile page contents. */
ENTRY(M1_Reset_Handler)
MEMORY {
    HEADER (r) : ORIGIN = 0x08005000, LENGTH = 0x200
    CODE (rx) : ORIGIN = 0x08005200, LENGTH = 0x21e00
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 96K
}
/* No FILEHDR/PHDRS in a load segment: that would extend its physical address
 * backwards into the protected bootloader merely to include ELF metadata. */
PHDRS {
    identity PT_LOAD FLAGS(4);
    vectors PT_LOAD FLAGS(4);
    ramcode PT_LOAD FLAGS(5);
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
    zero PT_LOAD FLAGS(6);
    stack PT_LOAD FLAGS(6);
}
SECTIONS {
    .m1_identity : { KEEP(*(.m1_identity)) } > HEADER :identity
    .m1_vectors : { KEEP(*(.m1_vectors)) } > CODE :vectors
    INCLUDE firmware/boards/monsgeek_m1_v5_tmr/linker/storage_ram.ld
    .text : ALIGN(4) { *(.text*) *(.rodata*) . = ALIGN(4); } > CODE :code
    .data : ALIGN(4) {
        __m1_data_start__ = .;
        *(.data*)
        . = ALIGN(4); __m1_data_end__ = .;
    } > RAM AT> CODE :data
    __m1_data_load__ = LOADADDR(.data);
    __m1_application_flash_end__ = LOADADDR(.data) + SIZEOF(.data);
    .bss (NOLOAD) : AT(ADDR(.bss)) {
        __m1_bss_start__ = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4); __m1_bss_end__ = .;
    } > RAM :zero
    .m1_stack (NOLOAD) : AT(ADDR(.m1_stack)) {
        __m1_stack_bottom__ = .;
        KEEP(*(.m1_stack))
        __m1_stack_top__ = .;
    } > RAM :stack
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) }
}
ASSERT(SIZEOF(.m1_identity) == 14, "M1 boot identity size")
ASSERT(ADDR(.m1_vectors) == 0x08005200 && SIZEOF(.m1_vectors) == 512,
       "M1 vector address/size")
ASSERT(__m1_stack_bottom__ >= __m1_bss_end__ && (__m1_stack_top__ & 7) == 0 &&
       __m1_stack_top__ <= ORIGIN(RAM) + LENGTH(RAM), "M1 stack/RAM overflow")
