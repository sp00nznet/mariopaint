/*
 * Mario Paint — Static Recompilation
 *
 * Entry point: loads the ROM, configures SNES Mouse on port 1,
 * registers recompiled functions, and runs the main frame loop.
 *
 * The boot chain is:
 *   mp_008000() — reset vector (hardware init → mp_0084D5)
 *   mp_0084D5() — application init → mp_00865A (main loop)
 *   mp_00865A() — main loop (infinite, calls mp_0080D4 via VBlank)
 *
 * The main loop calls func_table_call() for subroutines that haven't
 * been recompiled yet. Those calls are no-ops until the functions are
 * registered, which means the game will progressively come to life as
 * more functions are recompiled.
 */

#include <snesrecomp/snesrecomp.h>
#include <mp/cpu_ops.h>
#include <mp/functions.h>

#include <stdio.h>

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

    printf("Mario Paint recomp: running hardware init\n");

    /*
     * Run hardware initialization in stages. The full boot chain
     * (mp_008000 → mp_008013 → mp_0084D5 → mp_00865A) depends on
     * many Bank 01 subroutines that aren't recompiled yet.
     *
     * For now, run the PPU/register setup directly, then drive
     * the frame loop manually. As more functions are recompiled,
     * we can switch to the full boot chain.
     */

    /* Switch to native mode, set up CPU state */
    OP_SEI();
    OP_CLC();
    op_xce();
    op_rep(0x30);
    g_cpu.S = 0x1FFF;
    g_cpu.DP = 0x0000;

    /* Initialize PPU registers and display */
    mp_00833B();
    mp_00837D();

    printf("Mario Paint recomp: hardware init complete, entering frame loop\n");

    /* Main frame loop — driven by snesrecomp */
    while (snesrecomp_begin_frame()) {
        /* Trigger VBlank and run NMI handler */
        snesrecomp_trigger_vblank();
        mp_0080D4();

        /* Render and present */
        snesrecomp_end_frame();
    }

    snesrecomp_shutdown();
    printf("Mario Paint recomp: shutdown complete\n");
    return 0;
}
