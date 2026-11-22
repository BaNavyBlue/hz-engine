"""Session 25's mutations of the Z80 driver's C port (src/audio/hz_snd.c),
run with CHECK 'sound' (tools/hzsound_check.py against the original's
cached logs). See HANDOVER.md for the ones expected not to be caught."""

CHECK = 'sound'

S = 'src/audio/hz_snd.c'
MUTATIONS = [
    # the machine: timers, frames, the mailbox
    ('timer A period', S, 'return (u64)(1024 - s->ta) * SND_YM_SAMPLE;',
     'return (u64)(1023 - s->ta) * SND_YM_SAMPLE;'),
    ('timer B period x16', S, 'return (u64)(256 - s->tb) * 16 * SND_YM_SAMPLE;',
     'return (u64)(256 - s->tb) * 15 * SND_YM_SAMPLE;'),
    ('$25 low bits', S, 's->ta = (u16)((s->ta & ~3u) | (v & 3));',
     's->ta = (u16)((s->ta & ~3u) | (v & 1));'),
    ('timer restarts on any load', S,
     'if ((v & 1) && !(s->timer_ctrl & 1)) {', 'if (v & 1) {'),
    ('flag A without enable', S, '''        if (s->timer_ctrl & 4) {
            s->timer_flags |= 1;''', '''        if (s->timer_ctrl & 1) {
            s->timer_flags |= 1;'''),
    ('reset B clears A', S, '''        if (v & 0x20) {
            s->timer_flags &= (u8)~2u;''', '''        if (v & 0x20) {
            s->timer_flags &= (u8)~3u;'''),
    ('Timer B before A', S, '''    u8 status = s->timer_flags;
    if (status & 1) {
        timer_a_tick(s);
    }
    if (status & 2) {
        timer_b_tick(s);
    }''', '''    u8 status = s->timer_flags;
    if (status & 2) {
        timer_b_tick(s);
    }
    if (status & 1) {
        timer_a_tick(s);
    }'''),
    ('VBlank while paused', S, '''            if (!s->paused) {
                vblank_interrupt(s);''', '''            if (true) {
                vblank_interrupt(s);'''),
    ('timers served while paused', S, '''    if (s->paused) {
        return;
    }
    u8 status''', '''    u8 status'''),
    ('paused drops the resume', S, '''        if (s->command == 0x0C) {
            cmd_pause_end(s);''', '''        if (s->command == 0x0D) {
            cmd_pause_end(s);'''),
    ('frame period', S, 's->next_frame += SND_FRAME;',
     's->next_frame += SND_FRAME + 1008;'),
    # chip writes
    ('part II from channel 4', S, '''    if (s->channel >= 3) {
        ym_write1(s, reg, v);''', '''    if (s->channel >= 4) {
        ym_write1(s, reg, v);'''),
    ('muted is $FE', S, 'return b == 0xFF;\n}\n\nstatic void ym_write0(',
     'return b == 0xFE;\n}\n\nstatic void ym_write0('),
    ('volume writes always checked', S, '''    if (s->volume_unchecked) {
        ym_write_channel(s, reg, v);''', '''    if (false) {
        ym_write_channel(s, reg, v);'''),
    ('PSG volume always checked', S, '''    if (s->volume_unchecked) {
        psg_write(s, v);''', '''    if (false) {
        psg_write(s, v);'''),
    ('channel code', S, 'return (u8)(ch + (ch >= 3 ? 1 : 0));',
     'return (u8)(ch + (ch > 3 ? 1 : 0));'),
    # volumes
    ('Volume_Add overflow', S, '''    if ((~(a ^ b) & (a ^ r) & 0x80) != 0) {
        return 0x7F;''', '''    if ((~(a ^ b) & (a ^ r) & 0x80) != 0) {
        return 0x7E;'''),
    ('Volume_Add negative', S, '''    if (r & 0x80) {
        return 0x7F;
    }
    return r;
}

/* Note_Add''', '''    return r;
}

/* Note_Add'''),
    ('Note_Add carry', S, 'return (u8)(a + 0x60);', 'return (u8)(a + 0x5F);'),
    ('Note_Add wrap', S, '''    if (a < 0x60) {
        return a;''', '''    if (a <= 0x60) {
        return a;'''),
    ('Volume_Combine wrap', S, '''    if (sum > 0xFF) {
        r = 0;''', '''    if (sum > 0xFF) {
        r = 0x63;'''),
    ('Volume_Combine cap', S, '''        if (r >= 0x64) {
            r = 0x63;
        }
    }''', '''        if (r >= 0x63) {
            r = 0x62;
        }
    }'''),
    ('Volume_Clamp sign', S, '''    if (r & 0x80) {
        return 0;
    }
    if (r >= 0x64) {''', '''    if (r & 0x40) {
        return 0;
    }
    if (r >= 0x64) {'''),
    ('volume 0 attenuation', S, '''    if (a == 0) {
        b = 0x7F;''', '''    if (a == 0) {
        b = 0x7E;'''),
    ('track fade not added', S, 'a = volume_add(a, t->fade_level);', 'a = a;'),
    ('global fade on sfx', S, '''        if (s->sfx_pass == 0) {
            a = volume_add(s->fade_level, b);''', '''        if (true) {
            a = volume_add(s->fade_level, b);'''),
    ('TL order', S, 'static const u8 steps[4] = { 0x00, 0x08, 0xFC, 0x08 };',
     'static const u8 steps[4] = { 0x00, 0x04, 0x04, 0x04 };'),
    ('carriers shifted the other way', S, '''            if (c & 1) {
                v = volume_add(v, b);
            }
            c >>= 1;''', '''            if (c & 8) {
                v = volume_add(v, b);
            }
            c <<= 1;'''),
    ('TL instrument offset', S,
     'u16 de = (u16)(instrument_find(s, t->instrument) + 6);',
     'u16 de = (u16)(instrument_find(s, t->instrument) + 7);'),
    ('noise to tone 3 bit', S, '            if (s->music_noise & 0x08) {',
     '            if (s->music_noise & 0x04) {'),
    ('tone volume latch', S, 'l = (u8)(((ch - 6) * 2 + 9) << 4);',
     'l = (u8)(((ch - 6) * 2 + 8) << 4);'),
    ('noise track to tone 3 latch', S, '''        if (t->noise == 0) {
            l = 0xD0;''', '''        if (t->noise == 0) {
            l = 0xF0;'''),
    ('PSG volume unchanged skip', S, '''    if (a == t->psg_volume) {
        return;
    }
    t->psg_volume = a;
    volume_write_psg(s, t, a | l);''', '''    t->psg_volume = a;
    volume_write_psg(s, t, a | l);'''),
    ('PSG volume shift', S, '''    b >>= 2;                                             /* .psg_volume */''',
     '''    b >>= 3;                                             /* .psg_volume */'''),
    # key on / off, pan, LFO
    ('note off release bend', S,
     'u16 hl = (u16)((u8)(0x64 - (t->slide_speed & 0x7F)) << 2);',
     'u16 hl = (u16)((u8)(0x64 - (t->slide_speed & 0x7F)) << 3);'),
    ('bend down target', S, '''            hl = (u16)(~hl + 1);
            de = 0x0000;''', '''            hl = (u16)(~hl + 1);
            de = 0x0100;'''),
    ('PSG release asked', S, '''    if (t->env_state & 0x44) {
        return;
    }
    t->env_state = 0x40;''', '''    if (t->env_state & 0x40) {
        return;
    }
    t->env_state = 0x40;'''),
    ('key on LFO restart', S, '''        if (s->lfo_sweep & 0x80) {
            s->lfo_spare = 0;''', '''        if (s->lfo_sweep & 0x40) {
            s->lfo_spare = 0;'''),
    ('key on LFO delay out', S, '''            fms = 0;
            ams = 0;
        }''', '''            fms = 0;
        }'''),
    ('key on sweep rate', S,
     'a = (u8)((u8)(0 - s->lfo_sweep) & 0x7F);', 'a = (u8)(s->lfo_sweep & 0x7F);'),
    ('key on code', S,
     'ym_write0_checked(s, t->priority, 0x28, (u8)(0xF0 + ch_code(c)));',
     'ym_write0_checked(s, t->priority, 0x28, (u8)(0xF0 + c));'),
    ('PSG key on', S, '''    t->env_state = 0x80;
}''', '''    t->env_state = 0x20;
}'''),
    ('FM_KeyOnOff ops', S,
     'ym_write0(s, 0x28, (u8)(s->key_ops - 3 + ch_code(ch)));',
     'ym_write0(s, 0x28, (u8)(s->key_ops - 3 + ch));'),
    ('pan with flags 0', S, '''    if (t->flags == 0) {
        v &= 0xC0;''', '''    if ((t->flags & 1) == 0) {
        v &= 0xC0;'''),
    ('LFO register $40', S, 's->lfo_reg = (u8)(t->lfo_freq | 0x40);',
     's->lfo_reg = (u8)(t->lfo_freq | 0x80);'),
    ('noise mode kept', S, 's->music_noise = (u8)(c | 0x80);',
     's->music_noise = (u8)(c | 0x88);'),
    ('noise register', S, 'psg_write_checked(s, mute, (u8)((c & 7) | 0xE0));',
     'psg_write_checked(s, mute, (u8)((c & 3) | 0xE0));'),
    # instruments
    ('operator order', S,
     'static const u8 steps[4] = { 0x04, 0x08, 0xFC, 0x08 };',
     'static const u8 steps[4] = { 0x04, 0x04, 0x04, 0x04 };'),
    ('LFO sweep byte', S, 't->lfo_sweep = rd(s, (u16)(de + 4));',
     't->lfo_sweep = rd(s, (u16)(de + 3));'),
    ('FMS bits', S, 't->fms = (u8)((l & 0x70) >> 4);',
     't->fms = (u8)((l & 0x30) >> 4);'),
    ('TLs skipped', S, '''    h = (u8)(h + 0x10);
    de = (u16)(de + 4);''', '''    h = (u8)(h + 0x10);
    de = (u16)(de + 3);'''),
    ('LFO bytes offset', S, '''    de = (u16)(de + 5);
    track_lfo_bytes(s, t, de);''', '''    de = (u16)(de + 4);
    track_lfo_bytes(s, t, de);'''),
    ('PSG instrument size', S, 'u16 off = (u16)(c * 16);',
     'u16 off = (u16)(c * 15);'),
    ('PSG instruments table', S,
     'base = (u16)(s->song + rd16(s, (u16)(s->song + 0x0C)));',
     'base = (u16)(s->song + rd16(s, (u16)(s->song + 0x0A)));'),
    ('FM instrument size', S, 'u16 off = (u16)(index * 36);',
     'u16 off = (u16)(index * 35);'),
    ('init carriers $0F', S, '''    t->carriers = 0x0F;
    track_note_off(s, t);''', '''    t->carriers = 0x08;
    track_note_off(s, t);'''),
    ('set instrument level 0', S, '''    track_set_level(s, t, 0);
    fm_instrument_rest(s, t, de);''', '''    fm_instrument_rest(s, t, de);'''),
    ('algorithm mask', S, '''    t->algorithm = (u8)(rd(s, de) & 0x3F);
    de++;
    t->carriers = SND_CARRIER_OPS[t->algorithm & 7];
    track_set_level''', '''    t->algorithm = (u8)(rd(s, de) & 0x3F);
    de++;
    t->carriers = SND_CARRIER_OPS[t->algorithm & 3];
    track_set_level'''),
    # pitch
    ('detune up step', S, '''            bool borrow = a < 0x20;''', '''            bool borrow = a < 0x1F;'''),
    ('detune down', S, '''            d--;
            carry = (unsigned)a + 0x20 > 0xFF;''', '''            carry = (unsigned)a + 0x20 > 0xFF;'''),
    ('slide when tied', S, '''    } else if ((u8)(0 - t->gate_count) & 0x80) {
        slide = true;''', '''    } else if ((u8)(0 - t->gate_count) & 0x40) {
        slide = true;'''),
    ('slide direction', S, '''        if (target >= current) {
            a = (u8)(0 - a);''', '''        if (target > current) {
            a = (u8)(0 - a);'''),
    ('slide step scale', S, 't->slide_step = (u16)((s16)(s8)a * 32);',
     't->slide_step = (u16)((s16)(s8)a * 16);'),
    ('bend keeps the step', S, '''        /* .jump: the step is left as it is */
        t->pitch_frac''', '''        t->slide_step = 0;
        t->pitch_frac'''),
    ('FM block', S, 'u8 d = (u8)((de >> 8) | (u8)(octave << 3));',
     'u8 d = (u8)((de >> 8) | (u8)(octave << 2));'),
    ('FM fine', S, '''                   + SND_FM_FINE_TABLE[semi][t->pitch_frac & 0x1F]);''',
     '''                   + SND_FM_FINE_TABLE[semi][t->pitch_frac & 0x0F]);'''),
    ('PSG octave floor', S, 'u8 c = octave < 2 ? 2 : octave;',
     'u8 c = octave < 1 ? 1 : octave;'),
    ('PSG fine subtracted', S,
     'de = (u16)(de - SND_PSG_FINE_TABLE[semi][t->pitch_frac & 0x1F]);',
     'de = (u16)(de + SND_PSG_FINE_TABLE[semi][t->pitch_frac & 0x1F]);'),
    ('PSG data byte', S, 'u8 h = (u8)((de >> 4) & 0x3F);',
     'u8 h = (u8)((de >> 4) & 0x1F);'),
    ('PSG latch', S, 's->psg_latch = (u8)(((ch - 6) * 2 + 8) << 4);',
     's->psg_latch = (u8)(((ch - 6) * 2 + 9) << 4);'),
    ('slide end test', S, 'if ((((diff >> 8) ^ (de >> 8)) & 0x80) == 0) {',
     'if ((((diff >> 8) ^ (de >> 8)) & 0x80) != 0) {'),
    ('slide frac scale', S,
     'u16 hl = (u16)(t->pitch_note << 8 | (u8)(t->pitch_frac << 3));',
     'u16 hl = (u16)(t->pitch_note << 8 | (u8)(t->pitch_frac << 2));'),
    ('LFO sweep delay', S, '''    if (s->lfo_delay != 0) {
        s->lfo_delay--;
        return;
    }''', '''    if (s->lfo_delay > 1) {
        s->lfo_delay--;
        return;
    }'''),
    ('LFO sweep both', S, '''    if (l == 2) {
        s->lfo_sweep_rate = 0;''', '''    if (l == 1) {
        s->lfo_sweep_rate = 0;'''),
    ('volume fade alternation', S, '''    if (a & 0x80) {
        a = (u8)(a + 2);
    }
    t->fade_level = a;''', '''    if (a & 0x80) {
        a = 0;
    }
    t->fade_level = a;'''),
    # the sequencer
    ('init length 1', S, '''    t->length_count = 1;
    t->lfo_delay = 1;''', '''    t->length_count = 2;
    t->lfo_delay = 1;'''),
    ('init LFO delay 1', S, '''    t->length_count = 1;
    t->lfo_delay = 1;''', '''    t->length_count = 1;
    t->lfo_delay = 0;'''),
    ('init pan', S, 't->pan = 0xC0;\n    track_init_instrument',
     't->pan = 0x80;\n    track_init_instrument'),
    ('repeat back past $EC n', S, 't->phrase = (u16)((d << 8 | e) + 2);',
     't->phrase = (u16)((d << 8 | e) + 1);'),
    ('repeat count', S, '''    t->repeat_stack[sp - 3]--;
    if (t->repeat_stack[sp - 3] == 0) {''', '''    if (t->repeat_stack[sp - 3] == 1) {'''),
    ('4th repeat level', S, '''        if (t->repeats_open == 3) {
            break;
        }''', '''        if (t->repeats_open == 4) {
            break;
        }'''),
    ('phrase return byte', S, 'return rd(s, (u16)(s->song + 8));',
     'return 0;'),
    ('loop point', S, 't->loop = (u16)(t->order + 1);',
     't->loop = t->order;'),
    ('track end bit', S, '''            music_clear_channel_bit(s);
            track_note_off(s, t);
            return 0xFF;''', '''            track_note_off(s, t);
            return 0xFF;'''),
    ('sfx phrase base', S,
     't->phrase = (u16)(s->sfx_table + (u16)(d << 8 | e));',
     't->phrase = (u16)(s->sfx_table + (u16)(e << 8 | d));'),
    ('note params stop', S, '''            u8 v = rd(s, hl);
            if (!(v & 0x80)) {
                hl++;
                t->note_length = v;''', '''            u8 v = rd(s, hl);
            if (!(v & 0xC0)) {
                hl++;
                t->note_length = v;'''),
    ('velocity not kept', S, '''                        t->note_velocity = v;
                        n.velocity = v;''', '''                        n.velocity = v;'''),
    ('phrase end clears loops', S, '''        } else if (a == 0xFF) {
            track_clear_loops(t);
            return n;''', '''        } else if (a == 0xFF) {
            return n;'''),
    ('envelope start drop', S,
     't->env_start = a >= t->env_start_drop ? (u8)(a - t->env_start_drop) : 0;',
     't->env_start = a > t->env_start_drop ? (u8)(a - t->env_start_drop) : 0;'),
    ('sfx velocity base', S, '''    if (s->sfx_pass != 0) {
        a = 0x63;''', '''    if (s->sfx_pass != 0) {
        a = 0x60;'''),
    ('channel volume index', S,
     'u16 hl = (u16)(s->song + rd16(s, (u16)(s->song + 4)) + s->channel);',
     'u16 hl = (u16)(s->song + rd16(s, (u16)(s->song + 4)));'),
    ('tie test', S, '''    } else if (!((u8)(0 - t->gate_count) & 0x80)) {
        track_key_on(s, t);''', '''    } else {
        track_key_on(s, t);'''),
    ('wait length', S, 't->length_count = (u8)(c + 1);',
     't->length_count = c;'),
    ('wait release seek', S, '''            if (rd(s, hl) >= 0xFE) {
                break;''', '''            if (rd(s, hl) >= 0xFF) {
                break;'''),
    ('wait release clears', S, '''        m->phrase = hl;                                  /* .seek_done */
        m->wait = 0;''', '''        m->phrase = hl;                                  /* .seek_done */'''),
    ('LFO on lower only', S, '''    if (s->lfo_channel <= c) {
        return;
    }''', '''    if (s->lfo_channel < c) {
        return;
    }'''),
    ('LFO off pan', S, '''    s->lfo_channel = 0xFF;
    fm_write_pan(s, t);
}''', '''    s->lfo_channel = 0xFF;
}'''),
    ('pan right', S, 'e = (c & 0x80) ? 0x40 : 0x80;',
     'e = (c & 0x80) ? 0x80 : 0x40;'),
    ('transpose wrap', S, '''            if (v & 0x80) {
                v = (u8)(v + 0x60);''', '''            if (v & 0x80) {
                v = (u8)(v + 0x5F);'''),
    ('volume flag fade', S,
     't->fade_level = c == 0 ? 0 : (u8)(t->fade_level + c);',
     't->fade_level = c == 0 ? 0 : t->fade_level;'),
    ('tempo flag', S,
     'ym_set_tempo(s, (u8)(s->song_tempo + s->tempo_offset));',
     'ym_set_tempo(s, s->song_tempo);'),
    ('slide always', S, '''        t->slide_speed &= 0x7F;
        t->flags &= 0x7F;''', '''        t->slide_speed &= 0x7F;'''),
    ('bend down flag', S, '''        t->flags |= 0xC0;''', '''        t->flags |= 0x80;'''),
    ('LFO flags music only', S, '''        if (s->sfx_pass == 0) {
            track_lfo_on(s, t);''', '''        if (true) {
            track_lfo_on(s, t);'''),
    ('$EE skip', S, '''    if (v < 0xF0) {
        *hl = (u16)(p + 2);''', '''    if (v < 0xF0) {
        *hl = (u16)(p + 1);'''),
    ('$F8-$FA skip', S, '''    if (v < 0xFB) {
        *hl = (u16)(p + 1);''', '''    if (v < 0xFA) {
        *hl = (u16)(p + 1);'''),
    ('fading volume between notes', S, '''    u8 c = t->volume;
    if (s->channel >= 6) {
        c = t->env_level;
    }''', '''    u8 c = t->volume;'''),
    ('gate counted', S, '''    t->gate_count--;
    if (t->gate_count == 0) {''', '''    if (t->gate_count == 1) {'''),
    ('music LFO sweep', S, '''    if (s->lfo_channel == s->channel) {
        lfo_sweep(s);
    }''', ''),
    ('restart from loop point', S, 't->order = t->loop;', 't->order = (u16)(t->loop + 1);'),
    ('sfx end gives channel back', S, '''        s->sfx_pass = 0;
        m->priority = 0;
        s->lfo_channel = (u8)(s->lfo_saved & 0xFF);''', '''        s->sfx_pass = 0;
        s->lfo_channel = (u8)(s->lfo_saved & 0xFF);'''),
    ('sfx end instrument', S, '''        track_set_instrument(s, m, m->instrument);
        track_write_frequency(s, m);
        if (m->noise != 0) {
            m->noise = 0xFF;                             /* Track_UseNoise */
        }
        if (m->flags & 1) {
            track_lfo_on(s, m);
        }
    }
    s->lfo_channel = 0xFF;''', '''        track_write_frequency(s, m);
        if (m->noise != 0) {
            m->noise = 0xFF;                             /* Track_UseNoise */
        }
        if (m->flags & 1) {
            track_lfo_on(s, m);
        }
    }
    s->lfo_channel = 0xFF;'''),
    ('sfx end bit', S, 's->sfx_on &= rotl8(0x7F, (u8)(ch + 1));',
     's->sfx_on &= rotl8(0x7F, ch);'),
    # envelopes
    ('envelope key on decay', S, 't->env_state = v == t->env_peak ? 0x10 : 0x20;',
     't->env_state = 0x20;'),
    ('envelope attack cap', S, '''        if (v >= t->env_peak) {
            t->env_state = 0x10;''', '''        if (v > t->env_peak) {
            t->env_state = 0x10;'''),
    ('envelope decay floor', S, '            if (v <= t->env_floor) {',
     '            if (v < t->env_floor) {'),
    ('envelope release end', S, '''        if (v == 0) {
            t->env_state = 0;
        } else {''', '''        if (v == 0) {
        } else {'''),
    ('envelope reload', S, '''    t->env_count = (u8)(0x64 - t->env_speed);            /* .reload */''',
     '''    t->env_count = (u8)(0x63 - t->env_speed);            /* .reload */'''),
    ('envelope channel restored', S, '''    t->env_count = (u8)(0x64 - t->env_speed);            /* .reload */
    s->channel = saved;''', '''    t->env_count = (u8)(0x64 - t->env_speed);            /* .reload */'''),
    ('LFO update channel quirk', S, '''    u8 saved = s->channel;                         /* LFO_UpdateFMTracks */
    for (int k = 0; k < 6; k++) {
        fm_update_lfo(s, &s->music[k]);''', '''    u8 saved = s->channel;                         /* LFO_UpdateFMTracks */
    for (int k = 0; k < 6; k++) {
        s->channel = (u8)k;
        fm_update_lfo(s, &s->music[k]);'''),
    ('LFO AMS nibble', S, 'b = (u8)(b << 4 | b >> 4);', 'b = (u8)(b << 3);'),
    ('LFO register off write', S,
     'ym_write0_checked(s, s->psg_latch, 0x22, (u8)(a & 7));',
     'ym_write0_checked(s, s->psg_latch, 0x22, (u8)(a & 0xF));'),
    ('LFO register on bit', S,
     'ym_write0_checked(s, s->psg_latch, 0x22, (u8)((a & 7) | 8));',
     'ym_write0_checked(s, s->psg_latch, 0x22, (u8)(a & 7));'),
    # the handlers
    ('Timer A reset value', S, 'ym_write0(s, 0x27, 0x1F);', 'ym_write0(s, 0x27, 0x15);'),
    ('Timer A music LFO', S, '''    if (s->music_on != 0) {
        lfo_update_register(s);
    }''', '''    lfo_update_register(s);'''),
    ('sfx envelope pass', S, '''    s->sfx_pass = 0xFF;
    for (int k = 0; k < 3; k++) {''', '''    for (int k = 0; k < 3; k++) {'''),
    ('noise track envelope', S, 'for (int k = 0; k < 3; k++) {\n        psg_envelope_step(s, &s->sfx[6 + k]',
     'for (int k = 0; k < 2; k++) {\n        psg_envelope_step(s, &s->sfx[6 + k]'),
    ('music tick snapshot', S, '''        for (int k = 0; k < SND_TRACK_COUNT; k++) {
            if (l & 1) {
                music_track_tick''', '''        for (int k = 0; k < SND_TRACK_COUNT; k++) {
            l = (u8)(s->music_on >> k);
            if (l & 1) {
                music_track_tick'''),
    ('VBlank LFO hidden', S, '''        s->lfo_channel = 0xFF;
        s->lfo_channel_hi = 0xFF;
        for (int k = 0; k < SND_SFX_COUNT; k++) {''', '''        for (int k = 0; k < SND_SFX_COUNT; k++) {'''),
    ('VBlank noise restore', S, '''        if ((s->sfx_on | s->sfx8_on) == 0 && (s->music_noise & 0x80)) {''',
     '''        if ((s->music_noise & 0x80)) {'''),
    ('fade step', S, '''    s->fade_count = s->fade_speed;
    s->fade_level++;''', '''    s->fade_level++;'''),
    ('fade end', S, '''    if (s->fade_level == 0x7F) {
        s->music_on = 0;''', '''    if (s->fade_level == 0x7E) {
        s->music_on = 0;'''),
    ('tempo divide', S, 'u16 de = (u16)(a + 0x32);', 'u16 de = (u16)(a + 0x31);'),
    ('halt music bits drift', S, '''        if (cy) {
            track_halt_or_resume(s, &s->sfx[k], 0xFF);
        } else {
            cy = mbits & 1;                              /* .fm_music */
            mbits >>= 1;''', '''        cy = cy;
        {
            bool sfx = cy;
            cy = mbits & 1;                              /* .fm_music */
            mbits >>= 1;
            if (sfx) {
                track_halt_or_resume(s, &s->sfx[k], 0xFF);
                continue;
            }'''),
    ('halt keyed only', S, '''    if (!(t->carriers & 0x80)) {                         /* .resume */
        return;
    }''', ''),
    ('PSG releasing halted', S, '''        } else if (s->music[k].env_state != 0) {   /* .psg_music_release */
            track_halt_or_resume(s, &s->music[k], 0x00);
        }''', '''        }'''),
    ('noise track halted', S, '''    if (s->sfx8_on != 0 || s->sfx[8].env_state != 0) {''',
     '''    if (s->sfx8_on != 0) {'''),
    ('song header tempo', S, 's->song_tempo = rd(s, (u16)(s->song + 2));',
     's->song_tempo = rd(s, (u16)(s->song + 1));'),
    ('song LFO owner', S, '''    s->lfo_channel = 0xFF;
    s->lfo_channel_hi = 0xFF;
}

/* Song_StartTracks''', '''    s->lfo_channel_hi = 0xFF;
}

/* Song_StartTracks'''),
    ('empty track phrase 0', S, '''        if (track_next_phrase(s, t) == 0xFF) {
            t->phrase = 0;
        }''', '''        track_next_phrase(s, t);'''),
    ('play song check 9', S, '''    if (rd(s, de) != 0x09) {
        cmd_ack(s);''', '''    if (rd(s, de) == 0x08) {
        cmd_ack(s);'''),
    ('play song big-endian', S,
     'u16 de = (u16)((rd(s, p) << 8 | rd(s, (u16)(p + 1))) | 0x8000);',
     'u16 de = (u16)((rd(s, (u16)(p + 1)) << 8 | rd(s, p)) | 0x8000);'),
    ('sfx priority +1', S, '''    b++;
    if (b == 0) {''', '''    if (b == 0xFF) {'''),
    ('sfx count test', S, 'if (c >= rd(s, (u16)(table + 1))) {',
     'if (c > rd(s, (u16)(table + 1))) {'),
    ('sfx takes equal', S, 'if (x->priority == 0 || x->priority >= b) {',
     'if (x->priority == 0 || x->priority > b) {'),
    ('sfx mutes music', S, '''                if (ch != 8) {
                    m->priority = 0xFF;
                }''', ''),
    ('sfx skip length', S, '''                    if (a < 0xE0) {
                        hl++;
                        break;''', '''                    if (a < 0xE0) {
                        break;'''),
    ('sfx restore at count 0', S, '''            if (count == 0) {                            /* .restore */
                s->lfo_channel = (u8)(s->lfo_saved & 0xFF);''', '''            if (count == 0) {                            /* .restore */
                s->lfo_channel = 0xFF;'''),
    ('stop sfx music back', S, '''            if ((l & 1) && k < SND_TRACK_COUNT) {''', '''            if (k < SND_TRACK_COUNT) {'''),
    ('stop sfx bits', S, 'c = (u8)((c >> 1) | (b & 1) << 7);', 'c = (u8)(c >> 1);'),
    ('stop sfx noise check byte', S,
     'psg_set_noise(s, s->sfx[1].priority, s->music_noise);',
     'psg_set_noise(s, 0, s->music_noise);'),
    ('fade 0 PSG levels', S, '''        if (b < 3) {
            t->env_level = 0;
        }''', ''),
    ('pause unchecked', S, '''    s->volume_unchecked = true;
    tracks_halt_or_resume(s, 0x00, 0x03);''', '''    tracks_halt_or_resume(s, 0x00, 0x03);'''),
    ('pause key byte', S, 'tracks_halt_or_resume(s, 0xFF, 0xF3);',
     'tracks_halt_or_resume(s, 0xFF, 0xF0);'),
    ('set song word', S, 's->song = (u16)(s->arg[0] << 8 | s->arg[1]);',
     's->song = (u16)(s->arg[1] << 8 | s->arg[0]);'),
    ('Init timer A', S, 'ym_write0(s, 0x24, 0x52);', 'ym_write0(s, 0x24, 0x53);'),
    ('PSG silence', S, '0x80, 0x00, 0xA0, 0x00, 0xC0, 0x00, 0xE0, 0x9F, 0xBF, 0xDF, 0xFF',
     '0x80, 0x00, 0xA0, 0x00, 0xC0, 0x00, 0xE0, 0x9F, 0xBF, 0xDF'),
    ('mirrored RAM', S, 'return z80_ram(s, (u16)(addr & 0x1FFF));',
     'return z80_ram(s, addr);'),
]
