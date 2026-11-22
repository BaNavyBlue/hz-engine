/* (Session 78) The network game's id of an assets folder (net_game_id: the
 * code's NET_GAME_CODE and every file the game's loader reads, a PNG by its
 * picture; hz_file.h), as the game works it out at start-up with no mods.
 *
 *     ./hz_gameid ASSETS...
 *
 * One line a folder: the id in hex, then the folder. Two folders whose ids
 * agree play together. `make assets_check` compares the C extractor's
 * files with assets/'s by it. */
#include "../src/core/hz_file.h"
#include "../src/game/assets.h"
#include "../src/net/net_client.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: hz_gameid ASSETS...\n");
        return 2;
    }
    static HzAssets assets;
    int status = 0;
    for (int i = 1; i < argc; i++) {
        hz_file_hash_begin();
        bool ok = hz_assets_load(&assets, argv[i]);
        u64 hash = hz_file_hash_end();
        if (!ok) {
            fprintf(stderr, "hz_gameid: %s: can't load the assets\n",
                    argv[i]);
            status = 1;
            continue;
        }
        printf("%016llX %s\n", (unsigned long long)net_game_id(hash),
               argv[i]);
    }
    return status;
}
