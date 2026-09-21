#ifndef DEVICE_STORE_H
#define DEVICE_STORE_H
#include "keyboard_calibration.h"
/* Logical record size, NOT a platform's flash erase size. Each of two slots
 * must have a separately erasable, board-owned allocation of at least this
 * size. Callbacks translate a slot number; shared code never sees addresses. */
#define CAL_PAGE_SIZE 512u
/* A backend may report unreadable CONTENT (e.g. ECC), allowing bounded cold
 * initialization. Controller failures must return a different nonzero code. */
#define DEVICE_STORE_INVALID_CONTENT 116u
typedef uint32_t (*cal_read_fn)(unsigned slot, uint8_t *page);
typedef uint32_t (*cal_write_fn)(unsigned slot, const uint8_t *page);
typedef uint32_t (*cal_erase_fn)(unsigned slot);

uint32_t calibration_crc32(const uint8_t *p, unsigned n);
#include "keyboard_app.h"

/* One complete snapshot per page. Never split settings/calibration generations
 * between independent writers: only a verified new snapshot replaces the old. */
typedef struct {
    uint8_t record[CAL_PAGE_SIZE];
    uint32_t generation, calibration_generation, error, changed_at, pending_crc, checked_at;
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
bool device_store_clear(device_store_t *s,cal_read_fn read,cal_erase_fn erase);
#endif
