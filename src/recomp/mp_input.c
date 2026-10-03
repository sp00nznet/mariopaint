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
 * mp_01D9E1 clocks the mouse serially out of $4016 exactly as the ROM does;
 * snesrecomp keeps the cursor locked to the host pointer by choosing the
 * deltas it reports (see recomp_input_set_mouse_cursor_addr in main.c).
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

#include <string.h>
#include <stdlib.h>
#include <snesrecomp/platform.h>

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
 * $01:D9E1 — Mouse read (NMI). Straight translation of $01:D9E1-$01:DB30.
 *
 * For each port (X = 1, then 0): if the auto-read signature nibble at
 * $4218+2X says "mouse", clock 16 more bits out of $4016+X into the Y/X
 * displacement bytes, convert them to two's complement, fold the auto-read
 * buttons ($0132/$013A/$0142 = held/pressed/repeat, built by $01:E747) into
 * $04CA+X, flag a double-click, and nudge the mouse's speed setting toward
 * $04C4+X.
 *
 * $04CA layout (bit 6 of $4218 is Left, bit 7 is Right):
 *   bit 4/5/6  left  held / pressed / double-click
 *   bit 0/1/2  right held / pressed / double-click
 *
 * Cursor locking to the host pointer happens in snesrecomp (main.c registers
 * the cursor address), so this routine can stay faithful to the ROM.
 * ======================================================================== */

/* $01:DABF — second press of the same button within $20 frames, without the
 * mouse moving in between, sets that button's double-click bit. */
static void mp_01DABF(int x) {
    uint8_t timer = bus_wram_read8(MOUSE_BTN_TIMER + x);
    uint8_t btn   = bus_wram_read8(MOUSE_BUTTONS + x);
    if (timer == 0) {
        if (btn & 0x22) {
            bus_wram_write8(MOUSE_BTN_PREV + x, btn & 0x22);
            bus_wram_write8(MOUSE_BTN_TIMER + x, 0x20);
        }
        return;
    }
    if ((bus_wram_read8(MOUSE_X_DISP + x) | bus_wram_read8(MOUSE_Y_DISP + x)) == 0) {
        bus_wram_write8(MOUSE_BTN_TIMER + x, timer - 1);
        uint8_t match = btn & 0x22 & bus_wram_read8(MOUSE_BTN_PREV + x);
        if (!match) return;
        bus_wram_write8(MOUSE_BUTTONS + x, btn | (uint8_t)(match << 2));
    }
    bus_wram_write8(MOUSE_BTN_TIMER + x, 0);
    bus_wram_write8(MOUSE_BTN_PREV + x, 0);
}

/* $01:DAF9 — cycle the mouse's sensitivity until it matches $04C4+X. Each
 * $01:DB25 strobe steps the speed; gives up after $1F tries. */
static void mp_01DAF9(int x) {
    for (;;) {
        uint8_t speed = (bus_read8(0x00, 0x4218 + 2 * x) >> 4) & 3;
        if (speed == bus_wram_read8(MOUSE_SPEED_SAVE + x)) break;
        uint8_t ctr = bus_wram_read8(MOUSE_SPEED_CTR);
        if (ctr & 0x80) ctr = 0x20;
        bus_wram_write8(MOUSE_SPEED_CTR, --ctr);
        if (ctr == 0) break;
        bus_write8(0x00, 0x4016, 1);                 /* $01:DB25 */
        bus_read8(0x00, 0x4016 + x);
        bus_write8(0x00, 0x4016, 0);
    }
    bus_wram_write8(MOUSE_SPEED_CTR, 0xFF);
}

/* $01:DA73 — sign-magnitude displacement byte to two's complement. */
static void mp_01DA73(uint16_t addr) {
    uint8_t v = bus_wram_read8(addr);
    bus_wram_write8(addr, (v & 0x80) ? (uint8_t)(-(v & 0x7F)) : (uint8_t)(v & 0x7F));
}

void mp_01D9E1(void) {
    if (bus_wram_read8(MOUSE_ACTIVE) != 0) return;
    bus_wram_write8(MOUSE_ACTIVE, 0x01);

    if (bus_wram_read8(MOUSE_ENABLE) == 0) {
        for (int i = 0; i < 2; i++) {
            bus_wram_write8(MOUSE_BUTTONS + i, 0);
            bus_wram_write8(MOUSE_X_DISP + i, 0);
            bus_wram_write8(MOUSE_Y_DISP + i, 0);
        }
        bus_wram_write8(MOUSE_ACTIVE, 0x00);
        return;
    }

    /* $01:DA0D — wait out the auto-read before touching $4218 or $4016.
     * Native-driven frames finish it before NMI, so this only spins in the
     * timed loop. */
    while ((bus_read8(0x00, 0x4212) & 1) && recomp_timed_spin(64)) {}
    bus_wram_write8(MOUSE_DETECT, 0);
    for (int i = 0; i < 2; i++) {
        bus_wram_write8(MOUSE_X_DISP + i, 0);
        bus_wram_write8(MOUSE_Y_DISP + i, 0);
        bus_wram_write8(MOUSE_BUTTONS + i, 0);
    }
    for (int x = 1; x >= 0; x--) {
        uint8_t sig = bus_read8(0x00, 0x4218 + 2 * x);
        bus_wram_write8(0x00DE, sig);
        if ((sig & 0x0F) != 0x01) {
            bus_wram_write8(MOUSE_BTN_PREV + x, 0);
            bus_wram_write8(MOUSE_BTN_TIMER + x, 0);
            continue;
        }
        bus_wram_write8(MOUSE_DETECT, bus_wram_read8(MOUSE_DETECT) | (uint8_t)(1 << x));

        /* 16 serial bits: first 8 land in $04C8+X (Y), next 8 in $04C6+X (X). */
        uint16_t bits = 0;
        for (int i = 0; i < 16; i++)
            bits = (uint16_t)((bits << 1) | (bus_read8(0x00, 0x4016 + x) & 1));
        bus_wram_write8(MOUSE_Y_DISP + x, (uint8_t)(bits >> 8));
        bus_wram_write8(MOUSE_X_DISP + x, (uint8_t)bits);
        mp_01DA73(MOUSE_X_DISP + x);
        mp_01DA73(MOUSE_Y_DISP + x);

        /* $01:DA88 */
        uint8_t held = bus_wram_read8(HELD_P1 + 2 * x);
        uint8_t pres = bus_wram_read8(PRESSED_P1 + 2 * x);
        uint8_t rept = bus_wram_read8(REPEAT_P1 + 2 * x);
        uint8_t btn = (uint8_t)(((rept >> 6) & 1) << 6 | ((pres >> 6) & 1) << 5 | ((held >> 6) & 1) << 4 |
                                ((rept >> 7) & 1) << 2 | ((pres >> 7) & 1) << 1 | ((held >> 7) & 1));
        bus_wram_write8(MOUSE_BUTTONS + x, btn);
        mp_01DABF(x);
        mp_01DAF9(x);
    }

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
 * Toolbar icon animators. Both write a 6-byte frame from a ROM table as
 * three tilemap bytes at VRAM word vaddr and three at vaddr+1 (VMAIN=$01,
 * low bytes only). The table address goes through PHY ... PLY and PLP then
 * restores P, so Y returns holding the table address (low byte only while X
 * is 8-bit) and A's low byte is the last tile written.
 * ======================================================================== */
static void icon_frame(uint16_t vaddr, uint16_t rom_tbl) {
    bus_write8(0x00, 0x2115, 0x01);
    for (int row = 0; row < 2; row++) {
        bus_write8(0x00, 0x2116, (uint8_t)(vaddr + row));
        bus_write8(0x00, 0x2117, (uint8_t)((vaddr + row) >> 8));
        for (int i = 0; i < 3; i++) {
            uint8_t t = bus_read8(0x00, rom_tbl + row * 3 + i);
            bus_write8(0x00, 0x2118, t);
            CPU_SET_A8(t);
        }
    }
    g_cpu.Y = g_cpu.flag_X ? (rom_tbl & 0xFF) : rom_tbl;
}

/* $00:81CA — Eraser bomb icon. $058D == 0: nothing. Negative: the still frame
 * ($00:822A). Positive: blink between $00:8230 and $00:8236 on bit 3 of the
 * frame counter $016C. */
void mp_0081CA(void) {
    uint8_t st = bus_wram_read8(0x058D);
    CPU_SET_A8(st);
    g_cpu.flag_Z = st == 0;
    g_cpu.flag_N = (st & 0x80) != 0;
    if (st == 0) return;
    uint16_t tbl = 0x822A;
    if (!(st & 0x80))
        tbl = (bus_wram_read8(0x016C) & 0x08) ? 0x8236 : 0x8230;
    icon_frame(0x3321, tbl);
}

/* $00:823C — Animated icon shown while tool $AA == 7 and $0589 == 0:
 * $00:829B / $00:82A1 alternating on bit 4 of $016C. */
void mp_00823C(void) {
    uint8_t v = bus_wram_read8(0x0589);
    if (v == 0) {
        v = bus_wram_read8(0x00AA);
        g_cpu.flag_C = v >= 0x07;
        if (v == 0x07) {
            g_cpu.flag_Z = true; g_cpu.flag_N = false;
            icon_frame(0x3329, (bus_wram_read8(0x016C) & 0x10) ? 0x82A1 : 0x829B);
            return;
        }
        CPU_SET_A8(v);
        g_cpu.flag_Z = false;
        g_cpu.flag_N = (uint8_t)(v - 0x07) & 0x80;
        return;
    }
    CPU_SET_A8(v);
    g_cpu.flag_Z = false;
    g_cpu.flag_N = (v & 0x80) != 0;
}

/* ========================================================================
 * $00:8B48 — Apply the mouse displacement to the cursor.
 *
 * $04C6/$04C8 are two's complement here ($01:D9E1 converted them). A step
 * that would leave [$04D4,$04D6) / [$04D8,$04DA) is dropped, not clamped.
 * $1B26/$1B28 get 1 (moved) or 4 (still), with bit 1 set on the edge.
 * ======================================================================== */
void mp_008B48(void) {
    int16_t cx = (int16_t)(bus_wram_read16(CURSOR_X) + (int8_t)bus_wram_read8(MOUSE_X_DISP));
    if ((int16_t)(cx - bus_wram_read16(CURSOR_X_MAX)) < 0 && (int16_t)(cx - bus_wram_read16(CURSOR_X_MIN)) >= 0)
        bus_wram_write16(CURSOR_X, (uint16_t)cx);
    int16_t cy = (int16_t)(bus_wram_read16(CURSOR_Y) + (int8_t)bus_wram_read8(MOUSE_Y_DISP));
    if ((int16_t)(cy - bus_wram_read16(CURSOR_Y_MAX)) < 0 && (int16_t)(cy - bus_wram_read16(CURSOR_Y_MIN)) >= 0)
        bus_wram_write16(CURSOR_Y, (uint16_t)cy);

    uint16_t moved = (bus_wram_read8(MOUSE_X_DISP) | bus_wram_read8(MOUSE_Y_DISP)) ? 1 : 4;
    uint16_t v = (uint16_t)((((moved ^ bus_wram_read16(0x1B28)) & moved) << 1) | moved);
    bus_wram_write16(0x1B26, v);
    bus_wram_write16(0x1B28, v);
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
