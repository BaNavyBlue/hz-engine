#ifndef HZ_SAVES_H
#define HZ_SAVES_H

/* Saved games: the port's stand-in for the password. On the cartridge the
 * win record (a byte per difficulty A-D, a bit per map won by a human
 * against the computer) has no battery behind it, so the game's end shows
 * it as an 11-letter password and CONT takes it back. The port keeps it in
 * a file instead: saves/progress.json (the Esc screen's settings folder,
 * $HZ_SAVES), up to HZ_SAVE_SLOTS games, each a win record and the side
 * the human played. A slot is written when a win is recorded (where the
 * game's end made the password), and CONT on the options screen lists the
 * slots to load one -- which does what an accepted password does.
 *
 * The file, written whole on every change:
 *   { "saves": [ { "slot": 1, "human": "P1", "time": 1759700000,
 *                  "won": { "A": [0, 1], "B": [], "C": [], "D": [] } } ] }
 * "won": per difficulty the maps won (0 ABGRUND ... 7 OASE); "time": when
 * it was written (seconds since 1970), so a new game with every slot used
 * takes the oldest. Slots not listed are empty; a malformed entry is
 * skipped with a message. */

#include "../core/hz_types.h"

#define HZ_SAVE_SLOTS 8

typedef struct {
    bool used;
    u8   record[4];     /* as v_win_record: difficulty A's byte first */
    u8   human;         /* 1 or 2: the side the human played */
    long time;
} HzSaveSlot;

typedef struct {
    HzSaveSlot slot[HZ_SAVE_SLOTS];
} HzSaves;

/* saves/progress.json, or $HZ_SAVES/progress.json. */
const char *saves_path(void);

/* The file into s (all empty if there is none). */
void saves_read(HzSaves *s);

/* s to the file; false (with a message) if it can't be written. */
bool saves_write(const HzSaves *s);

/* The record into slot `slot`, or with slot < 0 into a new one (the first
 * empty, else the one written longest ago); the file rewritten. The slot
 * used, or -1 if the file couldn't be written. */
int saves_store(int slot, const u8 record[4], int human);

/* The wins in a record (map and difficulty pairs, 0-32), and the first map
 * not won on every difficulty (8 if there is none). */
int saves_wins(const u8 record[4]);
int saves_next_map(const u8 record[4]);

#endif /* HZ_SAVES_H */
