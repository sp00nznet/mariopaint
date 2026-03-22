/*
 * Mario Paint — Recompiled Bank 01 helper routines.
 *
 * These are the critical DMA, PPU, and system subroutines that
 * everything in the game depends on. They handle:
 *   - OAM/VRAM/palette DMA transfers
 *   - PPU register mirror writeback
 *   - BG scroll updates
 *   - HDMA setup (windows, BG2 scroll effects)
 *   - Frame sync (VBlank wait)
 *   - Brightness control and joypad reading
 *   - OAM buffer management (sprite cleanup)
 *
 * Translated from the Mario Paint (JU) 65816 disassembly.
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <mp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

#include <stdio.h>
#include <stdbool.h>

/* Defined in main.c — set when window is closed */
extern bool g_quit;

/*
 * SNES hardware register addresses.
 */
#define REG_INIDISP     0x2100
#define REG_OBSEL       0x2101
#define REG_OAMADDL     0x2102
#define REG_OAMADDH     0x2103
#define REG_OAMDATA     0x2104
#define REG_BGMODE      0x2105
#define REG_MOSAIC      0x2106
#define REG_BG1SC       0x2107
#define REG_BG2SC       0x2108
#define REG_BG3SC       0x2109
#define REG_BG4SC       0x210A
#define REG_BG12NBA     0x210B
#define REG_BG34NBA     0x210C
#define REG_BG1HOFS     0x210D
#define REG_BG1VOFS     0x210E
#define REG_BG2HOFS     0x210F
#define REG_BG2VOFS     0x2110
#define REG_BG3HOFS     0x2111
#define REG_BG3VOFS     0x2112
#define REG_BG4HOFS     0x2113
#define REG_BG4VOFS     0x2114
#define REG_VMAIN       0x2115
#define REG_VMADDL      0x2116
#define REG_VMADDH      0x2117
#define REG_VMDATAL     0x2118
#define REG_VMDATAH     0x2119
#define REG_M7SEL       0x211A
#define REG_CGRAM_ADDR  0x2121
#define REG_CGRAM_DATA  0x2122

#define REG_W12SEL      0x2123
#define REG_W34SEL      0x2124
#define REG_WOBJSEL     0x2125
#define REG_WH0         0x2126
#define REG_WH1         0x2127
#define REG_WH2         0x2128
#define REG_WH3         0x2129
#define REG_WBGLOG      0x212A
#define REG_WOBJLOG     0x212B
#define REG_TM          0x212C
#define REG_TS          0x212D
#define REG_TMW         0x212E
#define REG_TSW         0x212F
#define REG_CGWSEL      0x2130
#define REG_CGADSUB     0x2131
#define REG_COLDATA     0x2132
#define REG_SETINI      0x2133

#define REG_NMITIMEN    0x4200
#define REG_HVBJOY      0x4212
#define REG_MDMAEN      0x420B
#define REG_HDMAEN      0x420C
#define REG_JOY1L       0x4218

/* DMA channel base addresses (each channel is 16 bytes apart) */
#define DMA_CH(n)       (0x4300 + (n) * 0x10)
#define DMA_PARAMS(n)   (DMA_CH(n) + 0)
#define DMA_DEST(n)     (DMA_CH(n) + 1)
#define DMA_SRCLO(n)    (DMA_CH(n) + 2)
#define DMA_SRCHI(n)    (DMA_CH(n) + 3)
#define DMA_SRCBANK(n)  (DMA_CH(n) + 4)
#define DMA_SIZELO(n)   (DMA_CH(n) + 5)
#define DMA_SIZEHI(n)   (DMA_CH(n) + 6)

/* OAM buffer in WRAM */
#define OAM_BUF         0x0226   /* 512 bytes: 128 sprites x 4 bytes */
#define OAM_BUF_HI      0x0426   /* 32 bytes: upper OAM table */

/* ========================================================================
 * $01:E024 — Fill OAM low table buffer with A (16-bit)
 *
 * Fills WRAM $2000-$27FF (relative to $7E) with A.
 * Called with A = fill value (e.g. $3DFE).
 * ======================================================================== */
void mp_01E024(void) {
    uint16_t val = CPU_A16();
    uint8_t *wram = bus_get_wram();
    for (uint32_t i = 0x2000; i < 0x2800; i += 2) {
        wram[i]     = (uint8_t)(val & 0xFF);
        wram[i + 1] = (uint8_t)(val >> 8);
    }
}

/* ========================================================================
 * $01:E033 — Fill OAM high table buffer with A (16-bit)
 *
 * Fills WRAM $2800-$2FFF with A.
 * ======================================================================== */
void mp_01E033(void) {
    uint16_t val = CPU_A16();
    uint8_t *wram = bus_get_wram();
    for (uint32_t i = 0x2800; i < 0x3000; i += 2) {
        wram[i]     = (uint8_t)(val & 0xFF);
        wram[i + 1] = (uint8_t)(val >> 8);
    }
}

/* ========================================================================
 * $01:E042 — Fill third OAM area with A (16-bit)
 *
 * Fills WRAM $3000-$37FF with A.
 * ======================================================================== */
void mp_01E042(void) {
    uint16_t val = CPU_A16();
    uint8_t *wram = bus_get_wram();
    for (uint32_t i = 0x3000; i < 0x3800; i += 2) {
        wram[i]     = (uint8_t)(val & 0xFF);
        wram[i + 1] = (uint8_t)(val >> 8);
    }
}

/* ========================================================================
 * $01:E060 — Fill WRAM block with A (16-bit)
 *
 * Fills Y words at $7E:X with accumulator value.
 * X = start offset in bank $7E, Y = byte count (must be even).
 * ======================================================================== */
void mp_01E060(void) {
    uint16_t val = CPU_A16();
    uint16_t start = g_cpu.X;
    uint16_t count = g_cpu.Y;
    uint8_t *wram = bus_get_wram();
    for (uint32_t i = 0; i < count; i += 2) {
        uint32_t addr = (uint32_t)start + i;
        wram[addr]     = (uint8_t)(val & 0xFF);
        wram[addr + 1] = (uint8_t)(val >> 8);
    }
}

/* ========================================================================
 * $01:E06F — Clear OAM buffer (all sprites offscreen)
 *
 * Sets all 128 OAM entries to Y=$F4, X=$00 (offscreen).
 * Sets upper OAM table to $5555 (all sprites small, no X bit 8).
 * Clears $0446 (OAM write index).
 * ======================================================================== */
void mp_01E06F(void) {
    /* Fill OAM low table: $F400 per entry (Y=$F4, X=$00) — puts sprites offscreen */
    for (uint16_t x = 0; x < 0x0200; x += 2) {
        bus_wram_write16(OAM_BUF + x, 0xF400);
    }
    /* Fill upper OAM table with $5555 */
    for (uint16_t x = 0; x < 0x0020; x += 2) {
        bus_wram_write16(OAM_BUF_HI + x, 0x5555);
    }
    /* Clear sprite write index */
    bus_wram_write16(0x0446, 0x0000);
}

/* ========================================================================
 * $01:E09B — Screen enable / OAM cleanup
 *
 * Marks unused OAM slots (from $0446 to end) as offscreen ($F400).
 * Fixes up the upper OAM table bits for the boundary sprite.
 * Resets $0446 to 0.
 * ======================================================================== */
void mp_01E09B(void) {
    uint16_t idx = bus_wram_read16(0x0446);

    if (idx < 0x0200) {
        /* Fill remaining OAM entries with $F400 (offscreen) */
        for (uint16_t x = idx; x < 0x0200; x += 2) {
            bus_wram_write16(OAM_BUF + x, 0xF400);
        }

        /* Fix up upper OAM table for the boundary */
        /* The boundary sprite index determines which upper OAM byte/bits to fix */
        uint16_t upper_x = (idx & 0xFFE0) >> 4;  /* (idx AND $FFE0) >> 4 = byte index */
        uint16_t bit_y = (idx & 0x001C) >> 1;     /* (idx AND $001C) >> 1 = bit table index */

        /* Data table for bit masks: $FFFF,$FFFC,$FFF0,$FFC0,$FF00,$FC00,$F000,$C000 */
        static const uint16_t masks[8] = {
            0xFFFF, 0xFFFC, 0xFFF0, 0xFFC0,
            0xFF00, 0xFC00, 0xF000, 0xC000
        };

        /* Apply mask: keep used bits as-is, set unused bits to $5555 pattern */
        uint16_t cur = bus_wram_read16(OAM_BUF_HI + upper_x);
        uint16_t mask = masks[bit_y];
        cur = (cur & ~mask) | (0x5555 & mask);
        bus_wram_write16(OAM_BUF_HI + upper_x, cur);

        /* Fill remaining upper OAM bytes with $5555 */
        for (uint16_t x = upper_x + 2; x < 0x0020; x += 2) {
            bus_wram_write16(OAM_BUF_HI + x, 0x5555);
        }
    }

    /* Reset sprite write index */
    bus_wram_write16(0x0446, 0x0000);
}

/* ========================================================================
 * $01:E103 — PPU register mirror writeback
 *
 * Copies all PPU register mirrors from WRAM to hardware registers.
 * This is called during NMI to update the PPU state.
 * ======================================================================== */
void mp_01E103(void) {
    bus_write8(0x00, REG_INIDISP, bus_wram_read8(0x0104));
    bus_write8(0x00, REG_OBSEL,   bus_wram_read8(0x0105));
    bus_write8(0x00, REG_BGMODE,  bus_wram_read8(0x0108));
    bus_write8(0x00, REG_MOSAIC,  bus_wram_read8(0x0109));
    bus_write8(0x00, REG_BG1SC,   bus_wram_read8(0x010A));
    bus_write8(0x00, REG_BG2SC,   bus_wram_read8(0x010B));
    bus_write8(0x00, REG_BG3SC,   bus_wram_read8(0x010C));
    bus_write8(0x00, REG_BG4SC,   bus_wram_read8(0x010D));
    bus_write8(0x00, REG_BG12NBA, bus_wram_read8(0x010E));
    bus_write8(0x00, REG_BG34NBA, bus_wram_read8(0x010F));
    bus_write8(0x00, REG_M7SEL,   bus_wram_read8(0x0110));
    bus_write8(0x00, REG_W12SEL,  bus_wram_read8(0x0111));
    bus_write8(0x00, REG_W34SEL,  bus_wram_read8(0x0112));
    bus_write8(0x00, REG_WOBJSEL, bus_wram_read8(0x0113));
    bus_write8(0x00, REG_WH0,     bus_wram_read8(0x0114));
    bus_write8(0x00, REG_WH1,     bus_wram_read8(0x0115));
    bus_write8(0x00, REG_WH2,     bus_wram_read8(0x0116));
    bus_write8(0x00, REG_WH3,     bus_wram_read8(0x0117));
    bus_write8(0x00, REG_WBGLOG,  bus_wram_read8(0x0118));
    bus_write8(0x00, REG_WOBJLOG, bus_wram_read8(0x0119));
    bus_write8(0x00, REG_TM,      bus_wram_read8(0x011A));
    bus_write8(0x00, REG_TMW,     bus_wram_read8(0x011C));
    bus_write8(0x00, REG_TS,      bus_wram_read8(0x011B));
    bus_write8(0x00, REG_TSW,     bus_wram_read8(0x011D));
    bus_write8(0x00, REG_CGWSEL,  bus_wram_read8(0x011E));
    bus_write8(0x00, REG_CGADSUB, bus_wram_read8(0x011F));
    bus_write8(0x00, REG_COLDATA, bus_wram_read8(0x0120));
    bus_write8(0x00, REG_SETINI,  bus_wram_read8(0x0121));

    /* Fall through to E1AB: BG scroll update */
    mp_01E1AB();
}

/* ========================================================================
 * $01:E1AB — BG scroll register writeback
 *
 * Copies BG1-BG4 scroll mirrors ($016E-$017D) to hardware.
 * SNES scroll registers are double-write (low byte then high byte).
 * ======================================================================== */
void mp_01E1AB(void) {
    /* BG1 horizontal scroll */
    bus_write8(0x00, REG_BG1HOFS, bus_wram_read8(0x016E));
    bus_write8(0x00, REG_BG1HOFS, bus_wram_read8(0x016F));
    /* BG1 vertical scroll */
    bus_write8(0x00, REG_BG1VOFS, bus_wram_read8(0x0170));
    bus_write8(0x00, REG_BG1VOFS, bus_wram_read8(0x0171));
    /* BG2 horizontal scroll */
    bus_write8(0x00, REG_BG2HOFS, bus_wram_read8(0x0172));
    bus_write8(0x00, REG_BG2HOFS, bus_wram_read8(0x0173));
    /* BG2 vertical scroll */
    bus_write8(0x00, REG_BG2VOFS, bus_wram_read8(0x0174));
    bus_write8(0x00, REG_BG2VOFS, bus_wram_read8(0x0175));
    /* BG3 horizontal scroll */
    bus_write8(0x00, REG_BG3HOFS, bus_wram_read8(0x0176));
    bus_write8(0x00, REG_BG3HOFS, bus_wram_read8(0x0177));
    /* BG3 vertical scroll */
    bus_write8(0x00, REG_BG3VOFS, bus_wram_read8(0x0178));
    bus_write8(0x00, REG_BG3VOFS, bus_wram_read8(0x0179));
    /* BG4 horizontal scroll */
    bus_write8(0x00, REG_BG4HOFS, bus_wram_read8(0x017A));
    bus_write8(0x00, REG_BG4HOFS, bus_wram_read8(0x017B));
    /* BG4 vertical scroll */
    bus_write8(0x00, REG_BG4VOFS, bus_wram_read8(0x017C));
    bus_write8(0x00, REG_BG4VOFS, bus_wram_read8(0x017D));
}

/* ========================================================================
 * $01:E2CE — Frame sync (wait for VBlank)
 *
 * Enables NMI, then busy-waits until the NMI handler clears $016A.
 * This is the main frame synchronization mechanism — the game sets
 * $016A=1, then the NMI handler (mp_0080D4) clears it after processing.
 *
 * In our recomp, we can't spin-wait (we drive the frame loop manually),
 * so we trigger a VBlank + NMI handler cycle, then return.
 * ======================================================================== */
void mp_01E2CE(void) {
    /*
     * Frame sync — the heart of the frame loop.
     *
     * Original 65816:
     *   LDA $0122; ORA #$01; STA $4200   — enable NMI + auto-joypad
     *   LDA #$01; STA $016A              — set "NMI pending" flag
     *   wait: LDA $016A; BNE wait        — spin until NMI clears it
     *
     * In the recomp, this drives a complete frame cycle:
     * begin_frame → trigger_vblank → NMI handler → end_frame.
     */

    /* Enable NMI + auto-joypad */
    uint8_t nmitimen = bus_wram_read8(0x0122);
    nmitimen |= 0x01;
    bus_write8(0x00, REG_NMITIMEN, nmitimen);

    /* Set NMI pending flag */
    bus_wram_write8(0x016A, 0x01);

    /* Drive the frame */
    if (!snesrecomp_begin_frame()) {
        g_quit = true;
        bus_wram_write8(0x016A, 0x00);
        return;
    }

    snesrecomp_trigger_vblank();
    mp_0080D4();  /* NMI handler — clears $016A */

    /* Safety: ensure $016A is cleared */
    bus_wram_write8(0x016A, 0x00);

    snesrecomp_end_frame();
}

/* ========================================================================
 * $01:E2F3 — Wait for VBlank, then enable NMI
 *
 * Waits until NOT in VBlank (bit 7 of $4212 clear),
 * then enables NMI (sets bit 7 of NMITIMEN mirror and hardware).
 * ======================================================================== */
void mp_01E2F3(void) {
    /* In the recomp we don't need to spin-wait for VBlank timing.
     * Just enable NMI. */
    uint8_t val = bus_wram_read8(0x0122);
    val |= 0x80;
    bus_write8(0x00, REG_NMITIMEN, val);
    bus_wram_write8(0x0122, val);
}

/* ========================================================================
 * $01:E30E — Disable NMI
 *
 * Clears bit 7 of NMITIMEN mirror and hardware register.
 * ======================================================================== */
void mp_01E30E(void) {
    uint8_t val = bus_wram_read8(0x0122);
    val &= 0x7F;
    bus_write8(0x00, REG_NMITIMEN, val);
    bus_wram_write8(0x0122, val);
}

/* ========================================================================
 * $01:E429 — OAM DMA transfer
 *
 * DMA channel 0: transfers the OAM buffer ($0226, 544 bytes)
 * from WRAM to OAM via register $2104.
 * ======================================================================== */
void mp_01E429(void) {
    /* Set OAM address from mirrors */
    bus_write8(0x00, REG_OAMADDH, bus_wram_read8(0x0107));
    bus_write8(0x00, REG_OAMADDL, bus_wram_read8(0x0106));

    /* DMA channel 0: WRAM OAM buffer → $2104 (OAM data write) */
    bus_write8(0x00, DMA_PARAMS(0), 0x00);        /* 1-register, 1-byte */
    bus_write8(0x00, DMA_DEST(0),   0x04);         /* $2104 low byte */
    bus_write8(0x00, DMA_SRCLO(0),  (OAM_BUF & 0xFF));
    bus_write8(0x00, DMA_SRCHI(0),  (OAM_BUF >> 8));
    bus_write8(0x00, DMA_SRCBANK(0), 0x00);
    bus_write8(0x00, DMA_SIZELO(0), 0x20);         /* $0220 = 544 bytes */
    bus_write8(0x00, DMA_SIZEHI(0), 0x02);
    bus_write8(0x00, REG_MDMAEN,    0x01);         /* Trigger channel 0 */
}

/* ========================================================================
 * $01:E460 — VRAM DMA transfer (canvas/graphics data)
 *
 * Transfers data from WRAM to VRAM in chunks of up to $1000 bytes.
 * State machine using $0206 (enable), $0208 (continuation flag),
 * $020A (remaining size), $020E (VRAM dest), $0210 (WRAM source).
 * ======================================================================== */
void mp_01E460(void) {
    /* Check if transfer is requested */
    if (bus_wram_read8(0x0206) == 0) return;

    /* Check if DMA queue is busy */
    if (bus_wram_read8(0x0202) != 0) return;

    /* First chunk setup */
    if (bus_wram_read16(0x0208) == 0) {
        bus_wram_write16(0x020A, 0x6000);  /* Total size: $6000 bytes */
        bus_wram_write16(0x020E, 0x0000);  /* VRAM destination: $0000 */

        uint16_t src = 0xA000;             /* Default WRAM source */
        if (bus_wram_read16(0x19FA) != 0) {
            src = 0x4000;                  /* Alternate source */
        }
        bus_wram_write16(0x0210, src);
    }

    /* Clear continuation flag */
    bus_wram_write16(0x0208, 0x0000);

    /* Clamp chunk size to $1000 */
    uint16_t remaining = bus_wram_read16(0x020A);
    uint16_t chunk;
    if (remaining > 0x1000) {
        chunk = 0x1000;
        bus_wram_write16(0x020A, remaining - 0x1000);
        bus_wram_write16(0x0208, 0x0001);  /* More chunks to come */
    } else {
        chunk = remaining;
    }
    bus_wram_write16(0x020C, chunk);

    /* DMA channel 0: WRAM source → VRAM ($2118) */
    bus_write8(0x00, DMA_SRCLO(0),  bus_wram_read8(0x0210));
    bus_write8(0x00, DMA_SRCHI(0),  bus_wram_read8(0x0211));
    bus_write8(0x00, DMA_SRCBANK(0), bus_wram_read8(0x0212));
    bus_write8(0x00, DMA_SIZELO(0), bus_wram_read8(0x020C));
    bus_write8(0x00, DMA_SIZEHI(0), bus_wram_read8(0x020D));
    bus_write8(0x00, DMA_PARAMS(0), 0x01);  /* 2-register, word write */
    bus_write8(0x00, DMA_DEST(0),   0x18);  /* $2118 = VRAM data lo */
    bus_write8(0x00, REG_VMAIN,     0x80);  /* Increment on high byte */
    bus_write8(0x00, REG_VMADDL,    bus_wram_read8(0x020E));
    bus_write8(0x00, REG_VMADDH,    bus_wram_read8(0x020F));
    bus_write8(0x00, REG_MDMAEN,    0x01);  /* Trigger channel 0 */

    /* Advance source and destination pointers */
    uint16_t vram_dest = bus_wram_read16(0x020E) + (chunk >> 1);
    bus_wram_write16(0x020E, vram_dest);

    uint16_t wram_src = bus_wram_read16(0x0210) + chunk;
    bus_wram_write16(0x0210, wram_src);
}

/* ========================================================================
 * $01:E500 — Sprite GFX DMA transfer
 *
 * Transfers sprite graphics from bank $7F to VRAM.
 * Similar chunked transfer to E460 but for OBJ tile data.
 * State: $0214 (enable), $0216 (continuation), $0218 (remaining),
 *        $021C (VRAM dest), $021E (source offset in $7F).
 * ======================================================================== */
void mp_01E500(void) {
    if (bus_wram_read8(0x0214) == 0) return;

    /* First chunk: initialize transfer parameters */
    if (bus_wram_read16(0x0216) == 0) {
        bus_wram_write16(0x0218, 0x5800);  /* Total: $5800 bytes */
        bus_wram_write16(0x021C, 0x4400);  /* VRAM dest: $4400 */
        bus_wram_write16(0x021E, 0x0000);  /* Source offset: 0 */
    }

    /* Clear flags */
    bus_wram_write16(0x0214, 0x0000);
    bus_wram_write16(0x0216, 0x0000);

    /* Clamp chunk to $1000 */
    uint16_t remaining = bus_wram_read16(0x0218);
    uint16_t chunk;
    if (remaining > 0x1000) {
        chunk = 0x1000;
        bus_wram_write16(0x0218, remaining - 0x1000);
        bus_wram_write16(0x0214, 0x0001);
        bus_wram_write16(0x0216, 0x0001);
    } else {
        chunk = remaining;
    }
    bus_wram_write16(0x021A, chunk);

    /* DMA channel 0: bank $7F → VRAM */
    bus_write8(0x00, DMA_SRCLO(0),  bus_wram_read8(0x021E));
    bus_write8(0x00, DMA_SRCHI(0),  bus_wram_read8(0x021F));
    bus_write8(0x00, DMA_SRCBANK(0), 0x7F);
    bus_write8(0x00, DMA_SIZELO(0), bus_wram_read8(0x021A));
    bus_write8(0x00, DMA_SIZEHI(0), bus_wram_read8(0x021B));
    bus_write8(0x00, DMA_PARAMS(0), 0x01);
    bus_write8(0x00, DMA_DEST(0),   0x18);
    bus_write8(0x00, REG_VMAIN,     0x80);
    bus_write8(0x00, REG_VMADDL,    bus_wram_read8(0x021C));
    bus_write8(0x00, REG_VMADDH,    bus_wram_read8(0x021D));
    bus_write8(0x00, REG_MDMAEN,    0x01);

    /* Advance pointers */
    bus_wram_write16(0x021C, bus_wram_read16(0x021C) + (chunk >> 1));
    bus_wram_write16(0x021E, bus_wram_read16(0x021E) + chunk);
}

/* ========================================================================
 * $01:E59B — HDMA window setup (channel 1)
 *
 * Sets up HDMA channel 1 for window position scrolling.
 * $0220 controls: 0=skip, negative=disable, positive=enable.
 * ======================================================================== */
void mp_01E59B(void) {
    uint8_t ctrl = bus_wram_read8(0x0220);
    if (ctrl == 0) return;

    if ((int8_t)ctrl < 0) {
        /* Disable: clear HDMA channel 1 bit and window settings */
        uint8_t hdma = bus_wram_read8(0x0127) & ~0x02;
        bus_wram_write8(0x0127, hdma);
        bus_write8(0x00, REG_HDMAEN, hdma);
        bus_wram_write8(0x0111, 0x00);
        bus_write8(0x00, REG_W12SEL, 0x00);
        bus_wram_write8(0x0112, 0x00);
        bus_write8(0x00, REG_W34SEL, 0x00);
        bus_wram_write8(0x0113, 0x00);
        bus_write8(0x00, REG_WOBJSEL, 0x00);
        bus_wram_write8(0x0220, 0x00);
        return;
    }

    /* Enable HDMA channel 1 for window 1 left position */
    uint16_t src;
    if (ctrl & 0x01) {
        src = 0x4000;  /* Animation cell GFX buffer */
    } else {
        src = 0x4200;  /* Alternate table at $7E:4200 */
    }

    bus_write8(0x00, DMA_PARAMS(1), 0x01);   /* 2-register */
    bus_write8(0x00, DMA_DEST(1),   0x26);   /* $2126 = WH0 */
    bus_write8(0x00, DMA_SRCLO(1),  (uint8_t)(src & 0xFF));
    bus_write8(0x00, DMA_SRCHI(1),  (uint8_t)(src >> 8));
    bus_write8(0x00, DMA_SRCBANK(1), 0x7E);

    /* Enable HDMA channel 1 */
    uint8_t hdma = bus_wram_read8(0x0127) | 0x02;
    bus_wram_write8(0x0127, hdma);
    bus_write8(0x00, REG_HDMAEN, hdma);
    bus_wram_write8(0x0220, 0x00);
}

/* ========================================================================
 * $01:E60C — HDMA BG2 vertical scroll setup (channel 2)
 *
 * Sets up HDMA channel 2 for BG2 vertical scroll effect.
 * ======================================================================== */
void mp_01E60C(void) {
    uint8_t ctrl = bus_wram_read8(0x0224);
    if (ctrl == 0) return;

    if ((int8_t)ctrl < 0) {
        uint8_t hdma = bus_wram_read8(0x0127) & ~0x04;
        bus_wram_write8(0x0127, hdma);
        bus_write8(0x00, REG_HDMAEN, hdma);
        bus_wram_write8(0x0224, 0x00);
        return;
    }

    uint16_t src = (ctrl & 0x01) ? 0x9600 : 0x9800;  /* $7F:9600 or $7F:9800 */

    bus_write8(0x00, DMA_PARAMS(2), 0x02);   /* 2-register, write twice */
    bus_write8(0x00, DMA_DEST(2),   0x10);   /* $2110 = BG2VOFS */
    bus_write8(0x00, DMA_SRCLO(2),  (uint8_t)(src & 0xFF));
    bus_write8(0x00, DMA_SRCHI(2),  (uint8_t)(src >> 8));
    bus_write8(0x00, DMA_SRCBANK(2), 0x7F);

    uint8_t hdma = bus_wram_read8(0x0127) | 0x04;
    bus_wram_write8(0x0127, hdma);
    bus_write8(0x00, REG_HDMAEN, hdma);
    bus_wram_write8(0x0224, 0x00);
}

/* ========================================================================
 * $01:E66B — HDMA BG2 horizontal scroll setup (channel 3)
 *
 * Sets up HDMA channel 3 for BG2 horizontal scroll effect.
 * ======================================================================== */
void mp_01E66B(void) {
    uint8_t ctrl = bus_wram_read8(0x0222);
    if (ctrl == 0) return;

    if ((int8_t)ctrl < 0) {
        uint8_t hdma = bus_wram_read8(0x0127) & ~0x08;
        bus_wram_write8(0x0127, hdma);
        bus_write8(0x00, REG_HDMAEN, hdma);
        bus_wram_write8(0x0222, 0x00);
        return;
    }

    uint16_t src = (ctrl & 0x01) ? 0x9200 : 0x9400;  /* $7F:9200 or $7F:9400 */

    bus_write8(0x00, DMA_PARAMS(3), 0x02);
    bus_write8(0x00, DMA_DEST(3),   0x0F);   /* $210F = BG2HOFS */
    bus_write8(0x00, DMA_SRCLO(3),  (uint8_t)(src & 0xFF));
    bus_write8(0x00, DMA_SRCHI(3),  (uint8_t)(src >> 8));
    bus_write8(0x00, DMA_SRCBANK(3), 0x7F);

    uint8_t hdma = bus_wram_read8(0x0127) | 0x08;
    bus_wram_write8(0x0127, hdma);
    bus_write8(0x00, REG_HDMAEN, hdma);
    bus_wram_write8(0x0222, 0x00);
}

/* ========================================================================
 * $01:E6CA — Palette/VRAM DMA queue processing
 *
 * Processes a queue of DMA commands stored at WRAM $0182.
 * Each entry is a variable-length record:
 *   byte 0: type (0=end, bit0: 1=CGRAM, 0=VRAM)
 *   byte 1-2: source address low/high
 *   byte 3: source bank
 *   byte 4-5: size low/high
 *   Then for CGRAM: 1 byte (CGRAM address)
 *   For VRAM: 3 bytes (VMAIN, VRAM addr lo, VRAM addr hi)
 * ======================================================================== */
void mp_01E6CA(void) {
    if (bus_wram_read8(0x0202) == 0) return;

    /* Set up pointer to DMA command list at $0182 */
    bus_wram_write16(0x0202, 0x0000);
    bus_wram_write16(0x0204, 0x0000);

    uint16_t y = 0;
    uint16_t base = 0x0182;

    for (;;) {
        uint8_t type = bus_wram_read8(base + y);
        if (type == 0) break;  /* End of queue */

        uint8_t de = type;  /* Save type byte */
        y++;

        /* Source address */
        bus_write8(0x00, DMA_SRCLO(0),  bus_wram_read8(base + y)); y++;
        bus_write8(0x00, DMA_SRCHI(0),  bus_wram_read8(base + y)); y++;
        bus_write8(0x00, DMA_SRCBANK(0), bus_wram_read8(base + y)); y++;

        /* Size */
        bus_write8(0x00, DMA_SIZELO(0), bus_wram_read8(base + y)); y++;
        bus_write8(0x00, DMA_SIZEHI(0), bus_wram_read8(base + y)); y++;

        if (de & 0x01) {
            /* CGRAM transfer */
            bus_write8(0x00, DMA_PARAMS(0), 0x00);   /* 1-register */
            bus_write8(0x00, DMA_DEST(0),   0x22);   /* $2122 = CGRAM data */
            bus_write8(0x00, REG_CGRAM_ADDR, bus_wram_read8(base + y)); y++;
        } else {
            /* VRAM transfer */
            bus_write8(0x00, DMA_PARAMS(0), 0x01);   /* 2-register word */
            bus_write8(0x00, DMA_DEST(0),   0x18);   /* $2118 = VRAM data lo */
            bus_write8(0x00, REG_VMAIN,     bus_wram_read8(base + y)); y++;
            bus_write8(0x00, REG_VMADDL,    bus_wram_read8(base + y)); y++;
            bus_write8(0x00, REG_VMADDH,    bus_wram_read8(base + y)); y++;
        }

        /* Trigger DMA */
        bus_write8(0x00, REG_MDMAEN, 0x01);
    }
}

/* ========================================================================
 * $01:E747 — Brightness control + joypad read
 *
 * Waits for auto-joypad read to complete ($4212 bit 0),
 * then reads all 4 joypad ports into RAM mirrors.
 * Implements button repeat logic.
 *
 * RAM layout (per port, 2 bytes each):
 *   $0132/$0134/$0136/$0138 — held buttons
 *   $013A/$013C/$013E/$0140 — pressed (new) buttons
 *   $0142/$0144/$0146/$0148 — repeat buttons
 *   $014A/$014C/$014E/$0150 — previous frame buttons
 *   $0162/$0164/$0166/$0168 — repeat timer
 *   $012A — initial repeat delay
 *   $012C — repeat rate
 * ======================================================================== */
void mp_01E747(void) {
    /* Wait for auto-joypad read to complete
     * In the recomp, snesrecomp handles this timing — just proceed */

    /* Read all 4 ports */
    for (int x = 6; x >= 0; x -= 2) {
        uint16_t held = bus_read16(0x00, REG_JOY1L + x);
        bus_wram_write16(0x0132 + x, held);

        uint16_t prev = bus_wram_read16(0x014A + x);
        uint16_t pressed = (held ^ prev) & held;
        bus_wram_write16(0x013A + x, pressed);
        bus_wram_write16(0x0142 + x, pressed);

        if (held == 0 || held != prev) {
            /* Reset repeat timer */
            bus_wram_write16(0x0162 + x, bus_wram_read16(0x012A));
        } else {
            /* Decrement repeat timer */
            uint16_t timer = bus_wram_read16(0x0162 + x) - 1;
            if (timer == 0) {
                /* Fire repeat */
                bus_wram_write16(0x0142 + x, held);
                bus_wram_write16(0x0162 + x, bus_wram_read16(0x012C));
            } else {
                bus_wram_write16(0x0162 + x, timer);
            }
        }

        /* Save current as previous */
        bus_wram_write16(0x014A + x, held);
    }
}

/* ========================================================================
 * $01:E794 — Fade in (brightness ramp up)
 *
 * Ramps brightness from 0 to 15 over multiple frames.
 * Each brightness level lasts $04BD frames (set to 1).
 * At the end, does one more frame sync.
 * ======================================================================== */
void mp_01E794(void) {
    /* Clear force-blank, start from brightness 0 */
    uint8_t disp = bus_wram_read8(0x0104);
    disp &= 0x70;  /* Keep bits 4-6, clear force blank and brightness */
    bus_wram_write8(0x0104, disp);

    /* Ramp brightness 0→15 */
    for (int brt = 0; brt < 15 && !g_quit; brt++) {
        bus_wram_write8(0x04BD, 0x01);
        /* Wait $04BD frames at this brightness */
        while (bus_wram_read8(0x04BD) != 0 && !g_quit) {
            mp_01E2CE();
            uint8_t t = bus_wram_read8(0x04BD);
            if (t > 0) bus_wram_write8(0x04BD, t - 1);
        }
        /* Increment brightness */
        uint8_t cur = bus_wram_read8(0x0104);
        cur++;
        bus_wram_write8(0x0104, cur);

        /* Check if we've reached max brightness */
        if ((cur & 0x0F) >= 0x0F) break;
    }

    /* Final frame sync */
    if (!g_quit) mp_01E2CE();
}

/* ========================================================================
 * $01:E895 — Check empty tile rows for optimization
 *
 * Scans tile data rows and marks which ones are empty in $0569.
 * Used to optimize VRAM transfers.
 * ======================================================================== */
void mp_01E895(void) {
    /* This routine scans pairs of tile data rows (at $0580/$0780 base,
     * working backwards) and marks empty rows in $0569+.
     * For now, implement as a stub — the optimization isn't critical
     * for initial rendering. */
    uint8_t *wram = bus_get_wram();

    uint16_t ptr1 = 0x0580 + 0x0070 * 0x0E;  /* Starting high */
    uint16_t ptr2 = 0x0780 + 0x0070 * 0x0E;

    for (int row = 0x000E; row >= 0; row--) {
        uint16_t ored = 0;
        for (int y = 0x003E; y >= 0; y -= 2) {
            ored |= (uint16_t)(wram[ptr1 + y] | (wram[ptr1 + y + 1] << 8));
            ored |= (uint16_t)(wram[ptr2 + y] | (wram[ptr2 + y + 1] << 8));
        }
        bus_wram_write16(0x0569 + row * 2, (ored == 0) ? 1 : 0);

        /* Skip row 8 ($0010/2 = 8) — uses different pointer adjustment */
        if (row * 2 == 0x0010) {
            ptr1 = 0x0580;
            ptr2 = 0x0780;
        } else {
            ptr1 -= 0x0040;
            ptr2 -= 0x0040;
        }
    }
}

/* ========================================================================
 * $01:E238 — Initialize random number table
 *
 * Seeds a pseudo-random table at $044C (55 words) using the
 * checksum value from DP $02 as the initial seed.
 * ======================================================================== */
void mp_01E238(void) {
    uint16_t seed = bus_wram_read16(0x02);
    uint16_t base_val = seed & 0x001F;
    bus_wram_write16(0x04B8, base_val);

    uint16_t a = 0x0001;
    uint16_t b = base_val;
    bus_wram_write16(0x044A, 0x0000);

    uint16_t modulus = bus_wram_read16(0x0448);
    uint16_t y = 0x0028;

    for (int i = 0x006C; i > 0; i -= 2) {
        bus_wram_write16(0x044C + y, a);
        uint16_t temp = b;
        b = a;
        a = temp;
        if (a < b) {
            a += modulus;
        }
        a -= b;

        y += 0x002A;
        if (y >= 0x006E) {
            y -= 0x006E;
        }
    }

    /* Shuffle 3 times */
    for (int pass = 0; pass < 3; pass++) {
        for (uint16_t yi = 0; yi < 0x006E; yi += 2) {
            uint16_t other_y;
            if (yi < 0x0030) {
                other_y = yi + 0x003E;
            } else {
                other_y = yi - 0x0030;
            }
            uint16_t val = bus_wram_read16(0x044C + yi);
            uint16_t sub = bus_wram_read16(0x044C + other_y);
            if (val < sub) {
                val += modulus;
            }
            val -= sub;
            bus_wram_write16(0x044C + yi, val);
        }
    }
}

/* ========================================================================
 * Register all Bank 01 recompiled functions.
 * ======================================================================== */
void mp_register_bank01(void) {
    func_table_register(0x01E024, mp_01E024);
    func_table_register(0x01E033, mp_01E033);
    func_table_register(0x01E042, mp_01E042);
    func_table_register(0x01E060, mp_01E060);
    func_table_register(0x01E06F, mp_01E06F);
    func_table_register(0x01E09B, mp_01E09B);
    func_table_register(0x01E103, mp_01E103);
    func_table_register(0x01E1AB, mp_01E1AB);
    func_table_register(0x01E238, mp_01E238);
    func_table_register(0x01E2CE, mp_01E2CE);
    func_table_register(0x01E2F3, mp_01E2F3);
    func_table_register(0x01E30E, mp_01E30E);
    func_table_register(0x01E429, mp_01E429);
    func_table_register(0x01E460, mp_01E460);
    func_table_register(0x01E500, mp_01E500);
    func_table_register(0x01E59B, mp_01E59B);
    func_table_register(0x01E60C, mp_01E60C);
    func_table_register(0x01E66B, mp_01E66B);
    func_table_register(0x01E6CA, mp_01E6CA);
    func_table_register(0x01E747, mp_01E747);
    func_table_register(0x01E794, mp_01E794);
    func_table_register(0x01E895, mp_01E895);
}
