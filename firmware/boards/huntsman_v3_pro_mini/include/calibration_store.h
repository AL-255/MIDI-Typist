#ifndef CALIBRATION_STORE_H
#define CALIBRATION_STORE_H
#include "keyboard_calibration.h"
#include "keyboard_menu.h"
#define CAL_PAGE_SIZE 512u
#define CAL_SLOT_A 0x78000u
#define CAL_SLOT_B 0x78200u
/* The two authorized pages carry a calibration part and an optional settings
 * part, each with its own generation, so one page rewrite updates either
 * without disturbing the other. */
#define SETTINGS_VERSION 1u
#define SETTINGS_BUILD_MAX 20u /* build identity characters plus NUL */
#define SETTINGS_PAYLOAD 16u
typedef uint32_t (*cal_read_fn)(unsigned slot, uint8_t *page);
typedef uint32_t (*cal_write_fn)(unsigned slot, const uint8_t *page);
typedef uint32_t (*cal_erase_fn)(unsigned slot);
/* Store failures beyond the controller codes. */
#define STORE_ERROR_INVALID   0x20001u  /* value out of range for the record  */
#define STORE_ERROR_FOREIGN   0x20002u  /* readable page we did not write     */
#define STORE_ERROR_VERIFY    0x20003u  /* erase/program did not read back    */
#define STORE_ERROR_CORRUPT   0x20004u  /* checksum failure, region cleared   */
typedef struct {
    uint32_t generation, error;
    uint8_t slot;
    bool saved;
    /* Bitmask of pages whose checksum failed and which were therefore cleared
     * by the boot integrity pass, so the session is a cold boot. */
    uint8_t corrupt_slots;
    /* Settings part of the same pages; settings_saved stays false for a blank,
     * unknown or foreign-build record, which is the cold-boot condition. */
    uint32_t settings_generation;
    uint8_t settings_slot;
    bool settings_saved;
} calibration_store_t;
/* Fn-menu configuration that outlives a power cycle. Ranges are the ones the
 * menu can produce; midi_press_level 0 means "keep the default trigger point"
 * because the 3500 default is not one of the ten Fn+Tab steps. */
typedef struct {
    /* Which menu action last set the press thresholds: none (defaults), the
     * keyboard trigger editor (both bounds from the calibration) or the MIDI
     * trigger page (uniform raw press, releases untouched). */
    uint8_t trigger_source;
    uint8_t trigger_level, rapid_level, rapid_enabled;
    uint8_t midi_press_level, velocity_start;
    uint8_t janko, lower_muted, brightness;
    uint8_t music_root, music_scale;
    int8_t octave;
    uint8_t performance_mode;
} device_settings_t;
uint32_t calibration_crc32(const uint8_t *p, unsigned n);
void calibration_record(uint8_t *page, uint8_t profile, uint8_t count, uint32_t gen, const uint16_t *lo, const uint16_t *hi);
bool calibration_record_valid(const uint8_t *page);
/* Recognizable page: intact CRC with a valid or explicitly absent calibration
 * part and an optional settings part. The controller adapter accepts these. */
bool device_page_valid(const uint8_t *page);
void device_settings_encode(uint8_t *payload, const device_settings_t *s);
bool device_settings_decode(const uint8_t *payload, device_settings_t *s);
void device_page_settings_put(uint8_t *page, const device_settings_t *s, uint32_t gen, const char *build);
bool device_page_settings_get(const uint8_t *page, device_settings_t *s, uint32_t *gen, char *build);
/* Boot integrity pass. Every page is checked against its CRC32 before anything
 * is loaded; a non-blank page carrying our markers that fails the check is a
 * torn write (power loss during erase or program), and the whole region is
 * cleared so corruption always ends as a cold boot. A readable page with
 * foreign contents is never deleted, and an unreadable page is reported by the
 * load path rather than guessed at. */
bool calibration_store_scrub(calibration_store_t *s, cal_read_fn read, cal_erase_fn erase);
void calibration_store_load(calibration_store_t *s, uint8_t profile, uint8_t count, uint16_t *lo, uint16_t *hi, cal_read_fn read);
/* Reads the settings part alone, so it can run after calibration endpoints are
 * known. expect_build mismatches leave settings_saved false. */
void calibration_store_load_settings(calibration_store_t *s, device_settings_t *out,
                                     const char *expect_build, cal_read_fn read);
bool calibration_store_save(calibration_store_t *s, const keyboard_calibration_t *cal, cal_read_fn read, cal_write_fn write);
bool calibration_store_save_settings(calibration_store_t *s, const device_settings_t *in,
                                     const char *build, cal_read_fn read, cal_write_fn write);
bool calibration_store_clear(calibration_store_t *s, cal_read_fn read, cal_erase_fn erase);
#endif
