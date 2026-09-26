#ifndef MIDI_TYPIST_M1_SLEEP_TIME_H
#define MIDI_TYPIST_M1_SLEEP_TIME_H
#include "m1_sleep.h"
/* RM 17.3: LICK range, not a measured board frequency. */
#define M1_LICK_MIN_HZ 30000u
#define M1_LICK_MAX_HZ 60000u
#define M1_RTC_DAY_TICKS (86400u*(M1_RTC_DIV_B+1u))
/* Foreground-only elapsed-time bridge. Start after RTC/TMR2 initialization,
 * then poll service until ready. Measures RTC subsecond ticks against TMR2;
 * never resets or writes the calendar/date/backup data, or assumes the
 * oscillator is exactly 40 kHz. A measurement/configuration fault latches. */
bool m1_sleep_time_begin(void);
void m1_sleep_time_service(void);
bool m1_sleep_time_ready(void);
bool m1_sleep_time_fault(void);
/* Same quiescence requirements as m1_sleep_wait. Suspends TMR2, measures
 * actual RTC progression (including early wake), resynchronizes RTC shadow
 * registers after wake, restores the board clock and resumes TMR2 with the
 * measured gap. Preserves PRIMASK except CLOCK_FATAL, which stays masked.
 * TIME_ERROR is terminal; a failure after suspend leaves TMR2 stopped.
 * Resolution is one RTC subsecond tick; oscillator drift during sleep is not
 * corrected. Sample-to-sample interval must stay below one RTC calendar day
 * at its accelerated 7/7 divider rate, not 24 wall-clock hours. */
m1_sleep_result_t m1_sleep_timed_wait(uint32_t ticks,bool platform_quiescent);
#endif
