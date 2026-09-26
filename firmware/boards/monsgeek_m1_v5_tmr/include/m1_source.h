#ifndef MIDI_TYPIST_M1_SOURCE_H
#define MIDI_TYPIST_M1_SOURCE_H
#include "m1_controls.h"
/* Awake source-change owner. Preserves sensor/LED rails and RAM settings.
 * Stops USB immediately on removal, drains wireless output, then parks live
 * before touching the PHY or initializing the RTC bridge. No WFI or flash.
 * Arrival keeps a wireless selection; loss of USB typing selects fallback.
 * Begin is forbidden during sleep/transport ownership or terminal faults. */
typedef enum { M1_SOURCE_RUNNING,M1_SOURCE_READY,M1_SOURCE_FAILED } m1_source_result_t;
bool m1_source_begin(uint32_t now_ms,bool external,m1_transport_t fallback);
/* Cancelled sleep before a peer command committed: live is already parked,
 * acquisition paused (not stopped/reinitialized), LED driver stopped and
 * its supply restored. Wireless peer must still be awake. No second pause. */
bool m1_source_begin_parked(uint32_t now_ms,bool external,m1_transport_t fallback);
m1_source_result_t m1_source_service(uint32_t now_ms,uint32_t now_us,bool external);
bool m1_source_external(void);
uint32_t m1_source_error(void);
#endif
