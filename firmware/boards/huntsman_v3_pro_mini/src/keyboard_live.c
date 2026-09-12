#include "keyboard_live.h"
#include "board.h"
#include "debug.h"
#include "keyboard_scan.h"
#include "huntsman_layout.h"
#include "optical_bus.h"
#include "optical_transport.h"
#include "usb_composite.h"
#include "scan_stream.h"
#include "keyboard_build.h"
#include <string.h>
#ifdef HUNTSMAN_TRAVEL_LIGHTING
#include "travel_lighting.h"
static travel_lighting_t s_lighting;
#endif

static optical_transport_t s_transport;
static keyboard_scan_t s_scan;
#ifndef HUNTSMAN_KEYBOARD_MODE
static keyboard_report_t s_sent;
static bool s_sent_valid;
static uint32_t s_last_report;
#endif
static bool s_host_keys, s_trace;
static volatile bool s_usb_reset;
static uint32_t s_last_frame;
static bool s_stream_requested;
#ifdef HUNTSMAN_KEYBOARD_MODE
#include "keyboard_app.h"
#include "keyboard_midi.h"
#include "keyboard_menu.h"
#include "flash_dump.h"
#include "calibration_store.h"
static keyboard_raw_t s_raw;
static keyboard_midi_t s_midi;
static keyboard_menu_t s_menu;
/* This is writable application RAM (the bootloader loads the full image
 * here). Keep the independent hold registers out of scarce peripheral SRAMX.
 * keyboard_live_init explicitly initializes the complete state before use. */
static keyboard_calibration_t s_cal __attribute__((section(".calibration_state")));
static calibration_store_t s_cal_store;
static keyboard_app_t s_app;
#define s_sent s_app.sent
#define s_sent_valid s_app.sent_valid
static uint32_t s_gui_sequence, s_gui_ack, s_last_gui;
static uint8_t s_gui_result;

static bool load_calibration(uint8_t profile,uint8_t count,uint16_t *lo,uint16_t *hi)
{
    calibration_store_load(&s_cal_store,profile,count,lo,hi,flash_calibration_read);
    if(s_cal_store.saved) s_scan.calibrated=count;
    return s_cal_store.saved;
}
static bool save_calibration(const keyboard_calibration_t *cal)
{
    bool ok=calibration_store_save(&s_cal_store,cal,flash_calibration_read,flash_calibration_write);
    if(ok) s_scan.calibrated=cal->count;
    return ok;
}
static bool clear_profile(void)
{
    return calibration_store_clear(&s_cal_store,flash_calibration_read,flash_calibration_erase);
}
static void reset_sensors(uint8_t profile) { keyboard_scan_init(&s_scan,profile); }
static const keyboard_app_ops_t app_ops={
    load_calibration,save_calibration,clear_profile,reset_sensors,debug_write
};
static void gui16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8u; }
static void gui32(uint8_t *p, uint32_t v) { gui16(p, v); gui16(p + 2, v >> 16u); }
static void gui_snapshot(uint32_t now)
{
    if (!scan_stream_gui_enabled() || (uint32_t)(now - s_last_gui) < 33u) return;
    s_last_gui = now;
    uint8_t out[SCAN_STREAM_GUI_SIZE] = {0};
    memcpy(out, "HKG", 3u); out[3] = 0u; /* constant magic: frames carry no layout number */
    gui16(out + 4, sizeof(out));
    out[6] = s_midi.velocity_start; /* Fn+V transmitted-velocity start, 1..10 */
    out[7] = s_raw.profile; out[8] = s_raw.count;
    out[9] = s_raw.enabled | (s_raw.armed << 1u) | (s_raw.valid << 2u) |
             ((s_transport.phase == OPT_FAULT) << 3u) | ((s_lighting.phase == LIGHT_FAULT) << 4u) |
             ((s_raw.engine.config.fn != 0u) << 5u) | ((s_midi.janko != 0u) << 6u);
    out[10] = s_gui_result;
    out[11] = s_raw.engine.config.mode;
    gui32(out + 12, s_gui_sequence++); gui32(out + 16, s_raw.revision);
    gui32(out + 20, s_gui_ack); gui32(out + 24, s_transport.errors);
    gui32(out + 28, s_lighting.errors);
    for (unsigned i = 0; i < s_raw.count; ++i) {
        gui16(out + 32 + i*2, s_raw.raw[i]);
        gui16(out + 162 + i*2, s_raw.press[i]);
        gui16(out + 292 + i*2, s_raw.release[i]);
        if (s_raw.down[i]) out[422 + i/8] |= 1u << (i%8);
        const keyboard_velocity_t *v = &s_raw.velocity[i];
        _Static_assert(sizeof(float) == sizeof(uint32_t), "GUI float32 size");
        uint32_t bits;
        memcpy(&bits, &v->value, sizeof(bits)); /* preserve IEEE-754 bits */
        gui32(out + 447 + i*4, bits);
        gui32(out + 707 + i*4, v->captures);
        out[967 + i] = v->ready | (v->valid << 1u) | ((v->pending != 0u) << 2u);
        if (calibration_active(&s_cal) && s_cal.holds[i].active) out[967+i] |= 8u;
    }
    memcpy(out + 431, &s_sent, sizeof(s_sent)); /* last accepted USB submission */
    out[9] &= ~32u;
    for (unsigned i = 0; i < s_raw.count; ++i) {
        out[1036 + i] = s_midi.mapping[i];
        if (s_raw.down[i] && keyboard_key_for_sensor(s_raw.profile, i) == KEY_ID_FN) out[9] |= 32u;
    }
    out[1032] = s_midi.mode; out[1033] = (uint8_t)s_midi.octave;
    out[1034] = 1; out[1035] = s_midi.panic != 0;
    gui32(out + 1104, s_midi.errors); gui32(out + 1108, s_midi.changes);
    out[1112]=s_cal.state; out[1113]=s_cal.completed; out[1114]=s_cal.selected;
    out[1115]=calibration_active(&s_cal) | (s_cal_store.saved<<1u) | 4u;
    uint32_t elapsed=s_cal.selected<s_cal.count ? (uint32_t)(now-s_cal.holds[s_cal.selected].since) : 0u;
    uint32_t idle=(uint32_t)(now-s_cal.activity);
    gui16(out+1116,s_cal.state==CAL_COLLECT && s_cal.selected!=255u ? (elapsed<1000u?elapsed:1000u) : 0u);
    gui16(out+1118,calibration_active(&s_cal) && idle<5000u ? 5000u-idle : 0u);
    memcpy(out+1120,s_cal.done,9u); out[1129]=s_cal.reason;
    if (s_cal.selected<s_cal.count) {
        gui16(out+1130,s_cal.upper[s_cal.selected]); gui16(out+1132,s_cal.lower[s_cal.selected]);
    }
    gui32(out+1136,s_cal_store.generation); gui32(out+1140,s_cal_store.error);
    uint32_t checksum = 0;
    for (unsigned i = 0; i < sizeof(out)-4u; i += 2u) checksum += out[i] | (uint16_t)out[i+1] << 8u;
    gui32(out + sizeof(out)-4u, checksum);
    scan_stream_gui_push(out);
}
#endif

static void value(const char *label, uint32_t n)
{
    char text[12], *end = text + sizeof(text) - 1u;
    *end = '\0';
    do { *--end = (char)('0' + n % 10u); n /= 10u; } while (n);
    debug_write(label); debug_write(end);
}

static void config_status(void)
{
#ifdef HUNTSMAN_KEYBOARD_MODE
    const keyboard_config_t *s = &s_raw.engine.config;
#else
    const keyboard_config_t *s = &s_scan.engine.config;
#endif
    value("KEYS host=", s_host_keys); value(" fn=", s->fn); value(" mode=", s->mode);
    value(" act=", s->actuation); value(" rapid=", s->rapid);
    value(" enabled=", s->rapid_enabled); value(" saved=", s->saved_actuation);
    value(",", s->saved_rapid); value(" revision=", s->revision);
    debug_write(" RAM-only\r\n");
#ifdef HUNTSMAN_KEYBOARD_MODE
    value("RAW enabled=", s_raw.enabled); value(" armed=", s_raw.armed);
    value(" valid=", s_raw.valid); value(" revision=", s_raw.revision);
    debug_write(" per-key Schmitt; press<release; RAM-only\r\n");
#endif
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

#ifdef HUNTSMAN_TRAVEL_LIGHTING
static void lighting_status(void)
{
    value("LIGHT phase=", s_lighting.phase); value(" on=", s_lighting.requested);
    value(" profile=", s_lighting.profile); value(" transfers=", s_lighting.transfers);
    value(" frames=", s_lighting.frames); value(" errors=", s_lighting.errors);
    value(" calibrated=", s_scan.calibrated); value(" count=", s_scan.count);
    if (s_lighting.fault) { debug_write(" fault="); debug_write(s_lighting.fault); }
    debug_write(" PWM linear in optical endpoints; not measured millimeters\r\n");
}
#endif

static void release_host(void)
{
    s_host_keys = false;
    s_sent_valid = false; /* retry neutral on busy USB, not only once */
#ifdef HUNTSMAN_KEYBOARD_MODE
    keyboard_app_invalidate(&s_app,board_millis());
#endif
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
    s_stream_requested = true;
#ifdef HUNTSMAN_KEYBOARD_MODE
    keyboard_app_init(&s_app,&s_raw,&s_midi,&s_menu,&s_cal,&app_ops);
    s_cal_store=(calibration_store_t){.slot=255};
    s_gui_sequence = s_gui_ack = s_last_gui = 0u;
    s_gui_result = 0u;
#endif
#ifdef HUNTSMAN_TRAVEL_LIGHTING
    travel_lighting_init(&s_lighting);
#endif
    release_host();
}

void keyboard_live_usb_reset(void) { s_usb_reset = true; scan_stream_usb_reset(); }

void keyboard_live_service(void)
{
    const uint32_t now = board_millis();
    if (s_usb_reset) {
        s_usb_reset = false; release_host();
#ifdef HUNTSMAN_KEYBOARD_MODE
        calibration_abort(&s_cal,CAL_INVALID,now);
#endif
    }
#ifdef HUNTSMAN_TRAVEL_LIGHTING
    /* This preset is a working effect, not an OFF-by-default diagnostic.
     * USB first; one optical route attempt; never restart on a fault/reset. */
    if (s_transport.phase == OPT_OFF && usb_composite_ready())
        (void)optical_transport_start(&s_transport, now);
#endif
    const uint8_t phase = s_transport.phase;
    const keyboard_config_t previous = s_scan.engine.config;
    if (optical_transport_service(&s_transport, now, optical_bus_ticks()))
    {
        s_last_frame = now;
        if (s_stream_requested)
        {
            scan_stream_start();
            scan_stream_push(s_transport.samples, s_transport.count, s_transport.profile, optical_bus_ticks());
        }
        if (!s_scan.count) keyboard_scan_init(&s_scan, s_transport.profile);
        keyboard_scan_frame(&s_scan, s_transport.samples, s_transport.tables[4], s_transport.tables[6], event);
#ifdef HUNTSMAN_KEYBOARD_MODE
        keyboard_app_frame(&s_app,s_transport.samples,s_transport.count,s_transport.profile,
                           s_scan.lower,s_scan.upper,s_scan.ready && s_scan.valid && usb_composite_ready(),now);
#endif
#ifdef HUNTSMAN_TRAVEL_LIGHTING
        if (s_lighting.phase == LIGHT_OFF)
            (void)travel_lighting_start(&s_lighting, s_transport.profile, now);
        travel_lighting_frame(&s_lighting, s_transport.samples, s_scan.lower, s_scan.upper,
                              s_scan.ready && s_scan.valid, now);
#ifdef HUNTSMAN_KEYBOARD_MODE
        keyboard_app_lights(&s_app,s_scan.lower,s_scan.upper,s_lighting.desired,now);
#endif
#endif
    }
    if (phase != s_transport.phase &&
        (s_transport.phase == OPT_FAULT || s_transport.phase == OPT_SCAN_READ)) scan_status();
    if (memcmp(&previous, &s_scan.engine.config, sizeof(previous))) config_status();
#ifdef HUNTSMAN_TRAVEL_LIGHTING
    const uint8_t light_phase = s_lighting.phase;
    if (s_transport.phase != OPT_SCAN_READ || !usb_composite_ready()) s_lighting.frame_valid = false;
    travel_lighting_service(&s_lighting, now);
    if (light_phase != s_lighting.phase &&
        (s_lighting.phase == LIGHT_RUN || s_lighting.phase == LIGHT_FAULT)) lighting_status();
#endif
#ifdef HUNTSMAN_KEYBOARD_MODE
    keyboard_app_service(&s_app,now,s_transport.phase==OPT_SCAN_READ && usb_composite_ready(),
                         usb_keyboard_send,usb_midi_send);
    s_host_keys=s_raw.armed && !calibration_active(&s_cal);
#else
    if (s_host_keys && (!s_scan.valid || (uint32_t)(now - s_last_frame) >= 100u ||
                       s_transport.phase != OPT_SCAN_READ || !usb_cdc_ready()))
    {
        release_host();
        debug_write("KEYS disarmed: stale/invalid scan or CDC disconnected\r\n");
    }
#endif
#ifndef HUNTSMAN_KEYBOARD_MODE
    keyboard_report_t report = {0};
    if (s_host_keys) report = s_scan.engine.report;
    if (!s_sent_valid || memcmp(&s_sent, &report, sizeof(report)) ||
        (uint32_t)(now - s_last_report) >= 1000u)
    {
        if (usb_keyboard_send(&report))
        {
            s_sent = report; s_sent_valid = true; s_last_report = now;
        }
    }
#endif
#ifdef HUNTSMAN_KEYBOARD_MODE
    gui_snapshot(now);
#endif
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
    if (!strcmp(line, "version")) { debug_write("build=" MT_BUILD_ID "\r\n"); return true; }
#ifdef HUNTSMAN_KEYBOARD_MODE
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
#endif
    /* No scan/lighting/config side effects may interfere with a staged
     * calibration. GUI snapshots and read-only dump commands above still work. */
#ifdef HUNTSMAN_KEYBOARD_MODE
    if (calibration_active(&s_cal)) { debug_write("ERR calibration active; cfg calcancel ID to cancel\r\n"); return true; }
    if (!strcmp(line,"menu status")) {
        value("MENU fn=",s_menu.fn<s_raw.count && s_raw.down[s_menu.fn]);
        value(" mode=",s_raw.engine.config.mode);
        value(" level=",s_raw.engine.config.actuation); value(" saved=",s_raw.engine.config.saved_actuation);
        value(" brightness=",s_menu.brightness); value("/19 pwm=",keyboard_menu_brightness(&s_menu));
        value(" reset_confirm=",s_menu.reset_confirmation); value(" ready=",s_menu.confirmation_ready);
        value(" lower_muted=",s_midi.lower_muted);
        value(" root=",s_midi.music.root); value(" scale=",s_midi.music.scale);
        value(" music_page=",s_menu.music_page); value(" janko=",s_midi.janko);
        value(" velocity_start=",s_midi.velocity_start);
        debug_write(" build=" MT_BUILD_ID);
        debug_write(" key="); debug_write(midi_root_names[s_midi.music.root]);
        debug_write(" scale_name="); debug_write(midi_scales[s_midi.music.scale].name);
        debug_write("\r\n");
        return true;
    }
#endif
    if (!strcmp(line, "help"))
    {
        debug_write("scan start | scan stop | scan status | scan sample XX (raw index hex)\r\n"
                    "stream on | stream off (HKS1 binary uint16 scan frames; default on when scanning)\r\n"
                    "stream key N [session [sensor]] (HKL1; decimal threshold 1..4096, optional uint32 session, optional sensor 0..64 pins one key, 255 = first press)\r\n"
                    "keys on | keys off | keys status | trace on | trace off\r\n"
                    "keys on requires neutral valid samples; one scan attempt per boot.\r\n");
#ifdef HUNTSMAN_KEYBOARD_MODE
        debug_write("version | stream gui | cfg get ID | cfg set ID SENSOR PRESS RELEASE | cfg all ID PRESS RELEASE | cfg enable ID 0/1\r\n"
                    "menu status; Fn+Tab MIDI trigger point, 1 = bottom-out, 0 = release-1, Esc saves;\r\n"
                    "Fn+V velocity start, Fn+K/L brightness down/up\r\n"
                    "cfg calibrate ID | cfg calcancel ID; Fn+C calibrates in keyboard mode\r\n"
                    "dump read ID ADDRESS (decimal, aligned 64-byte main-flash read; HBD1 binary response)\r\n"
                    "cfg midi ID SENSOR NOTE (0..127, 255=unmapped); Fn+Enter toggles MIDI; LCtrl/LAlt octave-/+\r\n"
                    "cfg velocity ID LEVEL (1..10, Fn+V: 0% .. 100% transmitted-velocity start)\r\n"
                    "Standalone raw keyboard auto-arms after neutral scan; settings RAM-only.\r\n");
#endif
#ifdef HUNTSMAN_TRAVEL_LIGHTING
        debug_write("light on | light off | light status\r\n"
                    "Scanning and travel lighting start automatically after USB configuration.\r\n");
#else
        debug_write("Scan starts OFF.\r\n");
#endif
        return false; /* also print isolated TEST help */
    }
#ifdef HUNTSMAN_TRAVEL_LIGHTING
    if (!strcmp(line, "light on")) { s_lighting.requested = true; lighting_status(); return true; }
    if (!strcmp(line, "light off")) { s_lighting.requested = false; lighting_status(); return true; }
    if (!strcmp(line, "light status")) { lighting_status(); return true; }
#endif
    if (!strncmp(line, "stream key ", 11u))
    {
        uint32_t threshold = 0u, session = 0u, sensor = 255u;
        const char *p = line + 11u;
        bool valid = decimal(&p, &threshold) && threshold && threshold <= 4096u;
        if (valid && *p == ' ') { ++p; valid = decimal(&p, &session); }
        if (valid && *p == ' ' && p[1]) { ++p; valid = decimal(&p, &sensor); }
        if (!valid || *p || (sensor != 255u && sensor > 64u))
        { debug_write("ERR stream key threshold[1..4096] [uint32 session] [sensor 0..64 or 255]\r\n"); return true; }
        scan_stream_last_key((uint16_t)threshold, session, (uint8_t)sensor);
        s_stream_requested = true;
    }
    else if (!strcmp(line, "stream on")) { scan_stream_whole(); s_stream_requested = true; }
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
#ifdef HUNTSMAN_KEYBOARD_MODE
        keyboard_raw_enable(&s_raw, false);
#endif
        release_host(); config_status();
    }
    else if (!strcmp(line, "keys on"))
    {
#ifdef HUNTSMAN_KEYBOARD_MODE
        keyboard_raw_enable(&s_raw, true);
        config_status();
#else
        if (s_transport.phase != OPT_SCAN_READ || !usb_cdc_ready() ||
            (uint32_t)(board_millis() - s_last_frame) >= 100u || !keyboard_scan_neutral(&s_scan))
            debug_write("ERR keys not armed: scan must be fresh, settled, valid, and all keys released\r\n");
        else { s_host_keys = true; config_status(); }
#endif
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
