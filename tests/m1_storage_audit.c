#include "m1_storage.h"
#include "device_store.h"
#include "m1_board.h"

static keyboard_app_t app;
static keyboard_raw_t raw;
static keyboard_midi_t midi;
static keyboard_menu_t menu;
static keyboard_calibration_t calibration;
static device_store_t journal;
static uint16_t lower[M1_KEY_COUNT],upper[M1_KEY_COUNT],samples[M1_KEY_COUNT];
/* The audit owns synthetic peripheral quiescence/power qualification. */
static uint32_t write_slot(unsigned slot,const uint8_t *p)
{ return m1_storage_write(slot,p,true); }
static uint32_t erase_slot(unsigned slot)
{ return m1_storage_erase(slot,true); }
void m1_test_store_boot(void)
{
    keyboard_app_init(&app,&raw,&midi,&menu,&calibration,NULL);
    for(unsigned i=0;i<M1_KEY_COUNT;++i) { lower[i]=1000;upper[i]=4000;samples[i]=4000; }
    keyboard_app_frame(&app,samples,M1_KEY_COUNT,M1_PROFILE,lower,upper,true,10);
    device_store_load(&journal,M1_PROFILE,M1_KEY_COUNT,lower,upper,m1_storage_read);
    (void)device_store_apply(&journal,&app);
}
bool m1_test_store_save(unsigned brightness)
{ menu.brightness=brightness;return device_store_update(&journal,&app,NULL,m1_storage_read,write_slot); }
bool m1_test_store_clear(void)
{ return device_store_clear(&journal,m1_storage_read,erase_slot); }
uint32_t m1_test_store_status(void)
{ return journal.generation | ((uint32_t)menu.brightness<<16) | ((uint32_t)journal.fault<<31); }
__attribute__((used,section(".test_exports")))
const void *const m1_storage_exports[]={m1_storage_read,m1_storage_write,m1_storage_erase,
    m1_storage_fatal,m1_test_store_boot,m1_test_store_save,m1_test_store_clear,m1_test_store_status};
