#include "m1_image.h"
#include "m1_storage.h"
#include "m1_boot.h"
#include "m1_diagnostics.h"
#include "m1_startup.h"
#include "m1_time.h"
#include "m1_hal.h"
#include "m1_lighting.h"
#include "m1_wireless.h"
#include "m1_usb_hal.h"
#include "m1_transport.h"
#include "m1_runtime_power.h"
#include "m1_board.h"
#include "defaults.h"
#include "at32f402_405.h"
#include "at32f402_405_conf.h"

volatile m1_main_state_t m1_main_state;
volatile uint32_t m1_main_detail;
_Static_assert((uint64_t)(M1_CORE_HZ/1000u)*M1_FAULT_POLL_MS<=0x1000000u,
    "fault-poll period exceeds SysTick reload");
static volatile uint32_t fault_ticks;
void m1_fault_systick_irq(void) { ++fault_ticks; }
static bool wired(void)
{ return gpio_input_data_bit_read(GPIOC,GPIO_PINS_13)==RESET; }
static __attribute__((noreturn)) void battery_fault_wait(m1_main_state_t why,uint32_t detail)
{
    /* Valid-clock failure after all application producers have stopped. Do
     * not leave the sensor and LED supplies asserted with no USB diagnostics.
     * The original PC13 source sense remains an input through sleep GPIO. */
    (void)m1_time_stop(); /* no timestamps or application service remain */
    __disable_irq();m1_main_state=why;m1_main_detail=detail;
    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK,TRUE);
    crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK,TRUE);
    gpio_bits_reset(GPIOB,GPIO_PINS_6|GPIO_PINS_13);
    gpio_bits_reset(GPIOC,GPIO_PINS_6|GPIO_PINS_14);
    for(unsigned i=0;i<(ACC_IRQn+32u)/32u;++i) {
        NVIC->ICER[i]=UINT32_MAX;NVIC->ICPR[i]=UINT32_MAX;
    }
    /* Only a periodic core timer may wake this shallow WFI. No radio, scan,
     * storage or USB operation is retried on the battery fault path. */
    SysTick->CTRL=0;
    SysTick->LOAD=(M1_CORE_HZ/1000u)*M1_FAULT_POLL_MS-1u;
    SysTick->VAL=0;SCB->ICSR=SCB_ICSR_PENDSTCLR_Msk;
    SCB->SCR&=~SCB_SCR_SLEEPDEEP_Msk;
    NVIC_SetPriority(SysTick_IRQn,3);
    fault_ticks=0;
    SysTick->CTRL=SysTick_CTRL_CLKSOURCE_Msk|SysTick_CTRL_TICKINT_Msk|SysTick_CTRL_ENABLE_Msk;
    __DSB();__enable_irq();
    uint32_t observed=0;unsigned stable=0;
    for(;;) {
        __WFI();
        uint32_t tick=fault_ticks;
        if(tick==observed)continue;
        observed=tick;
        if(wired()) {
            if(++stable>=M1_FAULT_CABLE_CONFIRM_TICKS) {
                __disable_irq();NVIC_SystemReset();
            }
        } else stable=0;
    }
}
static __attribute__((noreturn)) void halted(m1_main_state_t why,uint32_t detail)
{
    __disable_irq();m1_main_state=why;m1_main_detail=detail;
    /* No retry/reset into a potentially changed clock or host ownership.
     * This development image has no complete runtime recovery policy yet. */
    for(;;)__WFI();
}
static __attribute__((noreturn)) void enter_iap(uint32_t now)
{
    /* A command ACK is acceptance, not recovery proof. Do not reset until the
     * persistent flag is verified: it protects header-first factory updates
     * even if their transfer/power fails. No metadata erase is permitted. */
    __disable_irq();
    if(!wired())halted(M1_MAIN_RECOVERY_FAULT,M1_STORAGE_UNSAFE);
    m1_live_stop(now);
    m1_hal_stop();m1_lighting_stop();m1_wireless_stop();m1_radio_stop();
    if(m1_usb_hw_stop()!=M1_USB_HW_OK || !wired())
        halted(M1_MAIN_RECOVERY_FAULT,M1_STORAGE_QUIESCE);
    uint32_t result=m1_storage_arm_recovery(true);
    if(result)halted(M1_MAIN_RECOVERY_FAULT,result);
    NVIC_SystemReset();
}
static __attribute__((noreturn)) void stop_live(m1_main_state_t why,uint32_t detail,uint32_t now)
{
    /* Valid-clock failures only. Stop acquisition/output producers without
     * restarting them. Keep USB diagnostics when available; on battery,
     * battery_fault_wait drops known rails and waits for external power. */
    m1_live_stop(now);
    m1_hal_stop();m1_lighting_stop();m1_wireless_stop();m1_radio_stop();
    m1_main_state=why;m1_main_detail=detail;
    if(!m1_usb_hw_running())battery_fault_wait(why,detail);
    m1_diagnostics_runtime_fault(detail);
    for(;;) {
        m1_time_point_t time;
        if(!m1_time_now(&time))halted(M1_MAIN_TIME_FAULT,0);
        if(m1_diagnostics_service(time.ms))enter_iap(time.ms);
    }
}
void m1_main(void)
{
    uint16_t density=*(const volatile uint16_t *)M1_FLASH_SIZE_REGISTER;
    if(density!=M1_FLASH_SIZE_KIB)halted(M1_MAIN_GEOMETRY_FAULT,density);
    /* Successful factory updates leave an erased flag page. Leave it untouched
     * so ordinary reset boots the application and retains its profile slots. */
    uint32_t recovery=m1_storage_check_recovery(false);
    if(recovery)halted(M1_MAIN_RECOVERY_FAULT,recovery);
    m1_main_state=M1_MAIN_CLOCK;
    m1_clock_result_t clock=m1_clock_init();
    if(clock!=M1_CLOCK_OK)halted(M1_MAIN_CLOCK_FAULT,clock);
    if(!m1_time_start())halted(M1_MAIN_TIME_FAULT,0);
    crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK,TRUE);
    gpio_init_type pin;gpio_default_para_init(&pin);
    pin.gpio_pins=GPIO_PINS_13;pin.gpio_mode=GPIO_MODE_INPUT;pin.gpio_pull=GPIO_PULL_UP;
    gpio_init(GPIOC,&pin);
    bool external=wired();
#if MT_M1_WIRELESS
    m1_transport_t transport=external?M1_TRANSPORT_USB:M1_DEFAULT_WIRELESS_TRANSPORT;
#else
    /* USB-only build. On battery there is no host at all: start on USB anyway so
     * acquisition, lighting, battery telemetry and cable-arrival handling keep
     * working, and let the idle policy put the device to sleep. */
    (void)external;
    m1_transport_t transport=M1_TRANSPORT_USB;
#endif
    if(!m1_boot_begin(transport,m1_transport_ops(),true))halted(M1_MAIN_BOOT_FAULT,m1_boot_error());
    m1_main_state=M1_MAIN_COLD;__enable_irq();
    for(;;) {
        m1_boot_service();
        m1_boot_state_t state=m1_boot_state();
        if(state==M1_BOOT_READY)break;
        if(state==M1_BOOT_CLOCK_FATAL)
            halted(M1_MAIN_BOOT_FAULT,m1_boot_error());
        m1_time_point_t now;
        if(!m1_time_now(&now))halted(M1_MAIN_TIME_FAULT,0);
        if(state==M1_BOOT_FAILED) {
            m1_main_state=M1_MAIN_BOOT_FAULT;m1_main_detail=m1_boot_error();
            if(!m1_usb_hw_running())halted(M1_MAIN_BOOT_FAULT,m1_boot_error());
        }
        /* After a valid-clock failure, keep the cold-start control channel
         * alive. Never retry startup, accept settings or emit keyboard notes. */
        if(m1_diagnostics_service(now.ms))enter_iap(now.ms);
    }
    m1_main_state=M1_MAIN_RUNNING;
    for(;;) {
        m1_time_point_t now;
        if(!m1_time_now(&now))halted(M1_MAIN_TIME_FAULT,0);
        m1_runtime_power_service(now.ms,now.us,wired());
        m1_runtime_power_state_t power=m1_runtime_power_state();
        if(power==M1_RUNTIME_CLOCK_FATAL)halted(M1_MAIN_CLOCK_FAULT,m1_runtime_power_error());
        if(power==M1_RUNTIME_TIME_FATAL)
            stop_live(M1_MAIN_TIME_FAULT,m1_runtime_power_error(),now.ms);
        if(power==M1_RUNTIME_FAILED)
            stop_live(M1_MAIN_DEVICE_FAULT,M1_DEVICE_POWER|(m1_runtime_power_error()<<16),now.ms);
        if(m1_live_update_requested())enter_iap(now.ms);
        /* The power owner deliberately stops these peripherals after park.
         * Its stage-specific guards replace the awake health checks then. */
        if(power!=M1_RUNTIME_AWAKE && power!=M1_RUNTIME_DRAIN)continue;
        uint32_t faults=(!m1_hal_healthy()?M1_DEVICE_SCAN:0u) |
            (!m1_lighting_healthy()?M1_DEVICE_LIGHT:0u) |
            (m1_live_transport_fault()?M1_DEVICE_TRANSPORT:0u) |
            (m1_live_storage_fault()?M1_DEVICE_STORAGE:0u) |
            (m1_live_transport()!=M1_TRANSPORT_USB && !m1_wireless_healthy()?M1_DEVICE_RADIO:0u);
        if(faults)stop_live(M1_MAIN_DEVICE_FAULT,faults,now.ms);
    }
}
