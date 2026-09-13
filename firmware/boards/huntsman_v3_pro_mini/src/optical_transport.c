#include "defaults.h"
#include "optical_transport.h"
#include "optical_bus.h"
#include <string.h>

/* Exact mode/request/reply/length table at production 0x2001ddd6. */
static const uint8_t s_modes[10] = {2,3,4,5,6,7,8,9,10,11};
static const uint8_t s_commands[10] = {0xa2,0xa3,0xa3,0xa4,0xa4,0xa4,0xa4,0xa4,0xa4,0xa5};
static const uint8_t s_replies[10] = {0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab};
static const uint16_t s_lengths[10] = {9,67,67,197,197,197,197,197,197,67};

void optical_transport_init(optical_transport_t *s) { memset(s, 0, sizeof(*s)); }

bool optical_transport_start(optical_transport_t *s, uint32_t now)
{
    if (s->phase != OPT_OFF) return false; /* one route sequence per boot */
    optical_bus_begin();
    s->phase = OPT_ROUTE;
    s->since = now;
    return true;
}

void optical_transport_stop(optical_transport_t *s)
{
    if (s->phase == OPT_OFF || s->phase == OPT_STOPPED || s->phase == OPT_FAULT) return;
    if (s->enabled) optical_bus_quarantine();
    s->phase = OPT_STOPPED;
}

static bool fault(optical_transport_t *s, const char *reason)
{
    if (s->enabled) optical_bus_quarantine();
    s->fault = reason;
    s->phase = OPT_FAULT;
    ++s->errors;
    return false;
}

static void submit(optical_transport_t *s, uint8_t command, uint8_t arg, uint16_t size, uint32_t now)
{
    memset(s->tx, 0, sizeof(s->tx));
    memset(s->rx, 0, sizeof(s->rx));
    s->tx[0] = command;
    s->tx[1] = arg;
    if (s->phase == OPT_PROBE)
    {
        s->tx[4] = 4u;
        memcpy(s->tx + 8u, "getv", 4u);
    }
    if (!optical_bus_submit(s->tx, s->rx, size)) { (void)fault(s, "SPI submit"); return; }
    s->pending = true;
    s->since = now;
    ++s->transfers;
}

bool optical_transport_service(optical_transport_t *s, uint32_t now, uint32_t tick)
{
    if (s->phase == OPT_OFF || s->phase >= OPT_STOPPED) return false;
    if (s->phase == OPT_ROUTE)
    {
        if ((uint32_t)(now - s->since) < OPTICAL_RESET_LOW_MS) return false;
        optical_bus_route();
        s->phase = OPT_ENABLE; s->since = now;
        return false;
    }
    if (s->phase == OPT_ENABLE)
    {
        if ((uint32_t)(now - s->since) < OPTICAL_RESET_HIGH_MS) return false;
        if (!optical_bus_enable()) return fault(s, "SPI init");
        s->enabled = true;
        s->phase = OPT_PROBE; s->since = now;
        return false;
    }
    bool frame = false;
    if (s->pending)
    {
        const int result = optical_bus_result();
        if (result < 0) return fault(s, "SPI completion");
        if (!result)
        {
            if ((uint32_t)(now - s->since) >= OPTICAL_SPI_TIMEOUT_MS) return fault(s, "SPI timeout");
            return false;
        }
        s->pending = false;
        s->since = now;
        switch (s->phase)
        {
            case OPT_PROBE: case OPT_PROBE_REPLY:
                if ((s->rx[0] & 0xf0u) == 0xc0u && (s->rx[1] & 0xf0u) == 0xa0u)
                    s->phase = OPT_TABLE_MODE;
                else if (s->phase == OPT_PROBE) s->phase = OPT_PROBE_REPLY;
                else return fault(s, "unsupported ASIC interface/probe");
                break;
            case OPT_TABLE_MODE: s->phase = OPT_TABLE_READ; break;
            case OPT_TABLE_READ:
                if (s->rx[0] != 0xc0u || s->rx[1] != s_replies[s->selector])
                    return fault(s, "table response header");
                memcpy(s->tables[s->selector], s->rx + 2u, s_lengths[s->selector] - 2u);
                if (!s->selector)
                {
                    s->profile = s->rx[7];
                    if (s->profile < 1u || s->profile > 3u) return fault(s, "unknown ASIC layout");
                    s->count = s->profile == 3u ? 65u : 60u + s->profile;
                }
                if (++s->selector == 10u) s->phase = OPT_SCAN_MODE;
                else s->phase = OPT_TABLE_MODE;
                break;
            case OPT_SCAN_MODE: s->skip = 2u; s->phase = OPT_SCAN_READ; break;
            case OPT_SCAN_READ:
                if (s->rx[0] != 0xc0u || (s->rx[1] != 0xa0u && s->rx[1] != 0xacu))
                    return fault(s, "scan response header");
                if (s->rx[1] == 0xacu) ++s->markers;
                else if (s->skip) --s->skip;
                else
                {
                    for (unsigned i = 0; i < s->count; ++i)
                        s->samples[i] = s->rx[2u + i * 2u] | (uint16_t)s->rx[3u + i * 2u] << 8u;
                    ++s->frames;
                    frame = true;
                }
                break;
            default: return fault(s, "invalid scan state");
        }
    }
    /* A transfer per nominal timer tick at most; USB remains higher priority.
     * Setup waits are intentionally conservative >=1 ms between commands. */
    if (s->tick == tick) return frame;
    s->tick = tick;
    if (s->phase != OPT_SCAN_READ && (uint32_t)(now - s->since) < 1u) return frame;
    if (!optical_bus_ready())
    {
        if ((uint32_t)(now - s->since) >= OPTICAL_READY_TIMEOUT_MS) return fault(s, "ASIC ready timeout");
        return frame;
    }
    switch (s->phase)
    {
        case OPT_PROBE: submit(s, 0x30u, 0u, 12u, now); break;
        case OPT_PROBE_REPLY: submit(s, 0u, 0u, 24u, now); break;
        case OPT_TABLE_MODE: submit(s, 0xb6u, s_modes[s->selector], 2u, now); break;
        case OPT_TABLE_READ: submit(s, s_commands[s->selector], 0u, s_lengths[s->selector], now); break;
        case OPT_SCAN_MODE: submit(s, 0xb6u, 0u, 2u, now); break;
        case OPT_SCAN_READ: submit(s, 0xa0u, 0u, (uint16_t)(2u + 2u * s->count), now); break;
        default: break;
    }
    return frame;
}
