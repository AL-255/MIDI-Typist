#include "defaults.h"
#include "keyboard_app.h"
#include "keyboard_layout.h"
#include <string.h>

static void defaults(keyboard_app_t *s)
{
    keyboard_raw_init(s->raw); s->raw->menu_managed=true;
    keyboard_midi_init(s->midi); keyboard_menu_init(s->menu);
    calibration_init(s->cal);
    s->loaded=s->reset_pending=false;
}
static void log_message(keyboard_app_t *s,const char *message);
bool keyboard_app_reset_profile(keyboard_app_t *s)
{
    if(!s->ops || !s->ops->clear_profile || !s->ops->clear_profile()) {
        log_message(s,"RESET failed; saved profile not confirmed cleared\r\n");
        return false;
    }
    /* SysEx can request RESET during a held chord, without the Fn menu's
     * neutral gate. Release outputs and require a fresh neutral frame. */
    keyboard_app_invalidate(s,s->last_frame);
    keyboard_midi_abort(s->midi);
    s->reset_pending=true;
    log_message(s,"RESET saved profile cleared; release all keys for defaults\r\n");
    return true;
}
void keyboard_app_init(keyboard_app_t *s,keyboard_raw_t *raw,keyboard_midi_t *midi,
                       keyboard_menu_t *menu,keyboard_calibration_t *cal,
                       const keyboard_app_ops_t *ops)
{
    *s=(keyboard_app_t){.raw=raw,.midi=midi,.menu=menu,.cal=cal,.ops=ops};
    defaults(s);
}
void keyboard_app_invalidate(keyboard_app_t *s,uint32_t now)
{
    keyboard_raw_invalidate(s->raw); keyboard_menu_cancel(s->menu);
    calibration_abort(s->cal,CAL_INVALID,now);
    s->frame_valid=s->sent_valid=false;
}
bool keyboard_app_calibrate(keyboard_app_t *s,uint32_t now,bool healthy)
{
    if(!healthy || !s->frame_valid || (uint32_t)(now-s->last_frame)>=SCAN_STALE_MS ||
       s->midi->mode || s->raw->engine.config.mode ||
       !calibration_start(s->cal,s->raw->profile,s->raw->count,now)) return false;
    keyboard_raw_invalidate(s->raw); s->sent_valid=false;
    return true;
}
static void log_message(keyboard_app_t *s,const char *message)
{
    if(s->ops && s->ops->log) s->ops->log(message);
}
void keyboard_app_frame(keyboard_app_t *s,const uint16_t *samples,uint8_t count,
                        uint8_t profile,uint16_t *lo,uint16_t *hi,bool valid,uint32_t now)
{
    s->last_frame=now;
    if(!samples || !lo || !hi || !keyboard_layout_valid(profile,count)) {
        keyboard_app_invalidate(s,now); return;
    }
    keyboard_raw_t *raw=s->raw;
    if(raw->count && (raw->profile!=profile || raw->count!=count)) {
        /* A newly discovered layout must not feed its shorter sample buffer
         * to a calibration staged for the previous keyboard. */
        keyboard_app_invalidate(s,now);
        calibration_init(s->cal);
        s->loaded=s->reset_pending=false;
    }
    raw->midi_mode=s->midi->mode || calibration_active(s->cal);
    const keyboard_config_t before=raw->engine.config;
    keyboard_raw_frame(raw,samples,count,profile,valid);
    s->frame_valid=valid && raw->valid;
    if(s->reset_pending && s->frame_valid) {
        bool neutral=true;
        for(unsigned i=0;i<count;++i) if(samples[i]<=raw->release[i]) neutral=false;
        /* Output may have been disabled through SysEx. Neutrality, not output
         * arming, controls RESET so disabled keyboards can reset too. */
        if(neutral) {
            if(s->ops && s->ops->reset_sensors) s->ops->reset_sensors(profile);
            defaults(s); keyboard_midi_abort(s->midi);
            s->frame_valid=false; /* wait for freshly initialized board samples */
        }
    }
    bool consumed=s->system_input && s->system_input(s,s->system_context,now);
    uint8_t action=consumed?MENU_NONE:keyboard_menu_frame(s->menu,raw,lo,hi,&before,now,
        calibration_active(s->cal),s->midi->lower_muted,&s->midi->music,s->midi->velocity_start);
    if(action==MENU_MODE) keyboard_midi_toggle(s->midi,raw,now);
    if(action==MENU_LOWER) keyboard_midi_toggle_lower(s->midi,raw);
    if(action==MENU_JANKO) keyboard_midi_toggle_janko(s->midi,raw);
    if(action==MENU_VELOCITY_SET) keyboard_midi_set_velocity_start(s->midi,s->menu->selection);
    if(action==MENU_SELECT_KEY) (void)keyboard_midi_select_music(s->midi,raw,s->menu->selection,s->midi->music.scale);
    if(action==MENU_SELECT_SCALE) (void)keyboard_midi_select_music(s->midi,raw,s->midi->music.root,s->menu->selection);
    if(action==MENU_CALIBRATION) (void)keyboard_app_calibrate(s,now,s->frame_valid);
    if(action==MENU_RESET) (void)keyboard_app_reset_profile(s);
    if(!s->loaded && s->frame_valid) {
        if(s->ops && s->ops->load_calibration) (void)s->ops->load_calibration(profile,count,lo,hi);
        s->loaded=true;
    }
    bool active=calibration_active(s->cal),neutral=true;
    if(active) for(unsigned i=0;i<count;++i) if(samples[i]<=raw->release[i]) neutral=false;
    calibration_frame(s->cal,samples,s->frame_valid,neutral,now);
    if(s->cal->state==CAL_SAVE) {
        bool success=s->ops && s->ops->save_calibration && s->ops->save_calibration(s->cal);
        if(success) {
            memcpy(lo,s->cal->lower,count*sizeof(*lo));
            memcpy(hi,s->cal->upper,count*sizeof(*hi));
        }
        calibration_finish(s->cal,success,now);
    }
    if(active && !calibration_active(s->cal)) keyboard_raw_invalidate(raw);
    raw->midi_mode=s->midi->mode || calibration_active(s->cal);
    if(!calibration_active(s->cal)) keyboard_midi_frame(s->midi,raw,lo,hi,now);
}
void keyboard_app_service(keyboard_app_t *s,uint32_t now,bool healthy,
                          keyboard_send_fn keyboard_send,midi_send_fn midi_send)
{
    const bool fresh=healthy && (uint32_t)(now-s->last_frame)<SCAN_STALE_MS;
    bool active=calibration_active(s->cal);
    calibration_tick(s->cal,fresh && s->frame_valid,now);
    if(active && !calibration_active(s->cal)) keyboard_raw_invalidate(s->raw);
    if(!fresh) {
        keyboard_raw_invalidate(s->raw); keyboard_menu_cancel(s->menu);
        s->frame_valid=false;
    }
    keyboard_midi_guard(s->midi,s->raw);
    if(midi_send) keyboard_midi_service(s->midi,now,midi_send);
    keyboard_report_t report={0};
    if(s->raw->armed && !calibration_active(s->cal) && !s->midi->mode) report=s->raw->engine.report;
    if(keyboard_send && (!s->sent_valid || memcmp(&s->sent,&report,sizeof(report)) ||
                        (uint32_t)(now-s->last_report)>=KEYBOARD_REPORT_REFRESH_MS) && keyboard_send(&report)) {
        s->sent=report; s->sent_valid=true; s->last_report=now;
    }
}
void keyboard_app_lights(keyboard_app_t *s,const uint16_t *lo,const uint16_t *hi,
                         uint8_t *frame,uint32_t now)
{
    if(!s->frame_valid) { memset(frame,0,LIGHTING_FRAME_SIZE); return; }
    lighting_travel_frame(s->raw->profile,s->raw->raw,lo,hi,s->frame_valid,frame);
    keyboard_midi_lights(s->midi,frame,now);
    calibration_lights(s->cal,frame,now);
    keyboard_menu_lights(s->menu,s->raw,lo,hi,frame,now,s->midi->mode,
        calibration_active(s->cal) || (s->cal->state!=CAL_IDLE && (uint32_t)(now-s->cal->since)<CALIBRATION_RESULT_MS),
        s->midi->janko);
    if(s->system_lights)s->system_lights(s,s->system_context,frame,now);
}
