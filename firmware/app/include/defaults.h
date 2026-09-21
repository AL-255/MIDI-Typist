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

/* Velocity: triggering sample is included, bottom-out sample normally isn't. */
#define RAW_BOTTOM_OUT 1500u
#define RAW_VELOCITY_WINDOW 10u
#define VELOCITY_FILTER_MIN_INTERVALS 5u
#define VELOCITY_MAX_COUNTS_PER_SECOND 4500000u
#define HUNTSMAN_ASSUMED_SCAN_HZ 8000u /* normalization, not measured throughput */

/* FUN60 Hall acquisition. A USB polling interval is NOT a full-matrix rate.
 * Timing is deliberately explicit; verify scan budget on the physical board. */
#define FUN60_SCAN_HZ 1000u
#define FUN60_SHIFT_SETTLE_US 1u
#define FUN60_ROW_SETTLE_US 5u
#define FUN60_MUX_SETTLE_US 1u
#define FUN60_ADC_TIMEOUT_US 100u
#define FUN60_ADC_CAL_TIMEOUT_US 10000u
#define FUN60_ADC_WARMUP_SAMPLES 1000u
#define FUN60_CLOCK_TIMEOUT_US 100000u
#define FUN60_HEXT_STARTUP_POLLS 12288u
#define FUN60_LED_LATCH_US 100u
#define FUN60_RELEASE_GAP_LEVEL 8u

/* MIDI controllers: independent of per-key note trigger thresholds. */
#define MIDI_WHEEL_RELEASE_RAW 3800u
#define MIDI_WHEEL_PRESSED_RAW 1000u
#define MIDI_OCTAVE_LIMIT 10
#define MIDI_WHEEL_PERIOD_MS 1u
#define MIDI_PRESSURE_PERIOD_MS 10u
#define MIDI_OCTAVE_BLINK_STEP_MS 60u
#define MIDI_QUEUE 128u
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
#define MIDI_CONTROL_HOST_MESSAGES 512u
#define MIDI_CONTROL_CAPTURE_SAMPLES 16384u
#define MIDI_CONTROL_TX_EVENTS 16u
#define MIDI_CONTROL_DEVICE_RECORDS 256u
#define MIDI_CONTROL_SAMPLE_BATCH 32u

/* SDK-free reference port: intentionally different editor response. */
#define SYNTHETIC_CALIBRATION_LOWER_RAW 1000u
#define SYNTHETIC_RELEASE_GAP_LEVEL 8u
#define SYNTHETIC_ACTUATION_LEVELS { \
    0,2048,4096,8192,12288,16384,24576,32768,40960,49152,57344 }

#if RAW_DEFAULT_PRESS < 1 || RAW_DEFAULT_PRESS >= RAW_DEFAULT_RELEASE || RAW_DEFAULT_RELEASE >= 4096
#error "Default Schmitt thresholds must satisfy 1 <= press < release < 4096"
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
#if FUN60_SCAN_HZ < 1 || FUN60_SCAN_HZ > 1000000 || 1000000 % FUN60_SCAN_HZ != 0
#error "FUN60 cadence must have a positive whole-microsecond period"
#endif
#if FUN60_SHIFT_SETTLE_US < 1 || FUN60_ROW_SETTLE_US < 1 || FUN60_MUX_SETTLE_US < 1 || FUN60_LED_LATCH_US < 1
#error "FUN60 electrical settling intervals must be positive"
#endif
#if FUN60_ADC_TIMEOUT_US < 1 || FUN60_ADC_CAL_TIMEOUT_US < 1 || FUN60_CLOCK_TIMEOUT_US < 1 || FUN60_ADC_WARMUP_SAMPLES < 1 || FUN60_HEXT_STARTUP_POLLS < 1
#error "FUN60 hardware waits must have a positive bound"
#endif
#if FUN60_ADC_TIMEOUT_US > 1000000 || FUN60_ADC_CAL_TIMEOUT_US > 1000000 || FUN60_CLOCK_TIMEOUT_US > 1000000 || FUN60_SHIFT_SETTLE_US > 1000000 || FUN60_ROW_SETTLE_US > 1000000 || FUN60_MUX_SETTLE_US > 1000000 || FUN60_LED_LATCH_US > 1000000
#error "FUN60 delays must remain within the wrapping cycle-counter budget"
#endif

#endif
