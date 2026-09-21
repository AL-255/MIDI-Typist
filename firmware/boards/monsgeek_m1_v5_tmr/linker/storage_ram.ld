/* Include INSIDE SECTIONS, before any .text* wildcard. Startup must copy this
 * section to SRAM before calling the writer. The complete image linker must
 * account for its flash LMA when defining __m1_application_flash_end__. */
.m1_storage_ram : ALIGN(4) {
    __m1_storage_ram_start__ = .;
    *(.ramfunc.m1_storage*)
    *(.text.flash_*)
    __m1_storage_ram_end__ = .;
} > RAM AT> CODE
__m1_storage_ram_load__ = LOADADDR(.m1_storage_ram);
ASSERT(__m1_application_flash_end__ > 0x08005200 &&
       __m1_application_flash_end__ <= 0x08027000,
       "M1 application overlaps custom profile slots")
ASSERT(__m1_storage_ram_start__ >= 0x20000000 &&
       __m1_storage_ram_end__ <= 0x20018000,
       "M1 flash writer must execute in main SRAM")
