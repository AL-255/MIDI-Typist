#include "flash_dump.h"
#include "board.h"
#include "board_config.h"
#include "fsl_iap.h"
#include "calibration_store.h"
#include <string.h>

/* No ROM calls: the original application uses the controller command path,
 * whereas this board reset on the SDK ROM-wrapper path. SDK register types,
 * masks and status codes are used here; board clocks are already initialized
 * by the SDK. Dumps issue only command 3. Calibration erase/program is
 * restricted to the two verified unused tail pages below. */
static bool s_timeout;
static status_t wait_done(unsigned limit)
{
    for (unsigned i=0; i<limit; ++i) {
        const uint32_t flags=FLASH->INT_STATUS;
        if (!(flags & FLASH_INT_STATUS_DONE_MASK)) continue;
        if (flags & FLASH_INT_STATUS_FAIL_MASK) return kStatus_FLASH_CommandFailure;
        if (flags & FLASH_INT_STATUS_ERR_MASK) return kStatus_FLASH_CommandNotSupported;
        if (flags & FLASH_INT_STATUS_ECC_ERR_MASK) return kStatus_FLASH_EccError;
        return 0;
    }
    s_timeout=true;
    return 0x10001;
}
static uint32_t part_id(void)
{
    /* The production flash-size selector is at +0xfe0, NOT the SDK's
     * DEVICE_ID0 (+0xff8, which contains the ROM revision). */
    return *(volatile const uint32_t *)(SYSCON_BASE + 0xfe0u);
}
static uint32_t flash_size(void)
{
    /* Original PARTID size selector, deliberately no unknown-die fallback. */
    switch (part_id() & 0x1ffu) {
        case 0: return 0x20000u;
        case 1: return 0x40000u;
        case 2: return 0x80000u;
        case 255: return 0x9de00u;
        default: return 0u;
    }
}
static status_t read_word(uint32_t address, uint8_t *out)
{
    if (s_timeout) return 0x10001;
    status_t result = kStatus_FLASH_CommandFailure;
    /* The controller intermittently reports FAIL/ERR/ECC for a read that
     * succeeds when repeated; the reference driver read each word once. Retry
     * twice so one flaky read cannot fail a store load or a save's read-back.
     * A controller that never signals DONE still latches. */
    for (unsigned attempt = 0; attempt < 3u; ++attempt) {
        board_watchdog_refresh();
        FLASH->INT_CLR_STATUS = 15u;
        FLASH->STARTA = FLASH_STARTA_STARTA(address >> 4u);
        FLASH->DATAW[0] = 0u; /* normal margin, ECC enabled, no DMACC */
        FLASH->CMD = FLASH_CMD_CMD(3u);
        bool done = false;
        for (unsigned spins = 0; spins < 96000u; ++spins) {
            const uint32_t flags = FLASH->INT_STATUS;
            if (!(flags & FLASH_INT_STATUS_DONE_MASK)) continue;
            done = true;
            if (flags & FLASH_INT_STATUS_FAIL_MASK) { result = kStatus_FLASH_CommandFailure; break; }
            if (flags & FLASH_INT_STATUS_ERR_MASK) { result = kStatus_FLASH_CommandNotSupported; break; }
            if (flags & FLASH_INT_STATUS_ECC_ERR_MASK) { result = kStatus_FLASH_EccError; break; }
            for (unsigned i = 0; i < 4u; ++i) {
                const uint32_t word = FLASH->DATAW[i];
                memcpy(out + 4u*i, &word, sizeof(word));
            }
            return kStatus_FLASH_Success;
        }
        if (!done) {
            s_timeout = true; /* Do not issue another command into a stuck controller. */
            return 0x10001;
        }
    }
    return result;
}
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4u; ++i) p[i] = v >> (i * 8u);
}
static uint32_t crc32(const uint8_t *p, unsigned n)
{
    uint32_t crc = UINT32_MAX;
    while (n--) {
        crc ^= *p++;
        for (unsigned b = 0; b < 8u; ++b) crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
void flash_dump_record(uint32_t id, uint32_t address, uint8_t out[FLASH_DUMP_SIZE])
{
    memset(out, 0, FLASH_DUMP_SIZE);
    memcpy(out, "HBD1", 4u);
    put32(out + 4, id); put32(out + 8, address); put32(out + 12, FLASH_DUMP_CHUNK);
    const uint32_t total = flash_size();
    status_t status = kStatus_FLASH_AddressError;
    if (id && !(address & (FLASH_DUMP_CHUNK - 1u)) && address <= FLASH_DUMP_LIMIT - FLASH_DUMP_CHUNK) {
        if (total >= 0x2800u + FLASH_DUMP_CHUNK && address <= total - 0x2800u - FLASH_DUMP_CHUNK)
            status = SystemCoreClock == BOARD_CORE_CLOCK_HZ ? kStatus_FLASH_Success : kStatus_FLASH_CommandNotSupported;
    }
    put32(out + 16, total); put32(out + 20, 512u);
    put32(out + 24, (uint32_t)status);
    for (unsigned i = 0; i < 4u; ++i) {
        status_t word_status = status;
        if (!status) {
            board_watchdog_refresh();
            word_status = read_word(address + i * 16u, out + 48u + i * 16u);
        }
        put32(out + 32u + i * 4u, (uint32_t)word_status);
        /* Failed reads are explicitly marked holes, not invented flash data. */
        if (word_status) memset(out + 48u + i * 16u, 0, 16u);
    }
    put32(out + 112u, part_id());
    put32(out + 116u, SYSCON->DIEID);
    put32(out + 124u, crc32(out, 124u));
}

static bool config_allowed(unsigned slot)
{
    return slot<2u && !s_timeout && SystemCoreClock==BOARD_CORE_CLOCK_HZ &&
        flash_size()>=CAL_SLOT_B+CAL_PAGE_SIZE+0x2800u;
}
uint32_t flash_calibration_read(unsigned slot, uint8_t *page)
{
    if (!page || !config_allowed(slot)) return kStatus_FLASH_AddressError;
    const uint32_t address=slot ? CAL_SLOT_B : CAL_SLOT_A;
    for (unsigned i=0; i<CAL_PAGE_SIZE; i+=16u) {
        board_watchdog_refresh();
        uint32_t result=read_word(address+i,page+i);
        if (result) return result;
    }
    return 0;
}
uint32_t flash_calibration_erase(unsigned slot)
{
    if (!config_allowed(slot)) return kStatus_FLASH_AddressError;
    const uint32_t address=slot ? CAL_SLOT_B : CAL_SLOT_A;
    board_watchdog_refresh();
    uint32_t irq=DisableGlobalIRQ();
    FLASH->INT_CLR_STATUS=15u;
    FLASH->STARTA=address>>4u; FLASH->STOPA=address>>4u;
    FLASH->CMD=4u; /* One 512-byte page erase, matching the original driver. */
    uint32_t result=wait_done(2000000u);
    SYSCON->FMCFLUSH=1u;
    EnableGlobalIRQ(irq);
    board_watchdog_refresh();
    return result;
}
uint32_t flash_calibration_write(unsigned slot, const uint8_t *page)
{
    if (!page || !config_allowed(slot) || !calibration_record_valid(page)) return kStatus_FLASH_AddressError;
    uint32_t result=flash_calibration_erase(slot);
    if (result) return result;
    const uint32_t address=slot ? CAL_SLOT_B : CAL_SLOT_A;
    uint32_t irq=DisableGlobalIRQ();
    FLASH->INT_CLR_STATUS=15u;
    for (unsigned i=0; i<CAL_PAGE_SIZE; i+=16u) {
        FLASH->STARTA=(address+i)>>4u;
        for (unsigned j=0; j<4u; ++j) { uint32_t word; memcpy(&word,page+i+j*4u,4u); FLASH->DATAW[j]=word; }
        FLASH->CMD=8u; /* Load the controller's page buffer, not flash itself. */
        result=wait_done(96000u);
        if (result) break;
        FLASH->INT_CLR_STATUS=15u;
    }
    if (!result) { FLASH->CMD=12u; result=wait_done(2000000u); }
    if (!result) SYSCON->FMCFLUSH=1u;
    EnableGlobalIRQ(irq);
    board_watchdog_refresh();
    return result;
}
