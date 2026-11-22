/* (Session 78) The game's extractor in C from a terminal (src/extract/,
 * which the game's first start runs; docs/first_start.md).
 *
 *     ./hz_assets IMAGE [DIR] [--only K[,K...]]
 *
 * IMAGE is the player's own cartridge image (raw, .smd, a copier's header
 * or a .zip); it must be the cartridge (extract.h). DIR (default assets)
 * gets every file tools/hz_extract.py makes from it -- text byte for byte,
 * PNGs the same picture -- but for maps/path_hints.png, the port's own
 * drawing, which is not the cartridge's and is shipped as it is. --only
 * runs those steps alone (0-based, as listed), for working on one. Exit 0
 * done, 1 a step failed, 2 not the cartridge or no image. `make
 * assets_check ROM=...` holds the result to assets/. */
#include "../src/extract/extract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void)
{
    fprintf(stderr, "usage: hz_assets IMAGE [DIR] [--only K[,K...]]\n"
                    "steps:\n");
    for (int k = 0; k < hz_ex_steps(); k++) {
        fprintf(stderr, "  %2d  %s\n", k, hz_ex_step_name(k));
    }
    return 2;
}

int main(int argc, char **argv)
{
    const char *image = NULL;
    const char *dir = "assets";
    bool only[64] = { false };
    bool some = false;
    int plain = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            for (char *p = argv[++i]; *p != '\0';) {
                char *end = NULL;
                long k = strtol(p, &end, 10);
                if (end == p || k < 0 || k >= hz_ex_steps()) {
                    return usage();
                }
                only[k] = some = true;
                p = (*end == ',') ? end + 1 : end;
                if (*end != ',' && *end != '\0') {
                    return usage();
                }
            }
        } else if (argv[i][0] == '-') {
            return usage();
        } else if (plain == 0) {
            image = argv[i];
            plain++;
        } else if (plain == 1) {
            dir = argv[i];
            plain++;
        } else {
            return usage();
        }
    }
    if (image == NULL) {
        return usage();
    }
    char err[1024];
    u8 *rom = NULL;
    u32 size = 0;
    if (!hz_ex_read_image(image, &rom, &size, err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        return 2;
    }
    if (!hz_ex_is_cartridge(rom, size, err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        free(rom);
        return 2;
    }
    HzExtract *e = hz_ex_begin(rom, size, dir);
    if (e == NULL) {
        fprintf(stderr, "out of memory\n");
        free(rom);
        return 1;
    }
    int status = 0;
    for (int k = 0; k < hz_ex_steps(); k++) {
        if (some && !only[k]) {
            continue;
        }
        printf("[%d/%d] %s\n", k + 1, hz_ex_steps(), hz_ex_step_name(k));
        fflush(stdout);
        if (!hz_ex_step(e, k)) {
            fprintf(stderr, "hz_assets: %s\n", hz_ex_error(e));
            status = 1;
            break;
        }
    }
    if (status == 0 && !hz_ex_finish(e)) {
        fprintf(stderr, "hz_assets: %s\n", hz_ex_error(e));
        status = 1;
    }
    printf("%s: %d files written\n", dir, hz_ex_files(e));
    hz_ex_end(e);
    free(rom);
    return status;
}
