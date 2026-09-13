#ifndef SCAN_STREAM_H
#define SCAN_STREAM_H
#include <stdbool.h>
#include <stdint.h>
#define SCAN_STREAM_KEY_SIZE 20u
#define SCAN_STREAM_GUI_SIZE 1152u
void scan_stream_gui(void);
bool scan_stream_gui_enabled(void);
void scan_stream_gui_push(const uint8_t report[SCAN_STREAM_GUI_SIZE]);
void scan_stream_last_key(uint16_t threshold, uint32_t session, uint8_t sensor);
/* GUI selects one sensor, streamed every scan without resampling. */
void scan_stream_init(void);
void scan_stream_start(void);
void scan_stream_stop(void);
bool scan_stream_active(void);
bool scan_stream_enabled(void);
void scan_stream_push(const uint16_t *samples, uint8_t count, uint8_t profile, uint32_t tick);
bool scan_stream_service(void); /* true when a SysEx payload was published */
void scan_stream_usb_reset(void);
uint32_t scan_stream_dropped(void);
/* One response at a time; no overwrite of queued or USB-owned dump data. */
bool scan_stream_dump_ready(void);
void scan_stream_dump_push(const uint8_t report[128]);
#endif
