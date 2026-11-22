/* The menu module's helpers ($56490-$573FF) and MenuModule_Title's flow
 * (Menu_TitleFlow, $53028): what title.c and options.c draw and wait
 * with. See front.h. */

#include "front.h"
#include "tables.h"

/* -- the VDP ------------------------------------------------------------- */

/* M_VDPSetAddr ($564A4): auto-increment 2, VRAM writes from addr. */
void front_vram_at(HzGame *g, u16 addr)
{
    vdp_reg(g->vdp, 0x8F02);
    vdp_set_write(g->vdp, VDP_VRAM, addr);
}

void front_vram_bytes(HzGame *g, u16 addr, const u8 *src, u32 bytes)
{
    front_vram_at(g, addr);
    vdp_write_bytes(g->vdp, src, bytes);
}

/* `bytes` of VRAM from addr set to `word`, through the data port (so the
 * address wraps at 64 KB, as Title_Run's clear relies on). */
void front_vram_fill(HzGame *g, u16 addr, u32 bytes, u16 word)
{
    front_vram_at(g, addr);
    for (u32 i = 0; i < bytes; i += 2) {
        vdp_write16(g->vdp, word);
    }
}

/* M_DisplayOn ($565DC) / M_DisplayOff ($565F6): m_vdp_reg1's b6, then
 * register 1 from it. */
void front_display_on(HzGame *g)
{
    g->front.vdp_reg1 |= 0x40;
    vdp_reg(g->vdp, (u16)(0x8100 | g->front.vdp_reg1));
}

void front_display_off(HzGame *g)
{
    g->front.vdp_reg1 &= (u8)~0x40;
    vdp_reg(g->vdp, (u16)(0x8100 | g->front.vdp_reg1));
}

/* M_Init ($5300C): interrupts masked; M_VDPInit (the menus' registers,
 * m_vdp_reg1 $24, display off); m_palette cleared and uploaded. */
void front_init(HzGame *g)
{
    HzFront *f = &g->front;
    g->interrupts_on = false;
    f->vdp_reg1 = 0x24;
    for (int r = 0; r < 19; r++) {
        vdp_reg(g->vdp, (u16)(0x8000 | (r << 8) | MENU_VDP_INIT_REGS[r]));
    }
    front_display_off(g);
    for (int i = 0; i < 64; i++) {
        f->palette[i] = 0;
    }
    front_upload_palette(g);
}

/* M_ResetScroll ($5679C): both planes' H and V scroll 0, and
 * m_scroll_x_b/y_b (the game end's). */
void front_reset_scroll(HzGame *g)
{
    g->front.scroll_x_b = 0;
    g->front.scroll_y_b = 0;
    vdp_set_write(g->vdp, VDP_VRAM, 0xDC00);
    vdp_write16(g->vdp, 0);
    vdp_set_write(g->vdp, VDP_VSRAM, 0);
    vdp_write16(g->vdp, 0);
    vdp_set_write(g->vdp, VDP_VRAM, 0xDC02);
    vdp_write16(g->vdp, 0);
    vdp_set_write(g->vdp, VDP_VSRAM, 2);
    vdp_write16(g->vdp, 0);
}

/* M_ApplyScroll ($567EC): m_scroll_x_b / m_scroll_y_b to plane B. */
void front_apply_scroll(HzGame *g)
{
    vdp_set_write(g->vdp, VDP_VRAM, 0xDC02);
    vdp_write16(g->vdp, (u16)g->front.scroll_x_b);
    vdp_set_write(g->vdp, VDP_VSRAM, 2);
    vdp_write16(g->vdp, (u16)g->front.scroll_y_b);
}

/* M_ClearSprites ($56816): m_sprites cleared and its 80 sprites uploaded
 * to $D800 (all zero: one 8 x 8 sprite off the top, linking nowhere). */
void front_clear_sprites(HzGame *g)
{
    front_vram_fill(g, 0xD800, 0x280, 0);
}

/* M_ClearPlanes ($5684E): plane B and the window filled with the
 * background tile ($30, palette 0), with the display off for it -- after
 * waiting for the frame if it was on. */
void front_clear_planes(HzGame *g)
{
    front_frame(g);
    front_display_off(g);
    front_vram_fill(g, FRONT_PLANE_B, 0x2000, 0x0030);
    front_display_on(g);
}

/* M_BlankPlanes ($530C0): plane B and the window filled with $26BE --
 * palette 1, tile $6BE, which is blank (Title_DrawLogo clears it, and the
 * logo's last tile, copied there by the slide, is blank too). */
void front_blank_planes(HzGame *g)
{
    front_vram_fill(g, FRONT_PLANE_B, 0x2000, 0x26BE);
}

/* -- frames and pads ----------------------------------------------------- */

/* M_WaitVBlank ($5651C), M_WaitVBlankIfOn, M_WaitVBlankEndIfOn: each
 * polls the VDP's status for one VBlank, the "if on" ones only with the
 * display on. M_WaitVBlank is only called with the display on (with it
 * off the status reads as VBlank throughout, and its wait for the flag to
 * clear would never end). */
void front_frame(HzGame *g)
{
    if ((g->front.vdp_reg1 & 0x40) && g->frame != NULL && !g->quit) {
        g->frame(g, g->frame_user);
    }
}

/* M_ReadJoypads ($568BA): both pads OR'd, active high, into a word whose
 * high byte is $FF (the 68k's not.w of a byte); then the presses: the
 * bits set now and not at the last read. (The 68k leaves the word at
 * v_joypad1, clobbering both pad bytes the game reads; Game_Start clears
 * them.) The first read after M_Init... sees $FF in the high byte as new:
 * nothing tests those bits. */
u16 front_read_pads(HzGame *g)
{
    u8 b = 0;
    for (int p = 0; p < 2; p++) {
        const HzPad *pad = &g->pad_in[p];
        b |= (u8)((pad->up ? HZ_PAD_UP : 0) | (pad->down ? HZ_PAD_DOWN : 0)
                  | (pad->left ? HZ_PAD_LEFT : 0)
                  | (pad->right ? HZ_PAD_RIGHT : 0)
                  | (pad->b ? HZ_PAD_B : 0) | (pad->c ? HZ_PAD_C : 0)
                  | (pad->a ? HZ_PAD_A : 0)
                  | (pad->start ? HZ_PAD_START : 0));
    }
    HzFront *f = &g->front;
    u16 now = (u16)(0xFF00 | b);
    u16 pressed = (u16)((now ^ f->pad_held) & now);
    f->pad_pressed = pressed;
    f->pad_held = now;
    return pressed;
}

/* M_WaitFramePad ($56948) and M_WaitFramePad2 ($5696A): a frame, then
 * the pads; the new presses. (They wait for the end of VBlank and for its
 * start respectively: a frame either way.) */
u16 front_wait_pad(HzGame *g)
{
    front_frame(g);
    return front_read_pads(g);
}

/* M_WaitFrames ($5441A): n + 1 frames (dbf). */
void front_wait_frames(HzGame *g, int n)
{
    for (int i = 0; i <= n && !g->quit; i++) {
        front_frame(g);
    }
}

/* -- text ------------------------------------------------------------------ */

/* M_SetAttrInline ($56A64): in front, the palette line (the inline word
 * rotated right 3), the plain font. */
void front_text_palette(HzGame *g, int line)
{
    g->front.text_attr = (u16)(0x8000 | ((line & 7) << 13));
}

/* M_SetTileBaseInline ($56AA4): the font set, the palette kept. */
void front_text_font(HzGame *g, u16 base)
{
    g->front.text_attr = (u16)((g->front.text_attr & 0xE000) | base);
}

void front_cursor(HzGame *g, int x, int y)
{
    g->front.cursor_x = (u16)x;
    g->front.cursor_y = (u16)y;
}

static u16 cursor_cell(const HzFront *f)
{
    return (u16)(FRONT_WINDOW + ((f->cursor_y << 6) + f->cursor_x) * 2);
}

/* One character at the cursor's cell, the cursor on: tile = character -
 * $30 + m_text_attr (M_PrintStrAtCursor and the others' loop). */
static void put_char(HzGame *g, u8 c)
{
    HzFront *f = &g->front;
    vdp_write16(g->vdp, (u16)(c - 0x30 + f->text_attr));
    f->cursor_x++;
}

/* M_PrintStrAtCursor / M_PrintInlineAtCursor. */
void front_print(HzGame *g, const char *text)
{
    front_vram_at(g, cursor_cell(&g->front));
    for (const char *s = text; *s != '\0'; s++) {
        put_char(g, (u8)(*s == ' ' ? MENU_BLANK : *s));
    }
}

/* M_PrintInlineAt ($56B5A). */
void front_print_at(HzGame *g, int x, int y, const char *text)
{
    front_cursor(g, x, y);
    front_print(g, text);
}

/* Text already in the font's codes (the assets' titles and names). */
void front_print_codes(HzGame *g, const u8 *codes, int n)
{
    front_vram_at(g, cursor_cell(&g->front));
    for (int i = 0; i < n; i++) {
        put_char(g, codes[i]);
    }
}

/* M_Print2DigitsAtCursor ($56EA6): n, capped at 99, as two digits. */
void front_print_2digits(HzGame *g, int n)
{
    if (n > 99) {
        n = 99;
    }
    u8 digits[2] = { (u8)('0' + n / 10), (u8)('0' + n % 10) };
    front_print_codes(g, digits, 2);
}

void front_box_line(HzGame *g, int x, int y, char left, char fill,
                    char right, int n)
{
    front_cursor(g, x, y);
    front_vram_at(g, cursor_cell(&g->front));
    put_char(g, (u8)left);
    for (int i = 0; i < n; i++) {
        put_char(g, (u8)fill);
    }
    put_char(g, (u8)right);
}

void front_box(HzGame *g, int x, int y, int w, int h)
{
    front_box_line(g, x, y, BOX_TOP_LEFT, BOX_TOP, BOX_TOP_RIGHT, w);
    for (int r = 1; r <= h; r++) {
        front_box_line(g, x, y + r, BOX_LEFT, MENU_BLANK, BOX_RIGHT, w);
    }
    front_box_line(g, x, y + h + 1, BOX_BOTTOM_LEFT, BOX_BOTTOM,
                   (char)BOX_BOTTOM_RIGHT, w);
}

/* -- palettes ---------------------------------------------------------------- */

/* M_UploadPaletteLines ($56F24): the lines of m_palette whose bits are in
 * mask (b0 line 0 ... b3 line 3) into CRAM. */
void front_upload_lines(HzGame *g, unsigned mask)
{
    vdp_reg(g->vdp, 0x8F02);
    for (int line = 0; line < 4; line++) {
        if (mask & (1u << line)) {
            vdp_set_write(g->vdp, VDP_CRAM, (u16)(line * 32));
            for (int i = 0; i < 16; i++) {
                vdp_write16(g->vdp, g->front.palette[line * 16 + i]);
            }
        }
    }
}

/* M_UploadPalette ($56F6E): all of m_palette. (It also clears
 * m_palette_pending, which only code nothing calls sets.) */
void front_upload_palette(HzGame *g)
{
    front_upload_lines(g, 0x0F);
}

/* M_ClearPalette ($56FAA): CRAM and m_palette black. (It also clears
 * m_palette_pending, which only code nothing calls sets.) */
void front_clear_palette(HzGame *g)
{
    for (int i = 0; i < 64; i++) {
        g->front.palette[i] = 0;
    }
    front_upload_palette(g);
}

/* M_PaletteSetTarget ($56FE0) */
void front_set_target(HzGame *g)
{
    for (int i = 0; i < 64; i++) {
        g->front.target[i] = g->front.palette[i];
    }
}

/* One channel a level ($2 in its nibble) towards the target's. */
static u16 step_channel(u16 now, u16 want, u16 mask, u16 step)
{
    now &= mask;
    want &= mask;
    if (now == want) {
        return now;
    }
    return (u16)(now < want ? now + step : now - step);
}

/* M_PaletteStepToTarget ($57086): every colour's three channels a level
 * towards m_palette_target. */
void front_step_to_target(HzGame *g)
{
    HzFront *f = &g->front;
    for (int i = 0; i < 64; i++) {
        u16 c = f->palette[i];
        u16 t = f->target[i];
        f->palette[i] = (u16)(step_channel(c, t, 0x0E00, 0x200)
                              | step_channel(c, t, 0x00E0, 0x20)
                              | step_channel(c, t, 0x000E, 0x2));
    }
}

/* One nibble of a target colour less `by`, floor 0. */
static u16 darken(u16 nibble, u16 by)
{
    return nibble > by ? (u16)(nibble - by) : 0;
}

/* M_PaletteApplyFade ($570F0): m_palette = the target with
 * m_fade_level / 2 off each channel's nibble. */
void front_apply_fade(HzGame *g)
{
    HzFront *f = &g->front;
    u16 by = (u16)((u16)f->fade_level >> 1);
    for (int i = 0; i < 64; i++) {
        u16 t = f->target[i];
        u16 r = darken(t & 0xF, by);
        u16 gr = darken((t >> 4) & 0xF, by);
        u16 b = darken(t >> 8, by);
        f->palette[i] = (u16)(((b << 8) | (gr << 4) | r) & 0x0EEE);
    }
}

/* M_FadeToBlack ($56FF6): m_fade_level 0 to $1F, a step a frame, each
 * uploaded after the frame it was made in. */
void front_fade_to_black(HzGame *g)
{
    HzFront *f = &g->front;
    f->fade_level = 0;
    do {
        front_apply_fade(g);
        front_frame(g);
        front_upload_lines(g, 0x0F);
        f->fade_level++;
    } while (f->fade_level < 0x20);
}

/* M_FadeFromBlack ($57042): m_fade_level $20 down to 0, a step a frame
 * (33 frames), every colour from black up to its target. */
void front_fade_from_black(HzGame *g)
{
    HzFront *f = &g->front;
    f->fade_level = 0x20;
    do {
        front_apply_fade(g);
        front_frame(g);
        front_upload_lines(g, 0x0F);
        f->fade_level--;
    } while (f->fade_level >= 0);
}

/* -- sound ------------------------------------------------------------------- */

/* M_Snd_PlaySfxNow ($57328) then M_Snd_FlushSfx ($57342): the effect
 * queued with priority 1, then sent ($02, sfx, 1) and the queue emptied.
 * (The SOUND test's only use; the menus are otherwise silent.) */
void front_play_sfx(HzGame *g, u8 sfx)
{
    g->sfx_priority = 1;
    g->sfx_queued = sfx;
    snd_flush_sfx(g);
}

/* -- MenuModule_Title -------------------------------------------------------- */

/* Menu_TitleFlow ($53028): M_Init, the title; if it ran out (the title
 * song ended before Start) -1, the attract demo; else the options and 0,
 * a game with v_options as they left them. */
int menu_title_flow(HzGame *g)
{
    front_init(g);
    if (title_run(g) != 0) {
        return -1;
    }
    options_run(g);
    return 0;
}
