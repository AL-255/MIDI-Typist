/* Actual cold-start/USB/radio/app binding, synthetic addresses only. Profile
 * reads are erased; no erase/program path is linked into this harness. */
#include "m1_boot.h"
#include "m1_diagnostics.h"
#include "m1_storage.h"
#include "m1_usb_hal.h"
#include "m1_usb.h"
#include <string.h>
static unsigned reads,writes,erases;
/* The full page qualification/SDK writer has a separate compiled audit. */
uint32_t m1_test_recovery_result=M1_STORAGE_VERIFY;
uint32_t __wrap_m1_storage_check_recovery(bool allow_armed)
{ return allow_armed?m1_test_recovery_result:M1_STORAGE_ARGUMENT; }
uint32_t __wrap_m1_storage_read(unsigned slot,uint8_t *page)
{
    ++reads;
    if(slot>1 || !page)return M1_STORAGE_QUIESCE;
    memset(page,255,MT_STORE_PAGE_SIZE);return 0;
}
uint32_t __wrap_m1_storage_write(unsigned slot,const uint8_t *page,bool quiescent)
{ ++writes;(void)slot;(void)page;(void)quiescent;return M1_STORAGE_QUIESCE; }
uint32_t __wrap_m1_storage_erase(unsigned slot,bool quiescent)
{ ++erases;(void)slot;(void)quiescent;return M1_STORAGE_QUIESCE; }
unsigned m1_test_boot_storage_io(void) { return reads|(writes<<16); }
unsigned m1_test_boot_storage_erases(void) { return erases; }
__attribute__((used,section(".test_exports")))
const void *const boot_exports[]={m1_boot_begin,m1_boot_service,m1_boot_state,m1_boot_error,
    m1_diagnostics_service,m1_diagnostics_runtime_fault,
    m1_live_factory_result,m1_live_transport,m1_usb_hw_running,m1_usb_hw_stop,
    m1_usb_ready,m1_live_storage_fault,m1_live_transport_fault,m1_test_boot_storage_io,
    m1_test_boot_storage_erases};
