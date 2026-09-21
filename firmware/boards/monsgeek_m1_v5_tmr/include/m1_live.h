#ifndef MIDI_TYPIST_M1_LIVE_H
#define MIDI_TYPIST_M1_LIVE_H
#include "m1_board.h"
#include "m1_controls.h"
#include "m1_factory.h"
/* Serialized foreground application owner. HAL clock/rails, periodic
 * scanner, lighting, battery inputs and USB lifecycle are initialized by the
 * outer startup/power coordinator, not implicitly retried here.
 * Init imports validated factory travel bounds read-only. Missing/invalid
 * calibration is rejected unless a valid custom snapshot supplies bounds;
 * it is never replaced with ADC rails. Calibration/RESET entry remains disabled.
 * The outer owner initializes the chosen USB/radio transport. Wireless init
 * requires a healthy scheduler configured for that exact mode; link readiness
 * may follow later. Optional transport ops must outlive this owner and prove
 * host release/physical selection (local radio idle is not that proof).
 * NULL ops disable Fn transport selection, not wireless keyboard/battery use.
 * USB SysEx configuration stays available while typing over radio. Performance
 * MIDI is USB-only; HID goes to the selected transport. No pairing/power sequencing
 * or automatic link restart happens here. */
typedef enum { M1_SAVE_DEFER, M1_SAVE_READY, M1_SAVE_FAULT } m1_save_result_t;
typedef struct {
    /* Called only after stable neutral input and local output completion.
     * DEFER leaves hardware unchanged; FAULT is terminal, never retried.
     * READY qualifies supply and quiesces scan/LED/radio DMA. Retain links,
     * rails and USB endpoints/identity: locally drained neutral reports are
     * not a radio-host acknowledgement or permission to switch/power down.
     * Must outlive live. A READY begin must always be paired with end.
     * end() resumes acquisition, discards pre-pause frames and keeps the outer
     * monotonic clocks accurate across masked-IRQ flash time. False is terminal:
     * never silently continue typing after an ambiguous peripheral restart. */
    m1_save_result_t (*begin)(void *context);
    bool (*end)(void *context);
    void *context;
} m1_live_storage_ops_t;
/* Restore is unconditional; NULL storage ops disable writes, not reads. Pending
 * edits remain explicitly unsaved. No physical power/drain proof is fabricated. */
bool m1_live_init(m1_transport_t current,const m1_transport_ops_t *transports,
                  const m1_live_storage_ops_t *storage);
m1_factory_result_t m1_live_factory_result(void);
/* Both clocks wrap independently. Do not derive now_ms from wrapping now_us. */
void m1_live_service(uint32_t now_ms,uint32_t now_us);
/* Neutralizes application ownership and stops streams, not hardware/rails.
 * Continue service to drain neutral HID/MIDI cleanup before explicit init can
 * resume. Wireless restart also requires the existing transport ops to confirm
 * old-host releases. Reinitialization restores committed settings; USB epochs do not. */
void m1_live_stop(uint32_t now_ms);
uint32_t m1_live_scan_losses(void);
m1_transport_t m1_live_transport(void);
/* A timed-out physical selection leaves host ownership ambiguous. This is
 * terminal for this owner; explicit init cannot silently reset the fault. */
bool m1_live_transport_fault(void);
bool m1_live_storage_fault(void);
#endif
