/* FUN60 ID2304 application allocation. No bootloader/factory data sections.
 * Conservative 64 KiB RAM budget (within the reference's observed RAM use).
 * The final two 2 KiB application sectors are exclusively our custom store. */
ENTRY(Reset_Handler)
MEMORY {
    HEADER (r) : ORIGIN = 0x08005000, LENGTH = 512
    APP (rx) : ORIGIN = 0x08005200, LENGTH = 0x21e00
    STORE (r) : ORIGIN = 0x08027000, LENGTH = 4096
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 56K
    STACK (rw) : ORIGIN = 0x2000e000, LENGTH = 8K
}
_estack = ORIGIN(STACK)+LENGTH(STACK);
SECTIONS {
    .app_header : { KEEP(*(.app_header)) } > HEADER
    .isr_vector : { KEEP(*(.isr_vector)) } > APP
    /* Place RAM functions before the generic .text match: the official flash
     * driver and all of its local callees must execute away from a busy bank.
     * Vendor source is unchanged. Startup copies this as part of .data. */
    .data : ALIGN(4) {
        _sdata = .;
        __ramfunc_start = .;
        *(.ramfunc .ramfunc.*)
        *at32f402_405_flash.c.obj(.text .text.* .rodata .rodata.*)
        __ramfunc_end = .;
        *(.data .data.*)
        . = ALIGN(4); _edata = .;
    } > RAM AT> APP
    _sidata = LOADADDR(.data);
    .text : ALIGN(4) { *(.text .text.*) *(.rodata .rodata.*) KEEP(*(.init)) KEEP(*(.fini)) } > APP
    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > APP
    .ARM.exidx : {
        __exidx_start = .; *(.ARM.exidx* .gnu.linkonce.armexidx.*) __exidx_end = .;
    } > APP
    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > APP
    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > APP
    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > APP
    .bss (NOLOAD) : ALIGN(4) {
        _sbss = .; *(.bss .bss.*) *(COMMON) . = ALIGN(4); _ebss = .;
    } > RAM
    .custom_store (NOLOAD) : { . = . + LENGTH(STORE); } > STORE
    /DISCARD/ : { *(.note.GNU-stack) }
}
ASSERT(SIZEOF(.app_header) <= LENGTH(HEADER), "application header exceeds boot contract")
ASSERT(ADDR(.isr_vector) == 0x08005200, "application vectors moved")
ASSERT(_ebss <= ORIGIN(STACK), "RAM collides with stack")
