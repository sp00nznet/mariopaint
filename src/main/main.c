/*
 * Mario Paint — Static Recompilation
 *
 * Entry point: loads the ROM, configures SNES Mouse on port 1,
 * registers recompiled functions, and runs the boot chain.
 *
 * Frame architecture:
 *   mp_01E2CE (frame sync) is the frame driver. Whenever game code
 *   calls it — during init, the main loop, or fade effects — it
 *   drives one complete frame cycle:
 *     1. snesrecomp_begin_frame() — SDL event pump, input
 *     2. snesrecomp_trigger_vblank() — PPU VBlank processing
 *     3. mp_0080D4() — NMI handler (DMA, PPU writes, joypad)
 *     4. snesrecomp_end_frame() — render, present, 60Hz sync
 *
 *   This means mp_00865A's infinite loop runs naturally, with
 *   mp_01E2CE yielding to the frame driver each iteration.
 *   g_quit is set when the user closes the window, causing
 *   mp_00865A to break its loop and return to main().
 */

#include <snesrecomp/snesrecomp.h>
#include <mp/cpu_ops.h>
#include <mp/functions.h>

#include <stdio.h>
#include <stdbool.h>

/* Global quit flag — set by mp_01E2CE when window is closed */
bool g_quit = false;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mario_paint.sfc>\n", argv[0]);
        return 1;
    }

    /* Initialize snesrecomp (LakeSnes + SDL2) */
    if (!snesrecomp_init("Mario Paint", 3)) {
        fprintf(stderr, "Failed to initialize snesrecomp\n");
        return 1;
    }

    /* Load the ROM */
    if (!snesrecomp_load_rom(argv[1])) {
        fprintf(stderr, "Failed to load ROM: %s\n", argv[1]);
        snesrecomp_shutdown();
        return 1;
    }

    /* Configure port 1 as SNES Mouse (Mario Paint requires it) */
    recomp_input_set_device(1, SNES_INPUT_MOUSE);

    /* Register all recompiled functions */
    mp_register_all();

    printf("Mario Paint recomp: running boot chain\n");

    /*
     * Run the full boot chain. mp_01E2CE drives frames internally,
     * so the entire sequence works naturally:
     *   mp_008000 → mp_008013 → mp_0084D5 → mp_00865A (infinite loop)
     *
     * mp_00865A runs until g_quit is set (window close).
     */
    mp_008000();

    snesrecomp_shutdown();
    printf("Mario Paint recomp: shutdown complete\n");
    return 0;
}
