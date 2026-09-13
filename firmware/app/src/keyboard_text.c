#include "defaults.h"
#include "keyboard_text.h"
#include "keyboard_layout.h"
#include "keyboard_lighting.h"
#include <string.h>

void keyboard_text_stop(keyboard_text_t *s) { s->length = 0; }
void keyboard_text_color(keyboard_text_t *s, uint8_t r, uint8_t g, uint8_t b)
{
    s->color[0]=r; s->color[1]=g; s->color[2]=b;
}

void keyboard_text_start(keyboard_text_t *s, uint8_t profile, const char *text, uint32_t now)
{
    keyboard_text_stop(s);
    s->started_at = now;
    s->profile = profile;
    keyboard_text_color(s,COLOR_WHITE);
    if (!text || !keyboard_layout_count(profile)) return;
    uint8_t letters[29];
    memset(letters, 255, sizeof(letters));
    const unsigned count = keyboard_layout_count(profile);
    for (unsigned i = 0; i < count; ++i) {
        const keyboard_action_t *a = keyboard_action(profile, keyboard_key_for_sensor(profile, i), 0);
        if (a && a->type == 2 && !a->arg0 && a->arg1 >= 4 && a->arg1 <= 29)
            letters[a->arg1 - 4] = i;
        if (a && a->type == 2 && !a->arg0 && a->arg1 == 0x2d) letters[26]=i;
        if (a && a->type == 2 && !a->arg0 && a->arg1 == 0x2e) letters[27]=i;
        if (a && a->type == 2 && !a->arg0 && a->arg1 == 0x38) letters[28]=i;
    }
    for (; *text && s->length < KEYBOARD_TEXT_MAX; ++text) {
        unsigned char c = (unsigned char)*text;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        unsigned index=c>='A' && c<='Z' ? (unsigned)(c-'A') : c=='-' ? 26u : c=='+' ? 27u : c=='?' ? 28u : 29u;
        if (index<29 && letters[index]!=255) s->sensors[s->length++]=letters[index];
    }
}

static void white(const keyboard_text_t *s, uint8_t *frame, unsigned position, uint8_t pwm)
{
    keyboard_light_set(s->profile,s->sensors[position],frame,
        (s->color[0]*pwm+127u)/255u,(s->color[1]*pwm+127u)/255u,(s->color[2]*pwm+127u)/255u);
}

bool keyboard_text_render(const keyboard_text_t *s, uint8_t *frame, uint32_t now)
{
    if (!s->length) return false;
    memset(frame, 0, LIGHTING_FRAME_SIZE);
    for (unsigned i = 0; i < s->length; ++i) white(s, frame, i, TEXT_BACKGROUND_PWM);
    const uint32_t phase = (uint32_t)(now - s->started_at) % (s->length * TEXT_LETTER_MS + TEXT_REPEAT_PAUSE_MS);
    if (phase < s->length * TEXT_LETTER_MS) white(s, frame, phase / TEXT_LETTER_MS, 255);
    return true;
}
