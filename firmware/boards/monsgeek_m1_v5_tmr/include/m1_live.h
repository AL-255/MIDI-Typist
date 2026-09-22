#ifndef MIDI_TYPIST_M1_LIVE_H
#define MIDI_TYPIST_M1_LIVE_H
#include "m1_board.h"
#include "m1_controls.h"
#include "m1_factory.h"
/* Accepted only over the active control session with a verified armed IAP flag. */
bool m1_live_update_requested(void);
/* Serialized foreground application owner. HAL clock/rails, periodic
 * scanner, lighting, battery inputs and USB lifecycle are initialized by the
 * outer startup/power coordinator, not implicitly retried here.
 * Init imports validated custom/factory electrical bounds read-only. An
 * explicit real released frame permits provisional startup bounds if factory
 * records are absent/out-of-domain. These are RAM-only and reported unsaved;
 * no scale is guessed for a stock record. Calibration and profile RESET both
 * require storage ops; without them RESET entry stays disabled.
 * The outer owner initializes the chosen USB/radio transport. Wireless init
 * requires a healthy scheduler configured for that exact mode; link readiness
 * may follow later. Optional transport ops must outlive this owner and prove
 * neutral-output handoff/physical selection (local SPI completion is not a
 * remote-host receipt acknowledgement).
 * NULL ops disable Fn transport selection, not wireless keyboard/battery use.
 * USB SysEx configuration stays available while typing over radio. Performance
 * MIDI is USB-only; HID goes to the selected transport. No pairing/power sequencing
 * or automatic link restart happens here. */
typedef enum { M1_SAVE_DEFER, M1_SAVE_READY, M1_SAVE_FAULT } m1_save_result_t;
typedef struct {
    /* Called after local neutral-output completion; autosave additionally
     * requires stable neutral input. Completed calibration keys may stay held.
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
    /* Optional read-only hardware preflight bitmap, never acquires ownership. */
    uint32_t (*blocked)(void *context);
} m1_live_storage_ops_t;
/* Restore is unconditional; NULL storage ops disable writes, not reads. Pending
 * edits remain explicitly unsaved. No physical power/drain proof is fabricated. */
bool m1_live_init(m1_transport_t current,const m1_transport_ops_t *transports,
                  const m1_live_storage_ops_t *storage,const uint16_t *released);
m1_factory_result_t m1_live_factory_result(void);
/* Both clocks wrap independently. Do not derive now_ms from wrapping now_us. */
void m1_live_service(uint32_t now_ms,uint32_t now_us);
/* Neutralizes application ownership and stops streams, not hardware/rails.
 * Continue service to drain neutral HID/MIDI cleanup before explicit init can
 * resume. Wireless restart also requires the existing transport ops to confirm
 * its neutral handoff. Reinitialization restores committed settings; USB epochs do not. */
void m1_live_stop(uint32_t now_ms);
/* RAM-preserving power handoff, unlike terminal stop/reinitialization.
 * suspend cancels transient key/velocity/calibration/menu state and the GUI
 * lease, but retains settings, active bounds and unsaved journal state. It
 * never writes flash or changes rails. Repeat suspension is idempotent.
 * Continue service until park accepts locally completed neutral HID/MIDI,
 * a neutral radio boundary, USB IN buffer release and completed LEDs. A
 * never-linked wireless peer may cancel an unsent neutral baseline; it must
 * never count that cancellation as delivered host data. Pending battery-only
 * metadata does not prevent handoff. Draining consumes/discards scan frames
 * so an absent/slow host cannot itself overflow the acquisition FIFO.
 * park then stops ALL live foreground HAL/service work; the outer owner takes
 * exclusive hardware ownership. This is NOT radio-host receipt, peer sleep,
 * LED blanking, stopped acquisition, or permission to change clocks/rails.
 * A live USB IRQ may still handle control/idle requests until the owner stops it.
 * No timeout silently grants ownership; the outer controller owns its deadline.
 * resume requires explicit physical restoration, healthy periodic acquisition,
 * lighting and the same ready USB transport or fresh matching wireless mode
 * (a wireless host may still be searching). It discards unread pre-wake frames
 * and control input, starts a new GUI lease, and requires fresh neutral scans.
 * Never reload flash or synthesize a key/velocity from a wake-only sample.
 * No suspend during physical transport selection or terminal owner faults.
 * Terminal stop after park never reclaims hardware; only cold reset can then
 * initialize this live owner again. The outer controller must service restored
 * USB/radio drivers before resume, so readiness is not a pre-sleep snapshot. */
bool m1_live_power_suspend(uint32_t now_ms);
bool m1_live_power_park(void);
bool m1_live_power_resume(uint32_t now_ms,bool platform_restored);
/* Awake cable handoff only. usb_disconnected requires the outer owner to
 * have physically stopped USB and relinquished every IN buffer. Lost USB
 * output is abandoned, NOT counted as delivered neutral reports. Wireless
 * output still drains normally. An Fn selection waiting for old-host drain is
 * cancelled before its first platform select/pair call; after that call,
 * suspension is rejected because physical ownership may already have changed.
 * Resume preserves RAM settings/bounds and may
 * retarget a disconnected USB keyboard to a restored wireless transport.
 * Same-mode USB may resume before host enumeration after physical restoration;
 * ordinary readiness edges still require release before typing. */
bool m1_live_source_suspend(uint32_t now_ms,bool usb_disconnected);
bool m1_live_source_resume(uint32_t now_ms,m1_transport_t target,bool platform_restored);
/* Awake policy observation. Activity is sticky between observations, including
 * short presses/encoder actions; false means ownership/input is not eligible.
 * No flash/peripheral writes. Called only by the serialized power owner. */
bool m1_live_power_activity(bool *activity);
uint32_t m1_live_scan_losses(void);
m1_transport_t m1_live_transport(void);
/* A timed-out physical selection leaves host ownership ambiguous. This is
 * terminal for this owner; explicit init cannot silently reset the fault. */
bool m1_live_transport_fault(void);
bool m1_live_storage_fault(void);
uint32_t m1_live_storage_error(void);
/* Read-only timing snapshot through the current control owner. After runtime
 * failure, retained counters/time describe the last live service, not a new
 * acquisition. Unavailable before successful live initialization. */
bool m1_live_publish_stats(void);
#endif
