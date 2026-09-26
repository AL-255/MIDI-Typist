#include "defaults.h"
#include "keyboard_live.h"
#include "board.h"
#include "debug.h"
#include "keyboard_scan.h"
#include "huntsman_layout.h"
#include "optical_bus.h"
#include "optical_transport.h"
#include "usb_composite.h"
#include "scan_stream.h"
#include "midi_control.h"
#include "keyboard_build.h"
#include <string.h>
#include "travel_lighting.h"
static travel_lighting_t s_lighting;

static optical_transport_t s_transport;
static keyboard_scan_t s_scan;
static bool s_host_keys, s_trace;
static volatile bool s_usb_reset;
static bool s_stream_requested;
#include "keyboard_app.h"
#include "keyboard_midi.h"
#include "keyboard_menu.h"
#include "flash_dump.h"
#include "device_store.h"
static keyboard_raw_t s_raw;
static keyboard_midi_t s_midi;
static keyboard_menu_t s_menu;
/* This is writable application RAM (the bootloader loads the full image
 * here). Keep the independent hold registers out of scarce peripheral SRAMX.
 * keyboard_live_init explicitly initializes the complete state before use. */
static keyboard_calibration_t s_cal __attribute__((section(".calibration_state")));
static device_store_t s_cal_store __attribute__((section(".calibration_state")));
static keyboard_app_t s_app __attribute__((section(".calibration_state")));
#define s_sent s_app.sent
#define s_sent_valid s_app.sent_valid
static uint32_t s_gui_sequence, s_gui_ack, s_last_gui;
static uint8_t s_gui_result;

static bool load_calibration(uint8_t profile,uint8_t count,uint16_t *lo,uint16_t *hi)
{
    device_store_load(&s_cal_store,profile,count,lo,hi,flash_calibration_read);
    if(s_cal_store.saved) s_scan.calibrated=count;
    return s_cal_store.saved;
}
static keyboard_save_result_t save_calibration(const keyboard_calibration_t *cal)
{
    bool ok=device_store_update(&s_cal_store,&s_app,cal,flash_calibration_read,flash_calibration_write);
    if(ok) s_scan.calibrated=cal->count;
    return ok?KEYBOARD_SAVE_COMPLETE:KEYBOARD_SAVE_FAILED;
}
static bool clear_profile(void)
{
    return device_store_clear(&s_cal_store,flash_calibration_read,flash_calibration_erase);
}
static void reset_sensors(uint8_t profile) { keyboard_scan_init(&s_scan,profile); }
static const keyboard_app_ops_t app_ops={
    load_calibration,save_calibration,clear_profile,reset_sensors,debug_write
};
static void gui_snapshot(uint32_t now)
{
    if (!scan_stream_gui_enabled() || (uint32_t)(now - s_last_gui) < GUI_REPORT_PERIOD_MS) return;
    s_last_gui = now;
    keyboard_telemetry_status_t status={
        .now=now,.sequence=s_gui_sequence++,.ack=s_gui_ack,.result=s_gui_result,
        .scan_errors=s_transport.errors,.light_errors=s_lighting.errors,
        .scan_fault=s_transport.phase==OPT_FAULT,.light_fault=s_lighting.phase==LIGHT_FAULT,
        .calibration_saved=s_cal_store.saved,.calibration_supported=true,
        .calibration_generation=s_cal_store.calibration_generation,
        .storage_error=s_cal_store.error,.storage_generation=s_cal_store.generation,
        .storage_flags=s_cal_store.valid | (s_cal_store.pending<<1u) | (s_cal_store.fault<<2u),
        .storage_slot=s_cal_store.slot
    };
    uint8_t out[MT_GUI_SIZE(MT_KEY_CAPACITY,KEYBOARD_NKRO_REPORT_BYTES)];
    size_t size=keyboard_telemetry_encode(&s_app,&status,out,sizeof(out));
    if(size) (void)scan_stream_gui_push(out,size);
}

static void value(const char *label, uint32_t n)
{
    char text[12], *end = text + sizeof(text) - 1u;
    *end = '\0';
    do { *--end = (char)('0' + n % 10u); n /= 10u; } while (n);
    debug_write(label); debug_write(end);
}

static void config_status(void)
{
    const keyboard_config_t *s = &s_raw.engine.config;
    value("KEYS host=", s_host_keys); value(" fn=", s->fn); value(" mode=", s->mode);
    value(" act=", s->actuation); value(" rapid=", s->rapid);
    value(" enabled=", s->rapid_enabled); value(" saved=", s->saved_actuation);
    value(",", s->saved_rapid); value(" revision=", s->revision);
    debug_write("\r\n");
    value("RAW enabled=", s_raw.enabled); value(" armed=", s_raw.armed);
    value(" valid=", s_raw.valid); value(" revision=", s_raw.revision);
    debug_write(" per-key Schmitt; press<release; automatic save after neutral\r\n");
}

static void scan_status(void)
{
    value("SCAN phase=", s_transport.phase); value(" profile=", s_transport.profile);
    value(" count=", s_transport.count); value(" transfers=", s_transport.transfers);
    value(" frames=", s_transport.frames); value(" markers=", s_transport.markers);
    value(" errors=", s_transport.errors); value(" settled=", s_scan.ready);
    value(" valid=", s_scan.valid); value(" calibrated=", s_scan.calibrated);
    value(" stream_dropped=", scan_stream_dropped());
    if (s_transport.fault) { debug_write(" fault="); debug_write(s_transport.fault); }
    debug_write("\r\n");
}

static void lighting_status(void)
{
    value("LIGHT phase=", s_lighting.phase); value(" on=", s_lighting.requested);
    value(" profile=", s_lighting.profile); value(" transfers=", s_lighting.transfers);
    value(" frames=", s_lighting.frames); value(" errors=", s_lighting.errors);
    value(" calibrated=", s_scan.calibrated); value(" count=", s_scan.count);
    if (s_lighting.fault) { debug_write(" fault="); debug_write(s_lighting.fault); }
    debug_write(" PWM linear in optical endpoints; not measured millimeters\r\n");
}

static void release_host(void)
{
    s_host_keys = false;
    s_sent_valid = false; /* retry neutral on busy USB, not only once */
    keyboard_app_invalidate(&s_app,board_millis());
}

static void event(uint8_t key, bool down, uint8_t level)
{
    if (!s_trace) return;
    static const char digits[] = "0123456789abcdef";
    char id[3] = {digits[key >> 4u], digits[key & 15u], '\0'};
    debug_write("KEY "); debug_write(id); debug_write(down ? " down" : " up");
    value(" level=", level); debug_write("\r\n");
}

void keyboard_live_init(void)
{
    optical_transport_init(&s_transport);
    keyboard_scan_init(&s_scan, 0u);
    scan_stream_init();
    s_stream_requested = false;
    keyboard_app_init(&s_app,&s_raw,&s_midi,&s_menu,&s_cal,&app_ops);
    memset(&s_cal_store,0,sizeof(s_cal_store));s_cal_store.slot=255;
    s_gui_sequence = s_gui_ack = s_last_gui = 0u;
    s_gui_result = 0u;
    travel_lighting_init(&s_lighting);
    release_host();
}

void keyboard_live_control_bind(void)
{
    midi_control_command_handler(keyboard_live_command);
    midi_control_bind_application(&s_app);
}
void keyboard_live_usb_reset(void) { s_usb_reset = true; scan_stream_usb_reset(); }

void keyboard_live_service(void)
{
    const uint32_t now = board_millis();
    if (s_usb_reset) {
        s_usb_reset = false; release_host();
        calibration_abort(&s_cal,CAL_INVALID,now);
    }
    /* USB first; one optical route attempt; never restart on a fault/reset. */
    if (s_transport.phase == OPT_OFF && usb_composite_ready())
        (void)optical_transport_start(&s_transport, now);
    const uint8_t phase = s_transport.phase;
    const keyboard_config_t previous = s_scan.engine.config;
    if (optical_transport_service(&s_transport, now, optical_bus_ticks()))
    {
        if (s_stream_requested)
        {
            scan_stream_start();
            scan_stream_push(s_transport.samples, s_transport.count, s_transport.profile, optical_bus_ticks());
        }
        if (!s_scan.count) keyboard_scan_init(&s_scan, s_transport.profile);
        keyboard_scan_frame(&s_scan, s_transport.samples, s_transport.tables[4], s_transport.tables[6], event);
        keyboard_app_frame(&s_app,s_transport.samples,s_transport.count,s_transport.profile,
                           s_scan.lower,s_scan.upper,s_scan.ready && s_scan.valid && usb_composite_ready(),now);
        (void)device_store_apply(&s_cal_store,&s_app);
        (void)device_store_service(&s_cal_store,&s_app,now,flash_calibration_read,flash_calibration_write);
        if (s_lighting.phase == LIGHT_OFF)
            (void)travel_lighting_start(&s_lighting, s_transport.profile, now);
        travel_lighting_frame(&s_lighting, s_transport.samples, s_scan.lower, s_scan.upper,
                              s_scan.ready && s_scan.valid, now);
        keyboard_app_set_caps_lock(&s_app,(usb_keyboard_leds() & KEYBOARD_HID_LED_CAPS_LOCK)!=0u);
        keyboard_app_lights(&s_app,s_scan.lower,s_scan.upper,s_lighting.desired,now);
    }
    if (phase != s_transport.phase &&
        (s_transport.phase == OPT_FAULT || s_transport.phase == OPT_SCAN_READ)) scan_status();
    if (memcmp(&previous, &s_scan.engine.config, sizeof(previous))) config_status();
    const uint8_t light_phase = s_lighting.phase;
    if (s_transport.phase != OPT_SCAN_READ || !usb_composite_ready()) s_lighting.frame_valid = false;
    travel_lighting_service(&s_lighting, now);
    if (light_phase != s_lighting.phase &&
        (s_lighting.phase == LIGHT_RUN || s_lighting.phase == LIGHT_FAULT)) lighting_status();
    keyboard_app_service(&s_app,now,s_transport.phase==OPT_SCAN_READ && usb_composite_ready(),
                         usb_keyboard_send,usb_midi_send);
    s_host_keys=s_raw.armed && !calibration_active(&s_cal);
    gui_snapshot(now);
}

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool decimal(const char **text, uint32_t *value)
{
    const char *p = *text;
    if (*p < '0' || *p > '9') return false;
    *value = 0u;
    while (*p >= '0' && *p <= '9')
    {
        const unsigned digit = (unsigned)(*p++ - '0');
        if (*value > (UINT32_MAX - digit) / 10u) return false;
        *value = *value * 10u + digit;
    }
    *text = p;
    return true;
}

bool keyboard_live_command(const char *line)
{
    /* Build identity: version plus build target, e.g. v0.1.0-RZ03-0499. */
    if (!strcmp(line, "version")) { debug_write("build=" MT_BUILD_INFO "\r\n"); return true; }
    if (!strncmp(line, "dump read ", 10u)) {
        const char *p = line + 10u;
        uint32_t id = 0, address = 0;
        if (decimal(&p, &id) && id && *p++ == ' ' && decimal(&p, &address) && !*p && scan_stream_dump_ready()) {
            uint8_t out[FLASH_DUMP_SIZE];
            s_stream_requested = false;
            flash_dump_record(id, address, out);
            scan_stream_dump_push(out);
        }
        return true;
    }
    if (!strcmp(line, "stream gui")) {
        scan_stream_gui(); s_stream_requested = true; return true;
    }
    if(keyboard_app_command(&s_app,line,board_millis(),s_scan.ready && s_scan.valid &&
        usb_composite_ready() && s_transport.phase==OPT_SCAN_READ,&s_gui_ack,&s_gui_result)) return true;
    /* No scan/lighting/config side effects may interfere with a staged
     * calibration. GUI snapshots and read-only dump commands above still work. */
    if (calibration_active(&s_cal)) { debug_write("ERR calibration active; cfg calcancel ID to cancel\r\n"); return true; }
    if (!strcmp(line,"menu status")) {
        /* While a menu owns input the engine stays disarmed, so down[] is not
         * a live Fn indicator; report the same raw-sample test the menu uses. */
        value("MENU fn=",s_menu.fn<s_raw.count && s_raw.raw[s_menu.fn]<=s_raw.release[s_menu.fn]);
        value(" mode=",s_raw.engine.config.mode);
        value(" level=",s_raw.engine.config.actuation); value(" saved=",s_raw.engine.config.saved_actuation);
        value(" brightness=",s_menu.brightness); value("/19 pwm=",keyboard_menu_brightness(&s_menu));
        value(" effect=",s_menu.effect);
        value(" reset_confirm=",s_menu.reset_confirmation); value(" ready=",s_menu.confirmation_ready);
        value(" lower_muted=",s_midi.lower_muted);
        value(" root=",s_midi.music.root); value(" scale=",s_midi.music.scale);
        value(" music_page=",s_menu.music_page); value(" janko=",s_midi.janko);
        value(" velocity_start=",s_midi.velocity_start);
        debug_write(" build=" MT_BUILD_INFO);
        debug_write(" key="); debug_write(midi_root_names[s_midi.music.root]);
        debug_write(" scale_name="); debug_write(midi_scales[s_midi.music.scale].name);
        debug_write("\r\n");
        return true;
    }
    if (!strcmp(line, "help"))
    {
        debug_write("scan start | scan stop | scan status | scan sample XX (raw index hex)\r\n"
                    "stream off (stop GUI/capture telemetry)\r\n"
                    "stream key N session sensor (HKL1; threshold 1..4096, uint32 session, sensor 0..64)\r\n"
                    "keys on | keys off | keys status | trace on | trace off\r\n"
                    "keys on requires neutral valid samples; one scan attempt per boot.\r\n");
        debug_write("version | git | stream gui | cfg get ID | cfg set ID SENSOR PRESS RELEASE | cfg all ID PRESS RELEASE | cfg enable ID 0/1\r\n"
                    "menu status; Fn+Tab MIDI trigger point, 1 = bottom-out, 0 = release-1, Esc saves;\r\n"
                    "Fn+V velocity start, Fn+K/L brightness down/up, Fn+\\ White/Rainbow\r\n"
                    "cfg calibrate ID | cfg calcancel ID; Fn+C calibrates in keyboard mode\r\n"
                    "dump read ID ADDRESS (decimal, aligned 64-byte main-flash read; HBD1 binary response)\r\n"
                    "cfg key ID SENSOR USAGE (0=off, 4..231; Fn fixed)\r\n"
                    "cfg midi ID SENSOR NOTE (0..127, 255=unmapped); Fn+Enter toggles MIDI; RAlt/RCtrl octave-/+\r\n"
                    "cfg velocity ID LEVEL (1..10, Fn+V: 0% .. 100% transmitted-velocity start)\r\n"
                    "cfg clean ID (erase custom settings and calibration, like Fn+R)\r\n"
                    "Standalone raw keyboard auto-arms after neutral; settings save automatically.\r\n");
        debug_write("light on | light off | light status\r\n"
                    "Scanning and travel lighting start automatically after USB configuration.\r\n");
        return true;
    }
    if (!strcmp(line, "light on")) { s_lighting.requested = true; lighting_status(); return true; }
    if (!strcmp(line, "light off")) { s_lighting.requested = false; lighting_status(); return true; }
    if (!strcmp(line, "light status")) { lighting_status(); return true; }
    if (!strncmp(line, "stream key ", 11u))
    {
        uint32_t threshold = 0u, session = 0u, sensor = 255u;
        const char *p = line + 11u;
        bool valid = decimal(&p, &threshold) && threshold && threshold <= 4096u;
        if (valid && *p == ' ') { ++p; valid = decimal(&p, &session); }
        if (valid && *p == ' ' && p[1]) { ++p; valid = decimal(&p, &sensor); }
        if (!valid || *p || (sensor > 64u))
        { debug_write("ERR stream key threshold[1..4096] [uint32 session] [sensor 0..64]\r\n"); return true; }
        scan_stream_last_key((uint16_t)threshold, session, (uint8_t)sensor);
        s_stream_requested = true;
    }
    else if (!strcmp(line, "stream off")) { s_stream_requested = false; scan_stream_stop(); }
    else if (!strcmp(line, "status") || !strcmp(line, "scan status")) scan_status();
    else if (!strcmp(line, "scan start"))
    {
        debug_write(optical_transport_start(&s_transport, board_millis()) ?
                    "SCAN starting; host keys remain off\r\n" : "ERR scan already attempted; no automatic retry\r\n");
    }
    else if (!strcmp(line, "scan stop"))
    {
        s_stream_requested = false; scan_stream_stop();
        release_host(); optical_transport_stop(&s_transport); scan_status();
    }
    else if (!strcmp(line, "keys status")) config_status();
    else if (!strcmp(line, "keys off")) {
        keyboard_raw_enable(&s_raw, false);
        release_host(); config_status();
    }
    else if (!strcmp(line, "keys on"))
    {
        keyboard_raw_enable(&s_raw, true);
        config_status();
    }
    else if (!strcmp(line, "trace on")) { s_trace = true; debug_write("TRACE on\r\n"); }
    else if (!strcmp(line, "trace off")) { s_trace = false; debug_write("TRACE off\r\n"); }
    else if (strlen(line) == 14u && !memcmp(line, "scan sample ", 12u) &&
             hex(line[12]) >= 0 && hex(line[13]) >= 0)
    {
        const unsigned i = (unsigned)(hex(line[12]) * 16 + hex(line[13]));
        if (i >= s_scan.count) debug_write("ERR sensor index/layout unavailable\r\n");
        else
        {
            value("SAMPLE index=", i); value(" key=", keyboard_key_for_sensor(s_transport.profile, i));
            value(" raw=", s_scan.raw[i]); value(" lower=", s_scan.lower[i]);
            value(" upper=", s_scan.upper[i]); value(" level=", s_scan.levels[i]);
            value(" pressed=", s_scan.keys[i].pressed); debug_write("\r\n");
        }
    }
    else return false;
    return true;
}
