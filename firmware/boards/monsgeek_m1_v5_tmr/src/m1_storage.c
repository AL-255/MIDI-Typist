#include "m1_storage.h"
#include "m1_image.h"
#include "device_store.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"

_Static_assert(CAL_PAGE_SIZE==M1_STORAGE_PAGE_BYTES,"M1 journal/page geometry");
_Static_assert(M1_STORAGE_SLOT_A+M1_STORAGE_PAGE_BYTES==M1_STORAGE_SLOT_B &&
               M1_STORAGE_SLOT_B+M1_STORAGE_PAGE_BYTES==M1_APPLICATION_END,
               "M1 application-tail reservation");
extern uint8_t __m1_application_flash_end__,__m1_storage_ram_start__,__m1_storage_ram_end__;
#define IN_RAM __attribute__((section(".ramfunc.m1_storage"),noinline))
static volatile bool fatal;
static uint32_t emergency_vectors[16] __attribute__((aligned(512)));
static bool context(void)
{ return !__get_IPSR() && !__get_BASEPRI() && !__get_FAULTMASK() && !(__get_CONTROL()&1u); }
static bool buffer(const void *p)
{ return (uintptr_t)p>=M1_STORAGE_SRAM_START && (uintptr_t)p<=M1_STORAGE_SRAM_END-M1_STORAGE_PAGE_BYTES; }
static uint32_t address(unsigned slot)
{ return slot?M1_STORAGE_SLOT_B:M1_STORAGE_SLOT_A; }
static bool linked(void)
{
    return (uintptr_t)&__m1_application_flash_end__>M1_APPLICATION_VECTOR &&
           (uintptr_t)&__m1_application_flash_end__<=M1_STORAGE_SLOT_A &&
           (uintptr_t)&__m1_storage_ram_start__>=M1_STORAGE_SRAM_START &&
           (uintptr_t)&__m1_storage_ram_end__>=(uintptr_t)&__m1_storage_ram_start__ &&
           (uintptr_t)&__m1_storage_ram_end__<=M1_STORAGE_SRAM_END;
}
static bool active(void)
{
    /* Any bus master could fetch flash while it is unavailable. */
    for(unsigned i=0;i<7;++i)
        if(((dma_channel_type *)(DMA1_CHANNEL1_BASE+i*0x14u))->ctrl_bit.chen ||
           ((dma_channel_type *)(DMA2_CHANNEL1_BASE+i*0x14u))->ctrl_bit.chen)return true;
    return ADC1->ctrl2_bit.adcen || (TMR3->ctrl1&1u) || (TMR6->ctrl1&1u) ||
           SPI2->sts_bit.bf || SPI3->sts_bit.bf;
}
static uint32_t readable(void)
{
    if(*(const volatile uint16_t *)M1_FLASH_SIZE_REGISTER!=M1_FLASH_SIZE_KIB)
        return M1_STORAGE_GEOMETRY;
    if(!linked())return M1_STORAGE_LINK;
    if(FLASH->sts_bit.obf)return M1_STORAGE_BUSY;
    if(FLASH->sts_bit.prgmerr || FLASH->sts_bit.epperr)return M1_STORAGE_CONTROLLER;
    return M1_STORAGE_OK;
}
uint32_t m1_storage_read(unsigned slot,uint8_t *page)
{
    if(slot>1 || !buffer(page))return M1_STORAGE_ARGUMENT;
    if(!context())return M1_STORAGE_CONTEXT;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    uint32_t result=readable();
    if(!result) {
        const volatile uint8_t *source=(const volatile uint8_t *)address(slot);
        for(unsigned i=0;i<M1_STORAGE_PAGE_BYTES;++i)page[i]=source[i];
    }
    __set_PRIMASK(mask);return result;
}
/* No return into flash, vector fetch from flash, retry or speculative reset
 * after an unresolved hardware operation. A watchdog may still reset us. */
__attribute__((noreturn)) static IN_RAM void fail_stop(void)
{
    __disable_irq();SysTick->CTRL=0;fatal=true;__DSB();
    for(;;)__WFI();
}
bool m1_storage_fatal(void) { return fatal; }
static IN_RAM uint32_t transaction(uint32_t target,const uint8_t *page)
{
    flash_unlock();
    uint32_t result=M1_STORAGE_OK;
    if(FLASH->ctrl_bit.oplk)result=M1_STORAGE_UNLOCK;
    else {
        flash_flag_clear(FLASH_ODF_FLAG);
        flash_status_type status=flash_sector_erase(target);
        if(FLASH->sts_bit.obf)fail_stop();
        if(status!=FLASH_OPERATE_DONE)result=M1_STORAGE_ERASE;
        else {
            const volatile uint32_t *stored=(const volatile uint32_t *)target;
            for(unsigned i=0;i<M1_STORAGE_PAGE_BYTES/4u;++i)
                if(stored[i]!=UINT32_MAX) { result=M1_STORAGE_VERIFY;break; }
            if(!result && page) {
                for(unsigned i=0;i<M1_STORAGE_PAGE_BYTES;i+=4u) {
                    uint32_t value=page[i]|(uint32_t)page[i+1]<<8|
                        (uint32_t)page[i+2]<<16|(uint32_t)page[i+3]<<24;
                    status=flash_word_program(target+i,value);
                    if(FLASH->sts_bit.obf)fail_stop();
                    if(status!=FLASH_OPERATE_DONE) { result=M1_STORAGE_PROGRAM;break; }
                    if(stored[i/4u]!=value) { result=M1_STORAGE_VERIFY;break; }
                }
            }
        }
    }
    flash_lock();__DSB();__ISB();
    if(!FLASH->ctrl_bit.oplk)result=M1_STORAGE_CONTROLLER;
    return result;
}
static uint32_t change(unsigned slot,const uint8_t *page,bool platform_safe)
{
    if(slot>1 || (page && !buffer(page)))return M1_STORAGE_ARGUMENT;
    if(!context())return M1_STORAGE_CONTEXT;
    if(!platform_safe)return M1_STORAGE_UNSAFE;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    uint32_t result=readable();
    if(!result && active())result=M1_STORAGE_UNSAFE;
    /* Require the normal locked, idle controller. Never inherit somebody
     * else's erase/program/options mode or silently clear an error. */
    if(!result && (FLASH->ctrl!=0x80u))result=M1_STORAGE_CONTROLLER;
    if(!result && page && !device_record_valid(page))result=M1_STORAGE_RECORD;
    if(!result) {
        uint32_t vector=SCB->VTOR;
        for(unsigned i=0;i<16;++i)emergency_vectors[i]=(uintptr_t)fail_stop;
        emergency_vectors[0]=__get_MSP();
        SCB->VTOR=(uintptr_t)emergency_vectors;__DSB();__ISB();
        result=transaction(address(slot),page);
        SCB->VTOR=vector;__DSB();__ISB();
    }
    __set_PRIMASK(mask);return result;
}
uint32_t m1_storage_write(unsigned slot,const uint8_t *page,bool platform_safe)
{ return page?change(slot,page,platform_safe):M1_STORAGE_ARGUMENT; }
uint32_t m1_storage_erase(unsigned slot,bool platform_safe)
{ return change(slot,NULL,platform_safe); }

static IN_RAM uint32_t arm_recovery(void)
{
    flash_unlock();
    uint32_t result=M1_STORAGE_OK;
    if(FLASH->ctrl_bit.oplk)result=M1_STORAGE_UNLOCK;
    else {
        flash_flag_clear(FLASH_ODF_FLAG);
        flash_status_type status=flash_word_program(M1_RECOVERY_FLAG_ADDRESS,M1_RECOVERY_FLAG_VALUE);
        if(FLASH->sts_bit.obf)fail_stop();
        if(status!=FLASH_OPERATE_DONE)result=M1_STORAGE_PROGRAM;
        else if(*(const volatile uint32_t *)M1_RECOVERY_FLAG_ADDRESS!=M1_RECOVERY_FLAG_VALUE)
            result=M1_STORAGE_VERIFY;
    }
    flash_lock();__DSB();__ISB();
    if(!FLASH->ctrl_bit.oplk)result=M1_STORAGE_CONTROLLER;
    return result;
}
uint32_t m1_storage_arm_recovery(bool platform_safe)
{
    if(!context())return M1_STORAGE_CONTEXT;
    if(!platform_safe)return M1_STORAGE_UNSAFE;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    uint32_t result=readable();
    if(!result && active())result=M1_STORAGE_UNSAFE;
    if(!result && FLASH->ctrl!=0x80u)result=M1_STORAGE_CONTROLLER;
    if(!result) {
        /* A successful factory update erases this page. Refuse any other
         * contents instead of erasing boot metadata speculatively. */
        const volatile uint32_t *flag=(const volatile uint32_t *)M1_RECOVERY_FLAG_ADDRESS;
        for(unsigned i=0;i<M1_STORAGE_PAGE_BYTES/4u;++i)
            if(flag[i]!=UINT32_MAX) { result=M1_STORAGE_VERIFY;break; }
    }
    if(!result) {
        uint32_t vector=SCB->VTOR;
        for(unsigned i=0;i<16;++i)emergency_vectors[i]=(uintptr_t)fail_stop;
        emergency_vectors[0]=__get_MSP();
        SCB->VTOR=(uintptr_t)emergency_vectors;__DSB();__ISB();
        result=arm_recovery();
        SCB->VTOR=vector;__DSB();__ISB();
    }
    __set_PRIMASK(mask);return result;
}
