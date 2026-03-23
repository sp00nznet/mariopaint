/*
 * Mario Paint — Recompiled input and cursor routines.
 *
 * Input chain (per frame, from NMI handler):
 *   mp_01D9E1 — mouse data read: gets displacement + button state
 *   mp_00815B — cursor sprite animation (clock icon)
 *   mp_008187 — additional cursor animation (shaking)
 *
 * Cursor movement (per frame, from main loop):
 *   mp_008B48 — applies mouse displacement to cursor position
 *
 * The original 65816 code reads mouse data bit-by-bit from the
 * serial port ($4016). We use the snesrecomp mouse API directly
 * for reliability.
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <mp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

#include <string.h>

/* Mouse displacement and button state in WRAM */
#define MOUSE_X_DISP    0x04C6  /* X displacement (8-bit signed magnitude) */
#define MOUSE_X_DISP_HI 0x04C7
#define MOUSE_Y_DISP    0x04C8  /* Y displacement (8-bit signed magnitude) */
#define MOUSE_Y_DISP_HI 0x04C9
#define MOUSE_BUTTONS    0x04CA  /* Button state bitfield */
#define MOUSE_BUTTONS_HI 0x04CB
#define MOUSE_DETECT     0x04C2  /* Mouse detection flags */
#define MOUSE_ACTIVE     0x04BF  /* Reentrancy guard */
#define MOUSE_ENABLE     0x04C1  /* Mouse enabled flag */
#define MOUSE_SPEED_SAVE 0x04C4  /* Saved mouse speed */
#define MOUSE_SPEED_CTR  0x04C3  /* Speed change debounce counter */
#define MOUSE_BTN_PREV   0x04CC  /* Previous button state */
#define MOUSE_BTN_TIMER  0x04CE  /* Button repeat timer */

/* Cursor position in WRAM */
#define CURSOR_X         0x04DC
#define CURSOR_Y         0x04DE

/* Cursor bounds */
#define CURSOR_X_MIN     0x04D4
#define CURSOR_X_MAX     0x04D6
#define CURSOR_Y_MIN     0x04D8
#define CURSOR_Y_MAX     0x04DA

/* OAM buffer */
#define OAM_BUF          0x0226
#define OAM_HI_BUF       0x0426

/* Held/pressed/repeat button state */
#define HELD_P1          0x0132
#define PRESSED_P1       0x013A
#define REPEAT_P1        0x0142

/* ========================================================================
 * $01:D9E1 — Mouse data read + button state computation
 *
 * Called from NMI handler. Reads mouse displacement and buttons,
 * computes the button bitfield at $04CA.
 *
 * Original code reads serial port bit-by-bit. We use the snesrecomp
 * mouse API instead for direct access to dx/dy/buttons.
 * ======================================================================== */
void mp_01D9E1(void) {
    /* Reentrancy guard */
    if (bus_wram_read8(MOUSE_ACTIVE) != 0) return;
    bus_wram_write8(MOUSE_ACTIVE, 0x01);

    /* Check if mouse is enabled */
    if (bus_wram_read8(MOUSE_ENABLE) == 0) {
        /* Mouse disabled — clear all mouse state */
        bus_wram_write8(MOUSE_BUTTONS, 0x00);
        bus_wram_write8(MOUSE_X_DISP, 0x00);
        bus_wram_write8(MOUSE_Y_DISP, 0x00);
        bus_wram_write8(MOUSE_BUTTONS_HI, 0x00);
        bus_wram_write8(MOUSE_X_DISP_HI, 0x00);
        bus_wram_write8(MOUSE_Y_DISP_HI, 0x00);
        bus_wram_write8(MOUSE_ACTIVE, 0x00);
        return;
    }

    /* Get mouse state from snesrecomp */
    SnesMouseState *ms = recomp_input_get_mouse(1);

    if (ms == NULL) {
        /* No mouse on port 1 — clear state */
        bus_wram_write8(MOUSE_BUTTONS, 0x00);
        bus_wram_write8(MOUSE_X_DISP, 0x00);
        bus_wram_write8(MOUSE_Y_DISP, 0x00);
        bus_wram_write8(MOUSE_ACTIVE, 0x00);
        return;
    }

    /* Mark mouse detected on port 1 */
    bus_wram_write8(MOUSE_DETECT, 0x01);

    /*
     * Convert SDL mouse delta to SNES mouse format.
     *
     * SNES mouse displacement is signed magnitude:
     *   bit 7 = direction (1 = left/up, 0 = right/down)
     *   bits 6-0 = magnitude (0-127)
     *
     * Mario Paint reads this and uses bit 7 as sign extend:
     *   if (disp & 0x80) disp |= 0xFF00  (sign extend to 16-bit)
     */
    /*
     * Use absolute mouse position for accurate cursor tracking.
     * Compute displacement as the difference from previous SNES position.
     * This eliminates drift from relative delta rounding errors.
     */
    static int prev_snes_x = 128, prev_snes_y = 128;
    int dx = 0, dy = 0;

    if (ms->abs_valid) {
        dx = ms->abs_x - prev_snes_x;
        dy = ms->abs_y - prev_snes_y;
        prev_snes_x = ms->abs_x;
        prev_snes_y = ms->abs_y;
    }

    /* Clamp to -127..+127 */
    if (dx > 127) dx = 127;
    if (dx < -127) dx = -127;
    if (dy > 127) dy = 127;
    if (dy < -127) dy = -127;

    /* Convert to SNES signed-magnitude format */
    uint8_t snes_dx, snes_dy;
    if (dx < 0) {
        snes_dx = 0x80 | (uint8_t)(-dx);
    } else {
        snes_dx = (uint8_t)dx;
    }
    if (dy < 0) {
        snes_dy = 0x80 | (uint8_t)(-dy);
    } else {
        snes_dy = (uint8_t)dy;
    }

    bus_wram_write8(MOUSE_X_DISP, snes_dx);
    bus_wram_write8(MOUSE_Y_DISP, snes_dy);

    /*
     * Build button state at $04CA.
     *
     * SNES mouse auto-joypad format (bits 0-15 of 32-bit serial):
     *   Bits 0-7:  Signature ($00 = mouse)
     *   Bit  8:    Right button
     *   Bit  9:    Left button
     *   Bits 10-11: Speed/sensitivity
     *   Bits 12-15: Unused
     *
     * The $04CA bitfield layout (from the disassembly):
     *   bit 0: left held
     *   bit 1: left pressed (new this frame)
     *   bit 2: left repeat
     *   bit 3: (unused)
     *   bit 4: right held (or left pressed in some contexts)
     *   bit 5: right held (triggers click actions)
     *
     * We read directly from the snesrecomp mouse state since
     * auto-joypad has mouse-format bits, not joypad-format bits.
     */
    uint8_t btn = 0;

    /* Current frame button state */
    bool left_held = ms->left;
    bool right_held = ms->right;

    /* Previous frame state for pressed detection */
    static bool prev_left = false;
    static bool prev_right = false;

    bool left_pressed = left_held && !prev_left;
    bool right_pressed = right_held && !prev_right;

    /* Build the $04CA bitfield matching what the original game expects:
     * Bit 0: left held       (used for draw-while-held checks)
     * Bit 1: left pressed    (new click this frame)
     * Bit 4: left held       (used for "button held" checks in draw logic)
     * Bit 5: left pressed    (used for "click" checks — toolbar, palette)
     * Right button: bit 1 of high nibble area */
    if (left_held)    btn |= 0x11;  /* bits 0 and 4: held */
    if (left_pressed) btn |= 0x22;  /* bits 1 and 5: pressed/click */
    if (right_held)   btn |= 0x02;  /* bit 1: right held */
    if (right_pressed) btn |= 0x04; /* bit 2: right pressed */

    prev_left = left_held;
    prev_right = right_held;

    /* Button repeat/hold logic (from CODE_01DABF) */
    uint8_t prev_btn = bus_wram_read8(MOUSE_BTN_PREV);
    uint8_t timer = bus_wram_read8(MOUSE_BTN_TIMER);

    if (timer == 0) {
        /* No active hold — check for new button press */
        uint8_t held_mask = btn & 0x22;  /* held bits for left+right */
        if (held_mask != 0) {
            bus_wram_write8(MOUSE_BTN_PREV, held_mask);
            bus_wram_write8(MOUSE_BTN_TIMER, 0x20);
        }
    } else {
        /* Timer active */
        if (snes_dx != 0 || snes_dy != 0) {
            /* Mouse moved — cancel repeat */
            bus_wram_write8(MOUSE_BTN_TIMER, 0x00);
            bus_wram_write8(MOUSE_BTN_PREV, 0x00);
        } else {
            timer--;
            bus_wram_write8(MOUSE_BTN_TIMER, timer);
            uint8_t held_mask = btn & 0x22;
            uint8_t matched = held_mask & prev_btn;
            if (timer == 0 && matched != 0) {
                /* Fire repeat: promote held to repeat bits */
                btn |= (matched << 2);
            }
        }
    }

    bus_wram_write8(MOUSE_BUTTONS, btn);

    /* Save mouse speed */
    bus_wram_write8(MOUSE_SPEED_SAVE, ms->speed);

    /* Clear reentrancy guard */
    bus_wram_write8(MOUSE_ACTIVE, 0x00);
}

/* ========================================================================
 * $00:815B — Cursor sprite animation (clock icon)
 *
 * If clock cursor is active ($09A7 != 0), updates the cursor sprite's
 * tile based on frame counter animation.
 * ======================================================================== */
void mp_00815B(void) {
    if (bus_wram_read8(0x09A7) == 0) return;

    /* Animate cursor: pick tile based on (frame_counter >> 4) & 6 */
    uint8_t frame = bus_wram_read8(0x016C);
    uint8_t idx = (frame >> 4) & 0x06;

    /* Data table: tile/prop pairs for 4 animation frames */
    static const uint8_t anim_data[8] = {
        0x0C, 0x31,  /* frame 0 */
        0x0E, 0x31,  /* frame 1 */
        0x2C, 0x31,  /* frame 2 */
        0x2E, 0x31,  /* frame 3 */
    };

    /* Set OAM sprite 0 tile and properties */
    bus_wram_write8(OAM_BUF + 2, anim_data[idx]);      /* Tile */
    bus_wram_write8(OAM_BUF + 3, anim_data[idx + 1]);  /* Prop */

    /* Set upper OAM bit (large sprite flag) */
    uint8_t upper = bus_wram_read8(OAM_HI_BUF);
    upper |= 0x02;
    bus_wram_write8(OAM_HI_BUF, upper);
}

/* ========================================================================
 * $00:8187 — Additional cursor animation (shaking effect)
 *
 * If $1B1F is set and timer $1B20 expired, applies a position
 * offset to the cursor sprite for a "shake" effect.
 * ======================================================================== */
void mp_008187(void) {
    if (bus_wram_read8(0x1B1F) == 0) return;
    if ((int8_t)bus_wram_read8(0x1B20) >= 0) return;

    /* Advance animation state */
    uint8_t state = bus_wram_read8(0x1B21);
    state += 4;
    if (state >= 8) state = 0;
    bus_wram_write8(0x1B21, state);

    /* Shake data: dx, dy, tile, timer for each state */
    static const uint8_t shake_data[8] = {
        0x00, 0x00, 0x24, 0x10,  /* state 0 */
        0x00, 0x00, 0x26, 0x10,  /* state 1 */
    };

    /* Apply offset to cursor sprite position */
    uint8_t x = bus_wram_read8(OAM_BUF + 0);
    x += shake_data[state];
    bus_wram_write8(OAM_BUF + 0, x);

    uint8_t y = bus_wram_read8(OAM_BUF + 1);
    y += shake_data[state + 1];
    bus_wram_write8(OAM_BUF + 1, y);

    bus_wram_write8(OAM_BUF + 2, shake_data[state + 2]);

    bus_wram_write8(0x1B20, shake_data[state + 3]);
}

/* ========================================================================
 * $00:81CA — Bomb icon animation
 *
 * Animates the bomb icon tile in VRAM when $058D is nonzero.
 * Writes 6 bytes to VRAM tilemap at $3321-$3322.
 * ======================================================================== */
void mp_0081CA(void) {
    if (bus_wram_read8(0x058D) == 0) return;

    /* Three animation frames of 6 tile bytes each */
    static const uint8_t bomb_normal[6]  = { 0xEC, 0xDC, 0xDD, 0xFC, 0xFD, 0xED };
    static const uint8_t bomb_blink1[6]  = { 0xEA, 0xDA, 0xDB, 0xFA, 0xFB, 0xEB };
    static const uint8_t bomb_blink2[6]  = { 0xE8, 0xD8, 0xD9, 0xF8, 0xF9, 0xE9 };

    const uint8_t *data = bomb_normal;
    if ((int8_t)bus_wram_read8(0x058D) >= 0) {
        /* Positive: alternate between blink frames based on frame counter */
        data = bomb_blink1;
        if (bus_wram_read8(0x016C) & 0x08) {
            data = bomb_blink2;
        }
    }

    /* Write 3 tiles to VRAM row at $3321 */
    bus_write8(0x00, 0x2115, 0x01);  /* VMAIN: increment after high byte */
    bus_write8(0x00, 0x2116, 0x21);  /* VMADDL */
    bus_write8(0x00, 0x2117, 0x33);  /* VMADDH */
    bus_write8(0x00, 0x2118, data[0]);
    bus_write8(0x00, 0x2118, data[1]);
    bus_write8(0x00, 0x2118, data[2]);

    /* Write 3 tiles to next row at $3322 */
    bus_write8(0x00, 0x2116, 0x22);
    bus_write8(0x00, 0x2117, 0x33);
    bus_write8(0x00, 0x2118, data[3]);
    bus_write8(0x00, 0x2118, data[4]);
    bus_write8(0x00, 0x2118, data[5]);
}

/* ========================================================================
 * $00:823C — Display animation
 *
 * Animates a display icon when in certain tool modes.
 * Similar to bomb animation but at VRAM $3329-$332A.
 * ======================================================================== */
void mp_00823C(void) {
    if (bus_wram_read8(0x0589) != 0) return;
    if (bus_wram_read8(0x00AA) != 0x07) return;

    static const uint8_t disp_frame1[6] = { 0x86, 0x76, 0x77, 0x96, 0x97, 0x87 };
    static const uint8_t disp_frame2[6] = { 0x88, 0x78, 0x79, 0x98, 0x99, 0x89 };

    const uint8_t *data = disp_frame1;
    if (bus_wram_read8(0x016C) & 0x10) {
        data = disp_frame2;
    }

    bus_write8(0x00, 0x2115, 0x01);
    bus_write8(0x00, 0x2116, 0x29);
    bus_write8(0x00, 0x2117, 0x33);
    bus_write8(0x00, 0x2118, data[0]);
    bus_write8(0x00, 0x2118, data[1]);
    bus_write8(0x00, 0x2118, data[2]);

    bus_write8(0x00, 0x2116, 0x2A);
    bus_write8(0x00, 0x2117, 0x33);
    bus_write8(0x00, 0x2118, data[3]);
    bus_write8(0x00, 0x2118, data[4]);
    bus_write8(0x00, 0x2118, data[5]);
}

/* ========================================================================
 * $00:8B48 — Cursor movement
 *
 * Applies mouse X/Y displacement to cursor position,
 * clamping to the configured screen bounds.
 * Also tracks whether the cursor moved (for button repeat logic).
 * ======================================================================== */
void mp_008B48(void) {
    /* Read X displacement — signed magnitude to two's complement */
    uint8_t raw_dx = bus_wram_read8(MOUSE_X_DISP);
    int16_t dx = raw_dx & 0x7F;
    if (raw_dx & 0x80) dx = -dx;  /* bit 7 = left */

    /* Apply to cursor X */
    int16_t cx = (int16_t)bus_wram_read16(CURSOR_X);
    int16_t new_cx = cx + dx;
    int16_t x_max = (int16_t)bus_wram_read16(CURSOR_X_MAX);
    int16_t x_min = (int16_t)bus_wram_read16(CURSOR_X_MIN);
    if (new_cx < x_max && new_cx >= x_min) {
        bus_wram_write16(CURSOR_X, (uint16_t)new_cx);
    }

    /* Read Y displacement — signed magnitude to two's complement */
    uint8_t raw_dy = bus_wram_read8(MOUSE_Y_DISP);
    int16_t dy = raw_dy & 0x7F;
    if (raw_dy & 0x80) dy = -dy;  /* bit 7 = up */

    /* Apply to cursor Y */
    int16_t cy = (int16_t)bus_wram_read16(CURSOR_Y);
    int16_t new_cy = cy + dy;
    int16_t y_max = (int16_t)bus_wram_read16(CURSOR_Y_MAX);
    int16_t y_min = (int16_t)bus_wram_read16(CURSOR_Y_MIN);
    if (new_cy < y_max && new_cy >= y_min) {
        bus_wram_write16(CURSOR_Y, (uint16_t)new_cy);
    }

    /* Track cursor movement for button repeat logic.
     * Y = 4 if no movement, Y = 1 if cursor moved. */
    uint16_t moved_y = 0x0004;
    if ((raw_dx & 0x7F) != 0 || (raw_dy & 0x7F) != 0) {
        moved_y = 0x0001;
    }

    /* Update $1B26/$1B28 movement detection state */
    uint16_t prev = bus_wram_read16(0x1B28);
    uint16_t edge = (moved_y ^ prev) & moved_y;
    uint16_t combined = (edge << 1) | moved_y;
    bus_wram_write16(0x1B26, combined);
    bus_wram_write16(0x1B28, moved_y);
}

/* ========================================================================
 * $01:DCB9 — Bomb timer animation
 *
 * Handles bomb countdown visual effect. Checks $1B1C and
 * animates a small sprite offset.
 * ======================================================================== */
void mp_01DCB9(void) {
    if (bus_wram_read8(0x1B1C) == 0) return;
    if ((int8_t)bus_wram_read8(0x1B1D) >= 0) return;

    /* Read animation state */
    uint8_t state = bus_wram_read8(0x1B1E);
    state += 4;
    if (state >= 8) state = 0;
    bus_wram_write8(0x1B1E, state);
}

/* ========================================================================
 * Register all input/cursor functions.
 * ======================================================================== */
void mp_register_input(void) {
    func_table_register(0x01D9E1, mp_01D9E1);
    func_table_register(0x00815B, mp_00815B);
    func_table_register(0x008187, mp_008187);
    func_table_register(0x0081CA, mp_0081CA);
    func_table_register(0x00823C, mp_00823C);
    func_table_register(0x008B48, mp_008B48);
    func_table_register(0x01DCB9, mp_01DCB9);
}
