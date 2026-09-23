#include "m1_board.h"
#include "m1_battery.h"
#include <string.h>

void m1_scan_init(m1_scan_t *scan) { memset(scan,0,sizeof(*scan)); }
void m1_scan_fault(m1_scan_t *scan)
{ ++scan->errors; scan->next_bank=0; scan->pending=scan->battery_valid=false; }
bool m1_scan_bank(m1_scan_t *scan,unsigned bank,const uint16_t *values)
{
    if(!values || bank>=M1_BANK_COUNT || bank!=scan->next_bank) {
        m1_scan_fault(scan);
        return false;
    }
    for(unsigned rank=0;rank<M1_ADC_RANKS;++rank) {
        if(values[rank]>M1_ADC_MAX) { m1_scan_fault(scan); return false; }
    }
    memcpy(scan->rows[bank],values,sizeof(scan->rows[bank]));
    if(++scan->next_bank!=M1_BANK_COUNT) return true;
    scan->next_bank=0;
    ++scan->sequence;
    if(scan->pending==M1_SCAN_QUEUE_FRAMES) {
        /* The foreground fell behind by a whole queue. Drop the oldest frame
         * and keep acquiring: the sequence numbers the consumer receives skip
         * the dropped ones, which is the board's existing lost-frame signal. */
        scan->head=(scan->head+1u)%M1_SCAN_QUEUE_FRAMES;
        --scan->pending;
    }
    unsigned slot=(scan->head+scan->pending)%M1_SCAN_QUEUE_FRAMES;
    uint16_t *frame=scan->frames[slot];
    scan->frame_sequence[slot]=scan->sequence;
    for(unsigned sensor=0;sensor<M1_KEY_COUNT;++sensor) {
        const m1_key_t *key=&m1_keys[sensor];
        /* ADC falls with travel. Native range 0..4095 -> canonical 1..4096.
         * Calibration travel endpoints are deliberately not used as ADC rails. */
        frame[sensor]=scan->rows[key->bank][key->rank]+1u;
    }
    scan->battery=scan->rows[M1_BATTERY_BANK][M1_BATTERY_RANK];
    scan->battery_valid=true;
    ++scan->pending;
    return true;
}
bool m1_scan_battery(const m1_scan_t *scan,uint16_t *adc,uint32_t *sequence)
{
    if(!scan->battery_valid || !adc || !sequence)return false;
    *adc=scan->battery; *sequence=scan->sequence;
    return true;
}
bool m1_scan_take(m1_scan_t *scan,uint16_t *frame,uint32_t *sequence)
{
    if(!scan->pending || !frame || !sequence) return false;
    memcpy(frame,scan->frames[scan->head],sizeof(scan->frames[0]));
    *sequence=scan->frame_sequence[scan->head];
    scan->head=(scan->head+1u)%M1_SCAN_QUEUE_FRAMES;
    --scan->pending;
    return true;
}
