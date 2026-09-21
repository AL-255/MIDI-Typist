#include "m1_image.h"
#include "m1_storage.h"
#include "m1_boot.h"
#include "m1_startup.h"
#include "m1_time.h"
#include "m1_hal.h"
#include "m1_lighting.h"
#include "m1_wireless.h"
#include "m1_usb_hal.h"
#include "defaults.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"

volatile m1_main_state_t m1_main_state;
volatile uint32_t m1_main_detail;
static bool wired(void)
{ return gpio_input_data_bit_read(GPIOC,GPIO_PINS_13)==RESET; }
static __attribute__((noreturn)) void halted(m1_main_state_t why,uint32_t detail)
{
    __disable_irq();m1_main_state=why;m1_main_detail=detail;
    /* No retry/reset into a potentially changed clock or host ownership.
     * This development image has no complete runtime recovery policy yet. */
    for(;;)__WFI();
}
static __attribute__((noreturn)) void stop_live(m1_main_state_t why,uint32_t detail,uint32_t now)
{
    /* Valid-clock failures only. Disconnect USB so its host drops held keys.
     * No claimed radio-host release or peer sleep: retain all power rails.
     * Full wireless release/recovery remains a runtime integration task. */
    m1_live_stop(now);
    (void)m1_usb_hw_stop();
    m1_hal_stop();m1_lighting_stop();m1_wireless_stop();m1_radio_stop();
    halted(why,detail);
}
void m1_main(void)
{
    uint16_t density=*(const volatile uint16_t *)M1_FLASH_SIZE_REGISTER;
    if(density!=M1_FLASH_SIZE_KIB)halted(M1_MAIN_GEOMETRY_FAULT,density);
    m1_main_state=M1_MAIN_CLOCK;
    m1_clock_result_t clock=m1_clock_init();
    if(clock!=M1_CLOCK_OK)halted(M1_MAIN_CLOCK_FAULT,clock);
    if(!m1_time_start())halted(M1_MAIN_TIME_FAULT,0);
    crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK,TRUE);
    gpio_init_type pin;gpio_default_para_init(&pin);
    pin.gpio_pins=GPIO_PINS_13;pin.gpio_mode=GPIO_MODE_INPUT;pin.gpio_pull=GPIO_PULL_UP;
    gpio_init(GPIOC,&pin);
    bool external=wired();
    m1_transport_t transport=external?M1_TRANSPORT_USB:M1_DEFAULT_WIRELESS_TRANSPORT;
    if(!m1_boot_begin(transport,NULL,true))halted(M1_MAIN_BOOT_FAULT,m1_boot_error());
    m1_main_state=M1_MAIN_COLD;__enable_irq();
    for(;;) {
        m1_boot_service();
        m1_boot_state_t state=m1_boot_state();
        if(state==M1_BOOT_READY)break;
        if(state==M1_BOOT_FAILED || state==M1_BOOT_CLOCK_FATAL)
            halted(M1_MAIN_BOOT_FAULT,m1_boot_error());
    }
    m1_main_state=M1_MAIN_RUNNING;
    for(;;) {
        m1_time_point_t now;
        if(!m1_time_now(&now))halted(M1_MAIN_TIME_FAULT,0);
        /* Cable changes are terminal in the development runtime, not an
         * implicit cold reboot or an unverified physical transport change. */
        if(wired()!=external)stop_live(M1_MAIN_SOURCE_FAULT,external,now.ms);
        m1_live_service(now.ms,now.us);
        uint32_t faults=(!m1_hal_healthy()?M1_DEVICE_SCAN:0u) |
            (!m1_lighting_healthy()?M1_DEVICE_LIGHT:0u) |
            (m1_live_transport_fault()?M1_DEVICE_TRANSPORT:0u) |
            (m1_live_storage_fault()?M1_DEVICE_STORAGE:0u) |
            (m1_live_transport()!=M1_TRANSPORT_USB && !m1_wireless_healthy()?M1_DEVICE_RADIO:0u);
        if(faults)stop_live(M1_MAIN_DEVICE_FAULT,faults,now.ms);
    }
}
