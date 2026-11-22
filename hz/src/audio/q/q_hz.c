#include "q_ps2.h"

/* HZ's side of the Q backend's link (q_ps2.h): HZ's driver plays no DAC
 * drums, so there are none to load. */
bool drums_load(const char *path, Ps2Reader reader, DrumEntry *table,
                int *count, u8 *ram)
{
    (void)path;
    (void)reader;
    (void)table;
    (void)ram;
    *count = 0;
    return false;
}
