#ifndef DEVICE_STORE_H
#define DEVICE_STORE_H
#include "keyboard_calibration.h"
/* The board build supplies its current record size, four-byte format identity
 * and recoverable invalid-content read error (zero means none). Addresses and
 * controller operations belong exclusively to the board's slot callbacks. */
#if !defined(MT_STORE_PAGE_SIZE) || !defined(MT_STORE_MAGIC) || !defined(MT_STORE_INVALID_READ)
#error "The board must define its profile journal contract"
#endif
#define CAL_PAGE_SIZE MT_STORE_PAGE_SIZE
typedef uint32_t (*cal_read_fn)(unsigned slot, uint8_t *page);
typedef uint32_t (*cal_write_fn)(unsigned slot, const uint8_t *page);
typedef uint32_t (*cal_erase_fn)(unsigned slot);

uint32_t calibration_crc32(const uint8_t *p, unsigned n);
#include "keyboard_app.h"

/* One complete snapshot per page. Never split settings/calibration generations
 * between independent writers: only a verified new snapshot replaces the old. */
typedef struct {
    uint16_t press[CAL_KEYS],release[CAL_KEYS];
    uint8_t notes[CAL_KEYS],keycodes[CAL_KEYS],globals[14],profile,count;
} device_settings_t;
typedef struct {
    uint8_t record[CAL_PAGE_SIZE];
    /* RAM-only exact comparison of the last validated polling inputs. This
     * is not another save format or a truncated hash of the configuration. */
    device_settings_t observed;
    bool observed_valid;
    uint32_t generation, calibration_generation, error, changed_at, checked_at;
    uint8_t slot;
    bool valid, saved, ready, applied, pending, cold, fault;
} device_store_t;
bool device_record_valid(const uint8_t *page);
void device_store_load(device_store_t *s,uint8_t profile,uint8_t count,
                       uint16_t *lo,uint16_t *hi,cal_read_fn read);
bool device_store_apply(device_store_t *s,keyboard_app_t *app);
bool device_store_update(device_store_t *s,keyboard_app_t *app,
                         const keyboard_calibration_t *cal,cal_read_fn read,cal_write_fn write);
bool device_store_service(device_store_t *s,keyboard_app_t *app,uint32_t now,
                          cal_read_fn read,cal_write_fn write);
/* Read-only planning: update pending/debounce status and return whether a
 * snapshot is due. Boards may defer for power/transport ownership without
 * calling a writer or latching a false hardware fault. force rechecks the
 * current snapshot immediately before committing a previously deferred save. */
bool device_store_poll(device_store_t *s,keyboard_app_t *app,uint32_t now,bool force);
bool device_store_clear(device_store_t *s,cal_read_fn read,cal_erase_fn erase);
#endif
