/* Test-only link probe. No application header, main loop or hardware startup. */
#include <stdint.h>
void Reset_Handler(void) { for(;;){} }
__attribute__((section(".isr_vector"),used))
const uintptr_t probe_vectors[]={0x20010000u,(uintptr_t)Reset_Handler};
