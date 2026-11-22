/* (Session 78) hz_extract.py's extract_ending in C (ex.h): the endings'
 * pictures, the story font, the two stories and the staff roll
 * (Ending_Run's). */
#include "ex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* story_text: the story font's codes as the characters they draw (its
 * punctuation sits on other codes, STORY_PUNCT), {XX} for a code with no
 * character of its own. */
static void story_text(char *out, size_t n, const u8 *b, int len)
{
    size_t k = 0;
    for (int i = 0; i < len && k + 5 < n; i++) {
        u8 c = b[i];
        char p = (c == 0x3A) ? '.' : (c == 0x3B) ? ',' : (c == 0x3C) ? '\''
               : (c == 0x3D) ? '"' : (c == 0x3E) ? '!' : (c == 0x40) ? ' '
               : 0;
        if (p != 0) {
            out[k++] = p;
        } else if ((c >= 0x30 && c <= 0x39) || (c >= 0x41 && c <= 0x5A)
                   || (c >= 0x61 && c <= 0x7A)) {
            out[k++] = (char)c;
        } else {
            k += (size_t)snprintf(out + k, n - k, "{%02X}", c);
        }
    }
    out[k] = '\0';
}

static bool words_at(HzEx *x, u32 a, u16 *words)
{
    const u8 *p = ex_at(x, a, 32);
    if (p == NULL) {
        return false;
    }
    ex_palette_words(p, 1, words);
    return true;
}

static bool has_zero(const u8 *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (b[i] == 0) {
            return true;
        }
    }
    return false;
}

static bool pictures(HzEx *x)
{
    u16 text[16], still[16], grave[16], forest_pal[16];
    if (!words_at(x, EXR_EndingTextPalette, text)
        || !words_at(x, EXR_EndingPaletteStillLife, still)
        || !words_at(x, EXR_EndingPaletteGrave, grave)
        || !words_at(x, EXR_ForestPalette, forest_pal)) {
        return false;
    }
    if (!ex_write_palette(x, "palettes/ending_text.png", text, 16, 16)) {
        return false;
    }
    u32 n0 = 0, n1 = 0, n2 = 0, n3 = 0, n4 = 0;
    u8 *font = ex_rle(x, EXR_RLE_MenuGfx, 3, &n0);
    u8 *ende = font ? ex_rle(x, EXR_RLE_MenuGfx, 4, &n1) : NULL;
    u8 *p0 = ende ? ex_rle(x, EXR_RLE_EndingPictures, 0, &n2) : NULL;
    u8 *p1 = p0 ? ex_rle(x, EXR_RLE_EndingPictures, 1, &n3) : NULL;
    u8 *forest = p1 ? ex_lzw(x, EXR_pack_forest, 0, &n4) : NULL;
    bool ok = forest != NULL
      && ex_write_tile_sheet(x, "ending/story_font.png", font, n0, 80, text,
                             16, false)
      && ex_write_picture(x, "ending/ende.png", ende, n1, 16, 4, text, 16)
      && ex_write_picture(x, "ending/still_life.png", p0, n2, 40, 28, still,
                          16)
      && ex_write_picture(x, "ending/grave.png", p1, n3, 40, 28, grave, 16)
      && ex_write_picture(x, "ending/forest.png", forest, n4, 40, 28,
                          forest_pal, 16);
    free(font);
    free(ende);
    free(p0);
    free(p1);
    free(forest);
    return ok;
}

static bool story(HzEx *x, const char *rel, const char *label, u32 a,
                  const char *who)
{
    const u8 *t = ex_at(x, a, 22 * 0x28);
    if (t == NULL) {
        return false;
    }
    ExBuf b;
    exb_init(&b);
    char line[200];
    snprintf(line, sizeof line, "The endings' story (%s), %s:", label, who);
    exb_header(&b, line);
    exb_header(&b, "22 lines of up to 38 characters, scrolled up over the "
                   "picture.");
    exb_header(&b, "Spaces at the end of a line are added back; {XX} is a "
                   "code of the");
    exb_header(&b, "story font (story_font.png) with no character of its "
                   "own.");
    for (int n = 0; n < 22; n++) {
        const u8 *r = t + n * 0x28;
        if (r[38] != 0 || r[39] != 0 || has_zero(r, 38)) {
            exb_free(&b);
            return ex_fail(x, "%s line %d: not 38 characters and two 0s",
                           label, n);
        }
        char s[200];
        story_text(s, sizeof s, r, 38);
        size_t k = strlen(s);
        while (k > 0 && s[k - 1] == ' ') {
            s[--k] = '\0';
        }
        exb_puts(&b, s);
        exb_putc(&b, '\n');
    }
    bool ok = ex_write_buf(x, rel, &b);
    exb_free(&b);
    return ok;
}

/* A staff roll line: "%-8s|%s|". */
static void staff_line(ExBuf *b, const char *kind, const u8 *r)
{
    char s[100];
    story_text(s, sizeof s, r, 16);
    exb_printf(b, "%-8s|%s|\n", kind, s);
}

static bool staff(HzEx *x)
{
    ExBuf b;
    exb_init(&b);
    exb_header(&b, "The staff roll (StaffText), a line each: its kind, then "
                   "16");
    exb_header(&b, "characters between bars (fewer are padded with spaces). "
                   "heading: red,");
    exb_header(&b, "name: yellow; a blank row comes before the first name "
                   "after a");
    exb_header(&b, "heading, 17 before the first heading after a name. Then "
                   "the last");
    exb_header(&b, "line, by the machine's region: overseas (\"Techno "
                   "Soft\") or japan.");
    u32 a = EXR_StaffText;
    const char *kind = "heading";
    for (;;) {
        const u8 *r = ex_at(x, a, 18);
        if (r == NULL) {
            exb_free(&b);
            return false;
        }
        if (r[16] != 0 || has_zero(r, 16)) {
            exb_free(&b);
            return ex_fail(x, "StaffText at $%X: not 16 characters and a 0",
                           (unsigned)a);
        }
        staff_line(&b, kind, r);
        a += 17;
        u8 mark = r[17];
        if (mark == 0) {
            break;
        }
        if (mark == 1) {
            kind = "name";
            a++;
        } else if (mark == 2) {
            kind = "heading";
            a++;
        }
    }
    static const struct {
        const char *kind, *label;
        u32 at;
    } LAST[2] = {
        { "overseas", "StaffCopyrightOverseas", EXR_StaffCopyrightOverseas },
        { "japan", "StaffCopyrightJapan", EXR_StaffCopyrightJapan },
    };
    for (int i = 0; i < 2; i++) {
        const u8 *r = ex_at(x, LAST[i].at, 17);
        if (r == NULL) {
            exb_free(&b);
            return false;
        }
        if (r[16] != 0) {
            exb_free(&b);
            return ex_fail(x, "%s: not 16 characters and a 0", LAST[i].label);
        }
        staff_line(&b, LAST[i].kind, r);
    }
    bool ok = ex_write_buf(x, "ending/staff.txt", &b);
    exb_free(&b);
    return ok;
}

/* extract_ending. */
bool ex_ending(HzEx *x)
{
    return pictures(x)
        && story(x, "ending/story_p1.txt", "EndingText1", EXR_EndingText1,
                 "unless the human played P2 against the computer")
        && story(x, "ending/story_p2.txt", "EndingText2", EXR_EndingText2,
                 "when the human played P2 against the computer")
        && staff(x);
}
