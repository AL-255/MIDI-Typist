#include "m1_image.h"
#include "m1_storage.h"
#include "m1_hal.h"
#include "m1_sleep.h"
#include "m1_usb_hal.h"
#include "defaults.h"
#include "at32f402_405.h"

void m1_fault_systick_irq(void);

extern uint32_t __m1_data_start__,__m1_data_end__,__m1_data_load__;
extern uint32_t __m1_bss_start__,__m1_bss_end__,__m1_stack_top__;
extern uint32_t __m1_storage_ram_start__,__m1_storage_ram_end__,__m1_storage_ram_load__;

__attribute__((used,section(".m1_identity")))
const char m1_application_identity[M1_APPLICATION_IDENTITY_BYTES]=M1_APPLICATION_IDENTITY;
_Static_assert(sizeof(M1_APPLICATION_IDENTITY)==M1_APPLICATION_IDENTITY_BYTES+1u,
               "boot identity excludes string terminator");
__attribute__((used,section(".m1_stack"),aligned(8)))
static uint8_t main_stack[M1_MAIN_STACK_BYTES];

__attribute__((noreturn)) void M1_Unhandled_Handler(void)
{
    __disable_irq();m1_main_state=M1_MAIN_EXCEPTION;m1_main_detail=__get_IPSR();
    /* No speculative reset/flash/rail operation from an unknown exception. */
    for(;;)__WFI();
}
__attribute__((noreturn)) void m1_reset(void)
{
    /* Reset_Handler masks interrupts before changing MSP. Do not inherit boot
     * SysTick, pending exceptions, priority grouping or enabled external IRQs. */
    SysTick->CTRL=0;SysTick->LOAD=0;SysTick->VAL=0;
    for(unsigned i=0;i<(ACC_IRQn+32u)/32u;++i) {
        NVIC->ICER[i]=UINT32_MAX;NVIC->ICPR[i]=UINT32_MAX;
    }
    SCB->ICSR=SCB_ICSR_PENDSTCLR_Msk|SCB_ICSR_PENDSVCLR_Msk;
    SCB->VTOR=M1_APPLICATION_VECTOR;
    SCB->CCR|=SCB_CCR_STKALIGN_Msk;
    NVIC_SetPriorityGrouping(3); /* four preemption bits, SDK __NVIC_PRIO_BITS */
    __set_BASEPRI(0);__set_FAULTMASK(0);__set_CONTROL(0);__DSB();__ISB();
    uint32_t *out=&__m1_data_start__;const uint32_t *in=&__m1_data_load__;
    while(out<&__m1_data_end__)*out++=*in++;
    out=&__m1_storage_ram_start__;in=&__m1_storage_ram_load__;
    while(out<&__m1_storage_ram_end__)*out++=*in++;
    out=&__m1_bss_start__;
    while(out<&__m1_bss_end__)*out++=0;
    __DSB();__ISB();
    m1_main();
}
__attribute__((naked,noreturn)) void M1_Reset_Handler(void)
{
    __asm volatile("cpsid i\n"
                   "movs r0, #0\n"
                   "msr control, r0\n"
                   "isb\n"
                   "ldr r0, =__m1_stack_top__\n"
                   "msr msp, r0\n"
                   "b m1_reset\n");
}

_Static_assert(16u+ACC_IRQn<M1_VECTOR_WORDS,"SDK IRQs must fit the vector table");
/* All unused slots trap. Designated overrides are intentional; names come
 * from the pinned device header rather than duplicated numerical IRQ IDs. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
__attribute__((used,section(".m1_vectors"),aligned(512)))
const uintptr_t m1_vectors[M1_VECTOR_WORDS]={
    [0]=(uintptr_t)&__m1_stack_top__,[1]=(uintptr_t)M1_Reset_Handler,
    [2 ... M1_VECTOR_WORDS-1]=(uintptr_t)M1_Unhandled_Handler,
    [15]=(uintptr_t)m1_fault_systick_irq,
    [16+ERTC_WKUP_IRQn]=(uintptr_t)m1_sleep_irq,
    [16+DMA1_Channel6_IRQn]=(uintptr_t)m1_hal_dma_irq,
    [16+TMR6_GLOBAL_IRQn]=(uintptr_t)m1_hal_timer_irq,
    [16+OTGHS_IRQn]=(uintptr_t)m1_usb_hw_irq,
};
#pragma GCC diagnostic pop
