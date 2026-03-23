/*
 * Mario Paint — Canvas interaction routines and title screen states.
 *
 * These routines handle cursor hit-testing, toolbar/palette clicks,
 * drawing tool dispatch, and title screen animation states.
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <mp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

#include <stdbool.h>

extern bool g_quit;

/* ========================================================================
 * $00:9001 — Tool state update
 *
 * Calls $0091C7 to check cursor position, then determines which
 * toolbar zone the cursor is in and updates tool state.
 * ======================================================================== */
void mp_009001(void) {
    func_table_call(0x0091C7);
    if (!g_cpu.flag_C) return;

    /* Cursor is in a valid zone — update state */
    bus_wram_write16(0x19A6, 0x0000);
    bus_wram_write16(0x19AA, 0x0000);

    /* Determine page and zone table */
    uint16_t page = bus_wram_read16(0x19FA);
    uint16_t tbl_idx = 0;
    if (page != 0) {
        tbl_idx = bus_wram_read16(0x19C2) + 1;
    }
    tbl_idx *= 2;

    /* Read zone table pointer from DATA_00926C */
    uint16_t zone_ptr = bus_read16(0x00, 0x926C + tbl_idx);

    /* Read number of zones and zone boundaries */
    uint16_t num_zones = bus_read16(0x00, zone_ptr);
    uint16_t y_check_start = num_zones * 4 + 4;

    /* Find which zone the cursor Y is in */
    uint16_t cursor_y = bus_wram_read16(0x04DE);
    uint16_t zone = 0;

    for (uint16_t i = 0; i < num_zones; i++) {
        uint16_t ofs = 4 + i * 4;
        uint16_t y_min = bus_read16(0x00, zone_ptr + ofs);
        uint16_t y_max = bus_read16(0x00, zone_ptr + ofs + 2);
        if (cursor_y >= y_min && cursor_y < y_max) {
            zone = i + 1;
            break;
        }
    }

    /* Store tool zone for click dispatch */
    bus_wram_write16(0x19A6, zone);
    bus_wram_write16(0x19AA, zone);
}

/* ========================================================================
 * $00:91C7 — Cursor zone check
 *
 * Checks if the cursor is in a clickable zone. Returns carry set
 * if cursor is in a valid zone, carry clear if not.
 * ======================================================================== */
void mp_0091C7(void) {
    uint16_t zone = bus_wram_read16(0x19AA);
    if (zone == 0) {
        g_cpu.flag_C = true;
        return;
    }

    /* Zone is active — do bounds checking */
    uint16_t page = bus_wram_read16(0x19FA);
    uint16_t tbl_idx = 0;
    if (page != 0) {
        tbl_idx = bus_wram_read16(0x19C2) + 1;
    }
    tbl_idx *= 2;

    uint16_t zone_ptr = bus_read16(0x00, 0x926C + tbl_idx);

    uint16_t zone_ofs = (bus_wram_read16(0x19A6) & 0xFF) * 4 + 4;

    /* Check cursor still within zone bounds */
    uint16_t tool = bus_wram_read16(0x04D0) & 0xFF;
    uint16_t cursor_x = bus_wram_read16(0x04DC);

    if (tool >= 5 && tool != 7) {
        cursor_x += 8;
    }

    uint16_t x_min = bus_read16(0x00, zone_ptr + zone_ofs);
    uint16_t x_max = bus_read16(0x00, zone_ptr + zone_ofs + 2);

    if ((int16_t)cursor_x < (int16_t)x_min || (int16_t)cursor_x >= (int16_t)x_max) {
        g_cpu.flag_C = false;
        return;
    }

    g_cpu.flag_C = true;
}

/* ========================================================================
 * $00:9564 — Drawing tool end/dispatch
 *
 * Dispatches to the appropriate drawing end handler based on
 * the current tool mode ($04D0). Uses a jump table.
 * ======================================================================== */
void mp_009564(void) {
    uint16_t tool = bus_wram_read16(0x04D0) & 0xFF;
    int16_t idx = (int16_t)tool - 4;
    if (idx < 0) idx = 0;
    if (idx == 0x000C) idx = 2;

    uint16_t x567 = bus_wram_read16(0x0567);
    if (x567 != 0) idx = 5;

    /* 8-entry dispatch table */
    static const uint32_t dispatch[] = {
        0x009C25, 0x009DAB, 0x009DFB,
        0x009C25, 0x009C25,
        0x0094E5, 0x0094E5, 0x0094E5
    };

    if (idx < 8) {
        func_table_call(dispatch[idx]);
    }
}

/* ========================================================================
 * $00:9598 — Toolbar click handler
 *
 * Dispatches toolbar clicks to the appropriate handler based on
 * the tool slot clicked ($A2) and current tool state.
 * ======================================================================== */
void mp_009598(void) {
    uint16_t tool = bus_wram_read16(0x04D0) & 0xFF;
    if (tool == 0x0006) return;  /* Stamp mode — ignore toolbar */

    uint16_t erase = bus_wram_read16(0x1992);
    if (erase != 0) {
        /* Erase tool active — use erase dispatch table */
        uint16_t slot = bus_wram_read16(0x00A2);
        static const uint32_t erase_tbl[] = {
            0x0094E5, 0x00A509, 0x00A55B, 0x00A617,
            0x00A637, 0x00A6F7, 0x00A792, 0x00A8A8,
            0x00A8FF, 0x00A95D, 0x00A9BE, 0x0094E5
        };
        if (slot < 12) func_table_call(erase_tbl[slot]);
        return;
    }

    uint16_t eb8 = bus_wram_read16(0x0EB8);
    if (eb8 != 0) {
        /* Special mode — alternate dispatch */
        uint16_t slot = bus_wram_read16(0x00A2);
        static const uint32_t alt_tbl[] = { 0x0094E5, 0x00E817, 0x00E87A };
        if (slot < 3) func_table_call(alt_tbl[slot]);
        return;
    }

    /* Normal toolbar dispatch */
    uint16_t slot = bus_wram_read16(0x00A2);
    static const uint32_t normal_tbl[] = { 0x0094E5, 0x009853, 0x00987B };
    if (slot < 3) func_table_call(normal_tbl[slot]);
}

/* ========================================================================
 * $00:95E7 — Palette/status bar click handler
 *
 * Dispatches palette area clicks based on slot ($A4).
 * ======================================================================== */
void mp_0095E7(void) {
    uint16_t slot = bus_wram_read16(0x00A4);
    /* Dispatch table for palette/status bar clicks */
    static const uint32_t dispatch[] = {
        0x0094E5, 0x0098C3, 0x0098C3, 0x0098C3, 0x0098F8
    };
    if (slot < 5) {
        func_table_call(dispatch[slot]);
    }
}

/* ========================================================================
 * $00:9760 — Right-click / P2 button handler
 *
 * Handles right-click or player 2 button actions.
 * ======================================================================== */
void mp_009760(void) {
    /* Complex handler — dispatch to sub-routines */
    func_table_call(0x009760);  /* Will be a no-op until self-registered */
}

/* ========================================================================
 * $00:8D2C — Toolbar zone hit test
 *
 * Determines which toolbar slot the cursor is over.
 * Stores result in $A2 (toolbar slot index).
 * ======================================================================== */
void mp_008D2C(void) {
    /* Read cursor X position and determine toolbar slot */
    uint16_t cx = bus_wram_read16(0x04DC);
    uint16_t cy = bus_wram_read16(0x04DE);

    /* Toolbar zone data is in ROM — read from zone tables */
    /* For now, implement simplified zone detection */
    uint16_t slot = 0;

    /* Canvas area check */
    if (cy >= 0x001C && cy < 0x00C4) {
        slot = 1;  /* Canvas area */
    } else if (cy < 0x001C) {
        slot = 2;  /* Top toolbar */
    }

    bus_wram_write16(0x00A2, slot);
    g_cpu.X = slot;
    CPU_SET_A16(bus_wram_read16(0x04CA));
}

/* ========================================================================
 * $00:8E0B — Palette bar hit test
 *
 * Determines which palette slot the cursor is over.
 * Stores result in $A4 (palette slot index).
 * ======================================================================== */
void mp_008E0B(void) {
    uint16_t cx = bus_wram_read16(0x04DC);
    uint16_t cy = bus_wram_read16(0x04DE);

    uint16_t slot = 0;
    if (cy >= 0x00C8) {
        /* In palette bar — determine column */
        if (cx < 0x0040) slot = 1;
        else if (cx < 0x0080) slot = 2;
        else if (cx < 0x00C0) slot = 3;
        else slot = 4;
    }

    bus_wram_write16(0x00A4, slot);
    g_cpu.X = slot;
    CPU_SET_A16(bus_wram_read16(0x04CA));
}

/* ========================================================================
 * Title screen animation states (12 states)
 *
 * These are registered individually for the state machine dispatch.
 * Each handles one phase of the title screen animation.
 * ======================================================================== */

/* State 0: Initialize cursor sprite and logo sprite slot */
void mp_018328(void) {
    bus_write16(0x7F, 0x0005, 0x00EF);  /* Cursor sprite ID */
    op_lda_imm16(0x0000);
    mp_019DFE();  /* Init sprite slot 0 */
    CPU_SET_A16(0xFFFF);
    mp_018C43();  /* Draw all title sprites */
    uint16_t state = bus_read16(0x7F, 0x0001);
    bus_write16(0x7F, 0x0001, state + 1);
}

/* State 1: Animate logo + check for click */
void mp_01833F(void) {
    CPU_SET_A16(0xFFFF);
    mp_018C43();
    mp_018CB7();
    func_table_call(0x018D35);  /* Check for title click */
}

/* State 2+: Complex animation — dispatch via func_table_call */
void mp_01834B(void) { func_table_call(0x01834B); }

/* ========================================================================
 * $01:91FE — Sprite position setup with animation
 *
 * Sets up a sprite at screen position X,Y and runs its animation.
 * ======================================================================== */
void mp_0191FE(void) {
    /* Store position and call sprite setup */
    uint16_t x = g_cpu.X;
    uint16_t y = g_cpu.Y;

    /* The actual implementation reads sprite data and sets up
     * position in the sprite slot. For now dispatch. */
    func_table_call(0x0191FE);
}

/* ========================================================================
 * $01:934F — Frame processing helper
 *
 * Called during title screen animation to process a frame.
 * ======================================================================== */
void mp_01934F(void) {
    func_table_call(0x01934F);
}

/* ========================================================================
 * Register all interaction functions.
 * ======================================================================== */
void mp_register_interact(void) {
    func_table_register(0x009001, mp_009001);
    func_table_register(0x0091C7, mp_0091C7);
    func_table_register(0x009564, mp_009564);
    func_table_register(0x009598, mp_009598);
    func_table_register(0x0095E7, mp_0095E7);
    func_table_register(0x008D2C, mp_008D2C);
    func_table_register(0x008E0B, mp_008E0B);
    func_table_register(0x018328, mp_018328);
    func_table_register(0x01833F, mp_01833F);
}
