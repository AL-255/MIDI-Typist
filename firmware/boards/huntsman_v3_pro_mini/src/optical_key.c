#include "defaults.h"
#include "optical_key.h"

uint8_t optical_key_level(uint16_t lower, uint16_t upper, uint16_t sample)
{
    if (lower >= upper) return 0u;
    if (sample <= lower) return 255u;
    if (sample >= upper) return 0u;
    return (uint8_t)(((uint32_t)(upper - sample) * 256u) / (upper - lower));
}

bool optical_key_calibrate(uint16_t raw_lower, uint16_t raw_upper, uint16_t settled,
                           bool use_settled, uint16_t *lower, uint16_t *upper)
{
    if (use_settled && raw_upper < (uint32_t)settled + OPTICAL_SETTLED_MARGIN_RAW) raw_upper = settled;
    if (!raw_lower || raw_lower == 0xffffu || !raw_upper || raw_upper > 0xfffu ||
        (uint32_t)raw_lower + OPTICAL_FACTORY_MIN_SPAN_RAW >= raw_upper) return false;
    *upper = raw_upper;
    *lower = raw_lower + ((uint32_t)(raw_upper - raw_lower) * OPTICAL_LOWER_TRIM_PERCENT) / 100u;
    return true;
}

int optical_key_update(optical_key_state_t *s, const optical_key_config_t *c, uint8_t level)
{
    if (!c->rapid)
    {
        if (!s->pressed && level > c->press) { s->pressed = 1u; return 1; }
        if (s->pressed && level < c->release) { s->pressed = 0u; return -1; }
        return 0;
    }
    if (s->cooldown) { --s->cooldown; return 0; }
    if (!s->pressed && !s->armed)
    {
        if (level <= c->press) return 0;
        s->armed = s->pressed = 1u;
        s->peak = level;
        s->cooldown = c->wait_down;
        return 1;
    }
    if (!level || (level < c->release && !c->continuous))
    {
        s->armed = 0u;
        s->trough = level;
        if (!s->pressed) return 0;
        s->pressed = 0u;
        s->cooldown = c->wait_up;
        return -1;
    }
    if (!s->pressed)
    {
        if (level < s->trough) { s->trough = level; return 0; }
        if (level == s->trough || (level != 255u && level - s->trough < c->press_delta)) return 0;
        s->peak = level;
        s->pressed = 1u;
        s->cooldown = c->wait_down;
        return 1;
    }
    if (level > s->peak) { s->peak = level; return 0; }
    if (level == s->peak || s->peak - level < c->release_delta) return 0;
    s->trough = level;
    s->pressed = 0u;
    s->cooldown = c->wait_up;
    return -1;
}
