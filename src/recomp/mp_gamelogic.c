/*
 * Mario Paint — Recompiled game logic dispatch and cursor rendering.
 *
 * Key routines:
 *   $008683 — Game logic dispatch (toolbar show/hide timer)
 *   $0087A8 — Post-logic dispatch (jump table on game state $09D6)
 *   $00878F — Animation frame check
 *   $009378 — Cursor sprite rendering (reads sprite frames from ROM)
 *   $01E8F6 — Empty tile row optimization check
 *   $01E393 — Indexed subroutine dispatch helper
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <mp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

/* OAM buffer addresses */
#define OAM_BUF       0x0226
#define OAM_HI_BUF    0x0426

/* ========================================================================
 * $01:E8F6 — Empty tile row optimization check
 *
 * Checks if the current palette/tool state means we can skip certain
 * rendering. Sets $0567 based on empty tile row flags in $0569.
 * ======================================================================== */
void mp_01E8F6(void) {
    bus_wram_write16(0x0567, 0x0000);

    uint16_t eb8 = bus_wram_read16(0x0EB8);
    if (eb8 != 0) {
        uint16_t d0 = bus_wram_read16(0x04D0);
        if (d0 == 0x0007) goto done;

        uint16_t ebe = bus_wram_read16(0x0EBE);
        if (ebe < 0x00F0) goto done;

        uint16_t idx = ebe & 0x000F;
        idx *= 2;
        bus_wram_write16(0x0567, bus_wram_read16(0x0569 + idx));
        return;
    }

    /* No $0EB8 active */
    uint16_t palette = bus_wram_read16(0x00A6);  /* CurrentPaletteRow */
    if (palette != 0x00F0) goto done;

    uint16_t d0 = bus_wram_read16(0x04D0) & 0x00FF;
    if (d0 == 0x0008) goto use_d0;
    if (d0 >= 0x0006) goto done;

use_d0:
    {
        uint16_t hi = (bus_wram_read16(0x04D0) >> 8) & 0xFF;
        uint16_t idx = hi * 2;
        bus_wram_write16(0x0567, bus_wram_read16(0x0569 + idx));
    }
    return;

done:
    return;
}

/* ========================================================================
 * $01:E393 — Indexed subroutine dispatch helper
 *
 * Takes A (8-bit index), reads a 16-bit pointer from the address
 * on stack + index*2+1, stores it. Used for indexed jump tables.
 *
 * This is a complex stack-manipulation routine. In practice, it's
 * used for animation frame dispatch. We implement it as a helper
 * that reads from ROM.
 * ======================================================================== */
void mp_01E393(void) {
    /* This routine is called with A = index and a pointer on the stack.
     * In practice, the callers that use this are complex animation
     * dispatchers. For now, register it so func_table_call doesn't no-op,
     * but the actual behavior depends on calling context. */
}

/* ========================================================================
 * $00:8683 — Game logic dispatch (toolbar visibility timer)
 *
 * Manages the toolbar show/hide state based on cursor activity.
 * When the cursor is active ($04CA buttons or $1B26 movement),
 * resets the idle timer ($09A1). When idle, runs toolbar
 * show/hide animations via the sprite engine.
 *
 * Also handles the special cursor mode ($05A7 "cursor tool")
 * which shows/hides a toolbar sprite.
 * ======================================================================== */
void mp_008683(void) {
    /* Check if mouse is active (buttons or movement) */
    uint16_t buttons = bus_wram_read16(0x04CA);
    if (buttons != 0) goto active;

    uint16_t movement = bus_wram_read16(0x1B26);
    if (!(movement & 0x0001)) goto inactive;

active:
    /* Check cursor tool mode */
    {
        uint8_t cursor_tool = bus_wram_read8(0x05A7);
        if (cursor_tool == 0) goto reset_timer;

        /* Cursor tool active: show toolbar sprite */
        bus_wram_write16(0x04CA, 0x0000);  /* Clear button state */
        op_sep(0x20);
        bus_wram_write8(0x05A6, 0x01);
        op_rep(0x20);

        /* JSL CODE_01962C — animate toolbar sprite (#$27) */
        op_lda_imm16(0x0027);
        func_table_call(0x01962C);

        /* JSL CODE_01F91E — draw toolbar sprite at (D8, CA) */
        g_cpu.X = 0x00D8;
        g_cpu.Y = 0x00CA;
        op_lda_imm16(0x0195);
        func_table_call(0x01F91E);
    }

reset_timer:
    /* Reset idle timer to $0258 frames (~10 sec at 60fps) */
    bus_wram_write16(0x09A1, 0x0258);
    return;

inactive:
    /* Check if cursor tool sprite needs cleanup */
    {
        uint8_t flag = bus_wram_read8(0x05A6);
        if (flag != 0) goto active;  /* Still showing, run show logic */
    }

    /* Check current tool mode for toolbar behavior */
    {
        uint16_t d0 = bus_wram_read16(0x04D0);
        if (d0 == 0x0006 || d0 == 0x0007) goto done;

        uint16_t eb8 = bus_wram_read16(0x0EB8);
        if (eb8 == 0x0001) goto done;

        /* Look up toolbar behavior from DATA_00877F */
        uint8_t tool = bus_wram_read8(0x00AA);  /* Current tool index */
        /* Read byte from ROM data table at $00:877F + tool */
        uint8_t behavior = bus_read8(0x00, 0x877F + tool);
        bus_wram_write16(0x09A5, behavior & 0xFF);

        if (behavior == 0xFF) goto done;
    }

    /* Idle timer logic */
    {
        uint16_t timer = bus_wram_read16(0x09A1);
        if (timer == 0) goto timer_expired;
        if ((int16_t)timer < 0) goto timer_negative;
        bus_wram_write16(0x09A1, timer - 1);
        if (timer - 1 != 0) goto done;
    }

timer_expired:
    /* Timer just hit zero — start toolbar hide animation */
    {
        /* JSL CODE_01CDE1 with pointer to DATA_01CFB4 */
        op_lda_imm16(0xCFB4);  /* DATA_01CFB4 */
        func_table_call(0x01CDE1);

        uint16_t a5 = bus_wram_read16(0x09A5);
        if (a5 == 0x0002) goto show_toolbar_sprite;

        /* Check palette row for tool color bar */
        uint16_t palette = bus_wram_read16(0x00A6);
        uint16_t eb8 = bus_wram_read16(0x0EB8);
        if ((int16_t)eb8 < 0) {
            palette = bus_wram_read16(0x0EBE);
        }
        if (palette >= 0x00E0) goto skip_color_bar;

        /* Show color bar sprite (#$26) */
        op_lda_imm16(0x0026);
        func_table_call(0x019DFE);

skip_color_bar:
        a5 = bus_wram_read16(0x09A5);
        if (a5 == 0) goto dec_timer;
    }

show_toolbar_sprite:
    /* Show toolbar sprite (#$27) */
    op_lda_imm16(0x0027);
    func_table_call(0x019DFE);

dec_timer:
    bus_wram_write16(0x09A1, bus_wram_read16(0x09A1) - 1);

timer_negative:
    /* Timer is negative — animate toolbar sprites */
    {
        uint16_t a5 = bus_wram_read16(0x09A5);
        if (a5 == 0x0002) goto animate_toolbar;

        uint16_t palette = bus_wram_read16(0x00A6);
        uint16_t eb8 = bus_wram_read16(0x0EB8);
        if ((int16_t)eb8 < 0) {
            palette = bus_wram_read16(0x0EBE);
        }
        if (palette >= 0x00E0) goto check_toolbar;

        /* Animate color bar sprite */
        op_lda_imm16(0x0026);
        func_table_call(0x01962C);
        g_cpu.X = 0x00EC;
        g_cpu.Y = 0x0007;
        op_lda_imm16(0x0194);
        func_table_call(0x01F91E);

check_toolbar:
        a5 = bus_wram_read16(0x09A5);
        if (a5 == 0) goto done;
    }

animate_toolbar:
    /* Animate toolbar sprite */
    op_lda_imm16(0x0027);
    func_table_call(0x01962C);
    g_cpu.X = 0x00D8;
    g_cpu.Y = 0x00CA;
    op_lda_imm16(0x0195);
    func_table_call(0x01F91E);

done:
    return;
}

/* ========================================================================
 * $00:878F — Animation frame check
 *
 * Checks if animation is still running. Decrements $0545
 * and returns carry set when animation is done (or buttons pressed).
 * ======================================================================== */
void mp_00878F(void) {
    if (bus_wram_read16(0x04CA) != 0) goto done;

    uint16_t cy = bus_wram_read16(0x04DE);  /* Cursor Y */
    if (cy >= 0x00CC) goto done;

    uint16_t timer = bus_wram_read16(0x0545);
    timer--;
    bus_wram_write16(0x0545, timer);
    if (timer == 0) goto done;

    /* Animation still running */
    g_cpu.flag_C = false;
    return;

done:
    bus_wram_write16(0x0545, 0x0000);
    g_cpu.flag_C = true;
}

/* ========================================================================
 * $00:87A8 — Post-logic dispatch (game state jump table)
 *
 * Dispatches to the handler for the current game state ($09D6).
 * State 0 = main drawing canvas, other states = various tools,
 * menus, minigames, etc.
 *
 * Jump table has 31 entries. Most target complex routines that
 * aren't recompiled yet — they'll be no-ops via func_table_call.
 * ======================================================================== */
void mp_0087A8(void) {
    /* Read game state and dispatch */
    uint16_t state = bus_wram_read16(0x09D6);

    /* Jump table addresses from DATA_0087B0 */
    static const uint32_t dispatch_table[31] = {
        0x008BEB,  /*  0: Main canvas (click handler) */
        0x00C900,  /*  1: */
        0x00CC10,  /*  2: */
        0x00C460,  /*  3: */
        0x00C4D6,  /*  4: */
        0x00BBD8,  /*  5: */
        0x00CF9B,  /*  6: */
        0x00D03E,  /*  7: */
        0x00D069,  /*  8: */
        0x00D2F6,  /*  9: */
        0x00D321,  /* 10: */
        0x00D505,  /* 11: */
        0x00D50D,  /* 12: */
        0x0094E5,  /* 13: (RTS — no-op) */
        0x0094E5,  /* 14: (RTS — no-op) */
        0x0094E5,  /* 15: (RTS — no-op) */
        0x0094E5,  /* 16: (RTS — no-op) */
        0x0094E5,  /* 17: (RTS — no-op) */
        0x0094E5,  /* 18: (RTS — no-op) */
        0x0094E5,  /* 19: (RTS — no-op) */
        0x0094E5,  /* 20: (RTS — no-op) */
        0x00D8F1,  /* 21: */
        0x00DB59,  /* 22: */
        0x00DB84,  /* 23: */
        0x00DBE4,  /* 24: */
        0x00DC29,  /* 25: */
        0x00DC8B,  /* 26: */
        0x00DCC5,  /* 27: */
        0x00DD6D,  /* 28: */
        0x00E4B9,  /* 29: */
        0x00EC88,  /* 30: */
    };

    if (state < 31) {
        func_table_call(dispatch_table[state]);
    }
}

/* ========================================================================
 * $00:9378 — Cursor sprite rendering
 *
 * Renders the cursor sprite into OAM based on:
 *   - Current tool mode ($04D0)
 *   - Cursor X/Y position ($04DC/$04DE)
 *   - Toolbar/palette state
 *   - Animation frame counters
 *
 * Reads sprite frame data from ROM tables at $00:950C-$955B.
 * The data table at $00:94E6 contains pointers to frame data.
 *
 * Frame data format: byte[0]=X_offset, byte[1]=Y_offset,
 * byte[2]=flags, then tile data bytes.
 * ======================================================================== */
void mp_009378(void) {
    /* JSL CODE_01E8F6 — tile row optimization check */
    func_table_call(0x01E8F6);

    /* Save current palette animation state */
    uint16_t saved_1996 = bus_wram_read16(0x1996);

    /* Determine cursor sprite index based on tool and position */
    uint16_t tool = bus_wram_read16(0x04D0);
    uint16_t cursor_y = bus_wram_read16(0x04DE);

    /* Check if cursor is in the canvas area */
    if (cursor_y >= 0x001C) {
        uint16_t xfa = bus_wram_read16(0x19FA);
        if (xfa != 0) {
            uint16_t xc2 = bus_wram_read16(0x19C2);
            if (xc2 == 0x0002) {
                if (cursor_y >= 0x00BC) goto toolbar_area;
                goto in_canvas;
            }
        }
        if (cursor_y < 0x00C4) goto in_canvas;
    }

toolbar_area:
    /* Cursor is in toolbar area — check for toolbar override */
    {
        uint16_t xaa = bus_wram_read16(0x19AA);
        if (xaa != 0) goto in_canvas;
        uint16_t xd1 = bus_wram_read16(0x09D1);
        if (xd1 != 0) goto in_canvas;
    }

    /* In toolbar: handle toolbar cursor logic */
    {
        uint16_t xa0 = bus_wram_read16(0x19A0);
        if (xa0 != 0) goto use_toolbar_cursor;

        /* Save current animation state */
        bus_wram_write16(0x199C, bus_wram_read16(0x1998));
        bus_wram_write16(0x199E, bus_wram_read16(0x199A));
        bus_wram_write16(0x19A0, 0x0001);
    }

use_toolbar_cursor:
    tool = 0x000C;  /* Toolbar cursor = index 12 */

in_canvas:
    /* Check if tool $0C needs to be applied vs. actual tool */
    if (tool != 0x000C) {
        uint16_t x567 = bus_wram_read16(0x0567);
        if (x567 != 0) goto toolbar_area;  /* Force toolbar cursor */

        uint16_t xa0 = bus_wram_read16(0x19A0);
        if (xa0 != 0) {
            /* Restore animation state from saved values */
            bus_wram_write16(0x1998, bus_wram_read16(0x199C));
            bus_wram_write16(0x199A, bus_wram_read16(0x199E));
            bus_wram_write16(0x19A0, 0x0000);
        }
    }

    /* Special tool $06 handling */
    if (tool == 0x0006) {
        uint8_t dp20 = bus_wram_read8(0x0020);
        if (dp20 == 0) {
            bus_wram_write16(0x1996, 0x0000);
            bus_wram_write16(0x1998, 0x0000);
            bus_wram_write16(0x199A, 0x0000);
        }
        uint16_t x19c0 = bus_wram_read16(0x19C0);
        if (x19c0 != 0) {
            tool += 0x000A;  /* Tool 6 + offset → index 16 */
        }
    }

    /* Read cursor sprite pointer from DATA_0094E6 table in ROM.
     * Table is at $00:94E6, each entry is 2 bytes (16-bit pointer). */
    uint16_t tool_idx = tool & 0x00FF;
    uint16_t sprite_ptr = bus_read16(0x00, 0x94E6 + tool_idx * 2);
    uint16_t next_ptr = bus_read16(0x00, 0x94E6 + tool_idx * 2 + 2);

    /* Compute animation frame count */
    uint16_t frame_count = next_ptr - sprite_ptr - 3;
    bus_wram_write16(0x19A3, frame_count);

    /* Read sprite X/Y offset and flags from frame data */
    uint8_t palette_page = (uint8_t)(bus_wram_read16(0x19A2) >> 8);
    bus_wram_write8(0x19A2 + 1, (uint8_t)(tool >> 8));  /* Save high byte */

    /* Frame data byte 0: cursor X offset */
    int8_t x_off_raw = (int8_t)bus_read8(0x00, sprite_ptr);
    /* Frame data byte 1: cursor Y offset */
    int8_t y_off_raw = (int8_t)bus_read8(0x00, sprite_ptr + 1);
    /* Frame data byte 2+: tile info (flags byte, then tile data) */

    /* Apply X offset to cursor position */
    int16_t cursor_x = (int16_t)bus_wram_read16(0x04DC);
    /* Sign-extend: ASL; CMP #$80; ROR pattern = arithmetic shift right trick
     * This converts unsigned with bit7=sign to proper signed */
    int16_t x_disp = x_off_raw;
    uint8_t oam_x = (uint8_t)((cursor_x + x_disp) & 0xFF);
    bus_wram_write8(OAM_BUF + 0, oam_x);

    /* Track X bit 8 for upper OAM */
    uint8_t x_bit8 = 0;
    int16_t full_x = cursor_x + x_disp;
    if (full_x < 0 || full_x >= 256) x_bit8 = 1;

    /* Check bit 7 of x_off for palette flag */
    uint8_t x_flag = bus_read8(0x00, sprite_ptr) & 0x80;
    uint8_t pal_1996 = 0;
    if (x_flag) pal_1996 = x_flag;
    pal_1996 |= (uint8_t)bus_wram_read16(0x1996);
    bus_wram_write8(0x1996, pal_1996);

    /* Apply Y offset to cursor position */
    int16_t cy = (int16_t)bus_wram_read16(0x04DE);
    int16_t y_disp = y_off_raw;
    uint8_t oam_y = (uint8_t)((cy + y_disp) & 0xFF);
    bus_wram_write8(OAM_BUF + 1, oam_y);

    /* Y bit 8 for upper OAM (combined with X bit8) */
    uint8_t y_bit8 = 0;
    int16_t full_y = cy + y_disp;
    if (y_off_raw < 0 && full_y < 0) y_bit8 = 2;  /* bit 1 of upper OAM */
    uint8_t upper_bits = x_bit8 | y_bit8;
    bus_wram_write8(0x19A5, upper_bits);

    /* Check for special pencil cursor (sprite_ptr == DATA_009515) */
    if (sprite_ptr == 0x9515) {
        uint16_t eb8 = bus_wram_read16(0x0EB8);
        if (eb8 == 0) {
            uint8_t pal_row = bus_wram_read8(0x00A6);
            if (pal_row < 0x70) goto normal_tile;
        }
        /* Use special pencil tile */
        bus_wram_write8(OAM_BUF + 2, 0x6C);  /* Pencil tile */
        bus_wram_write8(OAM_BUF + 3, bus_wram_read8(0x09CF));
        goto set_upper_oam;
    }

normal_tile:
    /* Handle animated cursors */
    if (frame_count == 0) goto static_cursor;

    /* Advance animation */
    {
        uint8_t anim_counter = bus_wram_read8(0x1998);
        uint8_t anim_limit = bus_read8(0x00, sprite_ptr + 2);  /* Flags byte = frame count */
        if (anim_counter >= anim_limit) {
            bus_wram_write8(0x1998, 0x00);
            uint8_t frame_idx = bus_wram_read8(0x199A) + 1;
            if (frame_idx >= (frame_count & 0xFF)) frame_idx = 0;
            bus_wram_write8(0x199A, frame_idx);
        }
    }

    /* Check if 1996 flag triggers animation advance */
    {
        uint8_t fl = bus_wram_read8(0x1996);
        if (fl != 0) {
            uint8_t c = bus_wram_read8(0x1998) + 1;
            bus_wram_write8(0x1998, c);
        }
    }

    /* Read tile from animation frame */
    {
        uint8_t frame_idx = bus_wram_read8(0x199A);
        uint16_t tile_offset = sprite_ptr + 3 + frame_idx;
        uint8_t tile = bus_read8(0x00, tile_offset);
        bus_wram_write8(OAM_BUF + 2, tile);

        uint8_t prop = bus_wram_read8(0x19A2) | bus_wram_read8(0x09CF);
        bus_wram_write8(OAM_BUF + 3, prop);
        goto set_upper_oam;
    }

static_cursor:
    /* Static cursor — read tile from byte 3 of frame data */
    {
        uint8_t tile = bus_read8(0x00, sprite_ptr + 3);
        bus_wram_write8(OAM_BUF + 2, tile);

        uint8_t prop = bus_wram_read8(0x19A2) | bus_wram_read8(0x09CF);
        bus_wram_write8(OAM_BUF + 3, prop);
    }

set_upper_oam:
    /* Write upper OAM bits for sprite 0 */
    {
        uint8_t upper = bus_wram_read8(OAM_HI_BUF);
        upper = (upper & ~0x03) | (bus_wram_read8(0x19A5) & 0x03);
        bus_wram_write8(OAM_HI_BUF, upper);
    }

    /* Restore saved state */
    bus_wram_write16(0x1996, saved_1996);
}

/* ========================================================================
 * Register all game logic functions.
 * ======================================================================== */
void mp_register_gamelogic(void) {
    func_table_register(0x01E8F6, mp_01E8F6);
    func_table_register(0x008683, mp_008683);
    func_table_register(0x00878F, mp_00878F);
    func_table_register(0x0087A8, mp_0087A8);
    func_table_register(0x009378, mp_009378);
}
