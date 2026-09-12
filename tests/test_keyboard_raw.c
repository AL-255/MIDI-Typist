#include "keyboard_raw.h"
#include "keyboard_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static keyboard_raw_t s;
static uint16_t raw[61];
static void frame(void) { keyboard_raw_frame(&s, raw, 61, 1, true); }
static bool a(void) { return keyboard_report_get_usage(&s.engine.report, 4); }

/* Explicit test pairs keep the velocity waveform fixtures independent of
 * the user's startup defaults. Main below tests the actual default pair. */
static void velocity_init(keyboard_raw_t *keys)
{
    keyboard_raw_init(keys);
    for (unsigned i=0;i<RAW_KEY_COUNT;++i) {
        keys->press[i]=3600; keys->release[i]=3700;
    }
}

static int compare_interval(const void *a, const void *b)
{
    const int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static double window_oracle(const uint16_t *window, unsigned count)
{
    /* Mirror of the MCU fit: speed = sum of kept intervals / kept count at
     * the declared rate. The median interval filter (discard the interval
     * furthest from the median, earliest wins ties) runs only when more
     * than five samples were collected; shorter windows keep every interval,
     * so their speed is exactly d(x)/count. */
    int intervals[9], ordered[9];
    int sum = 0;
    unsigned kept = count - 1;
    for (unsigned i = 0; i < kept; ++i) {
        intervals[i] = (int)window[i] - (int)window[i+1];
        ordered[i] = intervals[i];
        sum += intervals[i];
    }
    if (count > 5) {
        qsort(ordered, kept, sizeof(*ordered), compare_interval);
        const int twice_median = ordered[(kept-1)/2] + ordered[kept/2];
        unsigned discard = 0;
        int largest = -1;
        for (unsigned i = 0; i < kept; ++i) {
            int distance = 2*intervals[i] - twice_median;
            if (distance < 0) distance = -distance;
            if (distance > largest) { largest = distance; discard = i; }
        }
        sum -= intervals[discard];
        --kept;
    }
    return (double)sum * 8000.0 / (double)kept;
}

static void check_velocity(float actual, double raw_velocity)
{
    const double expected = raw_velocity <= 0 ? 0.0 : raw_velocity >= 4500000 ? 1.0
                            : raw_velocity / 4500000.0;
    const double difference = actual - expected;
    assert(actual >= 0.0f && actual <= 1.0f);
    assert(difference > -0.0000001 && difference < 0.0000001);
}

static void velocity_history_oracle(void)
{
    keyboard_raw_t keys;
    uint16_t values[65];
    bool down[65] = {false};
    bool ready[65] = {false}, pending[65] = {false};
    uint16_t window[65][10];
    unsigned count[65] = {0};
    uint32_t completed[65] = {0}, random = 42;
    double last[65] = {0};
    keyboard_raw_init(&keys);
    keyboard_raw_enable(&keys, false);
    for (unsigned i = 0; i < 65; ++i) values[i] = 3900;
    keyboard_raw_frame(&keys, values, 65, 3, true);
    assert(keyboard_raw_set_all(&keys, 3000, 3300));
    for (unsigned frame = 0; frame < 256; ++frame) {
        for (unsigned i = 0; i < 65; ++i) {
            random = random*1664525u + 1013904223u;
            values[i] = 1200 + random%2401; /* 1200..3600: crosses press, release and bottom-out */
            const bool next = down[i] ? values[i] <= 3300 : values[i] < 3000;
            const bool trigger = next && !down[i];
            if (values[i] > 3300) ready[i] = true;
            if (trigger && ready[i]) {
                ready[i] = false;
                pending[i] = true;
                count[i] = 1u;
                window[i][0] = values[i];
            }
            else if (pending[i]) {
                if (values[i] < 1500) { /* bottom-out closes the window */
                    /* The closing sample is excluded unless it is the only
                     * follow-up readback; then one interval is still a fit. */
                    if (count[i] < 2u) window[i][count[i]++] = values[i];
                    last[i] = window_oracle(window[i], count[i]); ++completed[i];
                    pending[i] = false; count[i] = 0u;
                }
                else {
                    window[i][count[i]++] = values[i];
                    if (count[i] >= 10u) {
                        last[i] = window_oracle(window[i], count[i]);
                        ++completed[i];
                        pending[i] = false; count[i] = 0u;
                    }
                }
            }
            down[i] = next;
        }
        keyboard_raw_frame(&keys, values, 65, 3, true);
        for (unsigned i = 0; i < 65; ++i) {
            assert(keys.velocity[i].captures == completed[i]);
            assert(keys.velocity[i].valid == (completed[i] != 0));
            if (completed[i]) check_velocity(keys.velocity[i].value, last[i]);
            assert(keys.velocity[i].pending == pending[i]);
            assert(keys.velocity[i].ready == ready[i]);
        }
    }
    puts("PASS 16640 randomized per-key frames against full-history fit/trigger oracle");
}

static void velocity_tests(void)
{
    keyboard_raw_t keys;
    uint16_t values[65];
    velocity_init(&keys);
    keyboard_raw_enable(&keys, false); /* tuning without host key injection */
    for (unsigned i = 0; i < 65; ++i) values[i] = 3900;
    keyboard_raw_frame(&keys, values, 65, 3, true);
    for (unsigned i = 0; i < 65; ++i) values[i] = 3500;
    keyboard_raw_frame(&keys, values, 65, 3, true); /* trigger: window = [3500] */
    for (unsigned i = 0; i < 65; ++i) assert(keys.velocity[i].pending);
    for (unsigned sample = 1; sample <= 9; ++sample) {
        for (unsigned i = 0; i < 65; ++i) values[i] = 3500 - (i+1)*sample;
        keyboard_raw_frame(&keys, values, 65, 3, true);
        for (unsigned i = 0; i < 65; ++i) {
            assert(keys.velocity[i].captures == (sample == 9)); /* ten samples close the window */
            if (sample == 9) {
                assert(keys.velocity[i].valid && !keys.velocity[i].pending);
                /* nine equal intervals of (i+1); the filter drops one, mean unchanged */
                check_velocity(keys.velocity[i].value, (int32_t)(8000*(i+1)));
            }
        }
    }
    assert(!keys.armed); /* velocity is independent of HID enable */
    values[0] = 3700; keyboard_raw_frame(&keys, values, 65, 3, true);
    assert(!keys.velocity[0].ready);
    values[0] = 3701; keyboard_raw_frame(&keys, values, 65, 3, true);
    assert(keys.velocity[0].ready && !keys.velocity[1].ready);
    values[0] = 3600; keyboard_raw_frame(&keys, values, 65, 3, true);
    assert(!keys.velocity[0].pending);

    /* Bottom-out cut: a very fast press fits on four samples (m=4, no
     * median filter), and the below-1500 sample that closes it is excluded. */
    values[0] = 3500; keyboard_raw_frame(&keys, values, 65, 3, true);
    values[0] = 3200; keyboard_raw_frame(&keys, values, 65, 3, true);
    values[0] = 2900; keyboard_raw_frame(&keys, values, 65, 3, true);
    values[0] = 2600; keyboard_raw_frame(&keys, values, 65, 3, true);
    assert(keys.velocity[0].pending && keys.velocity[0].captures == 1);
    values[0] = 1400; keyboard_raw_frame(&keys, values, 65, 3, true);
    assert(!keys.velocity[0].pending && keys.velocity[0].captures == 2);
    check_velocity(keys.velocity[0].value, 900.0/3.0*8000.0);
    assert(keys.velocity[1].captures == 1); /* other keys still collecting their own windows */
    for (unsigned n = 0; n < 8; ++n) keyboard_raw_frame(&keys, values, 65, 3, true);
    for (unsigned i = 1; i < 65; ++i) assert(keys.velocity[i].captures == 1);

    /* Rapid retriggers: a new press owns the window, discarding unfinished
     * collections; the completed fit uses only the last press's readbacks. */
    values[0] = 3900; keyboard_raw_frame(&keys, values, 65, 3, true); /* release rearms */
    const uint16_t rapid[] = {3500,3800,3490,3810,3480,3400,3390,3380,3370,3360};
    const uint16_t final_window[10] = {3480,3400,3390,3380,3370,3360,3360,3360,3360,3360};
    for (unsigned n = 0; n < sizeof(rapid)/sizeof(rapid[0]); ++n) {
        values[0] = rapid[n]; keyboard_raw_frame(&keys, values, 65, 3, true);
        assert(keys.velocity[0].pending);
    }
    for (unsigned n = 0; n < 3; ++n) keyboard_raw_frame(&keys, values, 65, 3, true);
    values[0] = 3360; keyboard_raw_frame(&keys, values, 65, 3, true); /* tenth sample */
    assert(keys.velocity[0].captures == 3 && keys.velocity[0].valid);
    check_velocity(keys.velocity[0].value, window_oracle(final_window, 10));
    assert(keys.velocity[1].captures == 1);
    const uint32_t revision = keys.revision;
    assert(!keyboard_raw_set_all(&keys, 3500, 3500));
    assert(keys.revision == revision && keys.velocity[0].valid);
    assert(keyboard_raw_set_all(&keys, 3000, 3300));
    assert(keys.revision == revision+1);
    for (unsigned i = 0; i < 65; ++i) {
        assert(keys.press[i] == 3000 && keys.release[i] == 3300);
        assert(!keys.velocity[i].valid && !keys.velocity[i].pending && !keys.velocity[i].ready);
    }
    for (unsigned i = 0; i < 65; ++i) values[i] = 3900;
    keyboard_raw_frame(&keys, values, 65, 3, true);
    values[0] = 2900; keyboard_raw_frame(&keys, values, 65, 3, true);
    assert(keys.velocity[0].pending);
    keyboard_raw_frame(&keys, values, 65, 3, false);
    assert(!keys.velocity[0].pending && !keys.velocity[0].valid);
    for (unsigned i = 0; i < 10; ++i) keyboard_raw_frame(&keys, values, 65, 3, true);
    assert(keys.velocity[0].captures == 3 && !keys.velocity[0].valid);
    puts("PASS 65 simultaneous fits, bottom-out window cut, release arming, retrigger ownership, atomic all-key edits, invalid cancellation");
}

static void velocity_clamp_tests(void)
{
    /* Trigger, window samples, and whether a below-bottom-out sample closes
     * the window early. All window values stay above the bottom-out 1500. */
    struct {
        uint16_t points[9];
        unsigned count;
        bool bottom;
        double raw;
    } fixtures[] = {
        {{3510,3520,3530,3540,3550,3560,3570,3580,3590}, 9, false, -80000},  /* rising -> 0 */
        {{3500,3500,3500,3500,3500,3500,3500,3500,3500}, 9, false, 0},       /* flat */
        {{3490,3480,3470,3460,3450,3440,3430,3420,3410}, 9, false, 80000},   /* filtered slow fall */
        {{3200,2900,2601}, 3, true, 899.0/3.0*8000},  /* four samples, no filter, fractional */
        {{2938}, 1, true, 562.0*8000},                /* just below 4500000 */
        {{2937}, 1, true, 563.0*8000},                /* just above 4500000 */
        {{2500}, 1, true, 1000.0*8000},               /* far above 4500000 */
        {{2900,2890,2880,2870,2860,2850,2840,2830,2820}, 9, false, 80000},   /* ten-sample filtered fall */
    };
    for (unsigned k = 0; k < sizeof(fixtures)/sizeof(fixtures[0]); ++k) {
        velocity_init(&s);
        for (unsigned i = 0; i < 61; ++i) raw[i] = 3900;
        frame(); raw[32] = 3500; frame(); /* trigger */
        for (unsigned i = 0; i < fixtures[k].count; ++i) { raw[32] = fixtures[k].points[i]; frame(); }
        if (fixtures[k].bottom) { raw[32] = 1400; frame(); } /* closes without this sample */
        assert(s.velocity[32].valid && s.velocity[32].captures == 1);
        check_velocity(s.velocity[32].value, fixtures[k].raw);
        if (k < 2) assert(s.velocity[32].value == 0.0f);
        if (k == 5 || k == 6) assert(s.velocity[32].value == 1.0f);
    }
    puts("PASS normalized float: negative/zero, fractional, below/above 4500000, bottom-out windows");
}

static void pop_filter_tests(void)
{
    /* Long windows (ten samples) filter one glitch interval at every
     * position, leaving the clean 10-counts/sample slope. */
    for (unsigned outlier=0;outlier<9;++outlier) {
        for (int spike=-500;spike<=500;spike+=1000) {
            velocity_init(&s);
            for(unsigned i=0;i<61;++i) raw[i]=3900;
            frame(); raw[32]=3500; frame();
            int value=3500;
            for (unsigned j=0;j<9;++j) {
                value -= j==outlier ? spike : 10;
                raw[32]=(uint16_t)value; frame();
            }
            check_velocity(s.velocity[32].value,80000);
        }
    }
    /* Short windows (five samples, four intervals) skip the filter entirely:
     * the glitch stays in the mean, which is exactly d(x)/count. The spikes
     * stay below release so the closing sample cannot retrigger the window. */
    for (int spike=-100;spike<=100;spike+=200) {
        velocity_init(&s);
        for(unsigned i=0;i<61;++i) raw[i]=3900;
        frame(); raw[32]=3500; frame();
        int value=3500;
        for (unsigned j=0;j<4;++j) {
            value -= j==2 ? spike : 10;
            raw[32]=(uint16_t)value; frame();
        }
        raw[32]=1400; frame(); /* bottom-out closes the five-sample window */
        check_velocity(s.velocity[32].value,(30.0+spike)/4.0*8000.0);
    }
    /* Earliest interval wins equal-distance ties in the filtered window. */
    const uint16_t ties[2][10] = {
        {3500,3500,3490,3480,3470,3460,3450,3440,3430,3410}, /* 0,10*7,20 -> discard 0 -> 90000 */
        {3500,3480,3470,3460,3450,3440,3430,3420,3410,3410}, /* 20,10*7,0 -> discard 20 -> 70000 */
    };
    for (unsigned k=0;k<2;++k) {
        velocity_init(&s);
        for(unsigned i=0;i<61;++i) raw[i]=3900;
        frame(); raw[32]=3500; frame();
        for (unsigned j=0;j<9;++j) { raw[32]=ties[k][j+1]; frame(); }
        check_velocity(s.velocity[32].value,k ? 70000 : 90000);
    }
    /* Six samples: five intervals, filter enabled; exact kept mean. */
    {
        velocity_init(&s);
        for(unsigned i=0;i<61;++i) raw[i]=3900;
        frame(); raw[32]=3500; frame();
        const uint16_t six[] = {3490,3480,3470,3460,3450};
        for (unsigned j=0;j<5;++j) { raw[32]=six[j]; frame(); }
        raw[32]=1400; frame(); /* bottom-out closes the six-sample window */
        check_velocity(s.velocity[32].value,80000);
    }
    puts("PASS pop filter: filtered ten-sample windows at every glitch position; five-sample windows unfiltered; earliest tie wins");
}

static void fn_layer_modifier_test(void)
{
    /* Left Shift must stay a normal modifier while Fn is held: with the Fn
     * layer's configuration entry replacing its action the bit was dropped,
     * so Fn+Shift+Esc sent an unshifted grave accent instead of a tilde. */
    keyboard_raw_init(&s);
    unsigned shift=RAW_KEY_COUNT, fn=RAW_KEY_COUNT, esc=RAW_KEY_COUNT;
    for (unsigned i=0;i<61;++i) {
        const uint8_t key=keyboard_key_for_sensor(1,i);
        const keyboard_action_t *a=keyboard_action(1,key,0);
        if (a && a->type==2 && a->arg0==2 && !a->arg1) shift=i;
        if (key==keyboard_layout(1)->fn) fn=i;
        if (key==keyboard_layout(1)->escape) esc=i;
    }
    assert(shift<61 && fn<61 && esc<61);
    for (unsigned i=0;i<61;++i) raw[i]=3900;
    frame(); /* neutral, armed */
    raw[shift]=3400; frame();
    assert(s.engine.report.modifiers==2 && !keyboard_report_get_usage(&s.engine.report,0x35));
    raw[fn]=3400; frame();
    assert(s.engine.report.modifiers==2);
    raw[esc]=3400; frame();
    assert(s.engine.report.modifiers==2 && keyboard_report_get_usage(&s.engine.report,0x35));
    /* Releasing Fn first keeps Shift asserted and drops the shortcut. */
    raw[fn]=3900; frame();
    assert(s.engine.report.modifiers==2 && !keyboard_report_get_usage(&s.engine.report,0x35));
    raw[esc]=3900; frame(); raw[shift]=3900; frame();
    assert(!s.engine.report.modifiers);
    /* Fn+RShift keeps its documented navigation remap. */
    keyboard_raw_init(&s);
    for (unsigned i=0;i<61;++i) raw[i]=3900;
    frame();
    raw[fn]=3400; frame();
    unsigned rshift=RAW_KEY_COUNT;
    for (unsigned i=0;i<61;++i) {
        const keyboard_action_t *a=keyboard_action(1,keyboard_key_for_sensor(1,i),0);
        if (a && a->type==2 && a->arg0==32) rshift=i;
    }
    assert(rshift<61);
    raw[rshift]=3400; frame();
    assert(!s.engine.report.modifiers && keyboard_report_get_usage(&s.engine.report,0x52));
    puts("PASS Fn layer: Left Shift keeps its modifier (Fn+Shift+Esc = tilde), Right Shift keeps its Up-arrow remap");
}

int main(void)
{
    velocity_tests();
    velocity_history_oracle();
    velocity_clamp_tests();
    pop_filter_tests();
    fn_layer_modifier_test();
    keyboard_raw_init(&s);
    for (unsigned i=0;i<RAW_KEY_COUNT;++i) assert(s.press[i]==3500 && s.release[i]==3600);
    for (unsigned i = 0; i < 61; ++i) raw[i] = 3900;
    assert(keyboard_key_for_sensor(1, 32) == 0x1f);
    raw[32] = 2400; frame();
    assert(!s.armed && !a()); /* held at startup */
    raw[32] = 3600; frame(); assert(!s.armed);
    raw[32] = 3601; frame(); assert(s.armed && !a());
    raw[32] = 3500; frame(); assert(!a());
    raw[32] = 3499; frame(); assert(a());
    for (unsigned i = 0; i < 100; ++i) {
        raw[32] = i % 2 ? 3500 : 3600; frame(); assert(a());
    }
    raw[32] = 3601; frame(); assert(!a());
    raw[32] = 3550; frame(); assert(!a());

    assert(!keyboard_raw_set(&s, 61, 3000, 3100));
    assert(!keyboard_raw_set(&s, 32, 0, 3100));
    assert(!keyboard_raw_set(&s, 32, 3100, 3100));
    assert(!keyboard_raw_set(&s, 32, 3200, 3100));
    assert(!keyboard_raw_set(&s, 32, 3000, 4097));
    assert(!keyboard_raw_set(&s, 32, 3000, 4096));
    assert(s.revision == 0 && s.armed);
    assert(keyboard_raw_set(&s, 32, 3000, 3200));
    assert(s.revision == 1 && !s.armed && !a());
    raw[32]=3900; frame(); assert(s.armed);
    raw[32] = 2999; frame(); assert(a());
    raw[33] = 2400; frame(); assert(s.down[33] && a());
    raw[32] = 3201; frame(); assert(!a() && s.down[33]);
    raw[33] = 3900; frame();

    /* Every ordinary key/modifier produces its mapped bit, together (NKRO). */
    for (unsigned i = 0; i < 61; ++i) {
        const keyboard_action_t *action = keyboard_action(1, keyboard_key_for_sensor(1, i), 0);
        if (action && action->type == 2) raw[i] = 2000;
    }
    frame();
    for (unsigned i = 0; i < 61; ++i) {
        const keyboard_action_t *action = keyboard_action(1, keyboard_key_for_sensor(1, i), 0);
        if (action && action->type == 2) {
            assert((s.engine.report.modifiers & action->arg0) == action->arg0);
            if (action->arg1 >= 4 && action->arg1 <= 0x73)
                assert(keyboard_report_get_usage(&s.engine.report, action->arg1));
        }
    }
    keyboard_raw_enable(&s, false); frame(); assert(!s.armed && !a());
    keyboard_raw_enable(&s, true); frame(); assert(!s.armed);
    for (unsigned i = 0; i < 61; ++i) raw[i] = 3900;
    frame(); assert(s.armed);
    raw[32] = 2000; frame(); assert(a());
    keyboard_raw_frame(&s, raw, 61, 1, false); assert(!a() && !s.valid && !s.armed);
    frame(); assert(!s.armed);
    raw[32] = 3900; frame(); assert(s.armed);
    raw[0] = 4097; frame(); assert(!s.armed && !s.valid);
    raw[0] = 0; frame(); assert(!s.armed && !s.valid);
    puts("PASS raw Schmitt boundaries, startup/invalid/enable/config guards, per-key settings and NKRO");
    return 0;
}
