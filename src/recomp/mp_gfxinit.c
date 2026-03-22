/*
 * Mario Paint — Recompiled graphics initialization routines.
 *
 * These routines load the initial graphics data (palette, tiles, tilemaps)
 * from ROM into VRAM and CGRAM via DMA. This is what gets the screen
 * from black to actually showing the Mario Paint UI.
 *
 * Key routines:
 *   $0087EE — Master graphics loader (palette + 6 VRAM DMA transfers)
 *   $008A75 — Clear animation cell GFX buffer
 *   $0089B1 — Tilemap page setup
 *   $0089C3 — Tilemap border fill
 *   $008A16 — BG2 tilemap fill
 *   $008A39 — BG3 tilemap fill
 *   $01E87B — Disable HDMA
 *   $01E88A — Disable DMA
 *   $01DE97/$01DEB2/$01DECD — Queue tilemap DMA transfers
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <mp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

#include <stdbool.h>

extern bool g_quit;

/* DMA channel register helpers */
#define DMA_CH(n)       (0x4300 + (n) * 0x10)
#define DMA_PARAMS(n)   (DMA_CH(n) + 0)
#define DMA_DEST(n)     (DMA_CH(n) + 1)
#define DMA_SRCLO(n)    (DMA_CH(n) + 2)
#define DMA_SRCHI(n)    (DMA_CH(n) + 3)
#define DMA_SRCBANK(n)  (DMA_CH(n) + 4)
#define DMA_SIZELO(n)   (DMA_CH(n) + 5)
#define DMA_SIZEHI(n)   (DMA_CH(n) + 6)

#define REG_INIDISP     0x2100
#define REG_VMAIN       0x2115
#define REG_VMADDL      0x2116
#define REG_VMADDH      0x2117
#define REG_VMDATAL     0x2118
#define REG_VMDATAH     0x2119
#define REG_CGRAM_ADDR  0x2121
#define REG_CGRAM_DATA  0x2122
#define REG_NMITIMEN    0x4200
#define REG_MDMAEN      0x420B
#define REG_HDMAEN      0x420C
#define REG_BG12NBA     0x210B
#define REG_BG34NBA     0x210C

/*
 * Helper: set up DMA channel 0 and trigger a transfer.
 */
static void dma_transfer(uint8_t params, uint8_t dest,
                         uint8_t src_bank, uint8_t src_hi, uint8_t src_lo,
                         uint8_t size_hi, uint8_t size_lo) {
    bus_write8(0x00, DMA_PARAMS(0), params);
    bus_write8(0x00, DMA_DEST(0),   dest);
    bus_write8(0x00, DMA_SRCLO(0),  src_lo);
    bus_write8(0x00, DMA_SRCHI(0),  src_hi);
    bus_write8(0x00, DMA_SRCBANK(0), src_bank);
    bus_write8(0x00, DMA_SIZELO(0), size_lo);
    bus_write8(0x00, DMA_SIZEHI(0), size_hi);
    bus_write8(0x00, REG_MDMAEN,    0x01);
}

/*
 * Helper: set up VRAM DMA (mode 1, word write to $2118).
 */
static void vram_dma(uint16_t vram_addr,
                     uint8_t src_bank, uint16_t src_addr,
                     uint16_t size) {
    bus_write8(0x00, REG_VMADDL, (uint8_t)(vram_addr & 0xFF));
    bus_write8(0x00, REG_VMADDH, (uint8_t)(vram_addr >> 8));
    bus_write8(0x00, REG_VMAIN,  0x80);  /* Increment on high byte write */
    dma_transfer(0x01, 0x18,
                 src_bank, (uint8_t)(src_addr >> 8), (uint8_t)(src_addr & 0xFF),
                 (uint8_t)(size >> 8), (uint8_t)(size & 0xFF));
}

/* ========================================================================
 * $01:E87B — Disable HDMA
 *
 * Clears the HDMA enable mirror and hardware register.
 * ======================================================================== */
void mp_01E87B(void) {
    bus_wram_write8(0x0127, 0x00);
    bus_write8(0x00, REG_HDMAEN, 0x00);
}

/* ========================================================================
 * $01:E88A — Disable DMA
 *
 * Writes 0 to DMA enable register (cancels pending DMA).
 * ======================================================================== */
void mp_01E88A(void) {
    bus_write8(0x00, REG_MDMAEN, 0x00);
}

/* ========================================================================
 * $00:87EE — Master graphics loader
 *
 * Loads ALL initial graphics data from ROM:
 *   1. CGRAM palette: $02:FE00 → CGRAM ($200 bytes = full 256-color palette)
 *   2. UI font tiles: $04:C000 → VRAM $6000 ($2000 bytes)
 *   3. Extra tiles:   $05:8000 → VRAM $7000 ($0800 bytes)
 *   4. BG3 tiles:     $03:8000 → VRAM $7400 ($1200 bytes)
 *   5. More tiles:    $04:FA00 → VRAM $7D00 ($0600 bytes)
 *   6. Sprite tiles:  $08:8000 → VRAM $4000 ($4000 bytes)
 *
 * Then sets up:
 *   - BG tile data designations (BG12NBA=$06, BG34NBA=$66)
 *   - Fills VRAM $3FF0-$3FFF with $FFFF (solid tile)
 *   - Sets up OAM buffers and tilemaps
 *   - Queues tilemap DMA and enables NMI
 * ======================================================================== */
void mp_0087EE(void) {
    op_sep(0x30);

    /* Disable NMI and HDMA/DMA */
    mp_01E30E();
    mp_01E87B();
    mp_01E88A();
    OP_SEI();

    /* Small delay (original code: LDA #$00; DEC; BNE — 256 iterations) */
    /* Not needed in recomp */

    /* 1. Upload palette: $02:FE00 → CGRAM (512 bytes = full palette) */
    bus_write8(0x00, REG_CGRAM_ADDR, 0x00);
    dma_transfer(0x00, 0x22,       /* mode 0, dest=$2122 (CGRAM write) */
                 0x02, 0xFE, 0x00, /* source: $02:FE00 */
                 0x02, 0x00);      /* size: $0200 */

    /* 2. UI font tiles: $04:C000 → VRAM $6000 ($2000 bytes) */
    vram_dma(0x6000, 0x04, 0xC000, 0x2000);

    /* 3. Extra tiles: $05:8000 → VRAM $7000 ($0800 bytes) */
    vram_dma(0x7000, 0x05, 0x8000, 0x0800);

    /* 4. BG3 tiles: $03:8000 → VRAM $7400 ($1200 bytes) */
    vram_dma(0x7400, 0x03, 0x8000, 0x1200);

    /* 5. More tiles: $04:FA00 → VRAM $7D00 ($0600 bytes) */
    vram_dma(0x7D00, 0x04, 0xFA00, 0x0600);

    /* 6. Sprite tiles: $08:8000 → VRAM $4000 ($4000 bytes) */
    vram_dma(0x4000, 0x08, 0x8000, 0x4000);

    /* Set BG tile data designations */
    bus_write8(0x00, REG_BG12NBA, 0x06);
    bus_wram_write8(0x010E, 0x06);
    bus_write8(0x00, REG_BG34NBA, 0x66);
    bus_wram_write8(0x010F, 0x66);

    /* Fill VRAM $3FF0-$3FFF with $FFFF (solid tile for borders) */
    bus_write8(0x00, REG_VMADDL, 0xF0);
    bus_write8(0x00, REG_VMADDH, 0x3F);
    bus_write8(0x00, REG_VMAIN,  0x80);
    for (int i = 0; i < 16; i++) {
        bus_write8(0x00, REG_VMDATAL, 0xFF);
        bus_write8(0x00, REG_VMDATAH, 0xFF);
    }

    op_rep(0x30);

    /* Fill OAM low table with $3DFE (offscreen marker) */
    op_lda_imm16(0x3DFE);
    mp_01E024();

    /* Fill OAM high table with $3FFF */
    op_lda_imm16(0x3FFF);
    mp_01E033();

    /* Set up tilemaps */
    mp_0089B1();
    mp_008A16();
    mp_008A39();
    mp_0089C3();

    /* Queue OAM tilemap DMA */
    func_table_call(0x01DE97);

    /* Enable interrupts and NMI */
    OP_CLI();
    mp_01E2F3();
}

/* ========================================================================
 * $00:89B1 — Tilemap page setup
 *
 * Sets up the BG1 tilemap based on which canvas page is active.
 * ======================================================================== */
void mp_0089B1(void) {
    uint16_t page = bus_wram_read16(0x19FA);
    if (page == 0) {
        func_table_call(0x00C414);  /* Standard canvas tilemap */
    } else {
        func_table_call(0x00C3DE);  /* Alternate page tilemap */
    }
}

/* ========================================================================
 * $00:89C3 — Tilemap border fill (BG1 tilemap buffer at $7E:2000)
 *
 * Fills the OAM/tilemap buffers with border tiles:
 *   $7E:2000-$203E = $21EE (top border, 32 tiles)
 *   $7E:2040-$205E = $210F..$2100 (left column, descending)
 *   $7E:2080-$209E = $211F..$2110 (another column)
 *   $7E:2060-$207E = $212F..$2120 (right column)
 *   $7E:20A0-$20BE = $213F..$2130 (another column)
 * ======================================================================== */
void mp_0089C3(void) {
    uint8_t *wram = bus_get_wram();

    /* Top border: fill $2000-$203E with $21EE */
    for (int x = 0x3E; x >= 0; x -= 2) {
        wram[0x2000 + x]     = 0xEE;
        wram[0x2000 + x + 1] = 0x21;
    }

    /* Left column descending: $2040+x = $210F, $210E, ... */
    {
        uint16_t val = 0x210F;
        for (int x = 0x1E; x >= 0; x -= 2) {
            wram[0x2040 + x]     = (uint8_t)(val & 0xFF);
            wram[0x2040 + x + 1] = (uint8_t)(val >> 8);
            val--;
        }
    }

    /* Second column: $2080+x = $211F, $211E, ... */
    {
        uint16_t val = 0x211F;
        for (int x = 0x1E; x >= 0; x -= 2) {
            wram[0x2080 + x]     = (uint8_t)(val & 0xFF);
            wram[0x2080 + x + 1] = (uint8_t)(val >> 8);
            val--;
        }
    }

    /* Right column: $2060+x = $212F, $212E, ... */
    {
        uint16_t val = 0x212F;
        for (int x = 0x1E; x >= 0; x -= 2) {
            wram[0x2060 + x]     = (uint8_t)(val & 0xFF);
            wram[0x2060 + x + 1] = (uint8_t)(val >> 8);
            val--;
        }
    }

    /* Fourth column: $20A0+x = $213F, $213E, ... */
    {
        uint16_t val = 0x213F;
        for (int x = 0x1E; x >= 0; x -= 2) {
            wram[0x20A0 + x]     = (uint8_t)(val & 0xFF);
            wram[0x20A0 + x + 1] = (uint8_t)(val >> 8);
            val--;
        }
    }
}

/* ========================================================================
 * $00:8A16 — BG2 tilemap fill
 *
 * Fills the BG2 tilemap buffer at $7E:2800 with tile numbers
 * $02DF down to $0020 (sequential tile grid for the canvas area).
 * Then calls DE/B2 to queue the DMA.
 * ======================================================================== */
void mp_008A16(void) {
    uint8_t *wram = bus_get_wram();
    uint16_t val = 0x02DF;

    for (int x = 0x63E; x >= 0; x -= 2) {
        wram[0x2800 + x]     = (uint8_t)(val & 0xFF);
        wram[0x2800 + x + 1] = (uint8_t)(val >> 8);
        val--;
        if (val < 0x0020) val = 0x02DF;  /* Wrap (original: CMP #$001F, BNE) */
    }

    func_table_call(0x01DEB2);
}

/* ========================================================================
 * $00:8A39 — BG3 tilemap fill
 *
 * Fills BG3 tilemap buffer at $7E:3000 with $2FFC (initial value),
 * then fills the canvas area at $7E:3102 with $03FE.
 * Sets BG3 scroll to (4, 4) and queues DMA.
 * ======================================================================== */
void mp_008A39(void) {
    /* Fill entire BG3 buffer with $2FFC */
    op_lda_imm16(0x2FFC);
    mp_01E042();

    /* Fill canvas grid: $7E:3102 onwards with $03FE, skipping every 32nd word */
    uint8_t *wram = bus_get_wram();
    uint16_t x = 0x0000;
    while (x < 0x0540) {
        wram[0x3102 + x]     = 0xFE;
        wram[0x3102 + x + 1] = 0x03;
        x += 2;
        /* Skip the last 2 bytes of each 64-byte row */
        if ((x & 0x003F) == 0x003E) {
            x += 2;
        }
    }

    /* Set BG3 scroll position */
    bus_wram_write16(0x0178, 0x0004);  /* BG3 vertical scroll */
    bus_wram_write16(0x0176, 0x0004);  /* BG3 horizontal scroll */

    /* Queue DMA for BG3 tilemap */
    func_table_call(0x01DECD);
}

/* ========================================================================
 * $00:8A75 — Clear animation cell GFX buffer
 *
 * Zeros out $C000 bytes at $7E:4000 (the animation/scratch buffer).
 * ======================================================================== */
void mp_008A75(void) {
    uint8_t *wram = bus_get_wram();
    /* Clear $7E:4000-$7E:FFFE ($C000 bytes) */
    /* AnimationCellGFXBuffer = $7E4000 → WRAM offset $4000 */
    for (uint32_t x = 0; x <= 0xBFFE; x += 2) {
        wram[0x4000 + x]     = 0x00;
        wram[0x4000 + x + 1] = 0x00;
    }
}

/* ========================================================================
 * $01:DE97 — Queue BG1 tilemap DMA
 *
 * Queues a DMA command to transfer the BG1 tilemap buffer
 * ($7E:2000, $800 bytes) to VRAM $3000.
 *
 * DMA record format: type(1), src_lo(1), src_hi(1), src_bank(1),
 *                    size_lo(1), size_hi(1), vmain(1), vram_lo(1), vram_hi(1)
 * ======================================================================== */
void mp_01DE97(void) {
    /* DMA record: $02, $7E:2000, size=$0800, VRAM $0080:$0030 */
    static const uint8_t record[] = {
        0x02,              /* type: VRAM transfer */
        0x00, 0x20,        /* source: $2000 */
        0x7E,              /* bank: $7E */
        0x00, 0x08,        /* size: $0800 */
        0x80,              /* VMAIN: increment on high byte */
        0x00, 0x30         /* VRAM address: $3000 */
    };
    mp_01DE_queue_dma(record, sizeof(record));
}

/* ========================================================================
 * $01:DEB2 — Queue BG2 tilemap DMA
 *
 * Queues DMA: $7E:2800 ($800 bytes) → VRAM $3400.
 * ======================================================================== */
void mp_01DEB2(void) {
    static const uint8_t record[] = {
        0x02, 0x00, 0x28, 0x7E, 0x00, 0x08, 0x80, 0x00, 0x34
    };
    mp_01DE_queue_dma(record, sizeof(record));
}

/* ========================================================================
 * $01:DECD — Queue BG3 tilemap DMA
 *
 * Queues DMA: $7E:3000 ($800 bytes) → VRAM $3800.
 * ======================================================================== */
void mp_01DECD(void) {
    static const uint8_t record[] = {
        0x02, 0x00, 0x30, 0x7E, 0x00, 0x08, 0x80, 0x00, 0x38
    };
    mp_01DE_queue_dma(record, sizeof(record));
}

/*
 * Shared helper: queue a DMA record into the command list at $0182.
 * Then either trigger it immediately (if in force blank) or mark
 * the queue as pending for the NMI handler.
 */
void mp_01DE_queue_dma(const uint8_t *record, int len) {
    /* Clear DMA pending flag */
    bus_wram_write16(0x0202, 0x0000);

    uint16_t write_pos = bus_wram_read16(0x0204);

    /* Copy record into command buffer at $0182 + write_pos */
    for (int i = 0; i < len; i++) {
        bus_wram_write8(0x0182 + write_pos + i, record[i]);
    }

    /* Advance write pointer (records are 9 bytes) */
    uint16_t new_pos = bus_wram_read16(0x0204) + 9;
    bus_wram_write16(0x0204, new_pos);

    /* Check if we should transfer immediately or queue for NMI */
    uint8_t inidisp = bus_wram_read8(0x0104);
    if (inidisp & 0x80) {
        /* Force blank active — check if NMI is enabled */
        uint8_t nmitimen = bus_wram_read8(0x0122);
        if (!(nmitimen & 0x80)) {
            /* NMI disabled + force blank: transfer now */
            func_table_call(0x01E6D0);
            return;
        }
    }

    /* Mark DMA queue as pending for NMI handler */
    bus_wram_write16(0x0202, 0x0001);
}

/* ========================================================================
 * $01:E6D0 — Direct DMA queue execution (bypass NMI)
 *
 * Same as E6CA but called directly when in force blank.
 * We just call our existing mp_01E6CA implementation.
 * ======================================================================== */
void mp_01E6D0(void) {
    /* Set the pending flag so E6CA processes the queue */
    bus_wram_write16(0x0202, 0x0001);
    mp_01E6CA();
}

/* ========================================================================
 * Register all graphics init functions.
 * ======================================================================== */
void mp_register_gfxinit(void) {
    func_table_register(0x01E87B, mp_01E87B);
    func_table_register(0x01E88A, mp_01E88A);
    func_table_register(0x0087EE, mp_0087EE);
    func_table_register(0x008A75, mp_008A75);
    func_table_register(0x0089B1, mp_0089B1);
    func_table_register(0x0089C3, mp_0089C3);
    func_table_register(0x008A16, mp_008A16);
    func_table_register(0x008A39, mp_008A39);
    func_table_register(0x01DE97, mp_01DE97);
    func_table_register(0x01DEB2, mp_01DEB2);
    func_table_register(0x01DECD, mp_01DECD);
    func_table_register(0x01E6D0, mp_01E6D0);
}
