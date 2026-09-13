#include "defaults.h"
#include "travel_lighting.h"
#include "lighting_bus.h"
#include <string.h>

void travel_lighting_init(travel_lighting_t *s)
{
    memset(s, 0, sizeof(*s));
    s->requested = DEFAULT_LIGHTING_ENABLED;
}

bool travel_lighting_start(travel_lighting_t *s, uint8_t profile, uint32_t now)
{
    if (s->phase != LIGHT_OFF || profile < 1u || profile > 3u) return false;
    s->profile = profile;
    lighting_bus_init();
    lighting_bus_enable_pins(false);
    s->phase = LIGHT_LOW;
    s->since = now;
    return true;
}

static void fault(travel_lighting_t *s, const char *reason)
{
    lighting_bus_quarantine();
    s->phase = LIGHT_FAULT;
    s->fault = reason;
    ++s->errors;
}

void travel_lighting_frame(travel_lighting_t *s, const uint16_t *raw, const uint16_t *lower,
                           const uint16_t *upper, bool valid, uint32_t now)
{
    lighting_travel_frame(s->profile, raw, lower, upper, valid, s->desired);
    s->frame_valid = valid;
    s->last_frame = now;
}

static void submit(travel_lighting_t *s, uint8_t address, uint8_t reg, unsigned size, uint32_t now)
{
    if (!lighting_bus_submit(address, reg, s->tx, size)) { fault(s, "I2C submit"); return; }
    s->pending = true;
    s->since = now;
    ++s->transfers;
}

void travel_lighting_service(travel_lighting_t *s, uint32_t now)
{
    if (s->phase == LIGHT_OFF || s->phase == LIGHT_FAULT) return;
    if (s->phase == LIGHT_LOW)
    {
        if ((uint32_t)(now - s->since) < LIGHTING_RESET_LOW_MS) return;
        lighting_bus_enable_pins(true);
        s->since = now;
        s->phase = LIGHT_HIGH;
        return;
    }
    if (s->phase == LIGHT_HIGH)
    {
        if ((uint32_t)(now - s->since) < LIGHTING_RESET_HIGH_MS) return;
        s->phase = LIGHT_PRIMARY;
    }
    if (s->pending)
    {
        const int result = lighting_bus_result();
        if (result < 0) { fault(s, "I2C completion"); return; }
        if (!result)
        {
            if ((uint32_t)(now - s->since) >= LIGHTING_I2C_TIMEOUT_MS) fault(s, "I2C timeout");
            return;
        }
        s->pending = false;
        s->since = now;
        if (s->phase == LIGHT_RUN)
        {
            if (++s->stage == (s->profile == 3u ? 3u : 1u))
            {
                s->stage = 0;
                ++s->frames;
                if (++s->cycles == LIGHTING_MAINTENANCE_FRAMES)
                {
                    s->cycles = 0;
                    s->phase = LIGHT_MAINTENANCE;
                    s->operation = 0;
                }
            }
        }
        else ++s->operation;
    }
    if ((uint32_t)(now - s->since) < s->delay_ms) return;
    s->delay_ms = 0;
    if (s->phase != LIGHT_RUN)
    {
        const lighting_op_t *ops = g_lighting_primary;
        unsigned count = g_lighting_primary_count;
        if (s->phase == LIGHT_SECONDARY) { ops = g_lighting_secondary; count = g_lighting_secondary_count; }
        if (s->phase == LIGHT_MAINTENANCE) { ops = g_lighting_maintenance; count = g_lighting_maintenance_count; }
        if (s->operation == count)
        {
            s->operation = 0;
            if (s->phase == LIGHT_PRIMARY && s->profile == 3u) s->phase = LIGHT_SECONDARY;
            else { s->phase = LIGHT_RUN; s->last_cycle = now - LIGHTING_FRAME_PERIOD_MS; }
            return;
        }
        const lighting_op_t *op = &ops[s->operation];
        memset(s->tx, op->fill, op->size);
        s->delay_ms = op->delay_ms;
        submit(s, op->address, op->reg, op->size, now);
        return;
    }
    if (s->stage == 0u)
    {
        if ((uint32_t)(now - s->last_cycle) < LIGHTING_FRAME_PERIOD_MS) return;
        s->last_cycle = now;
        if (s->requested && s->frame_valid && (uint32_t)(now - s->last_frame) < SCAN_STALE_MS)
            memcpy(s->snapshot, s->desired, sizeof(s->snapshot));
        else memset(s->snapshot, 0, sizeof(s->snapshot));
        memcpy(s->tx, s->snapshot, 192u);
        submit(s, 0x50u, 0u, 192u, now);
    }
    else if (s->stage == 1u)
    {
        memcpy(s->tx, s->snapshot + 192u, 12u);
        submit(s, 0x6cu, 4u, 12u, now);
    }
    else { s->tx[0] = 0u; submit(s, 0x6cu, 0x13u, 1u, now); }
}
