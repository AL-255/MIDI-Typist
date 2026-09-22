#include "defaults.h"
#include "keyboard_midi.h"
#include "keyboard_layout.h"
#include "keyboard_lighting.h"
#include <string.h>

#define MIDI_WHEEL_SPAN_RAW ((int)(MIDI_WHEEL_RELEASE_RAW - MIDI_WHEEL_PRESSED_RAW))

enum { ROLE_NOTE, ROLE_FN, ROLE_ENTER, ROLE_DOWN, ROLE_UP,
       ROLE_MODULATION, ROLE_BEND_DOWN, ROLE_BEND_UP, ROLE_SUSTAIN };

static void clear_voices(keyboard_midi_t *s)
{
    memset(s->active, 255, sizeof(s->active));
    memset(s->pending, 255, sizeof(s->pending));
    memset(s->pending_mask, 0, sizeof(s->pending_mask));
    memset(s->current, 255, sizeof(s->current));
    memset(s->released, 0, sizeof(s->released));
    memset(s->previous, 0, sizeof(s->previous));
    memset(s->refs, 0, sizeof(s->refs));
    memset(s->pressure, 0, sizeof(s->pressure));
    memset(s->sent_pressure, 255, sizeof(s->sent_pressure));
    s->head = s->count = 0;
    s->was_armed = false;
    s->bend=8192; s->modulation=0; s->wheel_sweep=0;
    s->sustain=false;
}

void keyboard_midi_init(keyboard_midi_t *s)
{
    memset(s, 0, sizeof(*s));
    memset(s->mapping, 255, sizeof(s->mapping));
    clear_voices(s);
    s->sent_bend=8192;
    _Static_assert(DEFAULT_MIDI_SCALE < MIDI_SCALE_COUNT, "invalid default scale");
    s->music=(midi_music_config_t){DEFAULT_MIDI_ROOT,DEFAULT_MIDI_SCALE};
    s->mode=DEFAULT_MIDI_MODE; s->janko=DEFAULT_MIDI_JANKO;
    s->lower_muted=DEFAULT_MIDI_LOWER_MUTED; s->octave=DEFAULT_MIDI_OCTAVE;
    s->velocity_start=DEFAULT_MIDI_VELOCITY_START; /* 0%: the measured velocity is transmitted unchanged */
}

void keyboard_midi_abort(keyboard_midi_t *s)
{
    bool cleanup=s->mode || s->host_dirty || s->panic;
    clear_voices(s);
    /* Sustain off first, individual Note Offs, All Sound Off/All Notes Off.
     * Also covers an IN packet already accepted before reset/mode change.
     * Never restart an in-progress sweep on repeated invalid frames. */
    /* Keyboard-only menus/calibration have no MIDI host state to release.
     * Sending a needless burst can wedge flash handoff when no MIDI reader
     * has opened the USB endpoint. Never drop an existing cleanup or actual
     * performance state, including a packet already accepted by the port. */
    if (cleanup && !s->panic) s->panic = MIDI_CLEANUP_EVENTS;
}

void keyboard_midi_guard(keyboard_midi_t *s, keyboard_raw_t *raw)
{
    if (s->was_armed && !raw->armed) keyboard_midi_abort(s);
}

void keyboard_midi_toggle(keyboard_midi_t *s, keyboard_raw_t *raw, uint32_t now)
{
    s->mode ^= 1u;
    raw->midi_mode=s->mode!=0;
    ++s->changes; s->changed_at=now;
    keyboard_midi_abort(s);
    keyboard_raw_invalidate(raw);
}

static uint8_t default_note(uint8_t usage)
{
    /* Scientific note names: C4=60, C5=72, C6=84. Two playable rows. */
    static const uint8_t map[][2] = DEFAULT_MIDI_NOTE_MAP;
    for (unsigned i = 0; i < sizeof(map)/sizeof(map[0]); ++i)
        if (map[i][0] == usage) return map[i][1];
    return MIDI_UNMAPPED;
}

void keyboard_midi_toggle_lower(keyboard_midi_t *s, keyboard_raw_t *raw)
{
    if (!s->mode) return;
    s->lower_muted=!s->lower_muted;
    keyboard_midi_abort(s); /* includes pending strikes and shared-pitch owners */
    keyboard_raw_invalidate(raw); /* all keys neutral before new note edges */
}


/* Jankó mode (Fn+J in MIDI mode): a staggered whole-tone layout replacing the
 * configured notes for the keys below. Physical HID usages keep this table
 * portable across board layouts; keys that are not listed (the bottom-row
 * controls, modifier roles, space) keep their configured mapping and role.
 * Rows: number row including Backspace, Tab row including backslash, Caps row
 * including Enter, Shift row with the two Shift keys. */
static const uint8_t janko_notes[][2] = DEFAULT_JANKO_NOTE_MAP;

/* Left Shift and Right Shift are part of the Jankó rows but the board tables
 * carry their modifier mask in arg0 with a zero usage (the same convention the
 * menu uses for Fn+Left Shift). Every other modifier keeps its control role. */

static uint8_t janko_note(const keyboard_action_t *a)
{
    if (!a || a->type != 2u) return MIDI_UNMAPPED;
    if (a->arg0 == 2u) return JANKO_LEFT_SHIFT;
    if (a->arg0 == 32u) return JANKO_RIGHT_SHIFT;
    if (a->arg0) return MIDI_UNMAPPED;
    for (unsigned i = 0; i < sizeof(janko_notes)/sizeof(janko_notes[0]); ++i)
        if (janko_notes[i][0] == a->arg1) return janko_notes[i][1];
    return MIDI_UNMAPPED;
}

/* Note a key plays now: the configured mapping, or the Jankó layout entry
 * while the mode is on. Keys without a Jankó entry keep their mapping. */
static uint8_t note_mapping(const keyboard_midi_t *s, unsigned sensor)
{
    if (!s->janko || !s->profile) return s->mapping[sensor];
    const uint8_t key = keyboard_key_for_sensor(s->profile, sensor);
    const keyboard_action_t *a = keyboard_action(s->profile, key, 0);
    if (!a || a->type != 2u) return s->mapping[sensor];
    const uint8_t note = janko_note(a);
    return note == MIDI_UNMAPPED ? s->mapping[sensor] : note;
}

/* Transmitted-velocity start: level 1 transmits the measured 0..1 estimate
 * unchanged, level 10 transmits every note at full velocity, and the levels
 * between raise the floor while keeping the top of the curve at 127. The
 * modal editor keeps all keys out of HID/MIDI while it is open, so no voice
 * cleanup or raw rearm is needed here; the value only affects future notes.
 * It is accepted in either mode: it shapes MIDI output only, the host cannot
 * toggle MIDI mode (Fn+Enter does), and silently discarding the write would
 * make cfg velocity acknowledge a change that never happened. */
void keyboard_midi_set_velocity_start(keyboard_midi_t *s, unsigned level)
{
    if(level<1u || level>10u) return;
    s->velocity_start=(uint8_t)level;
}

static uint8_t velocity_floor(const keyboard_midi_t *s)
{
    return (uint8_t)(((unsigned)(s->velocity_start ? s->velocity_start-1u : 0u) * 127u) / 9u);
}

void keyboard_midi_toggle_janko(keyboard_midi_t *s, keyboard_raw_t *raw)
{
    if (!s->mode) return; /* the layout only exists in MIDI mode */
    s->janko = !s->janko;
    keyboard_midi_abort(s); /* release notes before re-labelling the keys */
    keyboard_raw_invalidate(raw); /* all keys neutral before new note edges */
}

static bool note_enabled(const keyboard_midi_t *s, unsigned sensor)
{
    const uint8_t base=note_mapping(s,sensor);
    const int note=(int)base+12*(int)s->octave;
    /* Jankó mode always enables the lower row: Fn+Left Shift is ineffective. */
    const bool muted=!s->janko && s->lower_muted &&
        (s->lower_rows[sensor/8u] & (1u<<(sensor%8u)));
    return base!=MIDI_UNMAPPED && note>=0 &&
        midi_music_contains(&s->music,(unsigned)note) && !muted;
}

bool keyboard_midi_select_music(keyboard_midi_t *s, keyboard_raw_t *raw, unsigned root, unsigned scale)
{
    if(!s->mode || root>=12u || scale>=MIDI_SCALE_COUNT) return false;
    s->music=(midi_music_config_t){root,scale};
    ++raw->revision;
    keyboard_midi_abort(s);
    keyboard_raw_invalidate(raw);
    return true;
}

static void layout(keyboard_midi_t *s, const keyboard_raw_t *raw)
{
    if (s->profile) keyboard_midi_abort(s);
    s->profile = raw->profile;
    memset(s->mapping, 255, sizeof(s->mapping));
    memset(s->role, 0, sizeof(s->role));
    memset(s->lower_rows,0,sizeof(s->lower_rows));
    for (unsigned i = 0; i < raw->count; ++i) {
        const uint8_t key = keyboard_key_for_sensor(raw->profile, i);
        if (keyboard_lower_group(raw->profile,key)) s->lower_rows[i/8u]|=1u<<(i%8u);
        const keyboard_action_t *a = keyboard_action(raw->profile, key, 0);
        if (key == keyboard_layout(raw->profile)->fn) s->role[i] = ROLE_FN;
        else if (a && a->type == 2) {
            if (a->arg0 == 64) s->role[i] = ROLE_DOWN;
            else if (a->arg0 == 16) s->role[i] = ROLE_UP;
            else if (a->arg0 == 8) s->role[i] = ROLE_MODULATION;
            else if (a->arg0 == 1) s->role[i] = ROLE_BEND_DOWN;
            else if (a->arg0 == 4) s->role[i] = ROLE_BEND_UP;
            else if (a->arg1 == 0x2c) s->role[i] = ROLE_SUSTAIN;
            else if (a->arg1 == 0x28) s->role[i] = ROLE_ENTER;
            if (a->arg0 == 2) s->mapping[i] = DEFAULT_MIDI_LEFT_SHIFT_NOTE; /* left Shift: C4 in MIDI only */
            else if (!a->arg0) s->mapping[i] = default_note(a->arg1);
        }
    }
}

static bool enqueue(keyboard_midi_t *s, uint8_t status, uint8_t note, uint8_t value)
{
    if (s->count == MIDI_QUEUE) {
        ++s->errors;
        keyboard_midi_abort(s); /* no silent loss of a required Note Off */
        return false;
    }
    uint8_t *p = s->queue[(s->head + s->count++) % MIDI_QUEUE];
    p[0] = status; p[1] = note; p[2] = value;
    return true;
}

static bool note_off(keyboard_midi_t *s, uint8_t note)
{
    if (!s->refs[note] || --s->refs[note]) return true;
    s->pressure[note] = 0;
    return enqueue(s, 0x80, note, 0);
}

bool keyboard_midi_map(keyboard_midi_t *s, keyboard_raw_t *raw, unsigned sensor, unsigned note)
{
    if (sensor >= raw->count || !s->profile || (note > 127 && note != MIDI_UNMAPPED) ||
        s->role[sensor] == ROLE_FN || s->role[sensor] >= ROLE_DOWN)
        return false;
    keyboard_midi_abort(s);
    s->mapping[sensor] = note;
    ++raw->revision;
    keyboard_raw_invalidate(raw);
    return true;
}

static unsigned wheel_depth(uint16_t value)
{
    return value>=MIDI_WHEEL_RELEASE_RAW ? 0u :
        value<=MIDI_WHEEL_PRESSED_RAW ? MIDI_WHEEL_SPAN_RAW : MIDI_WHEEL_RELEASE_RAW-value;
}

void keyboard_midi_frame(keyboard_midi_t *s, keyboard_raw_t *raw,
                         const uint16_t *lower, const uint16_t *upper, uint32_t now)
{
    (void)now;
    if (raw->profile && s->profile != raw->profile) layout(s, raw);
    keyboard_midi_guard(s, raw);
    if (!raw->armed) return;
    s->was_armed=true;
    if (!s->mode || s->panic) {
        /* Keep edge history while normal keyboard mode/cleanup is active,
         * without searching every key for MIDI-only controls. Cleanup itself
         * remains owned by service(), and mode changes still invalidate input. */
        memcpy(s->previous, raw->down, sizeof(s->previous));
        return;
    }
    bool fn = false;
    int shift = 0;
    for (unsigned i = 0; i < raw->count; ++i) {
        if (s->role[i] == ROLE_FN && raw->down[i]) fn = true;
        if (raw->down[i] && !s->previous[i]) {
            if (s->role[i] == ROLE_UP) ++shift;
            if (s->role[i] == ROLE_DOWN) --shift;
        }
    }
    if (s->mode && !fn && shift) {
        int octave = s->octave + shift;
        s->octave = octave < -MIDI_OCTAVE_LIMIT ? -MIDI_OCTAVE_LIMIT : octave > MIDI_OCTAVE_LIMIT ? MIDI_OCTAVE_LIMIT : octave;
    }
    if (s->mode) {
        bool sustain=false;
        for(unsigned i=0;i<raw->count;++i)
            if(s->role[i]==ROLE_SUSTAIN && raw->down[i] && !fn &&
               (s->sustain || !s->previous[i])) sustain=true;
        if(sustain!=s->sustain) {
            /* Ordered with note edges, never coalesced like analog wheels.
             * Same-scan pedal changes precede Note Off/On processing. */
            if(!enqueue(s,0xb0,64,sustain?127:0)) goto overflow;
            s->sustain=sustain;
        }
        int bend=0;
        s->modulation=0;
        if (!fn) for (unsigned i=0; i<raw->count; ++i) {
            if(s->role[i]!=ROLE_MODULATION && s->role[i]!=ROLE_BEND_DOWN &&
               s->role[i]!=ROLE_BEND_UP)continue;
            unsigned depth=wheel_depth(raw->raw[i]);
            if (s->role[i]==ROLE_MODULATION) s->modulation=(depth*127u+(MIDI_WHEEL_SPAN_RAW/2u))/MIDI_WHEEL_SPAN_RAW;
            if (s->role[i]==ROLE_BEND_DOWN) bend-=(int)depth;
            if (s->role[i]==ROLE_BEND_UP) bend+=(int)depth;
        }
        /* Sum travel before quantization: equal opposing pressure is exactly
         * center despite MIDI's asymmetric negative/positive endpoint sizes. */
        s->bend=bend<0 ? 8192-((-bend*8192+(MIDI_WHEEL_SPAN_RAW/2))/MIDI_WHEEL_SPAN_RAW) :
                         8192+((bend*8191+(MIDI_WHEEL_SPAN_RAW/2))/MIDI_WHEEL_SPAN_RAW);
        memset(s->pressure, 0, sizeof(s->pressure));
        for (unsigned i = 0; i < raw->count; ++i) {
            if (s->previous[i] && !raw->down[i]) {
                if (s->current[i] < MIDI_PENDING_STRIKES) s->released[i] |= 1u << s->current[i];
                if (s->active[i] != MIDI_UNMAPPED) {
                    if (!note_off(s, s->active[i])) goto overflow;
                    s->active[i] = MIDI_UNMAPPED;
                }
            }
            /* Buffered notes fire when this key's velocity fit completes. The
             * fit window (up to ten readbacks, cut at bottom-out) closes later
             * than the old fixed five-frame slot rotation, so a Note On always
             * carries the completed estimate of its own press. Taps whose
             * windows were superseded by a newer press fire together with the
             * completed fit's velocity, keeping Note On/Off pairing. A window
             * that closes without a fit (the triggering sample was already
             * below bottom-out) still releases its notes with the last value,
             * so no press can strand a Note On. */
            if (s->pending_mask[i] && !raw->velocity[i].pending) {
                /* The fit window just closed (a completed fit, or a
                 * triggering sample already below bottom-out): the buffered
                 * notes fire with the current velocity value. */
                const unsigned floor = velocity_floor(s);
                unsigned velocity = floor +
                    (unsigned)((127u - floor) * raw->velocity[i].value + 0.5f);
                if (!velocity) velocity = 1; /* Note On zero means Note Off */
                for (unsigned slot = 0; slot < MIDI_PENDING_STRIKES; ++slot) {
                    const uint8_t note = s->pending[i][slot];
                    if (note == MIDI_UNMAPPED) continue;
                    if (!s->refs[note]++ && !enqueue(s, 0x90, note, velocity)) goto overflow;
                    s->sent_pressure[note] = 255;
                    if (s->released[i] & (1u << slot)) {
                        if (!note_off(s, note)) goto overflow;
                    } else s->active[i] = note;
                    if (s->current[i] == slot) s->current[i] = 255;
                    s->pending[i][slot] = MIDI_UNMAPPED;
                }
                s->pending_mask[i]=0;
            }
            if (!fn && raw->down[i] && !s->previous[i] && note_enabled(s,i)) {
                const int shifted = (int)note_mapping(s,i) + (int)s->octave * 12;
                /* Out-of-range notes are muted, never wrapped or clamped. */
                if (shifted >= 0 && shifted <= 127) {
                    unsigned slot = MIDI_PENDING_STRIKES;
                    for (unsigned candidate = 0; candidate < MIDI_PENDING_STRIKES; ++candidate)
                        if (s->pending[i][candidate] == MIDI_UNMAPPED) { slot = candidate; break; }
                    if (slot == MIDI_PENDING_STRIKES) {
                        /* Retriggers can indefinitely postpone the fit. Do
                         * not silently drop a strike when its slots fill. */
                        ++s->errors;
                        keyboard_midi_abort(s);
                        goto overflow;
                    }
                    s->pending[i][slot] = (uint8_t)shifted;
                    s->pending_mask[i] |= 1u << slot;
                    s->released[i] &= ~(1u << slot);
                    s->current[i] = slot;
                }
            }
            if (s->active[i] != MIDI_UNMAPPED) {
                const uint8_t pressure = ((unsigned)lighting_travel_pwm(raw->raw[i], lower[i], upper[i]) * 127u + 127u) / 255u;
                const uint8_t note = s->active[i];
                if (pressure > s->pressure[note]) s->pressure[note] = pressure;
            }
        }
    }
    memcpy(s->previous, raw->down, sizeof(s->previous));
    return;
overflow:
    keyboard_raw_invalidate(raw);
}

void keyboard_midi_service(keyboard_midi_t *s, uint32_t now, midi_send_fn send)
{
    if (s->panic) {
        unsigned index = MIDI_CLEANUP_EVENTS - s->panic;
        bool ok = index==0 ? send(11,0xb0,64,0) :
                  index<=128 ? send(8, 0x80, index-1u, 0) :
                  index<131 ? send(11, 0xb0, index == 129 ? 120 : 123, 0) :
                  index==131 ? send(11,0xb0,1,0) : send(14,0xe0,0,64);
        if (ok) {
            if (index==131) s->sent_modulation=0;
            if (index==132) s->sent_bend=8192;
            --s->panic;
            if(!s->panic)s->host_dirty=false;
        }
        return;
    }
    if (s->count) {
        const uint8_t *p = s->queue[s->head];
        if (send(p[0] >> 4u, p[0], p[1], p[2])) {
            s->host_dirty=true;
            s->head = (s->head + 1u) % MIDI_QUEUE;
            --s->count;
        }
        return;
    }
    /* Latest-value registers, not the note FIFO. Check both controllers once
     * per millisecond; a busy endpoint retains only their newest positions. */
    if (!s->wheel_sweep && (uint32_t)(now-s->wheel_at)>=MIDI_WHEEL_PERIOD_MS) {
        s->wheel_sweep=3; s->wheel_at=now;
    }
    if (s->wheel_sweep & 1u) {
        if (s->modulation!=s->sent_modulation) {
            if (!send(11,0xb0,1,s->modulation)) return;
            s->host_dirty=true;
            s->sent_modulation=s->modulation;
        }
        s->wheel_sweep &= ~1u;
    }
    if (s->wheel_sweep & 2u) {
        if (s->bend!=s->sent_bend) {
            if (!send(14,0xe0,s->bend & 127u,s->bend>>7u)) return;
            s->host_dirty=true;
            s->sent_bend=s->bend;
        }
        s->wheel_sweep &= ~2u;
    }
    if (!s->pressure_sweep && (uint32_t)(now - s->pressure_at) >= MIDI_PRESSURE_PERIOD_MS) {
        s->pressure_sweep = true; s->pressure_cursor = 0; s->pressure_at = now;
    }
    while (s->pressure_sweep) {
        const unsigned note = s->pressure_cursor;
        if (s->refs[note] && s->pressure[note] != s->sent_pressure[note]) {
            if (!send(10, 0xa0, note, s->pressure[note])) return;
            s->host_dirty=true;
            s->sent_pressure[note] = s->pressure[note];
        }
        if (++s->pressure_cursor == 128) s->pressure_sweep = false;
    }
}

/* Jankó layout marker: the keys that play accidentals are the piano's black
 * keys. Pitch class alone decides, so the marker follows the octave offset and
 * every row stays readable at a glance. */
static bool janko_black_key(const keyboard_midi_t *s, unsigned sensor)
{
    const uint8_t note = note_mapping(s, sensor);
    if (note == MIDI_UNMAPPED) return false;
    switch (note % 12u) {
        case 1u: case 3u: case 6u: case 8u: case 10u: return true;
        default: return false;
    }
}
void keyboard_midi_lights(const keyboard_midi_t *s, uint8_t *frame, uint32_t now)
{
    if (!s->profile) return;
    unsigned magnitude = s->octave < 0 ? -(int)s->octave : s->octave;
    if (magnitude > MIDI_OCTAVE_LIMIT) magnitude = MIDI_OCTAVE_LIMIT;
    /* Full period 1200 ms at +/-1, down to 120 ms at +/-10. Minimum
     * half-period 60 ms stays above the LED scheduler's 40 ms frame period. */
    const unsigned half_period = MIDI_OCTAVE_BLINK_STEP_MS * (MIDI_OCTAVE_LIMIT + 1u - magnitude);
    const bool blink_on = (now / half_period) % 2u == 0u;
    const unsigned count = keyboard_layout_count(s->profile);
    for (unsigned i = 0; i < count; ++i) {
        const bool octave_key = s->mode &&
            ((s->octave < 0 && s->role[i] == ROLE_DOWN) ||
             (s->octave > 0 && s->role[i] == ROLE_UP));
        if (s->mode && !note_enabled(s,i))
            keyboard_light_set(s->profile,i,frame,0,0,0);
        /* Yellow marks the black keys of the Jankó layout. Only the hue
         * changes: the travel intensity painted a moment ago is kept, so a
         * black key dims and brightens exactly like a white one. */
        else if (s->mode && s->janko && janko_black_key(s,i)) {
            uint8_t red, green, blue;
            keyboard_light_get(s->profile,i,frame,&red,&green,&blue);
            const uint8_t level = red > green ? (red > blue ? red : blue)
                                              : (green > blue ? green : blue);
            keyboard_light_set(s->profile,i,frame,level,level,0u);
        }
        /* Mode/octave hints remain explicit overlays, not note backlighting. */
        if (s->role[i] != ROLE_ENTER && !(s->mode && s->role[i]>=ROLE_DOWN)) continue;
        if (octave_key) {
            if(blink_on) keyboard_light_set(s->profile,i,frame,COLOR_MIDI);
            else keyboard_light_set(s->profile,i,frame,0,0,0);
            continue;
        }
        if(s->mode) keyboard_light_set(s->profile,i,frame,COLOR_MIDI);
        else keyboard_light_set(s->profile,i,frame,COLOR_CONFIRM);
    }
}
