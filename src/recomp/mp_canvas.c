/*
 * Mario Paint — Recompiled canvas and UI setup routines.
 *
 * Tilemap builders, sprite slot init, and DMA batch loader.
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <mp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

#include <stdbool.h>

extern bool g_quit;

/* OAM buffer addresses */
#define OAM_BUF       0x0226
#define OAM_HI_BUF    0x0426

/* ========================================================================
 * $00:C414 — Standard canvas tilemap builder
 *
 * Copies $580 bytes of BG1 tilemap data from ROM $02:8000
 * to WRAM $7E:20C0. This is the main drawing canvas tilemap.
 * ======================================================================== */
void mp_00C414(void) {
    uint8_t *wram = bus_get_wram();
    for (int x = 0x057E; x >= 0; x -= 2) {
        uint16_t val = bus_read16(0x02, 0x8000 + x);
        wram[0x20C0 + x]     = (uint8_t)(val & 0xFF);
        wram[0x20C0 + x + 1] = (uint8_t)(val >> 8);
    }
    bus_wram_write16(0x19AC, 0x0000);
}

/* ========================================================================
 * $00:C3DE — Alternate page canvas tilemap builder
 *
 * Like C414 but picks from one of three page tables based on $19C2.
 * Page tables are at $02:8580, $02:8B00, $02:9080.
 * ======================================================================== */
void mp_00C3DE(void) {
    /* Page table pointers (within bank $02) */
    static const uint16_t page_ptrs[3] = { 0x8580, 0x8B00, 0x9080 };

    uint16_t page = bus_wram_read16(0x19C2);
    if (page > 2) page = 0;
    uint16_t src = page_ptrs[page];

    uint8_t *wram = bus_get_wram();
    for (int x = 0x057E; x >= 0; x -= 2) {
        uint16_t val = bus_read16(0x02, src + x);
        wram[0x20C0 + x]     = (uint8_t)(val & 0xFF);
        wram[0x20C0 + x + 1] = (uint8_t)(val >> 8);
    }
    bus_wram_write16(0x19AC, 0x0000);
}

/* ========================================================================
 * $01:9DFE — Sprite slot initialization
 *
 * Initializes a sprite animation slot from the data table at
 * $01:9E24. Each slot has 8 bytes of state at $0792+slot*8:
 *   +0/+2: X/Y position (stored doubled)
 *   +4: delay/frame state
 *   +6: palette/attribute base
 *
 * Input: A = slot index
 * ======================================================================== */
void mp_019DFE(void) {
    uint16_t slot = CPU_A16();
    uint16_t data_ofs = slot * 8;
    uint16_t slot_ofs = slot * 8;

    /* Data table at $01:9E24, 8 bytes per entry */
    uint16_t x_pos  = bus_read16(0x01, 0x9E24 + data_ofs);
    uint16_t y_pos  = bus_read16(0x01, 0x9E24 + data_ofs + 2);
    uint16_t frames = bus_read16(0x01, 0x9E24 + data_ofs + 4);
    uint16_t attr   = bus_read16(0x01, 0x9E24 + data_ofs + 6);

    /* Store positions doubled (original: ASL before store) */
    bus_wram_write16(0x0792 + slot_ofs, x_pos * 2);
    bus_wram_write16(0x0794 + slot_ofs, y_pos * 2);
    bus_wram_write16(0x0796 + slot_ofs, frames);
    bus_wram_write16(0x0798 + slot_ofs, attr);
}

/* ========================================================================
 * $01:CDE1 — Multi-frame DMA batch loader
 *
 * Processes a list of DMA command records pointed to by A (in bank $01).
 * Waits for the DMA queue to drain between batches.
 *
 * Record format: groups of 9-byte DMA commands terminated by $00,
 * followed by either a positive byte (continue) or $FF (end).
 *
 * Used to load large tile sets that span multiple frames.
 *
 * Input: A = pointer to DMA command list (within bank $01)
 * ======================================================================== */
void mp_01CDE1(void) {
    uint16_t list_ptr = CPU_A16();

    /* Wait for DMA queue to drain */
    while (!g_quit) {
        uint16_t pending = bus_wram_read16(0x0202) | bus_wram_read16(0x0204);
        if ((pending & 0x00FF) == 0) break;
        mp_01E2CE();
    }
    if (g_quit) return;

    /* Determine which state variable to set based on address range */
    /* DATA_01CEE4 = threshold for choosing $1966 vs $1964 */
    uint16_t threshold = 0xCEE4;  /* DATA_01CEE4 address */
    if (list_ptr >= threshold) {
        bus_wram_write16(0x1966, list_ptr);
    } else {
        bus_wram_write16(0x1964, list_ptr);
    }

    /* Process DMA command groups.
     * The command list is in bank $01 at the given pointer.
     * Format: sequences of 9-byte DMA records ending with $00,
     * followed by a continuation byte. $FF = end. */
    uint16_t y = 0;

    while (!g_quit) {
        /* Copy one group of DMA records into the queue at $0182 */
        uint16_t write_pos = bus_wram_read16(0x0204);
        uint16_t end_pos = write_pos + 9;

        /* Copy 9 bytes of DMA record */
        for (uint16_t i = write_pos; i < end_pos; i++) {
            uint8_t val = bus_read8(0x01, list_ptr + y);
            bus_wram_write8(0x0182 + i, val);
            y++;
        }

        /* Check for continuation records in same group */
        while (true) {
            uint8_t next = bus_read8(0x01, list_ptr + y);
            bus_wram_write8(0x0182 + end_pos, next);
            if (next == 0x00) break;  /* End of group */
            y++;
            end_pos++;
        }

        /* Mark DMA pending and sync frame */
        bus_wram_write16(0x0202, 0x0001);
        mp_01E2CE();
        if (g_quit) return;

        /* Check continuation byte */
        y++;
        int8_t cont = (int8_t)bus_read8(0x01, list_ptr + y);
        if (cont < 0) break;  /* $FF or $80+ = end */
    }
}

/* ========================================================================
 * $01:F9BB — Sprite renderer with offscreen culling (variant of F91E)
 *
 * Same as F91E but skips sprites whose X position falls in the
 * offscreen range ($100-$1C0). Used for large sprite objects.
 * ======================================================================== */
void mp_01F9BB(void) {
    uint16_t sprite_id = CPU_A16();
    uint16_t base_x = g_cpu.X;
    uint16_t base_y = g_cpu.Y;

    /* Look up sprite data pointer from table in bank $0D */
    uint16_t ptr = bus_read16(0x0D, 0xB018 + sprite_id * 2);

    /* Read sub-sprite count */
    uint16_t count = bus_read16(0x0D, ptr);
    ptr += 2;

    uint16_t oam_idx = bus_wram_read16(0x0446);

    for (uint16_t i = 0; i < count; i++) {
        if (oam_idx >= 0x0200) break;

        uint16_t x_off = bus_read16(0x0D, ptr);
        ptr += 2;

        uint16_t screen_x = (base_x + x_off) & 0x01FF;

        /* Cull offscreen sprites */
        if (screen_x >= 0x0100 && screen_x < 0x01C0) {
            ptr += 3;  /* Skip Y, tile, attr */
            continue;
        }

        uint16_t x_bit9 = x_off & 0x0200;
        bus_wram_write8(OAM_BUF + oam_idx, (uint8_t)(screen_x & 0xFF));
        oam_idx++;

        /* Upper OAM */
        {
            uint16_t sprite_num = (oam_idx - 1) / 4;
            uint16_t hi_bits = ((x_bit9 | screen_x) >> 8) & 0x03;
            uint16_t word_ofs = (sprite_num / 8) * 2;
            uint16_t bit_slot = sprite_num & 0x07;

            uint16_t pattern = bus_read16(0x0D, 0xB000 + (hi_bits & 0x03) * 2);
            uint16_t mask = bus_read16(0x0D, 0xB008 + bit_slot * 2);

            uint16_t cur = bus_wram_read16(OAM_HI_BUF + word_ofs);
            cur = ((pattern ^ cur) & mask) ^ cur;
            bus_wram_write16(OAM_HI_BUF + word_ofs, cur);
        }

        uint8_t y_off = bus_read8(0x0D, ptr);
        ptr++;
        bus_wram_write8(OAM_BUF + oam_idx, (uint8_t)((base_y + y_off) & 0xFF));
        oam_idx++;

        uint16_t tile_attr = bus_read16(0x0D, ptr);
        ptr += 2;
        bus_wram_write8(OAM_BUF + oam_idx, (uint8_t)(tile_attr & 0xFF));
        oam_idx++;
        bus_wram_write8(OAM_BUF + oam_idx, (uint8_t)(tile_attr >> 8));
        oam_idx++;
    }

    bus_wram_write16(0x0446, oam_idx);
}

/* ========================================================================
 * Register all canvas/UI functions.
 * ======================================================================== */
void mp_register_canvas(void) {
    func_table_register(0x00C414, mp_00C414);
    func_table_register(0x00C3DE, mp_00C3DE);
    func_table_register(0x019DFE, mp_019DFE);
    func_table_register(0x01CDE1, mp_01CDE1);
    func_table_register(0x01F9BB, mp_01F9BB);
}
