#include "m1_boot.h"
#include "m1_startup.h"
#include "m1_time.h"
#include "m1_hal.h"
#include "m1_usb_hal.h"
#include "m1_wireless.h"
#include "m1_battery_hal.h"
#include "m1_save.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"

static m1_boot_state_t state;
static m1_boot_error_t error;
static m1_transport_t selected;
static const m1_transport_ops_t *transport_ops;
static bool external,board_owned,usb_owned,radio_owned;

static bool context(void)
{ return !__get_IPSR() && !__get_BASEPRI() && !__get_FAULTMASK() && !(__get_CONTROL()&1u); }
static bool wired(void)
{ return gpio_input_data_bit_read(GPIOC,GPIO_PINS_13)==RESET; }
static void fail(m1_boot_error_t why)
{
    error=why;
    if(m1_startup_clock_fatal()) { state=M1_BOOT_CLOCK_FATAL;return; }
    /* No application reports have been offered yet. Stop USB before touching
     * shared sleep GPIO/rails; local radio idle is not used as host proof. */
    if(radio_owned) { m1_wireless_stop();m1_radio_stop(); }
    if(usb_owned)(void)m1_usb_hw_stop();
    /* Startup has already attempted its own cleanup on failure. Do not
     * implicitly retry a GPIO restoration it could not establish. */
    if(board_owned && !m1_startup_fault())m1_startup_stop();
    state=M1_BOOT_FAILED;
}
m1_boot_state_t m1_boot_state(void) { return state; }
m1_boot_error_t m1_boot_error(void) { return error; }
bool m1_boot_begin(m1_transport_t transport,const m1_transport_ops_t *ops,
                   bool cold_quiescent)
{
    if(state!=M1_BOOT_OFF || !context() || !cold_quiescent ||
       !m1_transport_valid(transport) || (ops && (!ops->drained || !ops->select)))return false;
    m1_time_point_t now;
    if(!m1_time_now(&now)) { fail(M1_BOOT_TIME);return false; }
    selected=transport;transport_ops=ops;
    /* Only this cold-start owner may have called startup. Rejection before
     * ownership must not shut down someone else's existing peripherals. */
    if(!m1_startup_begin(now.ms,true)) {
        error=M1_BOOT_STARTUP;
        state=m1_startup_clock_fatal()?M1_BOOT_CLOCK_FATAL:M1_BOOT_FAILED;
        return false;
    }
    board_owned=true;external=wired();state=M1_BOOT_RAILS;return true;
}
void m1_boot_service(void)
{
    if(!context() || state==M1_BOOT_OFF || state>=M1_BOOT_READY)return;
    m1_time_point_t now;
    if(!m1_time_now(&now)) { fail(M1_BOOT_TIME);return; }
    if(wired()!=external) { fail(M1_BOOT_SOURCE);return; }
    switch(state) {
    case M1_BOOT_RAILS:
        m1_startup_service(now.ms,now.us);
        if(m1_startup_fault()) { fail(M1_BOOT_STARTUP);return; }
        if(!m1_startup_ready())return;
        /* No live application exists yet: discard warmup/unread frames and
         * stop ADC/DMA before the IRQ-masked SDK USB initializer. */
        if(!m1_hal_pause()) { fail(M1_BOOT_PAUSE);return; }
        state=M1_BOOT_LINKS;return;
    case M1_BOOT_LINKS:
        if(external) {
            if(m1_usb_hw_start(true)!=M1_USB_HW_OK) { fail(M1_BOOT_USB);return; }
            usb_owned=true;
        }
        /* SDK USB attachment may consume tens of milliseconds. Never stamp
         * a radio pulse using the time sampled before that blocking call. */
        if(!m1_time_now(&now)) { fail(M1_BOOT_TIME);return; }
        if(wired()!=external) { fail(M1_BOOT_SOURCE);return; }
        if(selected!=M1_TRANSPORT_USB) {
            radio_owned=true;
            if(!m1_radio_init(now.us)) { fail(M1_BOOT_RADIO_INIT);return; }
            state=M1_BOOT_RADIO;
        } else state=M1_BOOT_APPLICATION;
        return;
    case M1_BOOT_RADIO:
        m1_radio_service(now.us);
        if(!m1_radio_healthy()) { fail(M1_BOOT_RADIO_INIT);return; }
        if(!m1_radio_ready())return;
        /* Cold ownership was explicitly supplied at begin. No prior custom
         * reports exist; a completed SPI transfer is NOT release proof. */
        if(!m1_wireless_init(selected,true,now.us)) { fail(M1_BOOT_RADIO_LINK);return; }
        state=M1_BOOT_APPLICATION;return;
    case M1_BOOT_APPLICATION:
        m1_battery_hal_init();
        if(!m1_hal_resume()) { fail(M1_BOOT_RESUME);return; }
        if(!m1_live_init(selected,transport_ops,m1_save_ops())) { fail(M1_BOOT_APP);return; }
        state=M1_BOOT_READY;return;
    default: return;
    }
}
