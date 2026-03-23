/*
 * Mario Paint — Recompiled boot chain and system functions.
 *
 * Translated from the Mario Paint (JU) 65816 disassembly.
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 *
 * Key entry points:
 *   $00:8000 — Reset vector (hardware init)
 *   $00:80D4 — NMI handler (VBlank)
 *   $00:865A — Main loop
 *   $00:833B — Register init
 *   $00:837D — Graphics/display setup
 *   $00:849D — VRAM clearing
 *   $00:84AF — OAM initialization
 *   $00:84D5 — Main application init
 */

#include <mp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* Defined in main.c — set by mp_01E2CE when window is closed */
extern bool g_quit;

/* Forward declarations for functions in this file */
void mp_008000(void);  /* Reset vector */
void mp_008013(void);  /* Hardware init (post-mode switch) */
void mp_00833B(void);  /* Register initialization */
void mp_00837D(void);  /* Graphics & display setup */
void mp_00849D(void);  /* VRAM clearing */
void mp_0084AF(void);  /* OAM initialization */
void mp_0084D5(void);  /* Main application init */
void mp_0080D4(void);  /* NMI handler */
void mp_00865A(void);  /* Main loop */

/*
 * SNES hardware register addresses.
 * These are the standard SNES I/O register names from the disassembly.
 */
#define REG_INIDISP     0x2100  /* Screen display register */
#define REG_OBSEL       0x2101  /* OAM size and data area */
#define REG_OAMADDL     0x2102  /* OAM address low */
#define REG_OAMADDH     0x2103  /* OAM address high */
#define REG_OAMDATA     0x2104  /* OAM data write */
#define REG_BGMODE      0x2105  /* BG mode and tile size */
#define REG_MOSAIC      0x2106  /* Mosaic */
#define REG_BG1SC       0x2107  /* BG1 tilemap address/size */
#define REG_BG2SC       0x2108  /* BG2 tilemap address/size */
#define REG_BG3SC       0x2109  /* BG3 tilemap address/size */
#define REG_BG4SC       0x210A  /* BG4 tilemap address/size */
#define REG_BG12NBA     0x210B  /* BG1&2 tile data designation */
#define REG_BG34NBA     0x210C  /* BG3&4 tile data designation */
#define REG_BG1HOFS     0x210D  /* BG1 horizontal scroll */
#define REG_BG1VOFS     0x210E  /* BG1 vertical scroll */
#define REG_BG2HOFS     0x210F  /* BG2 horizontal scroll */
#define REG_BG2VOFS     0x2110  /* BG2 vertical scroll */
#define REG_BG3HOFS     0x2111  /* BG3 horizontal scroll */
#define REG_BG3VOFS     0x2112  /* BG3 vertical scroll */
#define REG_BG4HOFS     0x2113  /* BG4 horizontal scroll */
#define REG_BG4VOFS     0x2114  /* BG4 vertical scroll */
#define REG_VMAIN       0x2115  /* VRAM address increment */
#define REG_VMADDL      0x2116  /* VRAM address low */
#define REG_VMADDH      0x2117  /* VRAM address high */
#define REG_VMDATAL     0x2118  /* VRAM data write low */
#define REG_VMDATAH     0x2119  /* VRAM data write high */
#define REG_M7SEL       0x211A  /* Mode 7 settings */
#define REG_M7A         0x211B  /* Mode 7 matrix A */
#define REG_M7B         0x211C  /* Mode 7 matrix B */
#define REG_M7C         0x211D  /* Mode 7 matrix C */
#define REG_M7D         0x211E  /* Mode 7 matrix D */
#define REG_M7X         0x211F  /* Mode 7 center X */
#define REG_M7Y         0x2120  /* Mode 7 center Y */
#define REG_W12SEL      0x2123  /* BG1&2 window mask */
#define REG_W34SEL      0x2124  /* BG3&4 window mask */
#define REG_WOBJSEL     0x2125  /* OBJ/color window */
#define REG_WH0         0x2126  /* Window 1 left */
#define REG_WH1         0x2127  /* Window 1 right */
#define REG_WH2         0x2128  /* Window 2 left */
#define REG_WH3         0x2129  /* Window 2 right */
#define REG_WBGLOG      0x212A  /* BG window logic */
#define REG_WOBJLOG     0x212B  /* Color/OBJ window logic */
#define REG_TM          0x212C  /* Main screen layers */
#define REG_TS          0x212D  /* Sub screen layers */
#define REG_TMW         0x212E  /* Main screen window mask */
#define REG_TSW         0x212F  /* Sub screen window mask */
#define REG_CGWSEL      0x2130  /* Color math init */
#define REG_CGADSUB     0x2131  /* Color math select */
#define REG_COLDATA     0x2132  /* Fixed color data */
#define REG_SETINI      0x2133  /* Screen init settings */
#define REG_RDNMI       0x4210  /* NMI enable */
#define REG_NMITIMEN    0x4200  /* IRQ/NMI/joypad enable */
#define REG_WRIO        0x4201  /* Programmable I/O port */
#define REG_WRMPYA      0x4202  /* Multiplicand */
#define REG_WRMPYB      0x4203  /* Multiplier */
#define REG_WRDIVL      0x4204  /* Dividend low */
#define REG_WRDIVH      0x4205  /* Dividend high */
#define REG_WRDIVB      0x4206  /* Divisor */
#define REG_HTIMEL      0x4207  /* H-count timer low */
#define REG_HTIMEH      0x4208  /* H-count timer high */
#define REG_VTIMEL      0x4209  /* V-count timer low */
#define REG_VTIMEH      0x420A  /* V-count timer high */
#define REG_MDMAEN      0x420B  /* DMA enable */
#define REG_HDMAEN      0x420C  /* HDMA enable */
#define REG_MEMSEL      0x420D  /* Fast ROM enable */
#define REG_JOYSER0     0x4016  /* Joypad serial port 1 */
#define REG_APUIO0      0x2140  /* APU port 0 */

/* ========================================================================
 * $00:8000 — Reset Vector
 * ======================================================================== */
void mp_008000(void) {
    /* SEI; CLC; XCE — switch to native mode */
    OP_SEI();
    OP_CLC();
    op_xce();

    /* REP #$20 — 16-bit accumulator */
    op_rep(0x20);

    /* LDA #$0000; STA $0004E2 — clear demo flag */
    op_lda_imm16(0x0000);
    bus_wram_write16(0x04E2, CPU_A16());

    /* LDA #$0000; STA $000008 */
    op_lda_imm16(0x0000);
    bus_wram_write16(0x0008, CPU_A16());

    /* Fall through to hardware init */
    mp_008013();
}

/* ========================================================================
 * $00:8013 — Hardware initialization
 * ======================================================================== */
void mp_008013(void) {
    /* SEP #$20 — 8-bit accumulator */
    op_sep(0x20);

    /* Disable NMI, DMA, HDMA */
    bus_write8(0x00, REG_NMITIMEN, 0x00);
    bus_write8(0x00, REG_MDMAEN, 0x00);
    bus_write8(0x00, REG_HDMAEN, 0x00);

    /* Force blank */
    bus_write8(0x00, REG_INIDISP, 0x80);

    /* Clear screen settings and joypad serial port */
    bus_write8(0x00, REG_SETINI, 0x00);
    bus_write8(0x00, REG_JOYSER0, 0x00);

    /* REP #$30 — 16-bit A/X/Y */
    op_rep(0x30);

    /* Set stack to $1FFF */
    g_cpu.X = 0x1FFF;
    OP_TCS();

    /* Set DP = $0000 */
    g_cpu.Y = 0x0000;
    /* PHY; PLD */
    g_cpu.DP = 0x0000;

    /* Clear WRAM $000A-$1FFE with zeros */
    {
        uint8_t *wram = bus_get_wram();
        /* Compute a checksum of DP $00-$01 (like the original code does with ADC) */
        uint16_t checksum = 0;
        for (int i = 0x1FFF; i > 0; i--) {
            checksum += wram[i];
        }
        bus_wram_write16(0x02, checksum);

        /* Zero out $000A-$1FFE */
        for (uint32_t addr = 0x0008; addr < 0x1FFF; addr += 2) {
            bus_wram_write16(addr, 0x0000);
        }
    }

    /* SEP #$30 */
    op_sep(0x30);

    /* Set up pointer at DP $CC-$CE to DATA_188000 (music/sound data)
     * In LoROM, bank $18 offset $8000 = ROM offset $0C0000 */
    bus_wram_write8(0xCC, 0x00);  /* low byte of $8000 */
    bus_wram_write8(0xCD, 0x80);  /* high byte of $8000 */
    bus_wram_write8(0xCE, 0x18);  /* bank $18 */

    /* JSL CODE_01DF25 — upload SPC700 audio engine */
    func_table_call(0x01DF25);

    /* LDA #$02; JSL CODE_01D308 — init audio state */
    op_lda_imm8(0x02);
    func_table_call(0x01D308);

    /* Disable NMI again */
    bus_write8(0x00, REG_NMITIMEN, 0x00);
    bus_wram_write8(0x0122, 0x00);

    /* Force blank + brightness $0F */
    bus_write8(0x00, REG_INIDISP, 0x8F);
    bus_wram_write8(0x0104, 0x8F);

    /* Clear VRAM */
    mp_00849D();

    /* JSL CODE_01E06F — additional init */
    func_table_call(0x01E06F);

    /* Initialize registers and graphics */
    mp_00833B();
    mp_00837D();

    /* OAM address setup */
    bus_write8(0x00, REG_OAMADDH, 0x80);
    bus_write8(0x00, REG_OAMADDL, 0x00);

    /* OAM init */
    mp_0084AF();

    /* More initialization calls */
    func_table_call(0x01E06F);
    func_table_call(0x01E2F3);

    /* Configure NMITIMEN mirror */
    uint8_t val = bus_wram_read8(0x0122);
    val &= 0xCF;
    bus_wram_write8(0x0122, val);

    /* REP #$30 */
    op_rep(0x30);

    /* Set up pointer at $017E-$0180 */
    bus_wram_write16(0x017E, 0x80AD);
    bus_wram_write16(0x0180, 0x0000);

    /* CLI — enable interrupts */
    OP_CLI();

    /* Clear $0220 */
    bus_wram_write16(0x0220, 0x0000);

    /* Jump to main application init */
    mp_0084D5();
}

/* ========================================================================
 * $00:833B — Register Initialization
 * ======================================================================== */
void mp_00833B(void) {
    /* Enable auto-joypad read */
    bus_write8(0x00, REG_NMITIMEN, 0x01);
    bus_wram_write8(0x0122, 0x01);

    /* Clear I/O and math registers */
    bus_write8(0x00, REG_WRIO, 0x00);
    bus_write8(0x00, REG_WRMPYA, 0x00);
    bus_write8(0x00, REG_WRMPYB, 0x00);
    bus_write8(0x00, REG_WRDIVL, 0x00);
    bus_write8(0x00, REG_WRDIVH, 0x00);
    bus_write8(0x00, REG_WRDIVB, 0x00);

    /* Clear timer registers and mirrors */
    bus_write8(0x00, REG_HTIMEL, 0x00);
    bus_wram_write8(0x0125, 0x00);
    bus_write8(0x00, REG_HTIMEH, 0x00);
    bus_wram_write8(0x0126, 0x00);
    bus_write8(0x00, REG_VTIMEL, 0x00);
    bus_wram_write8(0x0123, 0x00);
    bus_write8(0x00, REG_VTIMEH, 0x00);
    bus_wram_write8(0x0124, 0x00);

    /* Disable DMA/HDMA */
    bus_write8(0x00, REG_MDMAEN, 0x00);
    bus_write8(0x00, REG_HDMAEN, 0x00);
    bus_wram_write8(0x0127, 0x00);

    /* Disable FastROM */
    bus_write8(0x00, REG_MEMSEL, 0x00);
    bus_wram_write8(0x0128, 0x00);
}

/* ========================================================================
 * $00:837D — Graphics & Display Setup
 * ======================================================================== */
void mp_00837D(void) {
    /* Force blank + max brightness */
    bus_write8(0x00, REG_INIDISP, 0x8F);
    bus_wram_write8(0x0104, 0x8F);

    /* OBJ size and tile base */
    bus_write8(0x00, REG_OBSEL, 0x02);
    bus_wram_write8(0x0105, 0x02);

    /* OAM address */
    bus_write8(0x00, REG_OAMADDH, 0x00);
    bus_wram_write8(0x0107, 0x00);
    bus_write8(0x00, REG_OAMADDL, 0x00);
    bus_wram_write8(0x0106, 0x00);

    /* Clear OAM data write */
    bus_write8(0x00, REG_OAMDATA, 0x00);
    bus_write8(0x00, REG_OAMDATA, 0x00);

    /* BG Mode 9 (Mode 1, BG3 priority) with tile sizes */
    bus_write8(0x00, REG_BGMODE, 0x09);
    bus_wram_write8(0x0108, 0x09);

    /* No mosaic */
    bus_write8(0x00, REG_MOSAIC, 0x00);
    bus_wram_write8(0x0109, 0x00);

    /* BG tilemap addresses: BG1=$30, BG2=$34, BG3=$38, BG4=$00 */
    bus_write8(0x00, REG_BG1SC, 0x30);
    bus_wram_write8(0x010A, 0x30);
    bus_write8(0x00, REG_BG2SC, 0x34);
    bus_wram_write8(0x010B, 0x34);
    bus_write8(0x00, REG_BG3SC, 0x38);
    bus_wram_write8(0x010C, 0x38);
    bus_write8(0x00, REG_BG4SC, 0x00);
    bus_wram_write8(0x010D, 0x00);

    /* BG tile data addresses: BG1&2=$06, BG3&4=$66 */
    bus_write8(0x00, REG_BG12NBA, 0x06);
    bus_wram_write8(0x010E, 0x06);
    bus_write8(0x00, REG_BG34NBA, 0x66);
    bus_wram_write8(0x010F, 0x66);

    /* Clear all BG scroll registers */
    bus_write8(0x00, REG_BG1HOFS, 0x00);
    bus_write8(0x00, REG_BG1HOFS, 0x00);
    bus_write8(0x00, REG_BG1VOFS, 0x00);
    bus_write8(0x00, REG_BG1VOFS, 0x00);
    bus_write8(0x00, REG_BG2HOFS, 0x00);
    bus_write8(0x00, REG_BG2HOFS, 0x00);
    bus_write8(0x00, REG_BG2VOFS, 0x00);
    bus_write8(0x00, REG_BG2VOFS, 0x00);
    bus_write8(0x00, REG_BG3HOFS, 0x00);
    bus_write8(0x00, REG_BG3HOFS, 0x00);
    bus_write8(0x00, REG_BG3VOFS, 0x00);
    bus_write8(0x00, REG_BG3VOFS, 0x00);
    bus_write8(0x00, REG_BG4HOFS, 0x00);
    bus_write8(0x00, REG_BG4HOFS, 0x00);
    bus_write8(0x00, REG_BG4VOFS, 0x00);
    bus_write8(0x00, REG_BG4VOFS, 0x00);

    /* VRAM increment, Mode 7 */
    bus_write8(0x00, REG_VMAIN, 0x00);
    bus_write8(0x00, REG_M7SEL, 0x00);
    bus_wram_write8(0x0110, 0x00);
    bus_write8(0x00, REG_M7A, 0x00);
    bus_write8(0x00, REG_M7B, 0x00);
    bus_write8(0x00, REG_M7C, 0x00);
    bus_write8(0x00, REG_M7D, 0x00);
    bus_write8(0x00, REG_M7X, 0x00);
    bus_write8(0x00, REG_M7Y, 0x00);

    /* Window mask settings */
    bus_write8(0x00, REG_W12SEL, 0x00);
    bus_wram_write8(0x0111, 0x00);
    bus_write8(0x00, REG_W34SEL, 0x00);
    bus_wram_write8(0x0112, 0x00);
    bus_write8(0x00, REG_WOBJSEL, 0x00);
    bus_wram_write8(0x0113, 0x00);
    bus_write8(0x00, REG_WBGLOG, 0x00);
    bus_wram_write8(0x0118, 0x00);
    bus_write8(0x00, REG_WOBJLOG, 0x00);
    bus_wram_write8(0x0119, 0x00);

    /* Window positions */
    bus_write8(0x00, REG_WH0, 0x00);
    bus_wram_write8(0x0114, 0x00);
    bus_write8(0x00, REG_WH2, 0x00);
    bus_wram_write8(0x0116, 0x00);
    bus_write8(0x00, REG_WH1, 0x01);
    bus_wram_write8(0x0115, 0x01);
    bus_write8(0x00, REG_WH3, 0x01);
    bus_wram_write8(0x0117, 0x01);

    /* Main/sub screen layers: BG1+BG2+BG4+OBJ = $13 */
    bus_write8(0x00, REG_TM, 0x13);
    bus_wram_write8(0x011A, 0x13);
    bus_write8(0x00, REG_TMW, 0x13);
    bus_wram_write8(0x011C, 0x13);
    bus_write8(0x00, REG_TS, 0x13);
    bus_wram_write8(0x011B, 0x13);
    bus_write8(0x00, REG_TSW, 0x13);
    bus_wram_write8(0x011D, 0x13);

    /* Color math: $30 init, $00 select */
    bus_write8(0x00, REG_CGWSEL, 0x30);
    bus_wram_write8(0x011E, 0x30);
    bus_write8(0x00, REG_CGADSUB, 0x00);
    bus_wram_write8(0x011F, 0x00);

    /* Fixed color: $E0 */
    bus_write8(0x00, REG_COLDATA, 0xE0);
    bus_wram_write8(0x0120, 0xE0);

    /* Screen init settings */
    bus_write8(0x00, REG_SETINI, 0x00);
    bus_wram_write8(0x0121, 0x00);
}

/* ========================================================================
 * $00:849D — VRAM Clearing
 * ======================================================================== */
void mp_00849D(void) {
    /* REP #$30 */
    op_rep(0x30);

    /* LDA #$0000, LDX #$2000, LDY #$2000 */
    /* JSL CODE_01E060 — fill VRAM with A for X*Y words */
    /* This fills $2000 words (8KB) of VRAM starting at address 0 with zeros */
    op_lda_imm16(0x0000);
    g_cpu.X = 0x2000;
    g_cpu.Y = 0x2000;
    func_table_call(0x01E060);

    /* SEP #$30 */
    op_sep(0x30);
}

/* ========================================================================
 * $00:84AF — OAM Initialization
 * ======================================================================== */
void mp_0084AF(void) {
    /* REP #$30 */
    op_rep(0x30);

    /* Initialize OAM buffer areas in WRAM */
    /* JSL CODE_01E024 — fill OAM low table */
    op_lda_imm16(0x3DFE);
    func_table_call(0x01E024);

    /* JSL CODE_01E033 — fill OAM high table */
    op_lda_imm16(0x3FFF);
    func_table_call(0x01E033);

    /* JSL CODE_01E042 */
    op_lda_imm16(0x03FE);
    func_table_call(0x01E042);

    /* SEP #$30 */
    op_sep(0x30);

    /* Additional OAM setup calls */
    func_table_call(0x01DE97);
    func_table_call(0x01DEB2);
    func_table_call(0x01DECD);
}

/* ========================================================================
 * $00:84D5 — Main Application Initialization
 *
 * Sets up the drawing canvas, cursor position, loads graphics,
 * initializes the audio engine, and enters the main loop.
 * ======================================================================== */
void mp_0084D5(void) {
    /* REP #$30 */
    op_rep(0x30);

    /* Clear various state variables */
    bus_wram_write16(0x0538, 0x0000);

    /* BG tilemap config mirrors */
    bus_wram_write16(0x09CF, 0x0030);
    bus_wram_write16(0x012A, 0x0034);
    bus_wram_write16(0x012C, 0x0010);

    /* Clear clock cursor and other state */
    bus_wram_write16(0x09A7, 0x0000);
    bus_wram_write16(0x09D4, 0x0000);
    bus_wram_write16(0x04BF, 0x0000);
    bus_wram_write16(0x04C4, 0x0000);

    /* Mouse sensitivity = 1 */
    bus_wram_write16(0x04C1, 0x0001);

    /* Initial cursor position: center of screen (128, 128) */
    bus_wram_write16(0x04DC, 0x0080);  /* Cursor X */
    bus_wram_write16(0x04DE, 0x0080);  /* Cursor Y */

    /* SEP #$20 */
    op_sep(0x20);

    /* Set up canvas buffer pointer at DP $0A-$0C to $7E:A000 */
    bus_wram_write8(0x0A, 0x00);  /* low byte */
    bus_wram_write8(0x0B, 0xA0);  /* high byte */
    bus_wram_write8(0x0C, 0x7E);  /* bank */
    bus_wram_write8(0x0F, 0x7E);  /* bank (duplicate) */

    /* REP #$30 */
    op_rep(0x30);

    bus_wram_write16(0x0212, 0x007E);

    /* Clear bank $7F:0000-$FFFF (64KB scratch buffer) */
    {
        uint8_t *wram = bus_get_wram();
        /* Bank $7F = WRAM $10000-$1FFFF */
        memset(wram + 0x10000, 0, 0x10000);
    }

    /* Clear $0206 and call initial graphics setup */
    bus_wram_write16(0x0206, 0x0000);
    func_table_call(0x008A75);

    bus_wram_write16(0x0208, 0x0000);
    bus_wram_write16(0x0206, bus_wram_read16(0x0206) + 1);

    /* Wait loop: call CODE_01E2CE until $0208 is nonzero */
    {
        int wait_count = 0;
        do {
            func_table_call(0x01E2CE);
            if (g_quit) return;
            wait_count++;
            if (wait_count > 30) {
                bus_wram_write16(0x0208, 0x0001);
                break;
            }
        } while (bus_wram_read16(0x0208) == 0);
    }

    /* Set up animation parameters */
    bus_wram_write16(0x0448, 0x0100);

    /* JSL CODE_01E238 with A = checksum from DP $02 */
    op_lda_imm16(bus_wram_read16(0x02));
    func_table_call(0x01E238);

    /* Clear tool/canvas state */
    bus_wram_write16(0x00B8, 0x0000);  /* spray can */
    bus_wram_write16(0x1992, 0x0000);  /* erase tool */
    bus_wram_write16(0x0EB8, 0x0000);
    bus_wram_write16(0x00A6, 0x0000);  /* palette row */

    /* Canvas bounds */
    bus_wram_write16(0x19AE, 0x0004);
    bus_wram_write16(0x19B0, 0x00FC);
    bus_wram_write16(0x19B2, 0x001C);
    bus_wram_write16(0x19B4, 0x00C4);

    /* JSL CODE_018000 — major subsystem init (title screen) */
    func_table_call(0x018000);

    /* JSR CODE_00E25C — additional canvas setup */
    func_table_call(0x00E25C);

    /* JSL CODE_01E895 */
    func_table_call(0x01E895);

    /* JSR CODE_00DE8E */
    func_table_call(0x00DE8E);

    /* SEP #$20 */
    op_sep(0x20);

    /* Additional init calls */
    func_table_call(0x01D388);
    func_table_call(0x01E30E);

    /* Set up music data pointer to bank $1A */
    bus_wram_write8(0xCC, 0x00);
    bus_wram_write8(0xCD, 0x80);
    bus_wram_write8(0xCE, 0x1A);

    /* Send $FF to APU port 0 */
    bus_write8(0x00, REG_APUIO0, 0xFF);

    /* Upload audio data */
    func_table_call(0x01DF25);

    /* REP #$20 */
    op_rep(0x20);

    /* Final initialization */
    func_table_call(0x01E2F3);

    bus_wram_write16(0x1012, 0x0001);

    /* LDA #$0003; JSL CODE_01D2BF */
    op_lda_imm16(0x0003);
    func_table_call(0x01D2BF);

    /* Clear more state */
    bus_wram_write16(0x19C2, 0x0000);
    bus_write16(0x7E, 0x3FFC, 0x0000);
    bus_wram_write16(0x19FA, 0x0000);
    bus_wram_write16(0x19AC, 0x0000);
    bus_wram_write16(0x19A8, 0xFFFF);

    /* JSR CODE_0087EE */
    func_table_call(0x0087EE);

    /* Initialize drawing with defaults */
    op_lda_imm16(0x0000);
    func_table_call(0x00B66C);
    op_lda_imm16(0x0000);
    func_table_call(0x00B6F4);

    /* JSR CODE_00E64F */
    func_table_call(0x00E64F);

    /* Sync frame */
    func_table_call(0x01E2CE);

    /* Set initial state */
    bus_wram_write16(0x09A9, 0xFFFF);

    /* Push args and call CODE_009FC4 */
    /* Original pushes three 16-bit values onto stack as args:
     * #$0000, #$0000, #$0001 */
    /* For now, call directly — the called function reads from stack */
    op_lda_imm16(0x0000);
    op_pha16();
    op_pha16();
    op_lda_imm16(0x0001);
    op_pha16();
    func_table_call(0x009FC4);
    /* Clean up stack (3 words pushed) */
    g_cpu.S += 6;

    /* More frame sync */
    func_table_call(0x01E2CE);
    func_table_call(0x01E794);

    /* Set drawing area bounds */
    bus_wram_write16(0x04D4, 0xFFF0);
    bus_wram_write16(0x04D8, 0x0008);
    bus_wram_write16(0x04D6, 0x0110);
    bus_wram_write16(0x04DA, 0x00D8);
    bus_wram_write16(0x04D0, 0x0000);

    func_table_call(0x01E06F);

    /* Clear final state variables */
    bus_wram_write16(0x09D1, 0x0000);
    bus_wram_write16(0x00A6, 0x0000);  /* palette row */
    bus_wram_write16(0x09AB, 0x0000);
    bus_wram_write16(0x00AA, 0x0000);
    bus_wram_write16(0x00AE, 0x0000);
    bus_wram_write16(0x1992, 0x0000);  /* erase tool */
    bus_wram_write16(0x1994, 0x0000);  /* erase size */
    bus_wram_write16(0x0EB8, 0x0000);
    bus_wram_write16(0x0EBC, 0x0000);
    bus_wram_write16(0x0EBA, 0x0000);
    bus_wram_write16(0x1988, 0x0000);

    bus_wram_write16(0x1A08, 0x0002);
    bus_write16(0x7E, 0x3FFE, 0x0002);

    bus_wram_write16(0x1B18, 0x0000);
    bus_wram_write16(0x09D6, 0x0000);

    func_table_call(0x00BA78);
    func_table_call(0x00F39E);

    bus_wram_write16(0x09A1, 0x0140);

    /* Ensure canvas VRAM is clean and BG settings are correct.
     * Use force blank + DMA to clear VRAM $0000-$2FFF (canvas area).
     * The canvas buffer at $7E:A000 was zeroed by mp_008A75. */
    {
        uint8_t saved = bus_wram_read8(0x0104);
        bus_write8(0x00, 0x2100, 0x80);  /* Force blank */

        /* Ensure BG tile data designations are correct for canvas */
        bus_write8(0x00, 0x210B, 0x06);
        bus_wram_write8(0x010E, 0x06);
        bus_write8(0x00, 0x210C, 0x66);
        bus_wram_write8(0x010F, 0x66);

        /* DMA the zeroed canvas buffer to VRAM.
         * Source: $7E:A000 (canvas buffer, zeroed)
         * Dest: VRAM $0000 (BG2 tile data)
         * Size: $6000 bytes */
        bus_write8(0x00, 0x2115, 0x80);  /* VMAIN: inc on high */
        bus_write8(0x00, 0x2116, 0x00);  /* VRAM addr = $0000 */
        bus_write8(0x00, 0x2117, 0x00);
        bus_write8(0x00, 0x4300, 0x01);  /* DMA mode: 2-reg word */
        bus_write8(0x00, 0x4301, 0x18);  /* Dest: $2118 */
        bus_write8(0x00, 0x4302, 0x00);  /* Src lo: $00 */
        bus_write8(0x00, 0x4303, 0xA0);  /* Src hi: $A0 */
        bus_write8(0x00, 0x4304, 0x7E);  /* Src bank: $7E */
        bus_write8(0x00, 0x4305, 0x00);  /* Size lo: $00 */
        bus_write8(0x00, 0x4306, 0x60);  /* Size hi: $60 = $6000 */
        bus_write8(0x00, 0x420B, 0x01);  /* Trigger DMA ch0 */

        bus_write8(0x00, 0x2100, saved);  /* Restore display */
    }

    /* Rebuild BG1 tilemap for canvas mode.
     * mp_018F52 (title transition) filled BG1 tilemap with $24E0.
     * Need to restore the canvas tilemap with proper border tiles
     * and canvas area, then DMA it to VRAM $3000. */
    mp_0089C3();   /* Border tiles */
    mp_0089B1();   /* Canvas tilemap from ROM */
    mp_01DE97();   /* Queue BG1 tilemap DMA */
    mp_008A16();   /* BG2 tilemap (includes DMA queue) */
    mp_008A39();   /* BG3 tilemap (includes DMA queue) */

    /* Set up ongoing canvas DMA refresh */
    bus_wram_write16(0x0208, 0x0000);
    bus_wram_write16(0x0206, 0x0001);

    printf("Mario Paint recomp: canvas mode ready\n");
    printf("  INIDISP=$%02X BGMODE=$%02X BG12NBA=$%02X BG34NBA=$%02X\n",
        bus_wram_read8(0x0104), bus_wram_read8(0x0108),
        bus_wram_read8(0x010E), bus_wram_read8(0x010F));
    printf("  BG1SC=$%02X BG2SC=$%02X BG3SC=$%02X TM=$%02X\n",
        bus_wram_read8(0x010A), bus_wram_read8(0x010B),
        bus_wram_read8(0x010C), bus_wram_read8(0x011A));

    /* Dump PPU state for debugging */
    snesrecomp_dump_ppu("ppudump.txt");

    /* Fall through to main loop */
    mp_00865A();
}

/* ========================================================================
 * $00:80D4 — NMI Handler (VBlank Interrupt)
 * ======================================================================== */
void mp_0080D4(void) {
    /* The NMI handler saves all registers, sets DP=0, DB=0 */
    /* (register save/restore handled by the frame loop) */

    /* SEP #$30 */
    op_sep(0x30);

    /* Read RDNMI to acknowledge */
    bus_read8(0x00, REG_RDNMI);

    /* JSL CODE_01E429 — DMA queue processing */
    func_table_call(0x01E429);

    /* JSL CODE_01E460 — HDMA setup */
    func_table_call(0x01E460);

    /* JSL CODE_01E59B — PPU register mirror writeback */
    func_table_call(0x01E59B);

    /* Check if full NMI processing is needed */
    if (bus_wram_read8(0x016A) != 0) {
        /* JSL CODE_01E500 — OAM DMA transfer */
        func_table_call(0x01E500);

        /* JSL CODE_01E6CA — palette DMA */
        func_table_call(0x01E6CA);

        /* JSL CODE_01E103 — BG scroll update */
        func_table_call(0x01E103);

        /* JSL CODE_01E60C */
        func_table_call(0x01E60C);

        /* JSL CODE_01E66B */
        func_table_call(0x01E66B);

        /* JSL CODE_01E1AB */
        func_table_call(0x01E1AB);
    }

    /* JSR CODE_0081CA — bomb icon animation */
    func_table_call(0x0081CA);

    /* JSR CODE_00823C — display animation */
    func_table_call(0x00823C);

    /* Increment frame counter */
    bus_wram_write16(0x016C, bus_wram_read16(0x016C) + 1);

    /* JSL CODE_01E747 — brightness control */
    func_table_call(0x01E747);

    /* JSL CODE_01D9E1 — audio processing */
    func_table_call(0x01D9E1);

    /* JSR CODE_00815B — input processing */
    func_table_call(0x00815B);

    /* JSL CODE_01DCB9 */
    func_table_call(0x01DCB9);

    /* JSR CODE_008187 */
    func_table_call(0x008187);

    /* JSL CODE_01DDB8, CODE_01DDE1, CODE_01DE2D */
    func_table_call(0x01DDB8);
    func_table_call(0x01DDE1);
    func_table_call(0x01DE2D);

    /* Decrement timers */
    bus_wram_write16(0x1B1D, bus_wram_read16(0x1B1D) - 1);
    bus_wram_write16(0x1B20, bus_wram_read16(0x1B20) - 1);

    /* REP #$30 */
    op_rep(0x30);

    /* JSL CODE_0FC000 — bank $0F routine */
    func_table_call(0x0FC000);

    /* Check for soft reset combo: L+R+Start+Select on port 2 */
    if (bus_wram_read16(0x099F) != 0) {
        if (bus_wram_read16(0x0134) == (0x0020 | 0x0010 | 0x1000 | 0x2000)) {
            /* Soft reset — jump to CODE_0082A7 */
            func_table_call(0x0082A7);
            return;
        }
    }

    /* Clear NMI processing flag */
    bus_wram_write16(0x016A, 0x0000);
}

/* ========================================================================
 * $00:865A — Main Loop
 *
 * Runs every frame: processes mouse input, dispatches game logic,
 * handles rendering, and syncs to VBlank.
 * ======================================================================== */
void mp_00865A(void) {
    while (!g_quit) {
        /* REP #$30 */
        op_rep(0x30);

        /* LDY #$0004; STY $0446 — set some state */
        g_cpu.Y = 0x0004;
        bus_wram_write16(0x0446, 0x0004);

        /* JSR CODE_008B48 — cursor/mouse movement */
        func_table_call(0x008B48);

        /* JSL CODE_008683 — game logic dispatch */
        func_table_call(0x008683);

        /* JSR CODE_0087A8 — post-logic processing */
        func_table_call(0x0087A8);

        /* JSR CODE_009378 — drawing/rendering */
        func_table_call(0x009378);

        /* Check animation flag */
        if (bus_wram_read16(0x0545) != 0) {
            /* JSR CODE_00878F — animation update */
            func_table_call(0x00878F);
            if (g_cpu.flag_C) {
                goto frame_sync;
            }
        }

        /* JSL CODE_01E09B — screen enable / brightness update */
        func_table_call(0x01E09B);

frame_sync:
        /* JSL CODE_01E2CE — wait for VBlank / frame sync */
        func_table_call(0x01E2CE);
    }
}

/* ========================================================================
 * Register all recompiled functions in the dispatch table.
 * ======================================================================== */
void mp_register_all(void) {
    /* Bank 00 — boot chain and system */
    func_table_register(0x008000, mp_008000);
    func_table_register(0x008013, mp_008013);
    func_table_register(0x00833B, mp_00833B);
    func_table_register(0x00837D, mp_00837D);
    func_table_register(0x00849D, mp_00849D);
    func_table_register(0x0084AF, mp_0084AF);
    func_table_register(0x0084D5, mp_0084D5);
    func_table_register(0x0080D4, mp_0080D4);
    func_table_register(0x00865A, mp_00865A);

    /* Bank 01 — DMA/PPU helpers */
    mp_register_bank01();

    /* Input / cursor */
    mp_register_input();

    /* Game logic dispatch */
    mp_register_gamelogic();

    /* Graphics init */
    mp_register_gfxinit();

    /* Sprite engine */
    mp_register_sprites();

    /* Canvas / UI */
    mp_register_canvas();

    /* Audio engine */
    mp_register_audio();

    /* Title screen */
    mp_register_title();

    /* Misc helpers */
    mp_register_helpers();

    /* Title loop / Bank 0F / click handler */
    mp_register_titleloop();

    /* Canvas interaction */
    mp_register_interact();

    /* Drawing tools */
    mp_register_tools();

    /* Drawing core */
    mp_register_draw();

    /* Miscellaneous */
    mp_register_misc();

    /* Shapes / remaining */
    mp_register_shapes();
}
