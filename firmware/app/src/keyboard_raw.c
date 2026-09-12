#include "keyboard_raw.h"
#include "keyboard_layout.h"
#include "keyboard_menu.h"
#include <string.h>

void keyboard_raw_invalidate(keyboard_raw_t *s)
{
    s->armed = s->valid = false;
    memset(s->down, 0, sizeof(s->down));
    for (unsigned i = 0; i < RAW_KEY_COUNT; ++i) {
        keyboard_velocity_t *v = &s->velocity[i];
        v->ready = v->valid = false;
        v->pending = v->count = 0u;
        v->value = 0;
        /* Preserve the completion counter across configuration/faults. A new
         * result requires a fresh release-armed trigger and a closed window. */
    }
    const keyboard_config_t saved=s->engine.config;
    keyboard_engine_init(&s->engine, s->profile);
    if (saved.profile==s->profile && saved.saved_actuation>=1u && saved.saved_actuation<=10u) {
        s->engine.config.saved_actuation=s->engine.config.actuation=saved.saved_actuation;
        s->engine.config.saved_rapid=s->engine.config.rapid=saved.saved_rapid;
        s->engine.config.rapid_enabled=saved.rapid_enabled;
        s->engine.config.locked=saved.locked;
        s->engine.config.revision=saved.revision;
    }
}

void keyboard_raw_init(keyboard_raw_t *s)
{
    memset(s, 0, sizeof(*s));
    for (unsigned i = 0; i < RAW_KEY_COUNT; ++i) {
        s->press[i] = RAW_DEFAULT_PRESS;
        s->release[i] = RAW_DEFAULT_RELEASE;
    }
    s->enabled = true;
    keyboard_raw_invalidate(s);
}

void keyboard_raw_enable(keyboard_raw_t *s, bool enabled)
{
    s->enabled = enabled;
    keyboard_raw_invalidate(s);
}

bool keyboard_raw_set(keyboard_raw_t *s, unsigned index, unsigned press, unsigned release)
{
    /* 4096 is the largest valid sample: a release threshold of 4096 could
     * never be exceeded and would prevent neutral arming forever. */
    if (index >= s->count || !press || press >= release || release >= 4096u) return false;
    s->press[index] = (uint16_t)press;
    s->release[index] = (uint16_t)release;
    ++s->revision;
    /* Config edits must not create an unrequested down edge or leave a stuck
     * host key. Require a fresh neutral frame before reporting again. */
    keyboard_raw_invalidate(s);
    return true;
}

unsigned keyboard_raw_press_level(unsigned level)
{
    /* MIDI-mode trigger range: level 1 is the default (shallowest) actuation
     * point and level 10 reaches the bottom-out floor, so a deep trigger can
     * never sit below the velocity window's closing threshold. */
    if (level < 1u) level = 1u;
    if (level > 10u) level = 10u;
    return RAW_DEFAULT_PRESS -
        ((level - 1u) * (RAW_DEFAULT_PRESS - RAW_BOTTOM_OUT)) / 9u;
}

bool keyboard_raw_set_press_all(keyboard_raw_t *s, unsigned press)
{
    if (!s->count || !press || press >= 4096u) return false;
    for (unsigned i = 0; i < s->count; ++i) {
        const unsigned release = s->release[i];
        if (release < 2u) return false;
        s->press[i] = (uint16_t)(press < release ? press : release - 1u);
    }
    ++s->revision;
    keyboard_raw_invalidate(s); /* one atomic main-loop configuration change */
    return true;
}

bool keyboard_raw_set_all(keyboard_raw_t *s, unsigned press, unsigned release)
{
    if (!s->count || !press || press >= release || release >= 4096u) return false;
    for (unsigned i = 0; i < s->count; ++i) {
        s->press[i] = (uint16_t)press;
        s->release[i] = (uint16_t)release;
    }
    ++s->revision;
    keyboard_raw_invalidate(s); /* one atomic main-loop configuration change */
    return true;
}

static void velocity_finish(keyboard_velocity_t *v, uint32_t sample_hz)
{
    v->pending = 0u;
    unsigned intervals = v->count ? v->count - 1u : 0u;
    v->count = 0u;
    if (!intervals) return; /* triggering sample alone; keep the last result */
    int32_t delta[RAW_VELOCITY_WINDOW-1u], sorted[RAW_VELOCITY_WINDOW-1u], sum = 0;
    for (unsigned i = 0; i < intervals; ++i) {
        delta[i] = (int32_t)v->window[i] - v->window[i+1u];
        sorted[i] = delta[i]; sum += delta[i];
    }
    /* Median filter only for windows longer than five samples (five or more
     * intervals): discard the interval furthest from the median (earliest
     * wins ties). Shorter windows keep every interval, so their speed is
     * exactly d(x)/count. */
    if (intervals > 4u) {
        for (unsigned i = 1; i < intervals; ++i) {
            const int32_t item = sorted[i];
            unsigned j = i;
            while (j && sorted[j-1] > item) { sorted[j] = sorted[j-1]; --j; }
            sorted[j] = item;
        }
        const int32_t twice_median = sorted[(intervals-1u)/2u] + sorted[intervals/2u];
        unsigned outlier = 0;
        int32_t largest = -1;
        for (unsigned i = 0; i < intervals; ++i) {
            int32_t distance = 2 * delta[i] - twice_median;
            if (distance < 0) distance = -distance;
            if (distance > largest) { largest = distance; outlier = i; }
        }
        sum -= delta[outlier];
        --intervals;
    }
    const float raw_velocity = (float)sum * ((float)sample_hz / (float)intervals);
    v->value = raw_velocity <= 0 ? 0.0f : raw_velocity >= 4500000 ? 1.0f
               : raw_velocity / 4500000.0f;
    ++v->captures;
    v->valid = true;
}

static void velocity_frame(keyboard_velocity_t *v, uint16_t raw, bool trigger, bool released, uint32_t sample_hz)
{
    if (released) v->ready = true;
    if (trigger && v->ready) {
        /* A new press always owns the window: the triggering sample is x0 and
         * any unfinished collection is discarded. The window then collects the
         * following readbacks until ten are gathered or the raw value crosses
         * below the bottom-out threshold (that sample is excluded), whichever
         * comes first; very fast presses therefore fit on a few samples. */
        v->ready = false;
        v->count = 1u;
        v->window[0] = raw;
        v->pending = 1u;
    }
    else if (v->pending) {
        if (raw < RAW_BOTTOM_OUT) {
            /* The closing sample is normally excluded. Keep it when the
             * window holds nothing else: an actuation point at or near the
             * bottom-out floor triggers so late that the first follow-up
             * readback is already below the threshold, and dropping it would
             * leave the press without any velocity at all. One interval is
             * still a valid, fast measurement. */
            if (v->count < 2u) v->window[v->count++] = raw;
            velocity_finish(v, sample_hz);
        }
        else {
            v->window[v->count++] = raw;
            if (v->count >= RAW_VELOCITY_WINDOW) velocity_finish(v, sample_hz);
        }
    }
}

void keyboard_raw_frame(keyboard_raw_t *s, const uint16_t *raw, uint8_t count,
                        uint8_t profile, bool valid)
{
    if (!keyboard_layout_valid(profile,count)) {
        keyboard_raw_invalidate(s); return;
    }
    if (s->profile != profile || s->count != count) {
        s->profile = profile; s->count = count;
        keyboard_raw_invalidate(s);
    }
    bool neutral = true;
    for (unsigned i = 0; i < count; ++i) {
        s->raw[i] = raw[i];
        if (!raw[i] || raw[i] > 4096u) valid = false;
        if (raw[i] <= s->release[i]) neutral = false;
    }
    if (!valid) { keyboard_raw_invalidate(s); return; }
    s->valid = true;
    if (!s->armed && s->enabled && neutral) {
        keyboard_engine_release_all(&s->engine);
        memset(s->down, 0, sizeof(s->down));
        s->armed = true;
    }
    bool changed[RAW_KEY_COUNT]={false};
    unsigned fn=RAW_KEY_COUNT;
    for (unsigned i = 0; i < count; ++i) {
        const bool next = s->down[i] ? raw[i] <= s->release[i] : raw[i] < s->press[i];
        velocity_frame(&s->velocity[i], raw[i], next && !s->down[i], raw[i] > s->release[i],keyboard_layout(profile)->sample_hz);
        if (next == s->down[i]) continue;
        s->down[i] = next;
        changed[i]=true;
        if (keyboard_key_for_sensor(profile,i)==keyboard_layout(profile)->fn) fn=i;
    }
    if (s->armed && !s->midi_mode) {
        bool (*event)(keyboard_engine_t *,uint8_t,bool)=s->menu_managed ?
            keyboard_application_event : keyboard_engine_event;
        /* Resolve simultaneous chords independently of ASIC sensor order. */
        if (fn<count) (void)event(&s->engine,keyboard_layout(profile)->fn,s->down[fn]);
        for (unsigned i=0; i<count; ++i)
            if (changed[i] && i!=fn && !(s->menu_managed && !s->engine.config.mode &&
                s->engine.config.fn && keyboard_menu_control(profile,keyboard_key_for_sensor(profile,i))))
                (void)event(&s->engine,keyboard_key_for_sensor(profile,i),s->down[i]);
    }
}
