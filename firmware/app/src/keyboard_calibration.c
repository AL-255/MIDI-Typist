#include "defaults.h"
#include "keyboard_layout.h"
#include "keyboard_calibration.h"
#include "keyboard_lighting.h"
#include <string.h>

static bool layout_ok(uint8_t p, uint8_t n) { return keyboard_layout_valid(p,n); }
static bool done(const keyboard_calibration_t *s, unsigned i) { return (s->done[i/8u] >> (i%8u)) & 1u; }
bool calibration_active(const keyboard_calibration_t *s) { return s->state >= CAL_RELEASE && s->state <= CAL_SAVE; }
void calibration_init(keyboard_calibration_t *s) { memset(s, 0, sizeof(*s)); s->selected = 255u; }
bool calibration_start(keyboard_calibration_t *s, uint8_t profile, uint8_t count, uint32_t now)
{
    if (calibration_active(s) || !layout_ok(profile,count)) return false;
    calibration_init(s);
    s->profile = profile; s->count = count; s->state = CAL_RELEASE;
    s->since = s->activity = now;
    return true;
}
void calibration_abort(keyboard_calibration_t *s, uint8_t reason, uint32_t now)
{
    if (!calibration_active(s)) return;
    memset(s->lower, 0, sizeof(s->lower)); memset(s->upper, 0, sizeof(s->upper));
    memset(s->done, 0, sizeof(s->done));
    memset(s->holds, 0, sizeof(s->holds));
    s->completed = 0; s->selected = 255;
    s->reason = reason; s->state = reason == CAL_STORAGE ? CAL_ERROR : CAL_ABORTED; s->since = now;
}
void calibration_tick(keyboard_calibration_t *s, bool healthy, uint32_t now)
{
    if (!calibration_active(s)) return;
    if (!healthy) calibration_abort(s,CAL_INVALID,now);
    else if ((uint32_t)(now-s->activity) >= CALIBRATION_IDLE_MS) calibration_abort(s,CAL_TIMEOUT,now);
}
bool calibration_bounds_valid(uint8_t profile, uint8_t count, const uint16_t *lo, const uint16_t *hi)
{
    if (!layout_ok(profile,count)) return false;
    for (unsigned i=0; i<count; ++i)
        if (!lo[i] || hi[i] > 4096u || hi[i] < lo[i]+CALIBRATION_MIN_SPAN_RAW) return false;
    return true;
}
void calibration_frame(keyboard_calibration_t *s, const uint16_t *raw, bool valid, bool neutral, uint32_t now)
{
    if (!calibration_active(s)) return;
    for (unsigned i=0; i<s->count; ++i) if (!raw[i] || raw[i]>4096u) valid=false;
    calibration_tick(s,valid,now);
    if (!calibration_active(s) || s->state == CAL_SAVE) return;
    if (s->state == CAL_RELEASE || s->state == CAL_SETTLE) {
        if (!neutral) { s->state=CAL_RELEASE; return; }
        if (s->state == CAL_RELEASE) { s->state=CAL_SETTLE; s->since=now; s->activity=now; }
        if ((uint32_t)(now-s->since)<CALIBRATION_SETTLE_MS) return;
        /* First whole valid frame after 500ms of all keys released. */
        for (unsigned i=0; i<s->count; ++i) {
            if (raw[i]<CALIBRATION_MIN_RELEASE_RAW) { calibration_abort(s,CAL_INVALID,now); return; }
            s->upper[i]=raw[i];
        }
        s->state=CAL_COLLECT; s->activity=now; return;
    }
    s->selected=255;
    for (unsigned key=0; key<s->count; ++key) {
        calibration_hold_t *h=&s->holds[key];
        if (done(s,key)) continue;
        /* Each sensor has its own timer/anchor/mean. Releasing or moving one
         * key cannot reset another key's hold. Completed keys may stay held. */
        if (raw[key]>s->upper[key]/CALIBRATION_PRESS_DIVISOR) { h->active=false; h->samples=0; continue; }
        if (!h->active) {
            h->active=true; s->activity=now;
            h->since=now; h->anchor=raw[key]; h->sum=raw[key]; h->samples=1;
        } else {
            int delta=(int)raw[key]-(int)h->anchor;
            if (delta < -CALIBRATION_MOTION_RAW || delta > CALIBRATION_MOTION_RAW) {
                /* Motion restarts only this key; noise does not indefinitely
                 * renew the global inactivity deadline. */
                h->since=now; h->anchor=raw[key]; h->sum=raw[key]; h->samples=1;
            } else {
                h->sum+=raw[key]; ++h->samples;
                if ((uint32_t)(now-h->since)>=CALIBRATION_HOLD_MS) {
                    s->lower[key]=(uint16_t)((h->sum+h->samples/2u)/h->samples);
                    s->done[key/8u] |= 1u << (key%8u);
                    ++s->completed; s->activity=now; h->active=false;
                }
            }
        }
        if (h->active && s->selected==255u) s->selected=key;
    }
    if (s->completed==s->count) s->state=CAL_SAVE;
}
void calibration_finish(keyboard_calibration_t *s, bool success, uint32_t now)
{
    if (s->state != CAL_SAVE) return;
    if (!success) calibration_abort(s,CAL_STORAGE,now);
    else { s->state=CAL_DONE; s->since=now; }
}
void calibration_lights(const keyboard_calibration_t *s, uint8_t *rgb, uint32_t now)
{
    if (!layout_ok(s->profile,s->count) || s->state == CAL_IDLE) return;
    if (!calibration_active(s) && (uint32_t)(now-s->since)>=CALIBRATION_RESULT_MS) return;
    for (unsigned i=0; i<s->count; ++i) {
        static const uint8_t palette[][3] = {
            {COLOR_CAL_PENDING}, {COLOR_CAL_REGISTERED}, {COLOR_CAL_RELEASE},
            {COLOR_CAL_HOLD}, {COLOR_CAL_ERROR}
        };
        unsigned color = done(s,i) ? 1u : 0u;
        if (s->state == CAL_RELEASE || s->state == CAL_SETTLE) color=2;
        if (s->state == CAL_COLLECT && s->holds[i].active) color=3;
        if (s->state == CAL_ABORTED || s->state == CAL_ERROR) color=4;
        keyboard_light_set(s->profile,i,rgb,palette[color][0],palette[color][1],palette[color][2]);
    }
}
