#ifndef SONGTEXT_H
#define SONGTEXT_H

#include "../core/hz_types.h"

/* The songs and sound effects as text (docs/sound.md): dir/sounds.txt,
 * the song files it names (or their trackers) and the effects' file,
 * built into the 32 KB the Z80 driver's bank window shows at $8000-$FFFF
 * (SoundData), byte for byte as tools/hzsongtext.py builds them. The
 * ROM's text builds the ROM's SoundData. */

#define SONGTEXT_SIZE 0x8000

/* Builds image from the text in dir; *effects_at gets the effects' table
 * (the Z80 address Snd_SetSfxTable gives the driver). Songs that don't all
 * fit before the effects each get a window of their own (docs/sound.md,
 * Room): *song_windows is then *song_count windows of SONGTEXT_SIZE,
 * malloc'd, the caller's, and image the effects with no song; while they
 * fit, *song_windows is NULL and *song_count 0. False if the text is
 * refused: the file, the line and why are said on stderr. */
bool songtext_build(const char *dir, u8 image[SONGTEXT_SIZE],
                    u16 *effects_at, u8 **song_windows, int *song_count);

/* A song file's tracker beside it (songs/NN_title.trk, docs/sound.md, "The
 * tracker") is read in its place (on, as the game does) or not (off: the
 * text alone, which builds the ROM's SoundData; hz_roundtrip). */
void songtext_use_trackers(bool on);

/* (Session 50) The number of song `song`'s FM instrument named `name` (its
 * head line's: `fm_instrument 8 kick_toms`), as the last songtext_build
 * read it, or -1 if it has none of that name (the drum map,
 * sound/q_drums.json, names its instruments). */
int songtext_fm_instrument(int song, const char *name);

#endif /* SONGTEXT_H */
