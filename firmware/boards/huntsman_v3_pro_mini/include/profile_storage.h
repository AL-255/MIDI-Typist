#ifndef HUNTSMAN_PROFILE_STORAGE_H
#define HUNTSMAN_PROFILE_STORAGE_H
#include "device_store.h"
/* The only two flash pages the application may erase or program. They sit in
 * the original allocator's verified-FF free payload, away from the primary
 * settings and serial number at 0x49000..0x49400. */
#define CAL_SLOT_A 0x78000u
#define CAL_SLOT_B 0x78200u
_Static_assert(CAL_PAGE_SIZE==512u,"Huntsman erase-page geometry");
#endif
