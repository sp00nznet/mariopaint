/*
 * Mario Paint — Recompiled audio engine.
 *
 * Handles SPC700 audio data upload and per-frame command processing.
 * The SPC700 upload uses the standard IPL handshake protocol through
 * APU ports $2140-$2143, which route through LakeSnes's real SPC700.
 *
 * Audio command queues at $04EC/$04FC/$050C/$051C (4 channels, 16 bytes each)
 * with write pointers at $0530-$0533 and read pointers at $052C-$052F.
 *
 * Reference: Yoshifanatic1/Mario-Paint-Disassembly
 */

#include <mp/cpu_ops.h>
#include <mp/functions.h>
#include <snesrecomp/snesrecomp.h>

#include <stdbool.h>

extern bool g_quit;

#define REG_APUIO0  0x2140
#define REG_APUIO1  0x2141
#define REG_APUIO2  0x2142
#define REG_APUIO3  0x2143

/* ========================================================================
 * $01:DF25 — SPC700 audio data upload
 *
 * Implements the standard SNES SPC700 IPL upload protocol.
 * Reads audio data from the pointer at DP $CC-$CE and uploads it
 * to the SPC700's audio RAM through the APU communication ports.
 *
 * The protocol is a handshake:
 *   1. Wait for $2140 = $BBAA (SPC700 ready)
 *   2. Send destination address via $2142-$2143
 *   3. Send data bytes via $2140-$2141 with acknowledge handshake
 *   4. Send end marker when transfer complete
 *
 * This routes through LakeSnes's real SPC700 emulation.
 * ======================================================================== */
void mp_01DF25(void) {
    /* Save audio queue state */
    bus_wram_write8(0x052C, bus_wram_read8(0x0530));
    bus_wram_write8(0x052D, bus_wram_read8(0x0531));
    bus_wram_write8(0x052E, bus_wram_read8(0x0532));
    bus_wram_write8(0x052F, bus_wram_read8(0x0533));

    /* Get source pointer from DP $CC-$CE and adjust for bank boundary */
    uint8_t src_lo = bus_wram_read8(0xCC);
    uint8_t src_hi = bus_wram_read8(0xCD);
    uint8_t src_bank = bus_wram_read8(0xCE);

    /* Adjust: Y = ptr - $8000 (offset within bank) */
    uint16_t src_addr = ((uint16_t)src_hi << 8) | src_lo;
    uint16_t y = src_addr - 0x8000;

    /* Fix up source to start at $8000 within bank */
    bus_wram_write8(0xCC, 0x00);
    bus_wram_write8(0xCD, 0x80);

    /* Wait for SPC700 ready signal ($BBAA at port 0) */
    {
        uint16_t timeout = 0xFFFF;
        while (timeout > 0) {
            uint16_t val = bus_read8(0x00, REG_APUIO0) |
                          ((uint16_t)bus_read8(0x00, REG_APUIO1) << 8);
            if (val == 0xBBAA) break;
            timeout--;
        }
    }

    /* Start upload with initial command byte $CC */
    uint8_t cmd = 0xCC;

    /* Main upload loop */
    for (;;) {
        /* Read block header: 2 bytes size, 2 bytes destination */
        /* Read size (little-endian) */
        uint8_t b0 = bus_read8(src_bank, 0x8000 + y);
        y++; if (y >= 0x8000) { y = 0; src_bank += 2; }
        uint8_t b1 = bus_read8(src_bank, 0x8000 + y);
        y++; if (y >= 0x8000) { y = 0; src_bank += 2; }
        uint16_t block_size = (uint16_t)b0 | ((uint16_t)b1 << 8);

        /* Read destination address */
        uint8_t d0 = bus_read8(src_bank, 0x8000 + y);
        y++; if (y >= 0x8000) { y = 0; src_bank += 2; }
        uint8_t d1 = bus_read8(src_bank, 0x8000 + y);
        y++; if (y >= 0x8000) { y = 0; src_bank += 2; }

        /* Send destination to APU ports 2-3 */
        bus_write8(0x00, REG_APUIO2, d0);
        bus_write8(0x00, REG_APUIO3, d1);

        /* Check if this is the end marker (size <= 1 means execute) */
        uint8_t exec_flag = 0;
        if (block_size <= 1) exec_flag = 1;
        bus_write8(0x00, REG_APUIO1, exec_flag);

        /* Send the command byte + $7F-ish offset to start */
        cmd += 2;
        if (cmd == 0) cmd = 2;  /* Skip 0 */
        bus_write8(0x00, REG_APUIO0, cmd);

        /* Wait for SPC700 to acknowledge */
        {
            uint16_t timeout = 0xFFFF;
            while (timeout > 0) {
                if (bus_read8(0x00, REG_APUIO0) == cmd) break;
                timeout--;
            }
        }

        /* If execute flag was set, we're done */
        if (exec_flag) break;

        /* Upload data bytes */
        uint8_t ack = cmd;
        for (uint16_t i = 0; i < block_size; i++) {
            uint8_t data = bus_read8(src_bank, 0x8000 + y);
            y++; if (y >= 0x8000) { y = 0; src_bank += 2; }

            /* Send data byte pair: data in port 0, counter in port 1 */
            if (i == 0) {
                /* First byte: send as pair */
                bus_write8(0x00, REG_APUIO1, data);
                bus_write8(0x00, REG_APUIO0, 0x00);
            } else {
                bus_write8(0x00, REG_APUIO1, data);
                ack++;
                bus_write8(0x00, REG_APUIO0, ack);
            }

            /* Wait for acknowledge */
            uint16_t timeout = 0xFFFF;
            while (timeout > 0) {
                if (bus_read8(0x00, REG_APUIO0) == ack) break;
                timeout--;
            }
        }

        /* Increment command for next block */
        cmd = ack + 3;
        if (cmd == 0) cmd = 3;
    }
}

/* ========================================================================
 * $01:D308 — Audio command queue write (channel 0)
 *
 * Writes A (8-bit) into the channel 0 command queue at $04EC.
 * Queue is circular, 16 bytes, write pointer at $0530.
 * ======================================================================== */
void mp_01D308(void) {
    op_sep(0x30);
    uint8_t val = CPU_A8();
    uint8_t wp = bus_wram_read8(0x0530);
    bus_wram_write8(0x04EC + wp, val);
    wp = (wp + 1) & 0x0F;
    if (wp != bus_wram_read8(0x052C)) {
        bus_wram_write8(0x0530, wp);
    }
}

/* ========================================================================
 * $01:D328 — Audio command queue write (channel 1)
 * ======================================================================== */
void mp_01D328(void) {
    op_sep(0x30);
    uint8_t val = CPU_A8();
    uint8_t wp = bus_wram_read8(0x0531);
    bus_wram_write8(0x04FC + wp, val);
    wp = (wp + 1) & 0x0F;
    if (wp != bus_wram_read8(0x052D)) {
        bus_wram_write8(0x0531, wp);
    }
}

/* ========================================================================
 * $01:D348 — Audio command queue write (channel 2)
 * ======================================================================== */
void mp_01D348(void) {
    op_sep(0x30);
    uint8_t val = CPU_A8();
    uint8_t wp = bus_wram_read8(0x0532);
    bus_wram_write8(0x050C + wp, val);
    wp = (wp + 1) & 0x0F;
    if (wp != bus_wram_read8(0x052E)) {
        bus_wram_write8(0x0532, wp);
    }
}

/* ========================================================================
 * $01:D368 — Audio command queue write (channel 3)
 * ======================================================================== */
void mp_01D368(void) {
    op_sep(0x30);
    uint8_t val = CPU_A8();
    uint8_t wp = bus_wram_read8(0x0533);
    bus_wram_write8(0x051C + wp, val);
    wp = (wp + 1) & 0x0F;
    if (wp != bus_wram_read8(0x052F)) {
        bus_wram_write8(0x0533, wp);
    }
}

/* ========================================================================
 * $01:D2BF — Audio command with special handling
 *
 * Sends a command through channel 0 with additional setup:
 * writes a $02 to channel 2 queue, sets $053A timer, then
 * writes the command to channel 0.
 * ======================================================================== */
void mp_01D2BF(void) {
    op_sep(0x30);
    uint8_t val = CPU_A8();

    /* Check if we can do the special path */
    if (bus_wram_read8(0x053A) != 0) goto simple;
    if (bus_wram_read8(0x0530) != bus_wram_read8(0x052C)) goto simple;

    /* Special path: send $02 to channel 2, set timer */
    CPU_SET_A8(0x02);
    mp_01D348();

    bus_wram_write8(0x053A, 0x20);

    /* Write to channel 0 queue */
    {
        uint8_t wp = bus_wram_read8(0x0530);
        bus_wram_write8(0x04EC + wp, val);
        wp = (wp + 1) & 0x0F;
        if (wp != bus_wram_read8(0x052C)) {
            bus_wram_write8(0x0530, wp);
        }
    }
    return;

simple:
    /* Simple path: just write to channel 0 at read pointer */
    {
        uint8_t rp = bus_wram_read8(0x052C);
        bus_wram_write8(0x04EC + rp, val);
    }
}

/* ========================================================================
 * $01:D388 — Audio initialization
 *
 * Sends command $00 via D2BF, waits for $053A to clear,
 * then syncs 18 frames.
 * ======================================================================== */
void mp_01D388(void) {
    CPU_SET_A8(0x00);
    mp_01D2BF();

    /* Wait for $053A to clear */
    while (bus_wram_read8(0x053A) != 0 && !g_quit) {
        /* $053A is decremented by the NMI audio processing */
        mp_01E2CE();
    }

    /* Wait 18 frames */
    for (int i = 0x12; i > 0 && !g_quit; i--) {
        mp_01E2CE();
    }
}

/* ========================================================================
 * $01:DDB8 — NMI audio: check queue drain timer
 *
 * Manages the $053D countdown timer that ensures audio commands
 * have time to process before new ones are sent.
 * ======================================================================== */
void mp_01DDB8(void) {
    if (bus_wram_read8(0x053B) == 0) return;

    uint8_t x = bus_wram_read8(0x053F);
    uint8_t rp = bus_wram_read8(0x052C + x);
    uint8_t wp = bus_wram_read8(0x0530 + x);

    if (rp != wp) return;

    if (bus_wram_read8(0x053D) == 0) {
        bus_wram_write8(0x053D, bus_wram_read8(0x053B));
        rp = (rp - 1) & 0x0F;
        bus_wram_write8(0x052C + x, rp);
    }

    uint8_t timer = bus_wram_read8(0x053D);
    if (timer > 0) bus_wram_write8(0x053D, timer - 1);
}

/* ========================================================================
 * $01:DDE1 — NMI audio: process channel 0 commands
 *
 * Reads commands from channel 0 queue ($04EC) and sends them
 * to APU port 0. Handles the handshake protocol where the SPC700
 * acknowledges by echoing the sent byte.
 * ======================================================================== */
void mp_01DDE1(void) {
    if (bus_wram_read8(0x0538) != 0) return;

    /* Check $053A countdown */
    if (bus_wram_read8(0x053A) != 0) {
        uint8_t t = bus_wram_read8(0x053A) - 1;
        bus_wram_write8(0x053A, t);
        return;
    }

    /* Process channel 0 queue */
    uint8_t rp = bus_wram_read8(0x052C);
    uint8_t wp = bus_wram_read8(0x0530);
    if (rp == wp) return;

    uint8_t sending = bus_wram_read8(0x0534);
    uint8_t data = bus_wram_read8(0x04EC + rp);

    if (sending == 0) {
        /* Send new byte */
        bus_write8(0x00, REG_APUIO0, data);
        bus_wram_write8(0x0534, 0x01);
    } else {
        /* Check if SPC700 acknowledged */
        uint8_t ack = bus_read8(0x00, REG_APUIO0);
        if (ack == data) {
            /* Acknowledged — advance read pointer */
            rp = (rp + 1) & 0x0F;
            bus_wram_write8(0x052C, rp);
            bus_wram_write8(0x0534, 0x00);
        } else {
            /* Not yet — resend */
            bus_write8(0x00, REG_APUIO0, data);
        }
    }
}

/* ========================================================================
 * $01:DE2D — NMI audio: process channels 1-3 commands
 *
 * Similar to DDE1 but processes all three remaining channels
 * (queues at $04FC, $050C, $051C → APU ports 1, 2, 3).
 * ======================================================================== */
void mp_01DE2D(void) {
    if (bus_wram_read8(0x0538) != 0) return;

    /* Process channels 3, 2, 1 (in that order) */
    static const uint16_t queue_base[4] = { 0x04EC, 0x04FC, 0x050C, 0x051C };

    for (int ch = 3; ch >= 1; ch--) {
        uint8_t rp = bus_wram_read8(0x052C + ch);
        uint8_t wp = bus_wram_read8(0x0530 + ch);
        if (rp == wp) {
            /* Queue empty — write 0 to APU port */
            bus_write8(0x00, REG_APUIO0 + ch, 0x00);
            continue;
        }

        uint8_t sending = bus_wram_read8(0x0534 + ch);
        uint8_t data = bus_wram_read8(queue_base[ch] + rp);

        if (sending == 0) {
            /* Check if APU port is ready (reads 0) */
            uint8_t port_val = bus_read8(0x00, REG_APUIO0 + ch);
            if (port_val != 0) continue;

            /* Ready — set retry counter and send */
            bus_wram_write8(0x0541 + ch, 0x04);
            bus_write8(0x00, REG_APUIO0 + ch, data);
            bus_wram_write8(0x0534 + ch, 0x01);
        } else {
            /* Check acknowledge */
            uint8_t ack = bus_read8(0x00, REG_APUIO0 + ch);
            if (ack != data) {
                /* Retry with countdown */
                uint8_t retry = bus_wram_read8(0x0541 + ch) - 1;
                bus_wram_write8(0x0541 + ch, retry);
                if (retry > 0) {
                    bus_write8(0x00, REG_APUIO0 + ch, data);
                    continue;
                }
            }

            /* Acknowledged or retries exhausted — advance */
            rp = (rp + 1) & 0x0F;
            bus_wram_write8(0x052C + ch, rp);
            bus_wram_write8(0x0534 + ch, 0x00);
            bus_wram_write8(0x0541 + ch, 0x00);
        }
    }
}

/* ========================================================================
 * $01:DFD3 — Compute sprite size mirror values
 *
 * Reads OBSEL ($0105) to determine the sprite sizes configured,
 * then stores the pixel dimensions into $012E (small) and $0130 (large).
 * ======================================================================== */
void mp_01DFD3(void) {
    /* Table: small/large sprite pixel widths indexed by OBSEL size bits */
    static const uint8_t small_sizes[6] = { 0x08, 0x08, 0x08, 0x10, 0x10, 0x20 };
    static const uint8_t large_sizes[6] = { 0x10, 0x20, 0x40, 0x20, 0x40, 0x40 };

    uint8_t obsel = bus_wram_read8(0x0105);
    uint8_t idx = (obsel & 0xE0) >> 5;
    if (idx >= 6) idx = 5;

    bus_wram_write16(0x012E, small_sizes[idx]);
    bus_wram_write16(0x0130, large_sizes[idx]);
}

/* ========================================================================
 * Register all audio engine functions.
 * ======================================================================== */
void mp_register_audio(void) {
    func_table_register(0x01DF25, mp_01DF25);
    func_table_register(0x01D308, mp_01D308);
    func_table_register(0x01D328, mp_01D328);
    func_table_register(0x01D348, mp_01D348);
    func_table_register(0x01D368, mp_01D368);
    func_table_register(0x01D2BF, mp_01D2BF);
    func_table_register(0x01D388, mp_01D388);
    func_table_register(0x01DDB8, mp_01DDB8);
    func_table_register(0x01DDE1, mp_01DDE1);
    func_table_register(0x01DE2D, mp_01DE2D);
    func_table_register(0x01DFD3, mp_01DFD3);
}
