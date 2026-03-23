/*
 * Mario Paint — Remaining boot chain helpers and utility routines.
 *
 * This file contains the miscellaneous routines needed to complete
 * the boot chain and game initialization:
 *   - Fade out, RNG read, sprite wrappers
 *   - Canvas palette/tool/stamp initialization
 *   - Title screen display setup
 *   - SRAM initialization
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <mp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

#include <stdbool.h>

extern bool g_quit;

/* ========================================================================
 * $01:E7C9 — Fade out (brightness ramp down)
 *
 * Ramps brightness from current level to 0, then sets force blank.
 * Complement of E794 (fade in).
 * ======================================================================== */
void mp_01E7C9(void) {
    uint8_t disp = bus_wram_read8(0x0104);
    uint8_t brt = disp & 0x0F;

    if (brt == 0) goto done;

    while (brt > 0 && !g_quit) {
        bus_wram_write8(0x04BD, 0x01);
        while (bus_wram_read8(0x04BD) != 0 && !g_quit) {
            mp_01E2CE();
            uint8_t t = bus_wram_read8(0x04BD);
            if (t > 0) bus_wram_write8(0x04BD, t - 1);
        }
        disp = bus_wram_read8(0x0104);
        disp--;
        bus_wram_write8(0x0104, disp);
        brt = disp & 0x0F;
    }

done:
    /* Set force blank */
    disp = bus_wram_read8(0x0104);
    disp |= 0x80;
    bus_wram_write8(0x0104, disp);
    if (!g_quit) mp_01E2CE();
}

/* ========================================================================
 * $01:E20C — Read random number from RNG table
 *
 * Advances the RNG index ($044A), performs shuffle at wraparound,
 * and returns the value from the RNG table at $044C.
 * ======================================================================== */
void mp_01E20C(void) {
    uint16_t idx = bus_wram_read16(0x044A);
    idx++;

    if (idx >= 0x0037) {
        /* Shuffle the table (CODE_01E298) */
        uint16_t modulus = bus_wram_read16(0x0448);
        for (uint16_t yi = 0; yi < 0x006E; yi += 2) {
            uint16_t other_y;
            if (yi < 0x0030) {
                other_y = yi + 0x003E;
            } else {
                other_y = yi - 0x0030;
            }
            uint16_t val = bus_wram_read16(0x044C + yi);
            uint16_t sub = bus_wram_read16(0x044C + other_y);
            if (val < sub) val += modulus;
            val -= sub;
            bus_wram_write16(0x044C + yi, val);
        }
        idx = 0;
    }

    bus_wram_write16(0x044A, idx);
    CPU_SET_A16(bus_wram_read16(0x044C + idx * 2));
}

/* ========================================================================
 * $00:8A12 — Wrapper: calls 008A16 (BG2 tilemap fill)
 * ======================================================================== */
void mp_008A12(void) {
    mp_008A16();
}

/* ========================================================================
 * $00:9FC4 — Tool mode change handler
 *
 * Handles transitions between tool modes (pencil, fill, stamp, etc.).
 * Manages the bomb icon animation state ($058D) based on which tool
 * transitions have a bomb effect.
 *
 * Called with stack args: tool_id (at $07 via DP), flags (at $05).
 * In the recomp, these were pushed onto the stack before calling.
 * ======================================================================== */
void mp_009FC4(void) {
    /* Set active flag */
    bus_wram_write16(0x0589, 0x0001);

    uint16_t new_tool = bus_wram_read16(0x00AA);
    uint16_t old_tool = bus_wram_read16(0x058F);

    if (new_tool == old_tool) goto skip_bomb;

    /* Check tool transition pairs for bomb effect */
    if (new_tool == 0x0000) {
        if (old_tool == 0x0001) goto bomb_off;
        goto skip_bomb;
    }
    if (new_tool == 0x0007) {
        if (old_tool == 0x0000) goto skip_bomb;
        goto bomb_off;
    }
    if (new_tool == 0x0002) {
        if (old_tool == 0x0007) goto bomb_on;
        goto bomb_timer;
    }
    if (new_tool == 0x0003) {
        if (old_tool == 0x0001) goto bomb_timer;
        goto bomb_on;
    }
    if (new_tool == 0x0004) {
        if (old_tool == 0x0003) goto bomb_on;
    }

bomb_timer:
    bus_wram_write16(0x058D, 0xFFFF);
    for (int i = 0x10; i > 0 && !g_quit; i--) {
        mp_01E2CE();
    }

bomb_on:
    bus_wram_write16(0x058D, 0x0001);
    goto skip_bomb;

bomb_off:
    bus_wram_write16(0x058D, 0xFFFF);
    for (int i = 0x10; i > 0 && !g_quit; i--) {
        mp_01E2CE();
    }
    bus_wram_write16(0x058D, 0x0000);

skip_bomb:
    /* Save current tool as previous */
    bus_wram_write16(0x058F, bus_wram_read16(0x00AA));

    /* Read the tool flags from the stack args.
     * In the original, $05 and $07 are on the DP-mapped stack.
     * We read them from the actual stack via g_cpu.S. */
    uint16_t flags = bus_wram_read16(g_cpu.S + 3);  /* $05 on stack frame */
    uint16_t tool_id = bus_wram_read16(g_cpu.S + 5); /* $07 on stack frame */

    /* Call tool setup via function table */
    func_table_call(0x00A25B);  /* CODE_00A25B */
    func_table_call(0x00A22D);  /* CODE_00A22D */

    /* Additional tool-specific setup based on flags */
    if (flags != 0) {
        /* Look up from DATA_00A0EB dispatch table */
        func_table_call(0x00A0EB);
    }
}

/* ========================================================================
 * $00:B66C — Load drawing tool pen graphics (foreground)
 *
 * Computes a ROM offset from the tool/palette index in A,
 * reads 128 bytes ($80) of tile data from ROM bank $0A at $8000,
 * and stores into the pen graphics buffer at $1BAA.
 * Then calls $01D56D to queue the pen tile DMA.
 * ======================================================================== */
void mp_00B66C(void) {
    uint16_t idx = CPU_A16();

    /* Compute ROM offset: complex bit manipulation */
    uint16_t row = idx & 0xFFF8;
    row <<= 1;  /* ASL */
    uint16_t col = idx & 0x0007;
    uint16_t offset = (row + col) << 6;  /* * 64 bytes per tile row */

    /* Clamp/adjust for special ranges */
    if (offset >= 0x7800) {
        /* Read from SRAM instead */
        offset -= 0x7800;
        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = bus_read16(0x70, offset + y);  /* SRAM */
            bus_wram_write16(0x1BAA + y, val);
        }
        offset += 0x0200;  /* Next row */
        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = bus_read16(0x70, offset + y);
            bus_wram_write16(0x1BAA + 0x40 + y, val);
        }
    } else {
        if (offset < 0x3800) {
            /* Normal range */
        } else {
            offset += 0x0800;  /* Skip gap */
        }

        /* Read tile data from ROM bank $0A */
        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = bus_read16(0x0A, 0x8000 + offset + y);
            bus_wram_write16(0x1BAA + y, val);
        }
        offset += 0x0200;  /* Next row = +$200 in ROM, +$40 in buffer */
        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = bus_read16(0x0A, 0x8000 + offset + y);
            bus_wram_write16(0x1BAA + 0x40 + y, val);
        }
    }

    /* Queue pen tile DMA */
    func_table_call(0x01D56D);
}

/* ========================================================================
 * $00:B6F4 — Load drawing tool pen graphics (background)
 *
 * Similar to B66C but for the background pen. Stores at $1B2A.
 * ======================================================================== */
void mp_00B6F4(void) {
    uint16_t idx = CPU_A16();

    uint16_t row = idx & 0xFFF8;
    row <<= 1;
    uint16_t col = idx & 0x0007;
    uint16_t offset = (row + col) << 6;

    if (offset >= 0x7800) {
        offset -= 0x7800;
        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = bus_read16(0x70, offset + y);
            bus_wram_write16(0x1B2A + y, val);
        }
        offset += 0x0200;
        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = bus_read16(0x70, offset + y);
            bus_wram_write16(0x1B2A + 0x40 + y, val);
        }
    } else {
        if (offset >= 0x0800) {
            /* offset already OK */
        } else {
            offset += 0x3000;  /* Wrap low addresses */
        }
        offset += 0x0800;

        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = bus_read16(0x0A, 0x8000 + offset + y);
            bus_wram_write16(0x1B2A + y, val);
        }
        offset += 0x0200;
        for (int y = 0; y < 0x40; y += 2) {
            uint16_t val = bus_read16(0x0A, 0x8000 + offset + y);
            bus_wram_write16(0x1B2A + 0x40 + y, val);
        }
    }

    func_table_call(0x01D56D);
}

/* ========================================================================
 * $00:BA78 — Initialize BG3 animation/scroll buffer
 *
 * Clears $7E:3800 (2KB), sets up initial values for the
 * BG3 scroll animation state.
 * ======================================================================== */
void mp_00BA78(void) {
    uint8_t *wram = bus_get_wram();
    for (int x = 0x07F8; x >= 0; x -= 2) {
        wram[0x3800 + x]     = 0x00;
        wram[0x3800 + x + 1] = 0x00;
    }
    /* Set initial values */
    wram[0x3800] = 0x00; wram[0x3801] = 0x00;
    wram[0x3802] = 0x80; wram[0x3803] = 0x80;

    bus_wram_write16(0x19EA, 0xFFFE);
    bus_wram_write16(0x19EC, 0x0004);
    bus_write16(0x7E, 0x3FFA, 0x0004);
    bus_wram_write16(0x19EE, 0x0000);
    bus_wram_write16(0x19F2, 0x0000);
}

/* ========================================================================
 * $00:DE8E — Load custom stamp display buffer
 *
 * Copies $800 bytes of stamp graphics from ROM $05:F800 to the
 * stamp display buffer, then initializes palette rows.
 * ======================================================================== */
void mp_00DE8E(void) {
    /* Copy stamp graphics */
    for (int x = 0x07FE; x >= 0; x -= 2) {
        uint16_t val = bus_read16(0x05, 0xF800 + x);
        bus_wram_write16(0x1A58 + x, val);  /* Custom stamp GFX buffer */
    }

    /* Set initial palette row */
    bus_wram_write16(0x00A6, 0x00F0);

    /* Initialize 16 palette rows (0-15) */
    for (int i = 0x000F; i >= 0 && !g_quit; i--) {
        bus_wram_write16(0x112C, i);
        func_table_call(0x00E0D4);  /* Compute palette row */
        func_table_call(0x00DF1C);  /* Store palette data */
    }
}

/* ========================================================================
 * $00:E25C — SRAM validation and clear
 *
 * Checks SRAM header at $70:07C0. If invalid, clears SRAM.
 * ======================================================================== */
void mp_00E25C(void) {
    /* Read and validate SRAM header */
    uint16_t header = bus_read16(0x70, 0x07C0);
    func_table_call(0x00E233);  /* Compute expected value */

    /* Compare with stored header */
    if (CPU_A16() == header) return;

    /* Invalid: clear SRAM $70:0000-$07BE */
    for (int x = 0x07BE; x >= 0; x -= 2) {
        bus_write16(0x70, x, 0x0000);
    }
}

/* ========================================================================
 * $00:E64F — Canvas palette row initialization
 *
 * Complex routine that initializes the palette color data for each
 * canvas row. Reads color data from various sources depending on
 * tool state ($0EB8, $09D6, $04D0).
 *
 * Due to its complexity and many sub-routine calls, we implement
 * the outer loop and dispatch to helpers via func_table_call.
 * ======================================================================== */
void mp_00E64F(void) {
    uint16_t x = 0x0000;
    uint16_t y = 0x0000;

    while (x < 0x0050) {  /* 80 entries / 2 bytes each ≈ up to $4E */
        if (x == 0x0000 || x == 0x004E) {
            /* Skip these indices — jump to end-of-row processing */
            goto next_row;
        }

        /* Determine source for palette data based on game state */
        uint16_t eb8 = bus_wram_read16(0x0EB8);
        if (eb8 != 0) {
            /* Alternate palette source from $0EC4 tables */
            bus_wram_write16(0x1004, bus_wram_read16(0x0EC4 + x));
            bus_wram_write16(0x1006, bus_wram_read16(0x0EE4 + x));
            bus_wram_write16(0x100A, bus_wram_read16(0x0ED4 + x));
            bus_wram_write16(0x100C, bus_wram_read16(0x0EF4 + x));
        } else {
            uint16_t palette_row = bus_wram_read16(0x00A6);
            if (palette_row >= 0x0070) {
                /* High palette: read from $1C2A tables */
                bus_wram_write16(0x1004, bus_wram_read16(0x1C2A + x));
                bus_wram_write16(0x1006, bus_wram_read16(0x1C4A + x));
                bus_wram_write16(0x100A, bus_wram_read16(0x1C3A + x));
                bus_wram_write16(0x100C, bus_wram_read16(0x1C5A + x));
            } else {
                /* Standard: read from $1CAA tables */
                bus_wram_write16(0x1004, bus_wram_read16(0x1CAA + x));
                bus_wram_write16(0x1006, bus_wram_read16(0x1CCA + x));
                bus_wram_write16(0x100A, bus_wram_read16(0x1CBA + x));
                bus_wram_write16(0x100C, bus_wram_read16(0x1CDA + x));
            }
        }

        /* Clear intermediate values */
        bus_wram_write16(0x1008, 0x0000);
        bus_wram_write16(0x100E, 0x0000);

        /* Process the color data — calls sub-routines for
         * bit manipulation and palette computation */
        func_table_call(0x00E6DA);

next_row:
        x += 2;
    }
}

/* ========================================================================
 * $00:F39E — Initialize palette display state
 *
 * Fills the $09E4 palette display buffer with $FFFF (all empty),
 * sets up various palette display parameters.
 * ======================================================================== */
void mp_00F39E(void) {
    /* Fill palette display buffer with $FFFF */
    for (int x = 0x023E; x >= 0; x -= 2) {
        bus_wram_write16(0x09E4 + x, 0xFFFF);
    }

    bus_wram_write16(0x0C28, 0x0050);

    /* Call palette row init */
    func_table_call(0x00F921);

    bus_wram_write16(0x0C32, 0x0001);
    bus_wram_write16(0x09E0, 0x0000);
    bus_wram_write16(0x0C26, 0x0000);
    bus_wram_write16(0x0C24, 0x0310);
}

/* ========================================================================
 * $01:8F52 — Title screen to canvas display transition
 *
 * Sets up the canvas display after the title screen:
 * fills BG1 tilemap with $24E0, queues DMA, sets up toolbar
 * sprite slot, enables brightness, and runs the toolbar
 * scroll-in animation.
 * ======================================================================== */
void mp_018F52(void) {
    /* Fill BG1 tilemap with $24E0 (blank canvas tile) */
    uint8_t *wram = bus_get_wram();
    for (int x = 0x07FE; x >= 0; x -= 2) {
        wram[0x2000 + x]     = 0xE0;
        wram[0x2000 + x + 1] = 0x24;
    }

    /* Queue tilemap DMA and sync */
    mp_01DE97();
    mp_01E2CE();
    if (g_quit) return;

    /* Initialize toolbar sprite slot $28 */
    op_lda_imm16(0x0028);
    mp_019DFE();

    /* Animate toolbar sprite */
    op_lda_imm16(0x0028);
    mp_01962C();

    /* Enable display (brightness = $0F, no force blank) */
    op_sep(0x20);
    bus_wram_write8(0x0104, 0x0F);
    op_rep(0x20);

    mp_01E2CE();
    if (g_quit) return;

    /* Set up canvas data transfer flag */
    bus_wram_write16(0x0211, 0x0001);

    /* Enable BG window masking */
    op_sep(0x20);
    bus_wram_write8(0x0111, 0x02);
    op_rep(0x20);

    /* Run toolbar scroll-in animation */
    for (int x = 0x0047; x > 0 && !g_quit; x--) {
        /* Set up animation params and call sub-routines */
        func_table_call(0x01904A);

        uint16_t hdma_flag = bus_wram_read16(0x0211);
        hdma_flag ^= 0x0003;
        bus_wram_write16(0x0211, hdma_flag);
        bus_wram_write16(0x0220, hdma_flag);

        mp_01E2CE();
        if (g_quit) return;

        func_table_call(0x01934F);

        /* Check for early exit on mouse click */
        uint8_t buttons = bus_wram_read8(0x04CA);
        if (buttons & 0x20) {
            /* Fast-forward animation */
            break;
        }
    }

    /* Final animation settle loop */
    while (!g_quit) {
        mp_01E06F();
        op_lda_imm16(0x0028);
        mp_01962C();
        mp_01E2CE();
        if (g_quit) return;

        func_table_call(0x01934F);

        uint8_t buttons = bus_wram_read8(0x04CA);
        if (buttons & 0x20) break;

        uint8_t flag = bus_wram_read8(0x05A9);
        if (flag != 0) break;
    }
}

/* ========================================================================
 * $01:F825 — Load toolbar display data
 *
 * Copies 8 bytes of toolbar display data from ROM bank $0D
 * (DATA_0DEB22) to the toolbar buffer at $1A10.
 * ======================================================================== */
void mp_01F825(void) {
    uint16_t slot = CPU_A16();
    uint16_t src = slot * 8;
    uint16_t dst = slot * 2;

    for (int i = 0; i < 4; i++) {
        uint16_t val = bus_read16(0x0D, 0xEB22 + src);
        bus_wram_write16(0x1A10 + dst, val);
        src += 2;
        dst += 2;
    }
}

/* ========================================================================
 * Register all helper functions.
 * ======================================================================== */
void mp_register_helpers(void) {
    func_table_register(0x01E7C9, mp_01E7C9);
    func_table_register(0x01E20C, mp_01E20C);
    func_table_register(0x008A12, mp_008A12);
    func_table_register(0x009FC4, mp_009FC4);
    func_table_register(0x00B66C, mp_00B66C);
    func_table_register(0x00B6F4, mp_00B6F4);
    func_table_register(0x00BA78, mp_00BA78);
    func_table_register(0x00DE8E, mp_00DE8E);
    func_table_register(0x00E25C, mp_00E25C);
    func_table_register(0x00E64F, mp_00E64F);
    func_table_register(0x00F39E, mp_00F39E);
    func_table_register(0x018F52, mp_018F52);
    func_table_register(0x01F825, mp_01F825);
}
