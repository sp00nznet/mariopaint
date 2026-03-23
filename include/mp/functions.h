/*
 * Mario Paint — recompiled function declarations.
 *
 * Functions are named mp_XXXXXX where XXXXXX is the 24-bit SNES address.
 * Addresses are from the Mario Paint (JU) ROM (LoROM).
 */
#ifndef MP_FUNCTIONS_H
#define MP_FUNCTIONS_H

/* Register all recompiled functions in the dispatch table */
void mp_register_all(void);

/* Boot / System (mp_boot.c) */
void mp_008000(void);  /* Reset vector */
void mp_008013(void);  /* Hardware init */
void mp_00833B(void);  /* Register init */
void mp_00837D(void);  /* Graphics setup */
void mp_00849D(void);  /* VRAM clearing */
void mp_0084AF(void);  /* OAM init */
void mp_0084D5(void);  /* Main application init */
void mp_0080D4(void);  /* NMI handler */
void mp_00865A(void);  /* Main loop */

/* Bank 01 — DMA/PPU helpers (mp_bank01.c) */
void mp_register_bank01(void);
void mp_01E024(void);  /* OAM low table fill */
void mp_01E033(void);  /* OAM high table fill */
void mp_01E042(void);  /* OAM third area fill */
void mp_01E060(void);  /* WRAM block fill (16-bit) */
void mp_01E06F(void);  /* Clear OAM buffer (sprites offscreen) */
void mp_01E09B(void);  /* Screen enable / OAM cleanup */
void mp_01E103(void);  /* PPU register mirror writeback */
void mp_01E1AB(void);  /* BG scroll register writeback */
void mp_01E238(void);  /* Random number table init */
void mp_01E2CE(void);  /* Frame sync (wait for VBlank) */
void mp_01E2F3(void);  /* Wait for VBlank, enable NMI */
void mp_01E30E(void);  /* Disable NMI */
void mp_01E429(void);  /* OAM DMA transfer */
void mp_01E460(void);  /* VRAM DMA transfer (canvas) */
void mp_01E500(void);  /* Sprite GFX DMA transfer */
void mp_01E59B(void);  /* HDMA window setup */
void mp_01E60C(void);  /* HDMA BG2 vertical scroll */
void mp_01E66B(void);  /* HDMA BG2 horizontal scroll */
void mp_01E6CA(void);  /* Palette/VRAM DMA queue */
void mp_01E747(void);  /* Brightness control + joypad */
void mp_01E794(void);  /* Fade in */
void mp_01E895(void);  /* Empty tile row check */

/* Game Logic (mp_gamelogic.c) */
void mp_register_gamelogic(void);
void mp_01E8F6(void);  /* Empty tile row check */
void mp_008683(void);  /* Game logic dispatch */
void mp_00878F(void);  /* Animation frame check */
void mp_0087A8(void);  /* Post-logic dispatch */
void mp_009378(void);  /* Cursor rendering */

/* Graphics Init (mp_gfxinit.c) */
void mp_register_gfxinit(void);
void mp_01E87B(void);  /* Disable HDMA */
void mp_01E88A(void);  /* Disable DMA */
void mp_0087EE(void);  /* Master palette/VRAM loader */
void mp_008A75(void);  /* Clear animation buffer */
void mp_0089B1(void);  /* Tilemap page setup */
void mp_0089C3(void);  /* Tilemap border fill */
void mp_008A16(void);  /* BG2 tilemap fill */
void mp_008A39(void);  /* BG3 tilemap fill */
void mp_01DE97(void);  /* Queue BG1 tilemap DMA */
void mp_01DEB2(void);  /* Queue BG2 tilemap DMA */
void mp_01DECD(void);  /* Queue BG3 tilemap DMA */
void mp_01E6D0(void);  /* Direct DMA queue execution */
void mp_01DE_queue_dma(const uint8_t *record, int len);

/* Sprite Engine (mp_sprites.c) */
void mp_register_sprites(void);
void mp_01F91E(void);  /* Simple sprite renderer */
void mp_01FA68(void);  /* Full sprite renderer (flip/palette) */
void mp_01962C(void);  /* Sprite animation driver */

/* Input / Cursor (mp_input.c) */
void mp_register_input(void);
void mp_01D9E1(void);  /* Mouse data read + button state */
void mp_00815B(void);  /* Cursor sprite animation */
void mp_008187(void);  /* Cursor shake animation */
void mp_0081CA(void);  /* Bomb icon animation */
void mp_00823C(void);  /* Display animation */
void mp_008B48(void);  /* Cursor movement */
void mp_01DCB9(void);  /* Bomb timer animation */

/* Audio Engine (mp_audio.c) */
void mp_register_audio(void);
void mp_01DF25(void);  /* SPC700 upload */
void mp_01D308(void);  /* Audio cmd queue ch0 */
void mp_01D328(void);  /* Audio cmd queue ch1 */
void mp_01D348(void);  /* Audio cmd queue ch2 */
void mp_01D368(void);  /* Audio cmd queue ch3 */
void mp_01D2BF(void);  /* Audio cmd special */
void mp_01D388(void);  /* Audio init */
void mp_01DDB8(void);  /* NMI audio: queue drain */
void mp_01DDE1(void);  /* NMI audio: ch0 process */
void mp_01DE2D(void);  /* NMI audio: ch1-3 process */
void mp_01DFD3(void);  /* Sprite size mirrors */

/* Title Screen (mp_title.c) */
void mp_register_title(void);
void mp_00D6D3(void);  /* SRAM checksum */
void mp_018000(void);  /* Title screen + canvas init */

/* Helpers (mp_helpers.c) */
void mp_register_helpers(void);
void mp_01E7C9(void);  /* Fade out */
void mp_01E20C(void);  /* RNG read */
void mp_008A12(void);  /* BG2 tilemap wrapper */
void mp_009FC4(void);  /* Tool mode change */
void mp_00B66C(void);  /* Pen graphics (FG) */
void mp_00B6F4(void);  /* Pen graphics (BG) */
void mp_00BA78(void);  /* BG3 scroll buffer init */
void mp_00DE8E(void);  /* Stamp display buffer */
void mp_00E25C(void);  /* SRAM validation */
void mp_00E64F(void);  /* Palette row init */
void mp_00F39E(void);  /* Palette display init */
void mp_018F52(void);  /* Title→canvas transition */
void mp_01F825(void);  /* Toolbar display data */

/* Canvas / UI (mp_canvas.c) */
void mp_register_canvas(void);
void mp_00C414(void);  /* Standard canvas tilemap */
void mp_00C3DE(void);  /* Alternate page tilemap */
void mp_019DFE(void);  /* Sprite slot init */
void mp_01CDE1(void);  /* Multi-frame DMA batch loader */
void mp_01F9BB(void);  /* Sprite renderer with culling */

#endif /* MP_FUNCTIONS_H */
