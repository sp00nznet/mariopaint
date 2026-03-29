/*
 * Mario Paint — Recompiled sprite engine.
 *
 * The sprite engine renders multi-sprite objects into OAM.
 * Sprite frame data is stored in ROM bank $0D at $B018 (pointer table)
 * with frame definitions containing per-tile X/Y offsets and tile IDs.
 *
 * Key routines:
 *   $01F91E — Simple sprite renderer (no flip/palette override)
 *   $01FA68 — Full sprite renderer (flip, palette, size override)
 *   $01962C — Sprite animation driver
 *
 * OAM buffer layout (per sprite, 4 bytes):
 *   byte 0: X position (low 8 bits)
 *   byte 1: Y position
 *   byte 2: Tile number
 *   byte 3: Attributes (palette, priority, flip)
 *
 * Upper OAM table: 2 bits per sprite (X bit 8, size flag)
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <mp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

/* OAM buffer addresses */
#define OAM_BUF       0x0226
#define OAM_HI_BUF    0x0426

/*
 * DATA_0DB000: Upper OAM bit patterns (4 entries, indexed by size/X-bit8)
 * DATA_0DB008: Upper OAM bit masks (8 entries, indexed by sprite-within-word)
 *
 * These are small constant tables in ROM bank $0D.
 * We read them via bus_read16 for correctness, but could also hardcode them.
 */

/* Helper: read 16-bit value from bank $0D */
static uint16_t rom0d_read16(uint16_t addr) {
    return bus_read16(0x0D, addr);
}

/*
 * Helper: update upper OAM bits for a sprite.
 *
 * The upper OAM table packs 4 sprites per byte (2 bits each).
 * This function updates the bits for the sprite at OAM index `sprite_idx`
 * with the given `bits` (0-3: bit0=X high, bit1=size).
 */
static void update_upper_oam(uint16_t sprite_idx, uint16_t bits) {
    /* sprite_idx is byte offset into OAM (0, 4, 8, ...) divided by 4 = sprite number
     * But we get the raw X index here. Compute sprite number. */
    uint16_t sprite_num = sprite_idx / 4;  /* 0-127 */
    uint16_t word_idx = (sprite_num / 8) * 2;  /* byte offset into upper OAM */
    uint16_t bit_pos = (sprite_num & 0x07);  /* which 2-bit slot within word */

    /* Read upper OAM bit pattern and mask from ROM tables */
    uint16_t pattern = rom0d_read16(0xB000 + (bits & 0x03) * 2);
    uint16_t mask = rom0d_read16(0xB008 + bit_pos * 2);

    uint16_t cur = bus_wram_read16(OAM_HI_BUF + word_idx);
    cur = (pattern ^ cur) & mask ^ cur;  /* Insert new bits */
    bus_wram_write16(OAM_HI_BUF + word_idx, cur);
}

/* ========================================================================
 * $01:F91E — Simple sprite renderer
 *
 * Renders a sprite object into OAM. Reads sprite frame data from ROM
 * and writes position + tile + attributes for each sub-sprite.
 *
 * Input:
 *   A = sprite ID (index into DATA_0DB018 pointer table)
 *   X = screen X position (9-bit)
 *   Y = screen Y position
 *
 * Sprite frame data format (at pointer from DATA_0DB018):
 *   word[0]: number of sub-sprites
 *   Then per sub-sprite:
 *     word[0]: X offset (signed, bit 9 = size flag)
 *     byte[0]: Y offset
 *     word[1]: tile + attributes
 * ======================================================================== */
void mp_01F91E(void) {
    uint16_t sprite_id = CPU_A16();
    uint16_t base_x = g_cpu.X;
    uint16_t base_y = g_cpu.Y;

    /* Look up sprite data pointer from table in bank $0D */
    uint16_t ptr = rom0d_read16(0xB018 + sprite_id * 2);

    /* Read sub-sprite count */
    uint16_t count = bus_read16(0x0D, ptr);
    ptr += 2;

    uint16_t oam_idx = bus_wram_read16(0x0446);

    for (uint16_t i = 0; i < count && oam_idx < 0x0200; i++) {
        /* Read X offset (word) */
        uint16_t x_off = bus_read16(0x0D, ptr);
        ptr += 2;

        /* Compute screen X */
        uint16_t screen_x = (base_x + x_off) & 0x01FF;
        uint16_t x_bit9 = x_off & 0x0200;  /* Size/X-high flag from offset */

        /* Combine size flag with position */
        uint16_t oam_x_word = x_bit9 | screen_x;

        /* Write X position (low byte) to OAM */
        bus_wram_write8(OAM_BUF + oam_idx, (uint8_t)(screen_x & 0xFF));
        oam_idx++;

        /* Update upper OAM bits */
        {
            uint16_t sprite_num = (oam_idx - 1) / 4;
            uint16_t hi_byte = (oam_x_word >> 8) & 0x03;

            uint16_t word_ofs = (sprite_num / 8) * 2;
            uint16_t bit_slot = (sprite_num & 0x07);

            uint16_t pattern = rom0d_read16(0xB000 + (hi_byte & 0x03) * 2);
            uint16_t mask = rom0d_read16(0xB008 + bit_slot * 2);

            uint16_t cur = bus_wram_read16(OAM_HI_BUF + word_ofs);
            cur = ((pattern ^ cur) & mask) ^ cur;
            bus_wram_write16(OAM_HI_BUF + word_ofs, cur);
        }

        /* Read Y offset (byte) and add to base Y */
        uint8_t y_off_lo = bus_read8(0x0D, ptr);
        ptr++;
        uint16_t screen_y = (base_y + y_off_lo) & 0xFF;
        bus_wram_write8(OAM_BUF + oam_idx, (uint8_t)screen_y);
        oam_idx++;

        /* Read tile + attributes (word) */
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
 * $01:FA68 — Full sprite renderer (with flip and palette override)
 *
 * Like F91E but supports:
 *   - Horizontal/vertical flip (bits 14-15 of sprite ID)
 *   - Palette override (bits 12-13 of sprite ID)
 *   - Position mirror offsets ($012E/$0130)
 *
 * Input:
 *   A = sprite ID (bits 0-12: ID, bit 13: V-flip, bit 14: H-flip,
 *       bits 12-13: palette bits)
 *   X = screen X, Y = screen Y
 * ======================================================================== */
void mp_01FA68(void) {
    uint16_t raw = CPU_A16();
    uint16_t base_x = g_cpu.X;
    uint16_t base_y = g_cpu.Y;

    /* Decode flags from high bits */
    uint16_t attr_flip = raw & 0xC000;   /* H-flip, V-flip */
    uint16_t attr_pal = raw & 0x3000;    /* Palette bits */

    /* Extract H-flip and V-flip flags */
    bool h_flip = (raw & 0x4000) != 0;
    bool v_flip = (raw & 0x8000) != 0;

    /* Get sprite ID (bits 0-12) */
    uint16_t sprite_id = (raw & 0x3FFF) >> 1;

    /* Look up sprite data pointer */
    uint16_t ptr = rom0d_read16(0xB018 + sprite_id * 2);

    /* Read sub-sprite count */
    uint16_t count = bus_read16(0x0D, ptr);
    ptr += 2;

    uint16_t oam_idx = bus_wram_read16(0x0446);

    for (uint16_t i = 0; i < count; i++) {
        if (oam_idx >= 0x0200) break;

        /* Read X offset */
        uint16_t x_off = bus_read16(0x0D, ptr);
        ptr += 2;

        /* Get mirror offset based on size flag */
        uint16_t mirror;
        if (x_off & 0x0200) {
            mirror = bus_wram_read16(0x0130);
        } else {
            mirror = bus_wram_read16(0x012E);
        }

        /* Apply H-flip to X offset */
        int16_t x_adj;
        if (h_flip) {
            x_adj = (int16_t)((x_off ^ 0x01FF) + 1);  /* Negate */
            x_adj -= (int16_t)mirror;
        } else {
            x_adj = (int16_t)(x_off & 0x01FF);
        }

        /* Compute screen X */
        uint16_t screen_x = ((uint16_t)(base_x + x_adj)) & 0x01FF;

        /* Skip if offscreen (X between $100 and $1C0) */
        if (screen_x >= 0x0100 && screen_x < 0x01C0) {
            ptr += 3;  /* Skip Y offset and tile data */
            continue;
        }

        /* Write X and upper OAM bits */
        uint16_t x_bit9 = x_off & 0x0200;
        bus_wram_write8(OAM_BUF + oam_idx, (uint8_t)(screen_x & 0xFF));
        oam_idx++;

        /* Upper OAM */
        {
            uint16_t sprite_num = (oam_idx - 1) / 4;
            uint16_t hi_bits = ((x_bit9 | screen_x) >> 8) & 0x03;

            uint16_t word_ofs = (sprite_num / 8) * 2;
            uint16_t bit_slot = sprite_num & 0x07;

            uint16_t pattern = rom0d_read16(0xB000 + (hi_bits & 0x03) * 2);
            uint16_t mask = rom0d_read16(0xB008 + bit_slot * 2);

            uint16_t cur = bus_wram_read16(OAM_HI_BUF + word_ofs);
            cur = ((pattern ^ cur) & mask) ^ cur;
            bus_wram_write16(OAM_HI_BUF + word_ofs, cur);
        }

        /* Read Y offset */
        uint8_t y_off = bus_read8(0x0D, ptr);
        ptr++;

        /* Apply V-flip */
        int16_t y_adj;
        if (v_flip) {
            y_adj = (int16_t)((y_off ^ 0xFF) + 1);
            y_adj -= (int16_t)mirror;
        } else {
            y_adj = (int16_t)y_off;
        }

        bus_wram_write8(OAM_BUF + oam_idx, (uint8_t)((base_y + y_adj) & 0xFF));
        oam_idx++;

        /* Read tile + attributes, apply flip and palette override */
        uint16_t tile_attr = bus_read16(0x0D, ptr);
        ptr += 2;

        tile_attr ^= attr_flip;   /* Apply H/V flip bits */
        tile_attr |= attr_pal;    /* Apply palette override */

        bus_wram_write8(OAM_BUF + oam_idx, (uint8_t)(tile_attr & 0xFF));
        oam_idx++;
        bus_wram_write8(OAM_BUF + oam_idx, (uint8_t)(tile_attr >> 8));
        oam_idx++;
    }

    bus_wram_write16(0x0446, oam_idx);
}

/* ========================================================================
 * $01:962C — Sprite animation driver
 *
 * Advances animation state for a sprite slot and renders the current
 * frame. Animation data is in Bank $0F at DATA_0F8000.
 *
 * Input: A = sprite slot index (e.g. $0026=color bar, $0027=toolbar)
 *
 * Per-slot state at $0792+slot*8:
 *   +0: X position (9-bit)
 *   +2: Y position
 *   +4: frame delay counter
 *   +5: current frame index
 *   +6: palette/attribute base
 *
 * Animation data format (4 bytes per frame, pointed to by $0F:8000 table):
 *   byte 0: delay count (or $80+ = command)
 *   byte 1: X delta
 *   byte 2: Y delta
 *   byte 3: sprite ID + flags → passed to $01FA68
 * ======================================================================== */
void mp_01962C(void) {
    uint16_t slot = CPU_A16();

    /* Compute slot data offset: slot * 8 */
    uint16_t slot_ofs = slot * 8;

    /* Animation pointer from Bank $0F table at $8000 */
    uint16_t anim_ptr = bus_read16(0x0F, 0x8000 + slot * 2);

    /* Decrement frame delay counter */
    op_sep(0x20);
    uint8_t delay = bus_wram_read8(0x0796 + slot_ofs);
    delay--;
    bus_wram_write8(0x0796 + slot_ofs, delay);

    if ((int8_t)delay < 0) {
        /* Delay expired — advance to next frame */
        uint8_t frame_idx = bus_wram_read8(0x0797 + slot_ofs);
        frame_idx++;
        bus_wram_write8(0x0797 + slot_ofs, frame_idx);

        op_rep(0x20);

        /* Read animation frame data */
        uint16_t frame_ofs = (uint16_t)bus_wram_read8(0x0797 + slot_ofs) * 4;

        /* Read frame entry: byte 0 = delay/command */
        uint8_t cmd = bus_read8(0x0F, anim_ptr + frame_ofs);

        if (cmd & 0x80) {
            /* Command byte — animation control.
             * The original uses $01E393 (indexed JSL dispatch) which
             * can't work in recompiled C. Handle commands directly:
             *   $80 = loop (reset frame to 0)
             *   $81 = end (hold last frame)
             *   $82+ = other (treat as end) */
            uint8_t cmd_type = cmd & 0x7F;
            if (cmd_type == 0) {
                /* Loop: reset frame index to 0 */
                bus_wram_write8(0x0797 + slot_ofs, 0x00);
                frame_ofs = 0;
                cmd = bus_read8(0x0F, anim_ptr);
                bus_wram_write8(0x0796 + slot_ofs, cmd);
            } else {
                /* End / other: rewind one frame and hold */
                uint8_t fi = bus_wram_read8(0x0797 + slot_ofs);
                if (fi > 0) fi--;
                bus_wram_write8(0x0797 + slot_ofs, fi);
                bus_wram_write8(0x0796 + slot_ofs, 1);
            }
            delay = bus_wram_read8(0x0796 + slot_ofs);
        } else {
            /* Normal frame: set delay */
            bus_wram_write8(0x0796 + slot_ofs, cmd);
        }

        /* Read frame data: X delta, Y delta, sprite data */
        frame_ofs = (uint16_t)bus_wram_read8(0x0797 + slot_ofs) * 4;

        op_rep(0x20);

        /* Read sprite render data from frame */
        uint8_t sprite_data_byte = bus_read8(0x0F, anim_ptr + frame_ofs + 3);
        if (sprite_data_byte & 0x80) {
            /* Has extended render data — advance position deltas */
            int8_t x_delta = (int8_t)bus_read8(0x0F, anim_ptr + frame_ofs + 1);
            int16_t x_pos = (int16_t)bus_wram_read16(0x0792 + slot_ofs);
            x_pos += (int16_t)x_delta;
            x_pos &= 0x03FF;
            bus_wram_write16(0x0792 + slot_ofs, (uint16_t)x_pos);

            int8_t y_delta = (int8_t)bus_read8(0x0F, anim_ptr + frame_ofs + 2);
            int16_t y_pos = (int16_t)bus_wram_read16(0x0794 + slot_ofs);
            y_pos += (int16_t)y_delta;
            y_pos &= 0x01FF;
            bus_wram_write16(0x0794 + slot_ofs, (uint16_t)y_pos);
        }
    } else {
        op_rep(0x20);
    }

    /* Render current frame sprite */
    {
        uint16_t frame_ofs = (uint16_t)bus_wram_read8(0x0797 + slot_ofs) * 4;

        /* Read render sprite ID + flags from frame data byte 3 */
        uint8_t render_byte = bus_read8(0x0F, anim_ptr + frame_ofs + 3);
        uint16_t render_id = (uint16_t)render_byte;

        /* Add palette/attribute base from slot */
        render_id += bus_wram_read16(0x0798 + slot_ofs);

        /* Get position */
        uint16_t x_pos = bus_wram_read16(0x0792 + slot_ofs) >> 1;
        uint16_t y_pos = bus_wram_read16(0x0794 + slot_ofs) >> 1;

        /* Call FA68 to render */
        CPU_SET_A16(render_id);
        g_cpu.X = x_pos;
        g_cpu.Y = y_pos;
        mp_01FA68();
    }
}

/* ========================================================================
 * Register sprite engine functions.
 * ======================================================================== */
void mp_register_sprites(void) {
    func_table_register(0x01F91E, mp_01F91E);
    func_table_register(0x01FA68, mp_01FA68);
    func_table_register(0x01962C, mp_01962C);
}
