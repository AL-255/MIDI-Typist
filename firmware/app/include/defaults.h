#ifndef MIDI_TYPIST_DEFAULTS_H
#define MIDI_TYPIST_DEFAULTS_H

/* Single source of truth for factory settings and behavioral tuning.
 * Units are in the names/comments. Existing valid device profiles override
 * factory settings; changing this file never erases a saved profile.
 * Hardware registers, flash geometry, HID/MIDI encodings and wire-format
 * limits belong to their board/protocol headers, NOT here.
 * Keep scalar definitions literal: tools/firmware_defaults.py reads these
 * without executing C or requiring a compiler. Table initializers are C-only.
 */

/* Canonical raw counts decrease with travel. Press < release < 4096. */
#define RAW_DEFAULT_PRESS 3500u
#define RAW_DEFAULT_RELEASE 3600u
#define DEFAULT_KEYBOARD_ENABLED 1u
#define DEFAULT_ACTUATION_LEVEL 4u
#define DEFAULT_RAPID_LEVEL 4u
#define DEFAULT_RAPID_ENABLED 1u
#define DEFAULT_PROFILE_LOCKED 0u
#define DEFAULT_MIDI_MODE 0u
#define DEFAULT_MIDI_JANKO 0u
#define DEFAULT_MIDI_LOWER_MUTED 0u
#define DEFAULT_MIDI_ROOT 0u
#define DEFAULT_MIDI_SCALE 9u /* MIDI_SCALE_CHROMATIC */
#define DEFAULT_MIDI_OCTAVE 0
#define DEFAULT_MIDI_VELOCITY_START 1u

/* Digital auxiliary inputs: consecutive samples, separate from analog keys. */
#define ENCODER_PHASE_STABLE_SAMPLES 2u
#define ENCODER_BUTTON_DEBOUNCE_MS 5u
#define ENCODER_EVENT_CAPACITY 32u
#define AUX_PULSE_MS 20u
#define DEFAULT_M1_ENCODER_POSITIVE_USAGE 0x00e9u
#define DEFAULT_M1_ENCODER_NEGATIVE_USAGE 0x00eau
#define DEFAULT_M1_ENCODER_BUTTON_USAGE 0x00e2u

/* Velocity: triggering sample is included, bottom-out sample normally isn't. */
#define RAW_BOTTOM_OUT 1500u
#define RAW_VELOCITY_WINDOW 10u
#define VELOCITY_FILTER_MIN_INTERVALS 5u
#define VELOCITY_MAX_COUNTS_PER_SECOND 4500000u
#define HUNTSMAN_ASSUMED_SCAN_HZ 8000u /* normalization, not measured throughput */

/* MIDI controllers: independent of per-key note trigger thresholds. */
#define MIDI_WHEEL_RELEASE_RAW 3800u
#define MIDI_WHEEL_PRESSED_RAW 1000u
#define MIDI_OCTAVE_LIMIT 10
#define MIDI_WHEEL_PERIOD_MS 1u
#define MIDI_PRESSURE_PERIOD_MS 10u
#define MIDI_OCTAVE_BLINK_STEP_MS 60u
#define MIDI_QUEUE 128u
#define MIDI_PENDING_STRIKES 5u
#define KEYBOARD_TEXT_MAX 32u
#define DEFAULT_MIDI_LEFT_SHIFT_NOTE 60u
#define JANKO_LEFT_SHIFT 61u
#define JANKO_RIGHT_SHIFT 83u

/* Calibration: independent timers and motion anchors for each key. */
#define CALIBRATION_SETTLE_MS 500u
#define CALIBRATION_HOLD_MS 1000u
#define CALIBRATION_IDLE_MS 5000u
#define CALIBRATION_RESULT_MS 1500u
#define CALIBRATION_MIN_SPAN_RAW 512u
#define CALIBRATION_MIN_RELEASE_RAW 2048u
#define CALIBRATION_PRESS_DIVISOR 2u /* accept at/below upper / divisor */
#define CALIBRATION_MOTION_RAW 64

/* Runtime scheduling and deferred flash writes (milliseconds). */
#define SCAN_STALE_MS 100u
#define KEYBOARD_REPORT_REFRESH_MS 1000u
#define GUI_REPORT_PERIOD_MS 33u
#define SETTINGS_CHECK_PERIOD_MS 20u
#define SETTINGS_SAVE_QUIET_MS 250u
#define LIGHTING_FRAME_PERIOD_MS 40u
#define LIGHTING_MAINTENANCE_FRAMES 26u
#define LIGHTING_RESET_LOW_MS 10u
#define LIGHTING_RESET_HIGH_MS 5u
#define LIGHTING_I2C_TIMEOUT_MS 20u
#define DEFAULT_LIGHTING_ENABLED 1u
#define OPTICAL_RESET_LOW_MS 10u
#define OPTICAL_RESET_HIGH_MS 150u
#define OPTICAL_SPI_TIMEOUT_MS 20u
#define OPTICAL_READY_TIMEOUT_MS 125u

/* Menu/LED tuning. PWM values are linear 0..255; tuples are RGB. */
#define DEFAULT_BRIGHTNESS_LEVEL 19u
#define TEXT_LETTER_MS 200u
#define TEXT_REPEAT_PAUSE_MS 500u
#define TEXT_BACKGROUND_PWM 77u
#define MENU_DIM_PWM 25u
#define MENU_BAR_STEP_MS 20u
#define COLOR_WHITE 255u,255u,255u
#define COLOR_CONFIRM 0u,255u,0u
#define COLOR_CANCEL 255u,0u,0u
#define COLOR_MIDI 0u,0u,255u
#define COLOR_JANKO 255u,255u,0u
#define COLOR_RAPID 253u,134u,17u
#define COLOR_CAL_PENDING 0u,0u,48u
#define COLOR_CAL_REGISTERED 0u,96u,0u
#define COLOR_CAL_RELEASE 32u,0u,64u
#define COLOR_CAL_HOLD 128u,48u,0u
#define COLOR_CAL_ERROR 96u,0u,0u
#define DEFAULT_BRIGHTNESS_STEPS { \
    0,3,6,10,15,21,27,36,45,56,68,81,96,112,128,144,172,194,224,255 }

/* Huntsman optical compatibility tuning. Do not adjust to fix electrical
 * faults. These preserve the reference algorithm and its factory fallbacks. */
#define OPTICAL_SETTLING_FRAMES 128u
#define OPTICAL_FALLBACK_LOWER_RAW 2240u
#define OPTICAL_FALLBACK_UPPER_RAW 3360u
#define OPTICAL_SETTLED_MARGIN_RAW 500u
#define OPTICAL_FACTORY_MIN_SPAN_RAW 2000u
#define OPTICAL_LOWER_TRIM_PERCENT 10u
#define OPTICAL_FIXED_PRESS_LEVEL 127u
#define OPTICAL_FIXED_RELEASE_LEVEL 100u
#define OPTICAL_MIN_LEVEL 8u
#define OPTICAL_MAX_PRESS_LEVEL 252u
#define OPTICAL_RELEASE_GAP_LEVEL 8u
#define OPTICAL_PREVIEW_RELEASE_GAP_LEVEL 6u
#define OPTICAL_ZERO_LEVEL 4u
#define OPTICAL_RAPID_WAIT_FRAMES 8u
#define OPTICAL_RELEASE_MIN_Q16 0x0100u
#define OPTICAL_RELEASE_GAP_Q16 0x0666u
#define DEFAULT_ACTUATION_LEVELS { \
    0xffff,0x0666,0x1998,0x3330,0x4cc8,0x6660,0x7ff8,0x9990,0xb328,0xccc0,0xe658 }
#define DEFAULT_RAPID_LEVELS { \
    0x1980,0x0666,0x0ccc,0x1332,0x1998,0x1ffe,0x2664,0x2cca,0x3330,0x3996,0x3ffc }


/* HID usage -> scientific MIDI note (C4=60). Control modifiers are separate. */
#define DEFAULT_MIDI_NOTE_MAP { \
        {0x2b,72},{0x14,74},{0x1a,76},{0x08,77},{0x15,79},{0x17,81}, \
        {0x1c,83},{0x18,84},{0x0c,86},{0x12,88},{0x13,89},{0x2f,91}, \
        {0x30,93},{0x31,95},{0x1e,73},{0x1f,75},{0x21,78},{0x22,80}, \
        {0x23,82},{0x25,85},{0x26,87},{0x2d,90},{0x2e,92},{0x2a,94}, \
        {0x04,61},{0x1d,62},{0x16,63},{0x1b,64},{0x06,65},{0x09,66}, \
        {0x19,67},{0x0a,68},{0x05,69},{0x0b,70},{0x11,71},{0x10,72}, \
        {0x0e,73},{0x36,74},{0x0f,75},{0x37,76},{0x38,77},{0x34,78} \
    }
#define DEFAULT_JANKO_NOTE_MAP { \
    {0x29,58},{0x1e,60},{0x1f,62},{0x20,64},{0x21,66},{0x22,68},{0x23,70}, \
    {0x24,72},{0x25,74},{0x26,76},{0x27,78},{0x2d,80},{0x2e,82},{0x2a,84}, \
    {0x2b,59},{0x14,61},{0x1a,63},{0x08,65},{0x15,67},{0x17,69},{0x1c,71}, \
    {0x18,73},{0x0c,75},{0x12,77},{0x13,79},{0x2f,81},{0x30,83},{0x31,85}, \
    {0x39,60},{0x04,62},{0x16,64},{0x07,66},{0x09,68},{0x0a,70},{0x0b,72}, \
    {0x0d,74},{0x0e,76},{0x0f,78},{0x33,80},{0x34,82},{0x28,84}, \
    {0x1d,63},{0x1b,65},{0x06,67},{0x19,69},{0x05,71},{0x11,73}, \
    {0x10,75},{0x36,77},{0x37,79},{0x38,81}, \
}

/* Host capture defaults. Device thresholds above are intentionally separate. */
#define CAPTURE_DEFAULT_THRESHOLD 3800u
#define CAPTURE_DEFAULT_POINTS 20u
#define CAPTURE_BUFFER_FRAMES 8192u
#define CAPTURE_TIMEOUT_SECONDS 5u

/* GUI control lease and bounded SysEx transport (no background PC service). */
#define MIDI_CONTROL_COMMAND_MAX 96u
#define MIDI_CONTROL_LEASE_MS 2500u
#define MIDI_CONTROL_RX_TIMEOUT_MS 1000u
#define MIDI_CONTROL_HEARTBEAT_MS 500u
#define MIDI_CONTROL_COMMAND_TIMEOUT_MS 3000u
#define GUI_POWER_POLL_MS 1000u
#define GUI_POWER_STALE_MS 3000u
#if GUI_POWER_POLL_MS < 100u || GUI_POWER_POLL_MS >= GUI_POWER_STALE_MS || GUI_POWER_STALE_MS > 60000u
#error Invalid GUI power status defaults
#endif
#define MIDI_CONTROL_HOST_MESSAGES 512u
#define MIDI_CONTROL_HOST_POLL_MS 2u
#define MIDI_CONTROL_HOST_CLOSE_MS 250u
#define MIDI_CONTROL_HOST_REAP_MS 1000u
#define MIDI_CONTROL_HOST_ERROR_BYTES 512u
#define MIDI_CONTROL_CAPTURE_SAMPLES 16384u
#define MIDI_CONTROL_TX_EVENTS 16u
#define MIDI_CONTROL_DEVICE_RECORDS 256u
#define MIDI_CONTROL_SAMPLE_BATCH 32u

/* M1 acquisition cadence is a requested custom rate, not a stock measurement. */
#define M1_SCAN_HZ 8000u
/* Absorb short foreground LED/control work without losing velocity samples. */
#define M1_SCAN_QUEUE_FRAMES 32u
#define M1_ADC_CALIBRATION_WAIT_LOOPS 1000000u
/* Finite SDK polling budgets. Physical flash timing still needs validation. */
#define M1_FLASH_ERASE_WAIT_LOOPS 1000000u
#define M1_FLASH_PROGRAM_WAIT_LOOPS 100000u
/* Native ADC baseline range admitted by the factory sample-startup path. */
#define M1_FACTORY_RELEASE_MIN_RAW 1000u
#define M1_FACTORY_RELEASE_MAX_RAW 4000u
/* Provisional cold-start travel when stock records cannot be interpreted.
 * The reference startup uses current ADC - 700 in RAM, never in flash. */
#define M1_STARTUP_TRAVEL_RAW 700u
#define M1_CALIBRATION_MIN_SPAN_RAW 128u
#define M1_CALIBRATION_MIN_RELEASE_RAW 1001u
#define M1_CALIBRATION_PRESS_DROP_RAW 128u
/* Custom LED scheduling bounds, not measured electrical timing. */
#define M1_LED_LATCH_US 1000u
#define M1_LED_TRANSFER_TIMEOUT_US 10000u
#define M1_POWER_STAGE_MS 10u
#define M1_MAIN_STACK_BYTES 8192u
#define M1_DEFAULT_WIRELESS_TRANSPORT 0u
#if M1_MAIN_STACK_BYTES < 8192u || M1_MAIN_STACK_BYTES > 32768u || (M1_MAIN_STACK_BYTES % 8u) != 0
#error "invalid M1 application stack reservation"
#endif
#if M1_DEFAULT_WIRELESS_TRANSPORT != 0u && M1_DEFAULT_WIRELESS_TRANSPORT != 1u && M1_DEFAULT_WIRELESS_TRANSPORT != 2u && M1_DEFAULT_WIRELESS_TRANSPORT != 5u
#error "invalid M1 default wireless transport"
#endif
#define M1_SENSOR_SETTLE_MS 1u
#define M1_COLD_SLEEP_TICKS 25u
/* Measure the low-speed RTC against TMR2 before using it to bridge sleep.
 * Window/timeout are policy; no assumed 40 kHz oscillator rate is used. */
#define M1_SLEEP_CLOCK_WINDOW_US 20000u
#define M1_SLEEP_CLOCK_TIMEOUT_US 1000000u
#define M1_SLEEP_CLOCK_SLOP_TICKS 2u
#define M1_SLEEP_CLOCK_RESUME_MARGIN_US 100000u
#if M1_SLEEP_CLOCK_WINDOW_US < 10000u || M1_SLEEP_CLOCK_TIMEOUT_US < M1_SLEEP_CLOCK_WINDOW_US || M1_SLEEP_CLOCK_TIMEOUT_US > 1000000u || M1_SLEEP_CLOCK_SLOP_TICKS > 8u
#error "invalid M1 sleep clock measurement window"
#endif
#if M1_SLEEP_CLOCK_RESUME_MARGIN_US == 0 || M1_SLEEP_CLOCK_RESUME_MARGIN_US > 1000000u
#error "invalid M1 sleep clock resume allowance"
#endif
#define M1_COLD_SCAN_SETTLE_US 10u
#define M1_TRANSPORT_SWITCH_TIMEOUT_MS 3000u
#define M1_RADIO_START_PULSE_US 10000u
#define M1_RADIO_TRANSFER_TIMEOUT_US 10000u
/* Custom foreground scheduling, not inferred stock timer units. */
#define M1_RADIO_POLL_US 1000u
#define M1_RADIO_QUERY_US 100000u
#define M1_RADIO_MODE_TIMEOUT_US 3000000u
#define M1_RADIO_STATUS_TIMEOUT_US 500000u
#define M1_RADIO_SLEEP_TIMEOUT_US 20000u
#define M1_RADIO_BT_REPORT_US 10000u
#define M1_RADIO_RF_REPORT_US 1000u
#define M1_WAKE_SCAN_TIMEOUT_US 2000u
#define M1_WAKE_ACQUIRE_FRAMES 10u
#define M1_WAKE_REFRESH_FRAMES 50u
#define M1_WAKE_DROP_COUNTS 300u
#define M1_BATTERY_SAMPLE_MS 30u
#define M1_BATTERY_FILTER_SAMPLES 8u
#define M1_BATTERY_CONFIRM_BATCHES 10u
#define M1_CHARGER_CONFIRM_SAMPLES 10u
#define M1_BATTERY_LOW_PERCENT 20u
#define M1_BATTERY_CRITICAL_PERCENT 5u
/* Custom save qualification: defer on low/unknown battery; external power
 * still needs a qualified source. These are policy, not measured flash limits. */
#define M1_FLASH_MIN_BATTERY_PERCENT 21u
#define M1_FLASH_MAX_PAUSE_US 100000u
#if M1_FLASH_MIN_BATTERY_PERCENT <= M1_BATTERY_LOW_PERCENT || M1_FLASH_MIN_BATTERY_PERCENT > 100u || M1_FLASH_MAX_PAUSE_US == 0 || M1_FLASH_MAX_PAUSE_US >= M1_RADIO_STATUS_TIMEOUT_US
#error "invalid M1 flash-save power/pause qualification"
#endif
#define M1_BATTERY_BLINK_MS 400u
#define M1_BATTERY_DISPLAY_PWM 100u
/* Reference power-policy qualification counts, not wall-clock durations. */
#define M1_POWER_QUALIFY_TICKS 100u
#define M1_POWER_FAST_QUALIFY_TICKS 8u
#define M1_POWER_CRITICAL_STEPS 5u
#define M1_POWER_UNSELECTED_STEPS 10u
#define M1_POWER_BT_SEARCH_STEPS 120u
#define M1_POWER_RADIO_SEARCH_STEPS 30u
/* Custom runtime cadence/idle defaults; not inferred stock timer units. */
#define M1_RUNTIME_POWER_PERIOD_MS 10u
#define M1_RUNTIME_BT_IDLE_STEPS 300u
#define M1_RUNTIME_RADIO_IDLE_STEPS 300u
#define M1_RUNTIME_HANDOFF_MS 3000u
#define M1_RUNTIME_SLEEP_SETTLE_MS 100u
#define M1_RUNTIME_SLEEP_TICKS 30u
#define M1_RUNTIME_SCAN_SETTLE_US 100u
#define M1_RUNTIME_BT_RETAIN_MS 30000u
#define M1_RUNTIME_RESTORE_STAGE_MS 10u
#define M1_RUNTIME_RESTORE_SETTLE_MS 5u
#if M1_RUNTIME_POWER_PERIOD_MS < 1 || M1_RUNTIME_POWER_PERIOD_MS > 1000u || M1_RUNTIME_BT_IDLE_STEPS > 65535u || M1_RUNTIME_RADIO_IDLE_STEPS > 65535u || M1_RUNTIME_HANDOFF_MS < 100u || M1_RUNTIME_HANDOFF_MS >= 0x80000000u || M1_RUNTIME_SLEEP_SETTLE_MS < 1 || M1_RUNTIME_SLEEP_SETTLE_MS >= M1_RUNTIME_HANDOFF_MS || M1_RUNTIME_SLEEP_TICKS < 1 || M1_RUNTIME_SLEEP_TICKS > 65536u || M1_RUNTIME_SCAN_SETTLE_US < 1 || M1_RUNTIME_SCAN_SETTLE_US >= 0x80000000u || M1_RUNTIME_BT_RETAIN_MS < 1 || M1_RUNTIME_BT_RETAIN_MS >= 0x80000000u || M1_RUNTIME_RESTORE_STAGE_MS < 1 || M1_RUNTIME_RESTORE_STAGE_MS >= M1_RUNTIME_HANDOFF_MS || M1_RUNTIME_RESTORE_SETTLE_MS < 1 || M1_RUNTIME_RESTORE_SETTLE_MS >= M1_RUNTIME_HANDOFF_MS
#error Invalid M1 runtime power defaults
#endif
#define AT32_CLOCK_WAIT_LOOPS 12288u
#define M1_USB_PHY_SETTLE_US 1000u
#define M1_USB_INIT_DELAY_LIMIT_MS 25u
#define M1_RELEASE_GAP_LEVEL 8u
#define M1_ACTUATION_LEVELS { \
    0,2048,4096,8192,12288,16384,24576,32768,40960,49152,57344 }

/* SDK-free reference port: intentionally different editor response. */
#define SYNTHETIC_CALIBRATION_LOWER_RAW 1000u
#define SYNTHETIC_RELEASE_GAP_LEVEL 8u
#define SYNTHETIC_ACTUATION_LEVELS { \
    0,2048,4096,8192,12288,16384,24576,32768,40960,49152,57344 }

#if RAW_DEFAULT_PRESS < 1 || RAW_DEFAULT_PRESS >= RAW_DEFAULT_RELEASE || RAW_DEFAULT_RELEASE >= 4096
#error "Default Schmitt thresholds must satisfy 1 <= press < release < 4096"
#endif
#if M1_SCAN_QUEUE_FRAMES < 2 || M1_SCAN_QUEUE_FRAMES > 255
#error "M1 scan queue must fit its bounded counter"
#endif
#if MIDI_PENDING_STRIKES < 1 || MIDI_PENDING_STRIKES > 8
#error "Pending MIDI strikes must fit the per-key slot bitmap"
#endif
#if M1_FLASH_ERASE_WAIT_LOOPS < 1 || M1_FLASH_PROGRAM_WAIT_LOOPS < 1 || M1_FLASH_ERASE_WAIT_LOOPS > 1000000u || M1_FLASH_PROGRAM_WAIT_LOOPS > 1000000u
#error "Invalid M1 flash polling budget"
#endif
#if M1_LED_LATCH_US < 1 || M1_LED_TRANSFER_TIMEOUT_US < 1 || M1_LED_LATCH_US >= 0x80000000u || M1_LED_TRANSFER_TIMEOUT_US >= 0x80000000u
#error "M1 LED timing must fit wrapping microsecond comparisons"
#endif
#if AT32_CLOCK_WAIT_LOOPS < 1 || M1_POWER_STAGE_MS < 1 || M1_SENSOR_SETTLE_MS < 1 || M1_POWER_STAGE_MS >= 0x80000000u || M1_SENSOR_SETTLE_MS >= 0x80000000u
#error "M1 startup waits must be bounded and positive"
#endif
#if M1_USB_PHY_SETTLE_US < 1000 || M1_USB_INIT_DELAY_LIMIT_MS < 25 || M1_USB_INIT_DELAY_LIMIT_MS > 1000 || M1_USB_PHY_SETTLE_US > M1_USB_INIT_DELAY_LIMIT_MS * 1000u
#error "M1 USB startup delays must cover SDK requirements and stay bounded"
#endif
#if M1_BATTERY_FILTER_SAMPLES < 1 || M1_BATTERY_FILTER_SAMPLES > 255 || M1_BATTERY_CONFIRM_BATCHES < 1 || M1_BATTERY_CONFIRM_BATCHES > 255 || M1_CHARGER_CONFIRM_SAMPLES < 1 || M1_CHARGER_CONFIRM_SAMPLES > 255
#error "invalid M1 battery filter/debounce"
#endif
#if M1_BATTERY_SAMPLE_MS < 1 || M1_BATTERY_SAMPLE_MS >= 0x80000000u || M1_BATTERY_BLINK_MS < 1 || M1_BATTERY_BLINK_MS >= 0x80000000u || M1_BATTERY_DISPLAY_PWM > 255 || M1_BATTERY_CRITICAL_PERCENT < 1 || M1_BATTERY_CRITICAL_PERCENT > M1_BATTERY_LOW_PERCENT || M1_BATTERY_LOW_PERCENT >= 100
#error "invalid M1 battery policy"
#endif
#if M1_TRANSPORT_SWITCH_TIMEOUT_MS < 1 || M1_TRANSPORT_SWITCH_TIMEOUT_MS >= 0x80000000u
#error "invalid M1 transport timeout"
#endif
#if M1_RADIO_START_PULSE_US < 1 || M1_RADIO_START_PULSE_US >= 0x80000000u || M1_RADIO_TRANSFER_TIMEOUT_US < 1 || M1_RADIO_TRANSFER_TIMEOUT_US >= 0x80000000u
#error "invalid M1 radio timing"
#endif
#if M1_RADIO_POLL_US < 1 || M1_RADIO_POLL_US >= M1_RADIO_QUERY_US || M1_RADIO_QUERY_US >= M1_RADIO_STATUS_TIMEOUT_US || M1_RADIO_STATUS_TIMEOUT_US >= M1_RADIO_MODE_TIMEOUT_US || M1_RADIO_MODE_TIMEOUT_US >= 0x80000000u || M1_RADIO_RF_REPORT_US < 1 || M1_RADIO_BT_REPORT_US < 1 || M1_RADIO_RF_REPORT_US >= M1_RADIO_STATUS_TIMEOUT_US || M1_RADIO_BT_REPORT_US >= M1_RADIO_STATUS_TIMEOUT_US
#error "invalid M1 wireless scheduling"
#endif
#if M1_RADIO_SLEEP_TIMEOUT_US <= M1_RADIO_TRANSFER_TIMEOUT_US || M1_RADIO_SLEEP_TIMEOUT_US >= 0x80000000u
#error "invalid M1 radio sleep handoff timeout"
#endif
#if M1_FACTORY_RELEASE_MIN_RAW < 1 || M1_FACTORY_RELEASE_MAX_RAW > 4095u || M1_FACTORY_RELEASE_MIN_RAW > M1_FACTORY_RELEASE_MAX_RAW
#error "invalid M1 factory baseline range"
#endif
#if M1_STARTUP_TRAVEL_RAW >= M1_FACTORY_RELEASE_MIN_RAW || M1_STARTUP_TRAVEL_RAW < M1_CALIBRATION_MIN_SPAN_RAW || M1_CALIBRATION_MIN_SPAN_RAW < 1 || M1_CALIBRATION_PRESS_DROP_RAW < M1_CALIBRATION_MIN_SPAN_RAW || M1_CALIBRATION_PRESS_DROP_RAW >= M1_CALIBRATION_MIN_RELEASE_RAW || M1_CALIBRATION_MIN_RELEASE_RAW > 4096u
#error "invalid M1 electrical calibration policy"
#endif
#if M1_COLD_SLEEP_TICKS < 1 || M1_COLD_SLEEP_TICKS > 65536u || M1_COLD_SCAN_SETTLE_US < 1 || M1_COLD_SCAN_SETTLE_US >= 0x80000000u
#error "invalid M1 battery cold-start timing"
#endif
#if M1_WAKE_SCAN_TIMEOUT_US < 1 || M1_WAKE_SCAN_TIMEOUT_US >= 0x80000000u || M1_WAKE_ACQUIRE_FRAMES < 1 || M1_WAKE_ACQUIRE_FRAMES > 255 || M1_WAKE_REFRESH_FRAMES < 1 || M1_WAKE_REFRESH_FRAMES > 255 || M1_WAKE_DROP_COUNTS < 1 || M1_WAKE_DROP_COUNTS >= 4096
#error "invalid M1 wake scan policy"
#endif
#if M1_POWER_QUALIFY_TICKS < 1 || M1_POWER_QUALIFY_TICKS > 255 || M1_POWER_FAST_QUALIFY_TICKS < 1 || M1_POWER_FAST_QUALIFY_TICKS > 255 || M1_POWER_CRITICAL_STEPS < 1 || M1_POWER_UNSELECTED_STEPS < 1 || M1_POWER_BT_SEARCH_STEPS < 1 || M1_POWER_RADIO_SEARCH_STEPS < 1
#error "invalid M1 power qualification"
#endif
#if RAW_VELOCITY_WINDOW < 2 || RAW_VELOCITY_WINDOW > 255 || VELOCITY_MAX_COUNTS_PER_SECOND < 1
#error "Invalid velocity window or normalization scale"
#endif
#if RAW_BOTTOM_OUT < 1 || RAW_BOTTOM_OUT >= RAW_DEFAULT_RELEASE - 1
#error "Bottom-out must leave a nonempty MIDI trigger range"
#endif
#if MIDI_WHEEL_PRESSED_RAW < 1 || MIDI_WHEEL_PRESSED_RAW >= MIDI_WHEEL_RELEASE_RAW || MIDI_WHEEL_RELEASE_RAW > 4096
#error "Invalid MIDI wheel endpoints"
#endif
#if CALIBRATION_HOLD_MS < 1 || CALIBRATION_HOLD_MS >= CALIBRATION_IDLE_MS || CALIBRATION_IDLE_MS > 65535
#error "Calibration durations must fit telemetry and allow a hold before timeout"
#endif
#if CALIBRATION_PRESS_DIVISOR < 1 || TEXT_LETTER_MS < 1 || MIDI_OCTAVE_BLINK_STEP_MS < 1
#error "Divisors and animation periods must be positive"
#endif
#if VELOCITY_FILTER_MIN_INTERVALS < 2 || CALIBRATION_SETTLE_MS >= CALIBRATION_IDLE_MS
#error "Filtering must retain an interval; calibration must settle before timeout"
#endif
#if DEFAULT_ACTUATION_LEVEL < 1 || DEFAULT_ACTUATION_LEVEL > 10 || DEFAULT_RAPID_LEVEL < 1 || DEFAULT_RAPID_LEVEL > 10
#error "Editor defaults must be in 1..10"
#endif
#if DEFAULT_MIDI_VELOCITY_START < 1 || DEFAULT_MIDI_VELOCITY_START > 10 || DEFAULT_MIDI_ROOT > 11
#error "Invalid MIDI velocity-start or root default"
#endif
#if MIDI_OCTAVE_LIMIT < 1 || MIDI_OCTAVE_LIMIT > 10 || DEFAULT_MIDI_OCTAVE < -MIDI_OCTAVE_LIMIT || DEFAULT_MIDI_OCTAVE > MIDI_OCTAVE_LIMIT
#error "MIDI octave range must fit supported note and telemetry limits"
#endif
#if KEYBOARD_TEXT_MAX < 1 || KEYBOARD_TEXT_MAX > 255 || MIDI_QUEUE < 1 || MIDI_QUEUE > 65535
#error "Queue/text capacities must fit their counters"
#endif
#if OPTICAL_SETTLING_FRAMES < 1 || OPTICAL_SETTLING_FRAMES > 255 || LIGHTING_MAINTENANCE_FRAMES < 1 || LIGHTING_MAINTENANCE_FRAMES > 255
#error "Optical settling and lighting cadence must fit their frame counters"
#endif
#if MIDI_CONTROL_HOST_MESSAGES < 1 || MIDI_CONTROL_HOST_MESSAGES > 65535 || MIDI_CONTROL_HOST_ERROR_BYTES < 2 || MIDI_CONTROL_HOST_ERROR_BYTES > 4096
#error "MIDI host queues and failure messages must be bounded"
#endif
#if MIDI_CONTROL_HOST_POLL_MS < 1 || MIDI_CONTROL_HOST_POLL_MS >= MIDI_CONTROL_HEARTBEAT_MS || MIDI_CONTROL_HOST_CLOSE_MS < 1 || MIDI_CONTROL_HOST_REAP_MS < 1 || MIDI_CONTROL_HOST_CLOSE_MS > MIDI_CONTROL_COMMAND_TIMEOUT_MS || MIDI_CONTROL_HOST_REAP_MS > MIDI_CONTROL_COMMAND_TIMEOUT_MS
#error "MIDI host scheduling and shutdown deadlines must be bounded"
#endif

#endif
