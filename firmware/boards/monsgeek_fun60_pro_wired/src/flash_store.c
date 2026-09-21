#include "fun60_flash.h"
#include "fun60_board.h"
#include "at32f402_405_conf.h"
#include <string.h>

#define RAM_CODE __attribute__((section(".ramfunc"),noinline))
_Static_assert(FUN60_STORE_BASE>=FUN60_VECTOR_BASE &&
    FUN60_STORE_BASE+FUN60_STORE_SLOTS*FUN60_STORE_SECTOR_BYTES==FUN60_IAP_END,
    "custom storage must fit exactly in application tail");
_Static_assert(!(FUN60_STORE_BASE%FUN60_STORE_SECTOR_BYTES),"erase alignment");
static bool fault;
static uint32_t staging[FUN60_STORE_SECTOR_BYTES/4];

/* On a genuine stuck controller, returning to flash is unsafe. Leave the
 * writer in RAM with IRQs masked. Never retry, reset into IAP, or touch another
 * page. USB will stop responding, making the failure visible to the host. */
static RAM_CODE void wait_safe_return(void)
{
    while(flash_operation_status_get()==FLASH_OPERATE_BUSY)__NOP();
}
static RAM_CODE uint32_t mutate(unsigned slot,unsigned words,bool program)
{
    uint32_t irq=__get_PRIMASK();__disable_irq();
    const uint32_t address=FUN60_STORE_BASE+slot*FUN60_STORE_SECTOR_BYTES;
    uint32_t result=0;
    /* A prior operation must be idle BEFORE unlock/erase. The official SDK
     * sector routine itself does not perform this precondition check. */
    flash_status_type status=flash_operation_status_get();
    if(status==FLASH_OPERATE_BUSY) {
        fault=true;wait_safe_return();result=FUN60_FLASH_LATCHED;
    } else {
        flash_unlock();
        flash_flag_clear(FLASH_ODF_FLAG|FLASH_PRGMERR_FLAG|FLASH_EPPERR_FLAG);
        status=flash_sector_erase(address);
        if(status!=FLASH_OPERATE_DONE)result=0x30100u+(unsigned)status;
        if(!result) for(unsigned i=0;i<FUN60_STORE_SECTOR_BYTES/4;++i)
            if(((volatile const uint32_t *)address)[i]!=UINT32_MAX) { result=FUN60_FLASH_VERIFY;break; }
        if(!result && program) for(unsigned i=0;i<words;++i) {
            status=flash_word_program(address+4*i,staging[i]);
            if(status!=FLASH_OPERATE_DONE) { result=0x30200u+(unsigned)status;break; }
            if(((volatile const uint32_t *)address)[i]!=staging[i]) { result=FUN60_FLASH_VERIFY;break; }
        }
        if(result)fault=true;
        wait_safe_return();flash_lock();
    }
    __DSB();__ISB();__set_PRIMASK(irq);
    return result;
}
uint32_t fun60_flash_read(unsigned slot,uint8_t *out,size_t length)
{
    if(slot>=FUN60_STORE_SLOTS || !out || !length || length>FUN60_STORE_SECTOR_BYTES)return FUN60_FLASH_ARGUMENT;
    if(fault)return FUN60_FLASH_LATCHED;
    memcpy(out,(const void *)(FUN60_STORE_BASE+slot*FUN60_STORE_SECTOR_BYTES),length);
    return 0;
}
uint32_t fun60_flash_write(unsigned slot,const uint8_t *data,size_t length)
{
    if(slot>=FUN60_STORE_SLOTS || !data || !length || length>sizeof(staging) || length%4)return FUN60_FLASH_ARGUMENT;
    if(fault)return FUN60_FLASH_LATCHED;
    memcpy(staging,data,length);
    return mutate(slot,length/4,true);
}
uint32_t fun60_flash_erase(unsigned slot)
{
    if(slot>=FUN60_STORE_SLOTS)return FUN60_FLASH_ARGUMENT;
    if(fault)return FUN60_FLASH_LATCHED;
    return mutate(slot,0,false);
}
