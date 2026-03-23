/*
 * Mario Paint — Recompiled title screen routine.
 *
 * $018000 is the biggest single routine in the game. It handles:
 *   1. SRAM checksum validation
 *   2. Title screen palette + sprite tile DMA from ROM
 *   3. Title screen tilemap setup and animation
 *   4. Fade-in effect
 *   5. Title screen input loop (wait for click or demo timeout)
 *   6. Transition: load main canvas palette + tiles + audio
 *   7. Fade-out and return to canvas mode
 *
 * Sub-routines that handle title animation/input ($018260, $018C43,
 * etc.) are dispatched via func_table_call and will work as they
 * get recompiled. The critical path here is the DMA chains.
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <mp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

#include <stdbool.h>
#include <string.h>

extern bool g_quit;

#define REG_INIDISP  0x2100
#define REG_VMAIN    0x2115
#define REG_VMADDL   0x2116
#define REG_VMADDH   0x2117
#define REG_VMDATAL  0x2118
#define REG_CGRAM    0x2121
#define REG_APUIO0   0x2140
#define REG_BG12NBA  0x210B
#define REG_BG34NBA  0x210C
#define REG_TM       0x212C
#define REG_TMW      0x212E
#define REG_NMITIMEN 0x4200
#define REG_MDMAEN   0x420B
#define REG_HDMAEN   0x420C

#define DMA_PARAMS(n)   (0x4300 + (n) * 0x10 + 0)
#define DMA_DEST(n)     (0x4300 + (n) * 0x10 + 1)
#define DMA_SRCLO(n)    (0x4300 + (n) * 0x10 + 2)
#define DMA_SRCHI(n)    (0x4300 + (n) * 0x10 + 3)
#define DMA_SRCBANK(n)  (0x4300 + (n) * 0x10 + 4)
#define DMA_SIZELO(n)   (0x4300 + (n) * 0x10 + 5)
#define DMA_SIZEHI(n)   (0x4300 + (n) * 0x10 + 6)

static void cgram_dma(uint8_t bank, uint16_t addr, uint16_t size) {
    bus_write8(0x00, REG_CGRAM, 0x00);
    bus_write8(0x00, DMA_PARAMS(0), 0x00);
    bus_write8(0x00, DMA_DEST(0),   0x22);
    bus_write8(0x00, DMA_SRCLO(0),  (uint8_t)(addr & 0xFF));
    bus_write8(0x00, DMA_SRCHI(0),  (uint8_t)(addr >> 8));
    bus_write8(0x00, DMA_SRCBANK(0), bank);
    bus_write8(0x00, DMA_SIZELO(0), (uint8_t)(size & 0xFF));
    bus_write8(0x00, DMA_SIZEHI(0), (uint8_t)(size >> 8));
    bus_write8(0x00, REG_MDMAEN, 0x01);
}

static void vram_dma(uint16_t vram_addr, uint8_t bank, uint16_t addr, uint16_t size) {
    bus_write8(0x00, REG_VMADDL, (uint8_t)(vram_addr & 0xFF));
    bus_write8(0x00, REG_VMADDH, (uint8_t)(vram_addr >> 8));
    bus_write8(0x00, REG_VMAIN,  0x80);
    bus_write8(0x00, DMA_PARAMS(0), 0x01);
    bus_write8(0x00, DMA_DEST(0),   0x18);
    bus_write8(0x00, DMA_SRCLO(0),  (uint8_t)(addr & 0xFF));
    bus_write8(0x00, DMA_SRCHI(0),  (uint8_t)(addr >> 8));
    bus_write8(0x00, DMA_SRCBANK(0), bank);
    bus_write8(0x00, DMA_SIZELO(0), (uint8_t)(size & 0xFF));
    bus_write8(0x00, DMA_SIZEHI(0), (uint8_t)(size >> 8));
    bus_write8(0x00, REG_MDMAEN, 0x01);
}

/* ========================================================================
 * $00:D6D3 — SRAM checksum validation
 *
 * Computes a checksum of SRAM bank $70 and stores result at $1982.
 * $1982 = 1 if checksum matches (valid save), 0 if not.
 * ======================================================================== */
void mp_00D6D3(void) {
    /* For now, mark SRAM as invalid (no save data) */
    bus_wram_write16(0x1982, 0x0000);

    /* Compute the actual checksum if SRAM has data */
    uint16_t sum = 0x7003;
    uint16_t xor_val = 0x2122;

    for (int x = 0x77FE; x >= 0; x -= 2) {
        uint16_t val = bus_read16(0x70, 0x0800 + x);
        sum += val;
        xor_val ^= val;
    }
    sum += bus_read16(0x70, 0x07FE);
    xor_val ^= bus_read16(0x70, 0x07FE);

    bus_wram_write16(0x197C, sum);
    bus_wram_write16(0x197E, xor_val);

    /* Check against stored checksums */
    if (bus_read16(0x70, 0x07C2) == sum &&
        bus_read16(0x70, 0x07C4) == xor_val) {
        bus_wram_write16(0x1982, 0x0001);
    }
}

/* ========================================================================
 * $01:8000 — Title screen and main init
 *
 * This is the "big bang" routine that runs the title screen and
 * then transitions to the main canvas.
 * ======================================================================== */
void mp_018000(void) {
    /* === Phase 1: SRAM check === */
    mp_00D6D3();

    /* === Phase 2: Load title screen graphics === */
    op_sep(0x20);
    mp_01E30E();    /* Disable NMI */
    mp_01E87B();    /* Disable HDMA */
    mp_01E88A();    /* Disable DMA */
    OP_SEI();

    /* Title palette: $02:FC00 → CGRAM ($200 bytes) */
    cgram_dma(0x02, 0xFC00, 0x0200);

    /* Title sprite tiles: $09:C000 → VRAM $4000 ($4000 bytes) */
    vram_dma(0x4000, 0x09, 0xC000, 0x4000);

    /* Enable interrupts and NMI */
    OP_CLI();
    mp_01E2F3();

    /* Set display layers: BG1+BG2+BG4+OBJ */
    bus_write8(0x00, REG_TM, 0x13);
    bus_wram_write8(0x011A, 0x13);
    bus_write8(0x00, REG_TMW, 0x13);
    bus_wram_write8(0x011C, 0x13);

    /* BG tile data: BG12=$04, BG34=$44 (title screen layout) */
    bus_write8(0x00, REG_BG12NBA, 0x04);
    bus_wram_write8(0x010E, 0x04);
    bus_write8(0x00, REG_BG34NBA, 0x44);
    bus_wram_write8(0x010F, 0x44);

    op_rep(0x20);

    /* Set up OAM with title screen tile base */
    op_lda_imm16(0x0045);
    mp_01E024();
    mp_01DE97();

    /* Clear BG2 tilemap */
    op_lda_imm16(0x0000);
    mp_01E033();
    func_table_call(0x008A12);

    /* Clear scroll positions */
    bus_wram_write16(0x0170, 0x0000);
    bus_wram_write16(0x016E, 0x0000);
    bus_wram_write16(0x0174, 0x0000);
    bus_wram_write16(0x0172, 0x0000);

    /* Compute sprite size mirrors */
    mp_01DFD3();

    /* Clear $7F bank state */
    bus_write8(0x7F, 0x021B, 0x00);

    /* Clear OAM buffer */
    mp_01E06F();

    /* === Phase 3: Title screen display === */
    /* Set up title screen sprites (skip sprite $FFFF = "all") */
    CPU_SET_A16(0xFFFF);
    func_table_call(0x018C43);

    if (g_quit) return;

    /* Fade in */
    mp_01E794();
    if (g_quit) return;

    /* Set up title screen state */
    bus_write16(0x7F, 0x0001, 0x0000);
    bus_write16(0x7F, 0x0005, 0x00EF);

    bus_wram_write16(0x1980, 0x0000);  /* Demo wait timer */

    /* === Phase 4: Title screen animation loop === */
    func_table_call(0x018260);
    if (g_quit) return;

    /* Clear animation buffer */
    mp_008A75();

    /* Send stop command to audio */
    op_lda_imm16(0x0000);
    mp_01D2BF();

    if (g_quit) return;

    /* Fade out */
    func_table_call(0x01E7C9);
    if (g_quit) return;

    /* Clear scroll */
    bus_wram_write16(0x0172, 0x0000);
    bus_wram_write16(0x0174, 0x0000);

    /* Clear OAM */
    mp_01E06F();

    /* Set HDMA window flag */
    bus_wram_write16(0x0220, 0xFFFF);

    /* Check if demo was active */
    if (bus_wram_read16(0x04E2) != 0) {
        /* Demo active — return to caller */
        return;
    }

    /* Check special flag */
    if (bus_wram_read16(0x0565) != 0) {
        func_table_call(0x019372);
        return;
    }

    /* === Phase 5: Load main canvas graphics === */
    op_sep(0x20);
    mp_01E30E();
    mp_01E87B();
    mp_01E88A();
    OP_SEI();

    /* Main canvas palette: $02:FC00 → CGRAM */
    cgram_dma(0x02, 0xFC00, 0x0200);

    /* Canvas sprite tiles: $04:EA00 → VRAM $4700 ($0E00 bytes) */
    vram_dma(0x4700, 0x04, 0xEA00, 0x0E00);

    op_rep(0x20);

    /* Check randomizer state */
    bus_wram_write16(0x0547, 0x0000);
    func_table_call(0x01E20C);
    uint16_t rng = CPU_A16();
    if (rng >= 0x00C0) {
        bus_wram_write16(0x0547, 0x0001);
    }

    /* Audio init */
    OP_CLI();
    mp_01E2F3();
    op_sep(0x20);
    mp_01D388();
    if (g_quit) return;

    /* Upload canvas audio samples */
    mp_01E30E();

    /* Select audio bank based on randomizer */
    uint16_t audio_flag = bus_wram_read16(0x0547);
    if (audio_flag == 0) {
        /* Bank $17:9000 */
        bus_wram_write8(0xCC, 0x00);
        bus_wram_write8(0xCD, 0x90);
        bus_wram_write8(0xCE, 0x17);
    } else {
        /* Bank $16:8000 */
        bus_wram_write8(0xCC, 0x00);
        bus_wram_write8(0xCD, 0x80);
        bus_wram_write8(0xCE, 0x16);
    }
    bus_write8(0x00, REG_APUIO0, 0xFF);
    mp_01DF25();
    if (g_quit) return;

    op_rep(0x20);
    mp_01E2F3();

    /* === Phase 6: Restore canvas display settings === */
    /* Original sets $04/$44 (title tile bases) here, but mp_0084D5
     * calls mp_0087EE later which expects $06/$66 (canvas tile bases).
     * Set canvas values directly to avoid corruption during the
     * frames between here and mp_0087EE. */
    op_sep(0x20);
    bus_write8(0x00, REG_BG12NBA, 0x06);
    bus_wram_write8(0x010E, 0x06);
    bus_write8(0x00, REG_BG34NBA, 0x66);
    bus_wram_write8(0x010F, 0x66);

    op_rep(0x20);

    /* Set up OAM and tilemaps for canvas mode */
    op_lda_imm16(0x0045);
    mp_01E024();
    mp_01DE97();

    op_lda_imm16(0x0000);
    mp_01E033();
    func_table_call(0x008A12);

    /* Send initial music command */
    if (bus_wram_read16(0x0547) == 0) {
        op_lda_imm16(0x0021);
    } else {
        op_lda_imm16(0x0022);
    }
    mp_01D308();

    /* Set up canvas display */
    func_table_call(0x018F52);

    /* Enable HDMA window */
    bus_wram_write16(0x0220, 0xFFFF);
}

/* ========================================================================
 * Register title screen functions.
 * ======================================================================== */
void mp_register_title(void) {
    func_table_register(0x00D6D3, mp_00D6D3);
    func_table_register(0x018000, mp_018000);
}
