/*
 * Mario Paint — Static Recompilation
 *
 * Entry point: loads the ROM, configures SNES Mouse on port 1,
 * registers recompiled functions, and runs the main frame loop.
 */

#include <snesrecomp/snesrecomp.h>
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

    printf("Mario Paint recomp: starting main loop\n");

    /* Main frame loop */
    while (snesrecomp_begin_frame()) {
        /*
         * TODO: Call recompiled game functions here.
         *
         * The boot chain will look something like:
         *   mp_XXXXXX();  // reset vector / hardware init
         *   mp_XXXXXX();  // NMI handler
         *   mp_XXXXXX();  // main loop iteration
         *
         * For now, just run the hardware (shows a black screen
         * with the ROM loaded into LakeSnes's memory map).
         */

        snesrecomp_end_frame();
    }

    snesrecomp_shutdown();
    printf("Mario Paint recomp: shutdown complete\n");
    return 0;
}
