#include "m1_board.h"
#include "m1_battery.h"
#include "keyboard_sample.h"
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
    for(unsigned sensor=0;sensor<M1_KEY_COUNT;++sensor) {
        const m1_key_t *key=&m1_keys[sensor];
        /* ADC falls with travel. Native range 0..4095 -> canonical 1..4096.
         * Calibration travel endpoints are deliberately not used as ADC rails. */
        (void)keyboard_sample_normalize(scan->rows[key->bank][key->rank],
                                       M1_ADC_MAX,0,&scan->latest[sensor]);
    }
    scan->battery=scan->rows[M1_BATTERY_BANK][M1_BATTERY_RANK];
    scan->battery_valid=true;
    ++scan->sequence;
    if(scan->pending) ++scan->overwritten;
    scan->pending=true;
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
    memcpy(frame,scan->latest,sizeof(scan->latest));
    *sequence=scan->sequence;
    scan->pending=false;
    return true;
}
