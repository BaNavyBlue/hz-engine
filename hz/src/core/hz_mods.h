#ifndef HZ_MODS_H
#define HZ_MODS_H

#include "hz_types.h"

#include <stddef.h>

/* The mods folder (docs/mods.md, PS2's): a file under it, at the place of
 * one under assets/, is read in place of the game's own. Only the game
 * turns it on (main.c); the tools and the checks read assets/ alone.
 *
 * Every asset read goes through hz_mods_resolve (hz_file_read does it),
 * which notes each mod it hands out. hz_assets_load runs its loaders a
 * step at a time: when a step is refused with mods in it, the mods it
 * refused are set aside (or, if the refusal named none of them, every mod
 * the step read), and the step runs again, until it loads or the game's
 * own files are refused. */

/* Turns the folder on for paths under assets_dir; mods_dir NULL or "":
 * off. */
void hz_mods_set(const char *assets_dir, const char *mods_dir);

/* The path to read for `path`: its mod's while there is one under the
 * folder and it isn't set aside (written into buf), else path. */
const char *hz_mods_resolve(const char *path, char *buf, size_t size);

/* A reader turned `path` down (the mod's path or the game's own path it
 * stands in for): if it was a mod, it is marked to be set aside. Gives
 * the path to name in the message: the mod's, else path. */
const char *hz_mods_refused(const char *path);

/* A loader's step starts (hz_assets_load). */
void hz_mods_step(void);

/* The step was refused: its refused mods set aside (else every mod it
 * read), each named on stderr. False if it read none: the game's own
 * files were refused. */
bool hz_mods_set_aside(void);

/* The folder (NULL: off), and the mods in use: read and not set aside. */
const char *hz_mods_dir(void);
int         hz_mods_count(void);
const char *hz_mods_name(int i);

#endif /* HZ_MODS_H */
