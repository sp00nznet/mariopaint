/*
 * Mario Paint — Title screen animation loop and Bank 0F routines.
 *
 * The title screen has a 12-state animation machine that runs
 * the Nintendo/Mario Paint logo sequence, then waits for input
 * or times out into the demo playback.
 *
 * Bank $0F contains the demo playback system that feeds
 * recorded mouse/button data into the input state.
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <mp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

#include <stdbool.h>

extern bool g_quit;

/* ========================================================================
 * $01:82B1 — Title screen cursor position update
 *
 * Applies mouse displacement ($04C6/$04C8) to cursor position
 * ($04DC/$04DE), clamped to title screen bounds.
 * ======================================================================== */
void mp_0182B1(void) {
    /* X displacement: signed magnitude → two's complement */
    uint8_t raw_dx = bus_wram_read8(0x04C6);
    int16_t dx = raw_dx & 0x7F;
    if (raw_dx & 0x80) dx = -dx;

    int16_t cx = (int16_t)bus_wram_read16(0x04DC);
    int16_t new_cx = cx + dx;
    if (new_cx < 0x00E8 && new_cx >= 0x0018) {
        bus_wram_write16(0x04DC, (uint16_t)new_cx);
    }

    /* Y displacement */
    uint8_t raw_dy = bus_wram_read8(0x04C8);
    int16_t dy = raw_dy & 0x7F;
    if (raw_dy & 0x80) dy = -dy;

    int16_t cy = (int16_t)bus_wram_read16(0x04DE);
    int16_t new_cy = cy + dy;
    if (new_cy < 0x00C0 && new_cy >= 0x0020) {
        bus_wram_write16(0x04DE, (uint16_t)new_cy);
    }
}

/* ========================================================================
 * $01:82F6 — Title screen cursor sprite draw
 *
 * Draws the cursor sprite at the current position using
 * the sprite ID stored at $7F:0005.
 * ======================================================================== */
void mp_0182F6(void) {
    uint16_t x = bus_wram_read16(0x04DC);
    uint16_t y = bus_wram_read16(0x04DE);
    uint16_t sprite_id = bus_read16(0x7F, 0x0005);

    g_cpu.X = x;
    g_cpu.Y = y;
    CPU_SET_A16(sprite_id);
    mp_01F91E();
}

/* ========================================================================
 * $01:8308 — Title screen state machine
 *
 * 12-state animation machine that runs the title screen sequence.
 * State stored at $7F:0001. Each state handles part of the
 * logo animation, then advances to the next.
 *
 * Most states involve complex sprite animations. We dispatch
 * each to func_table_call so they work progressively.
 * ======================================================================== */
void mp_018308(void) {
    uint16_t state = bus_read16(0x7F, 0x0001);
    uint16_t idx = state * 2;

    /* 12-entry dispatch table */
    static const uint32_t states[12] = {
        0x018328, 0x01833F, 0x01834B, 0x0183B6,
        0x0184E2, 0x018521, 0x0185A3, 0x018641,
        0x0187F9, 0x01885F, 0x0188E3, 0x018AAF
    };

    if (state < 12) {
        func_table_call(states[state]);
    }
}

/* ========================================================================
 * $01:8C43 — Title screen sprite setup
 *
 * Renders the 24 title screen sprites (tool icons, text, etc.).
 * Skips the sprite at index A (used to hide a specific one).
 *
 * Sprite data tables at $01:8C6F (X), $01:8C87 (Y), $01:8C9F (tile).
 * ======================================================================== */
void mp_018C43(void) {
    uint16_t skip_idx = CPU_A16();

    for (int x = 0x17; x >= 0; x--) {
        if ((uint16_t)x == skip_idx) continue;

        /* Read sprite X, Y, tile from ROM tables */
        uint8_t spr_x = bus_read8(0x01, 0x8C6F + x);
        uint8_t spr_y = bus_read8(0x01, 0x8C87 + x);
        uint8_t spr_tile = bus_read8(0x01, 0x8C9F + x);

        g_cpu.X = spr_x;
        g_cpu.Y = spr_y;
        CPU_SET_A16(spr_tile & 0xFF);
        mp_01F91E();
    }
}

/* ========================================================================
 * $01:8CB7 — Title screen: animate sprite slot 0
 * ======================================================================== */
void mp_018CB7(void) {
    op_lda_imm16(0x0000);
    mp_01962C();
}

/* ========================================================================
 * $01:8CBF — Title screen input check
 *
 * Checks for player input to skip the title screen:
 *   - P2 controller: L+R+A+B ($9080) → skip to canvas ($0565=0)
 *   - P2 controller: L+R+A+X ($A080) → skip to canvas ($0565=1)
 *   - P2 buttons = $FFFF → skip ($0565=1)
 *   - Mouse click on the logo → skip ($0565=0)
 *
 * If no input, calls E2CE (frame sync) and returns normally.
 * On skip, manipulates the stack to return directly from $018260.
 * ======================================================================== */
void mp_018CBF(void) {
    /* Check P2 pressed buttons for special combos */
    uint16_t p2_pressed = bus_wram_read16(0x013C);

    /* L+R+A+B = $9080 */
    if (p2_pressed & 0x9080) {
        uint16_t p2_held = bus_wram_read16(0x0134);
        if (p2_held == 0x9080) {
            bus_wram_write16(0x0565, 0x0000);
            g_quit = true;  /* Force exit from title loop */
            return;
        }
    }

    /* L+R+A+X = $A080 */
    if (p2_pressed & 0xA080) {
        uint16_t p2_held = bus_wram_read16(0x0134);
        if (p2_held == 0xA080) {
            bus_wram_write16(0x0565, 0x0001);
            g_quit = true;
            return;
        }
    }

    /* P2 all buttons = $FFFF */
    uint16_t p2_held = bus_wram_read16(0x0134);
    if (p2_held == 0xFFFF) {
        bus_wram_write16(0x0565, 0x0001);
        g_quit = true;
        return;
    }

    /* Mouse click check — any left click skips title screen.
     * Original checks if click is on the logo area, but our logo
     * sprite position ($0792/$0794) may not be initialized correctly
     * during early title states. Accept any click for now. */
    uint8_t buttons = bus_wram_read8(0x04CA);
    if (buttons & 0x20) {
        bus_wram_write16(0x0565, 0x0000);
        g_quit = true;
        return;
    }

    /* No skip — frame sync and continue */
    mp_01E2CE();
}

/* ========================================================================
 * $01:8260 — Title screen animation loop
 *
 * Main loop: clears OAM, updates cursor, draws sprites,
 * runs state machine, checks input. Loops until input detected
 * or demo timeout ($1980 reaches $0800).
 *
 * Note: The original code does a stack trick to exit (TCS; RTS)
 * from $018CBF. In the recomp we use a flag approach instead —
 * $018CBF sets g_quit temporarily when it wants to exit the loop,
 * and we restore g_quit=false after the loop.
 * ======================================================================== */
void mp_018260(void) {
    /* Save g_quit state — 018CBF may set it to break the loop */
    bool saved_quit = g_quit;

    while (!g_quit) {
        /* Clear OAM (all sprites offscreen) */
        mp_01E06F();

        /* Update cursor position from mouse delta */
        mp_0182B1();

        /* Draw cursor sprite */
        mp_0182F6();

        /* Run title state machine */
        mp_018308();

        /* Check input / frame sync */
        mp_018CBF();
        if (g_quit) break;

        /* Check mouse button activity (not movement — movement resets timer) */
        uint16_t mouse_buttons = bus_wram_read16(0x04CA);
        uint16_t mouse_move = bus_wram_read16(0x04C6) | bus_wram_read16(0x04C8);
        if (mouse_buttons != 0) {
            /* Button pressed — this should have been caught by 018CBF above,
             * but in case it wasn't, don't reset the timer */
        } else if (mouse_move != 0) {
            bus_wram_write16(0x1980, 0x0000);
            continue;  /* Movement resets timer */
        }

        /* Increment demo timer */
        uint16_t timer = bus_wram_read16(0x1980) + 1;
        bus_wram_write16(0x1980, timer);

        if (timer >= 0x00C0) {  /* ~3 seconds for quick testing */
            /* Demo timeout — set up demo playback */
            bus_wram_write16(0x04DC, 0x0080);
            bus_wram_write16(0x04DE, 0x0080);

            func_table_call(0x0FC00E);  /* Start demo */

            bus_wram_write16(0x0448, 0x0100);
            uint16_t seed = bus_wram_read16(0x02);
            CPU_SET_A16(seed);
            mp_01E238();

            break;  /* Exit title loop, demo will play */
        }
    }

    /* Restore g_quit — the 018CBF "exit" was just to break this loop */
    g_quit = saved_quit;
}

/* ========================================================================
 * $0F:C000 — Bank 0F: per-frame demo dispatch
 *
 * Called from the NMI handler every frame. If demo is not active
 * ($04E2 = 0), just returns. If active, plays back recorded input.
 * ======================================================================== */
void mp_0FC000(void) {
    uint16_t demo = bus_wram_read16(0x04E2);
    if (demo == 0) return;

    /* Demo is active — play back recorded input */
    /* Check if mouse/button activity should cancel demo */
    uint16_t mouse_any = bus_wram_read16(0x04C6) |
                         bus_wram_read16(0x04C8) |
                         bus_wram_read16(0x04CA);
    if (mouse_any != 0) goto end_demo;

    /* Read demo data from ROM.
     * Demo data is in banks $10-$12, structured as 3 parallel streams
     * (buttons, Y displacement, X displacement) indexed by $04E6. */
    uint16_t data_idx = bus_wram_read16(0x04E6);
    uint16_t bank_offset = bus_wram_read16(0x04E8);

    /* Check for delay frames */
    uint16_t delay = bus_wram_read16(0x04EA);
    if (delay != 0) {
        /* Still in delay — clear mouse state */
        bus_wram_write8(0x04CA, 0x00);
        bus_wram_write8(0x04C8, 0x00);
        bus_wram_write8(0x04C6, 0x00);
        bus_wram_write16(0x04EA, delay - 1);
        goto advance;
    }

    /* Read next demo frame */
    {
        uint8_t bank1 = 0x10 + (uint8_t)bank_offset;
        uint8_t bank2 = 0x11 + (uint8_t)bank_offset;
        uint8_t bank3 = 0x12 + (uint8_t)bank_offset;

        uint8_t cmd = bus_read8(bank1, 0x8000 + data_idx);

        if (cmd & 0x80) {
            /* Delay command */
            uint8_t delay_y = bus_read8(bank2, 0x8000 + data_idx);
            uint8_t delay_x = bus_read8(bank3, 0x8000 + data_idx);
            bus_wram_write8(0x04EB, delay_y);
            bus_wram_write16(0x04EA, (uint16_t)delay_x);
            bus_wram_write8(0x04CA, 0x00);
            bus_wram_write8(0x04C8, 0x00);
            bus_wram_write8(0x04C6, 0x00);
        } else {
            /* Normal frame: buttons, Y disp, X disp */
            bus_wram_write8(0x04CA, cmd);
            bus_wram_write8(0x04C8, bus_read8(bank2, 0x8000 + data_idx));
            bus_wram_write8(0x04C6, bus_read8(bank3, 0x8000 + data_idx));
        }
    }

advance:
    /* Advance data pointer */
    {
        uint16_t idx = bus_wram_read16(0x04E6) + 1;
        idx &= 0x7FFF;
        if (idx == 0) {
            /* Bank boundary — flip bank offset */
            uint16_t bo = bus_wram_read16(0x04E8);
            bo ^= 0x0003;
            bus_wram_write16(0x04E8, bo);
        }
        bus_wram_write16(0x04E6, idx);

        /* Check for end of demo data */
        uint8_t demo_num = bus_wram_read8(0x0008);
        static const uint16_t demo_ends[5] = {
            0x1938, 0x3631, 0x56CD, 0x5B4D, 0x7E4D
        };
        if (demo_num < 5 && idx >= demo_ends[demo_num]) {
            goto end_demo;
        }
    }
    return;

end_demo:
    /* End demo — advance to next demo, fade out, reset */
    {
        uint8_t demo_num = bus_wram_read8(0x0008);
        demo_num++;
        if (demo_num >= 5) demo_num = 0;
        bus_wram_write8(0x0008, demo_num);

        bus_wram_write16(0x04E2, 0x0000);  /* Demo no longer active */

        mp_01E7C9();  /* Fade out */

        /* Jump to soft reset (CODE_0082B8) */
        func_table_call(0x0082B8);
    }
}

/* ========================================================================
 * $0F:C00E — Start demo playback
 *
 * Initializes demo state and sets the demo active flag.
 * ======================================================================== */
void mp_0FC00E(void) {
    bus_wram_write16(0x0002, 0x0000);
    bus_wram_write16(0x04E8, 0x0000);
    bus_wram_write16(0x04EA, 0x0000);

    /* Look up demo start offset */
    uint8_t demo_num = bus_wram_read8(0x0008);
    static const uint16_t demo_starts[5] = { 0, 0x1937, 0x3630, 0x56CC, 0x5B4C };
    if (demo_num < 5) {
        bus_wram_write16(0x04E6, demo_starts[demo_num]);
    }

    bus_wram_write16(0x04E2, 0x0001);  /* Demo active */
}

/* ========================================================================
 * $00:8BEB — Canvas click handler (state 0 post-logic)
 *
 * Main click/interaction handler for the drawing canvas.
 * Processes mouse clicks, determines which tool area was clicked
 * (canvas, toolbar, palette), and dispatches to the appropriate
 * tool handler. Very complex with many sub-calls.
 * ======================================================================== */
void mp_008BEB(void) {
    uint8_t draw_state = bus_wram_read8(0x0020);

    if (draw_state != 0) {
        /* Drawing in progress — check tool-specific logic */
        uint16_t tool = bus_wram_read16(0x04D0) & 0x00FF;
        if (tool == 0x0006) {
            /* Stamp tool — special handling */
            goto stamp_logic;
        }

        func_table_call(0x0091C7);  /* Check draw continuation */
        if (g_cpu.flag_C) {
            /* Still drawing */
            uint16_t buttons = bus_wram_read16(0x04CA);
            if (buttons & 0x0010) return;  /* Button held */

            bus_wram_write8(0x0020, 0x00);

            /* Check tool sound */
            tool = bus_wram_read16(0x04D0) & 0x00FF;
            if (tool < 0x0005) {
                op_lda_imm16(0x0018);
                func_table_call(0x01D368);
            }

            uint16_t ae = bus_wram_read16(0x00AE);
            if (ae != 0) {
                func_table_call(0x008B18);  /* Undo buffer management */
            }
            return;
        }

        func_table_call(0x009564);  /* End of draw */
        return;
    }

    /* Not drawing — check for new input */
    bus_wram_write16(0x053B, 0x0000);
    func_table_call(0x009001);  /* Update tool state */

    uint16_t buttons = bus_wram_read16(0x04CA);
    if (!(buttons & 0x0011)) return;  /* No click */

    /* Check if cursor is in special zone */
    uint16_t special = bus_wram_read16(0x19AA);
    if (special != 0) goto check_toolbar;

    /* Check cursor Y position for zone determination */
    uint16_t cy = bus_wram_read16(0x04DE);
    if (cy < 0x0018) {
        /* Top area — toolbar click dispatch */
        func_table_call(0x008D2C);  /* Determine toolbar slot */
        func_table_call(0x009598);  /* Execute toolbar action */
        return;
    }
    if (cy >= 0x00C8) {
        /* Bottom area — palette/status bar */
        func_table_call(0x008E0B);
        func_table_call(0x0095E7);
        return;
    }
    return;  /* Canvas area — no default click action (tools handle it) */

check_toolbar:
    {
        /* P2 input check */
        uint16_t p2 = bus_wram_read16(0x099F);
        if (p2 != 0) {
            uint16_t tool = bus_wram_read16(0x00AA);
            if (tool != 0x0007) {
                if (buttons & 0x0002) {
                    func_table_call(0x009760);
                    return;
                }
            }
        }

        if (buttons & 0x0020) {
            func_table_call(0x008B03);
            func_table_call(0x009564);
        }
    }
    return;

stamp_logic:
    {
        uint16_t p2 = bus_wram_read16(0x099F);
        if (p2 == 0) goto stamp_canvas;

        uint16_t tool = bus_wram_read16(0x00AA);
        if (tool == 0x0007) goto stamp_canvas;

        uint16_t cy2 = bus_wram_read16(0x04DE);
        if (cy2 >= 0x00C8) goto stamp_canvas;

        uint16_t btns = bus_wram_read16(0x04CA);
        if (btns & 0x0002) {
            func_table_call(0x009760);
            return;
        }

stamp_canvas:
        ;
        uint16_t cy3 = bus_wram_read16(0x04DE);
        if (cy3 < 0x00C8) {
            /* In canvas — check draw state */
            draw_state = bus_wram_read8(0x0020);
            if (draw_state == 0x03) {
                func_table_call(0x009001);
                uint16_t sp = bus_wram_read16(0x19AA);
                if (sp == 0) {
                    uint16_t btns2 = bus_wram_read16(0x04CA);
                    if (btns2 & 0x0020) {
                        goto tool_action;
                    }
                }
                func_table_call(0x009564);
                return;
            }
            func_table_call(0x0091C7);
            if (!g_cpu.flag_C) {
                func_table_call(0x009564);
                return;
            }
            uint16_t btns3 = bus_wram_read16(0x04CA);
            if (btns3 & 0x0010) return;
        }

tool_action:
        op_lda_imm16(0x0007);
        func_table_call(0x01D368);
        bus_wram_write8(0x0020, 0x00);
    }
}

/* ========================================================================
 * $00:8B40 — Wrapper: calls 008BEB (canvas click handler)
 * ======================================================================== */
void mp_008B40(void) {
    mp_008BEB();
}

/* ========================================================================
 * $00:8B44 — Wrapper: calls 009378 (cursor rendering)
 * ======================================================================== */
void mp_008B44(void) {
    mp_009378();
}

/* ========================================================================
 * Register all title loop and Bank 0F functions.
 * ======================================================================== */
void mp_register_titleloop(void) {
    func_table_register(0x018260, mp_018260);
    func_table_register(0x0182B1, mp_0182B1);
    func_table_register(0x0182F6, mp_0182F6);
    func_table_register(0x018308, mp_018308);
    func_table_register(0x018C43, mp_018C43);
    func_table_register(0x018CB7, mp_018CB7);
    func_table_register(0x018CBF, mp_018CBF);
    func_table_register(0x0FC000, mp_0FC000);
    func_table_register(0x0FC00E, mp_0FC00E);
    func_table_register(0x008BEB, mp_008BEB);
    func_table_register(0x008B40, mp_008B40);
    func_table_register(0x008B44, mp_008B44);
}
