#!/usr/bin/env python3
"""Checks tools/hz_loop's log against a model of the frame loop's ported
routines, written from the 68k code, not from the C:

  ReadJoypads ($B30)       the pads as buttons -> S A C B R L D U bits
  Game_CheckPause ($4400)  the pause (its VBlanks, Snd_Pause, the frame
                           counter put back, the free-ammo cheat) and the
                           demo's Start; Demo_PlayInput ($44E4), the
                           demo's pads from DemoData (hz_loop's demos
                           start with each pad at run 0 and no frames
                           left: Demo_Start doesn't run, so the first run
                           plays for 256 frames)
  Game_SecondTick ($808)   timer, income, HQs, demo timeout, seed, boost
  Mechs_Update ($3044)     v_mech_tick, mech_pad, Mech_UpdateProduction
  HUD_UpdateGauges ($2F26) the shown bytes and the bars queued, from the
                           mechs' HP, ammo and fuel as Mech_Update leaves
                           them (its own check is hzmech_check's) and the
                           HQs' HP; HUD_QueueBar and HUD_FlushBars as
                           tools/hzhud_check.py models them, so the window
                           cells under the bars (logged as a CRC), from
                           HUDFrameTiles before any bar is drawn
  UnitAI_Update ($5DF20)   v_unit_ai_group + 1, back to 0 past $B
  Map_AnimTilesUpdate ($5738) and _Upload ($5812), on groups 0 and 6:
                           tools/hzanim_check.py's model, carried from the
                           map as loaded (the steps, the tiles shown, the
                           colour cycle, VRAM's tiles as a CRC, CRAM line
                           0 colours 2 and $B)
  VDP_WaitVBlank           one VBlank a pass, the frame counter + 1

For every pass the model starts from the logged state before it ("S") and
must arrive at the logged state after it ("E"); and each "S" must be the
previous "E" apart from what the harness says it poked.

  hzloop_check.py ROM FILE...   (or directories of *.txt)
"""

import bisect
import os
import sys
import zlib

from hzanim_check import Anim
from hzhud_check import CMDS, Model as HudModel

FIELDS = ("fc timer money1 money2 boost seed saved free cmd_sent cmd_arg "
          "tick flags1 flags2 hq1 hq2 posts1 posts2 units1 units2 game_type "
          "mpad1 mpad2 joy1 joy2 prod1 prod2 ptimer1 ptimer2 sfx mprev1 "
          "mprev2 hp1 ammo1 fuel1 hp2 ammo2 fuel2 shown1 shown2 bars group "
          "cyc0 cyc1 steps tiles_shown tiles colours dpos1 dpos2 drun1 "
          "drun2").split()

DEMO_DATA = 0x4E000     # DemoData: $800 a map; P1's runs at $100, P2's $480

ANIM_FIELDS = "cyc0 cyc1 steps tiles_shown tiles colours".split()

MECH_OUT = "hp1 ammo1 fuel1 hp2 ammo2 fuel2".split()

POKED = {1: ("hq1",), 2: ("hq2",), 4: ("posts1",), 8: ("posts2",),
         0x10: ("units1",), 0x20: ("units2",), 0x40: ("money1",),
         0x80: ("money2",), 0x100: ("fc",), 0x200: ("flags1",),
         0x400: ("flags2",), 0x800: ("prod1", "ptimer1"),
         0x1000: ("prod2", "ptimer2"), 0x2000: ("hp1", "ammo1", "fuel1"),
         0x4000: ("hp2", "ammo2", "fuel2")}

HUD_FRAME = 0x6C94      # HUDFrameTiles: 4 rows of 40 words


class Hud:
    """The window cells under the eight bars (P1 HP, HQ, ammo, fuel, then
    P2's), as words."""

    def __init__(self, rom):
        self.model = HudModel(rom)
        self.bars = []
        for p in (0, 1):
            for k, cmd in enumerate(CMDS[p]):
                addr = ((cmd >> 16) & 0x3FFF) | ((cmd & 3) << 14)
                row, col = (addr - 0xF000) // 0x80, (addr - 0xF000) % 0x80 // 2
                n = 8 if k < 2 else 6
                o = HUD_FRAME + (row * 40 + col) * 2
                self.bars.append([(rom[o + 2 * i] << 8) | rom[o + 2 * i + 1]
                                  for i in range(n)])

    def crc(self, bars):
        data = bytes(b for bar in bars for w in bar for b in (w >> 8, w & 0xFF))
        return zlib.crc32(data) & 0xFFFFFFFF

    def update(self, s, bars):
        """HUD_UpdateGauges, then HUD_FlushBars: s's shown bytes and the
        bars after."""
        shown = [(s["shown%d" % (k // 4 + 1)] >> (24 - 8 * (k % 4))) & 0xFF
                 for k in range(8)]
        inputs = [(s["flags%d" % n], s["hp%d" % n], s["hq%d" % n],
                   s["ammo%d" % n], s["fuel%d" % n]) for n in (1, 2)]
        shown, queued = self.model.update_gauges(shown, inputs)
        for p in (0, 1):
            sh = shown[p * 4:p * 4 + 4]
            s["shown%d" % (p + 1)] = ((sh[0] << 24) | (sh[1] << 16)
                                      | (sh[2] << 8) | sh[3])
        bars = [queued.get(k, bar) for k, bar in enumerate(bars)]
        s["bars"] = self.crc(bars)
        return bars

BITS = {"U": 0x01, "D": 0x02, "L": 0x04, "R": 0x08, "B": 0x10, "C": 0x20,
        "A": 0x40, "S": 0x80}

SFX_READY = 0x12
QUIT_AFTER = 200000     # hz_loop's hook quits past this many VBlanks


def parse_script(text):
    steps = []
    if text == "-":
        return steps
    for item in text.split(","):
        v, port, buttons = item.split(":")
        bits = 0
        for ch in buttons:
            if ch != "-":
                bits |= BITS[ch]
        steps.append((int(v), int(port) - 1, bits))
    return steps


class Pads:
    """What each port holds after a number of VBlanks: the hook of VBlank
    v applies the script's steps for v, so after n VBlanks the steps with
    v < n count, the last one per port winning."""

    def __init__(self, steps):
        self.v = [[], []]
        self.bits = [[], []]
        for v, port, bits in steps:
            self.v[port].append(v)
            self.bits[port].append(bits)
        for port in (0, 1):
            order = sorted(range(len(self.v[port])),
                           key=lambda i: self.v[port][i])
            self.v[port] = [self.v[port][i] for i in order]
            self.bits[port] = [self.bits[port][i] for i in order]

    def joy(self, n):
        out = [0, 0]
        for port in (0, 1):
            k = bisect.bisect_left(self.v[port], n)
            if k > 0:
                out[port] = self.bits[port][k - 1]
        return out


def state(words):
    return {k: int(w, 16) for k, w in zip(FIELDS, words)}


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def anim_state(a):
    """The anim model's state as hz_loop logs it."""
    return {"cyc0": a.cyc[0], "cyc1": a.cyc[1],
            "steps": int("".join("%02X" % v for v in a.step), 16),
            "tiles_shown": int("".join("%04X" % v for v in a.shown), 16),
            "tiles": zlib.crc32(bytes(a.tiles())),
            "colours": a.cram[2] << 16 | a.cram[11]}


def demo_play_input(s, rom, map_n):
    """Demo_PlayInput: each pad's byte from its DemoData run (2 bytes:
    buttons, frames); the run's count down a byte, at 0 the next run's
    count and the place on; a run of 0 frames ends the demo (P1's first,
    then P2's isn't read)."""
    for p, base in ((1, 0x100), (2, 0x480)):
        a = DEMO_DATA + map_n * 0x800 + base + s["dpos%d" % p] * 2
        if rom[a + 1] == 0:
            s["fc"] = (s["fc"] & 0xFFFF0000) | 0xFF00
            return
        s["joy%d" % p] = rom[a]
        run = (s["drun%d" % p] - 1) & 0xFF
        if run == 0:
            run = rom[a + 3]
            s["dpos%d" % p] += 1
        s["drun%d" % p] = run


def model_pass(st, n_pass, vblanks, pads, boost_hp, hud, bars, mech_out,
               anim, rom, map_n):
    """One pass of Game_FrameLoop from state st; returns (exit, vblanks,
    state after, the HUD bars after). mech_out: the mechs' HP, ammo and
    fuel as Mech_Update left them (the log's). anim: the map animation's
    model, stepped in place."""
    s = dict(st)
    demo = s16(s["game_type"]) < 0

    # ReadJoypads (the first pass enters past it; Game_Start cleared them)
    if n_pass > 0:
        s["joy1"], s["joy2"] = pads.joy(vblanks)

    # Game_CheckPause
    held = s["joy1"] | s["joy2"]
    if demo:
        if held & 0x80:
            s["fc"] = (s["fc"] & 0xFFFF0000) | 0xFF00
        else:
            demo_play_input(s, rom, map_n)
    elif held & 0x80:
        saved = s["fc"]
        s["cmd_sent"] = (s["cmd_sent"] + 1) & 0xFF
        s["cmd_arg"] = held
        quit_ = False

        def pause_frame():
            nonlocal vblanks, quit_
            vblanks += 1
            if vblanks > QUIT_AFTER:
                quit_ = True
            if s["fc"] != 0xFFFFFFFF:
                s["fc"] += 1        # VBlank counts; put back after
            s["joy1"], s["joy2"] = pads.joy(vblanks)
            return s["joy1"] | s["joy2"]

        d0 = pause_frame()
        while d0 & 0x80 and not quit_:
            d0 = pause_frame()
        d0 = pause_frame()
        while not d0 & 0x80 and not quit_:
            d0 = pause_frame()
        if quit_:
            return 3, vblanks, s, bars
        if not ((s["flags1"] | s["flags2"]) & 1) and d0 == 0xFF:
            s["free"] ^= 1
        d0 = pause_frame()
        while d0 & 0x80 and not quit_:
            d0 = pause_frame()
        s["cmd_sent"] = (s["cmd_sent"] + 1) & 0xFF
        s["cmd_arg"] = d0
        vblanks += 1
        s["fc"] = saved
        if quit_ or vblanks > QUIT_AFTER:
            return 3, vblanks, s, bars

    # Game_SecondTick
    t = s["timer"] + 1
    if t >= 60:
        t = 0
    s["timer"] = t
    for p, at in ((1, 59), (0, 58)):
        if t == at:
            key = "money%d" % (p + 1)
            income = (s["posts%d" % (p + 1)] + 1) * 4
            if demo:
                income *= 2
            s[key] = min((income + s[key]) & 0xFFFF, 50000)
    if s["hq1"] == 0 or s["hq2"] == 0:
        return 1, vblanks, s, bars  # Game_Over; v_winner isn't logged
    if demo:
        if s["fc"] & 0xFFFF >= 0xE10:
            return 2, vblanks, s, bars  # MainLoop_Title
    else:
        s["saved"] = s["seed"]
    if t != 0:
        f1, f2 = s["flags1"], s["flags2"]
        if not ((f1 ^ f2) & 1):
            s["boost"] = 0
        else:
            cpu = 0 if f1 & 1 else 1
            human = 1 - cpu
            ahead = (s["hq%d" % (cpu + 1)] < boost_hp
                     or s["posts%d" % (human + 1)] >= 5
                     or s["units%d" % (human + 1)] >= 25
                     or s["fc"] >= 0x23280)
            s["boost"] = (0x8000 if cpu == 0 else 0x8010) if ahead else 0

    # Mechs_Update: pads and production (camera: nothing moves it here)
    s["tick"] = (s["tick"] + 1) & 0xFFFF
    queued = False
    joy = (s["joy1"], s["joy2"])
    flags = (s["flags1"], s["flags2"])
    for p in (0, 1):
        s["mprev%d" % (p + 1)] = s["mpad%d" % (p + 1)]
        if not flags[p] & 1:
            pad = joy[p]
            if flags[1 - p] & 1:
                pad |= joy[1 - p]
            s["mpad%d" % (p + 1)] = pad
        if s["prod%d" % (p + 1)] == 1:
            timer = s["ptimer%d" % (p + 1)]
            s["ptimer%d" % (p + 1)] = (timer - 1) & 0xFFFF
            if timer == 0:
                s["prod%d" % (p + 1)] = 2
                queued = True

    for k in MECH_OUT:
        s[k] = mech_out[k]

    # UnitAI_Update: the group step
    s["group"] = s["group"] + 1 if s["group"] < 0xB else 0

    # HUD_UpdateGauges, then (after the VBlank) HUD_FlushBars
    bars = hud.update(s, bars)

    # Map_AnimTilesUpdate, then (after the VBlank) Map_AnimTilesUpload
    anim.frame(s["group"])
    s.update(anim_state(anim))

    # VDP_WaitVBlank and the VBlank handler; Snd_FlushSfx
    vblanks += 1
    if s["fc"] != 0xFFFFFFFF:
        s["fc"] += 1
    s["sfx"] = SFX_READY if queued else 0xFF
    return (3 if vblanks > QUIT_AFTER else 0), vblanks, s, bars


def check_file(path, hud, rom):
    errors = []
    passes = 0
    counts = {"pauses": 0, "cheat": 0, "income": 0, "boost_on": 0,
              "boost_off": 0, "ready": 0, "bars": 0}
    bars = hud.bars
    with open(path) as f:
        lines = [ln.split() for ln in f if ln.strip()]
    if not lines or lines[0][0] != "HZL1":
        return ["not an hz_loop log"], 0, counts
    anim = Anim.from_rom(rom, int(lines[0][1], 16))
    counts["anim"] = 0
    boost_hp = int(lines[1][2], 16)
    pads = Pads(parse_script(lines[2][1] if len(lines[2]) > 1 else "-"))
    prev_end = None
    i = 3
    while i + 1 < len(lines):
        sl, el = lines[i], lines[i + 1]
        i += 2
        n_pass, vb, mask = (int(w, 16) for w in sl[1:4])
        before = state(sl[4:])
        ex, vb_after = int(el[2], 16), int(el[3], 16)
        after = state(el[4:])
        passes += 1
        if prev_end is not None:
            poked = set()
            for bit, names in POKED.items():
                if mask & bit:
                    poked.update(names)
            for k in FIELDS:
                if k not in poked and before[k] != prev_end[k]:
                    errors.append("pass %d: %s changed between passes"
                                  % (n_pass, k))
        else:
            if before["bars"] != hud.crc(bars):
                errors.append("pass 0: the bars aren't HUDFrameTiles' cells")
            for k, v in anim_state(anim).items():
                if before[k] != v:
                    errors.append("pass 0: %s %X, the map as loaded %X"
                                  % (k, before[k], v))
        frames = anim.counts["frames"]
        m_exit, m_vb, m_after, m_bars = model_pass(
            before, n_pass, vb, pads, boost_hp, hud, bars,
            {k: after[k] for k in MECH_OUT}, anim, rom,
            int(lines[0][1], 16))
        counts["anim"] += anim.counts["frames"] - frames
        if m_bars != bars:
            counts["bars"] += sum(a != b for a, b in zip(m_bars, bars))
        bars = m_bars
        if m_exit != ex:
            errors.append("pass %d: exit %d, model %d" % (n_pass, ex, m_exit))
        if m_vb != vb_after:
            errors.append("pass %d: %d VBlanks after, model %d"
                          % (n_pass, vb_after, m_vb))
        for k in FIELDS:
            if m_after[k] != after[k]:
                errors.append("pass %d: %s %X, model %X"
                              % (n_pass, k, after[k], m_after[k]))
        if after["cmd_sent"] != before["cmd_sent"]:
            counts["pauses"] += 1
        if after["free"] != before["free"]:
            counts["cheat"] += 1
        if (after["money1"], after["money2"]) != (before["money1"],
                                                  before["money2"]):
            counts["income"] += 1
        if after["boost"] and after["timer"]:
            counts["boost_on"] += 1
        elif after["timer"]:
            counts["boost_off"] += 1
        if after["sfx"] == SFX_READY:
            counts["ready"] += 1
        prev_end = after
        if len(errors) > 20:
            break
    return errors, passes, counts


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    rom = open(argv[1], "rb").read()
    paths = []
    for a in argv[2:]:
        if os.path.isdir(a):
            paths += sorted(os.path.join(a, n) for n in os.listdir(a)
                            if n.endswith(".txt"))
        else:
            paths.append(a)
    if not paths:
        print(__doc__)
        return 2
    hud = Hud(rom)
    bad = 0
    for path in paths:
        errors, passes, c = check_file(path, hud, rom)
        if errors:
            bad += 1
            print("%-26s %d errors" % (os.path.basename(path), len(errors)))
            for e in errors[:10]:
                print("   ", e)
        else:
            print("%-26s ok: %d passes, %d pauses, %d cheat toggles, "
                  "%d paydays, boost on %d / off %d, %d ready, %d bars, "
                  "%d animated"
                  % (os.path.basename(path), passes, c["pauses"], c["cheat"],
                     c["income"], c["boost_on"], c["boost_off"], c["ready"],
                     c["bars"], c["anim"]))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
