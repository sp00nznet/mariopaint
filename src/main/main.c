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
 *
 * Unrecompiled subroutines fall through to snesrecomp's 65816
 * interpreter (recomp_interp_*), so func_table_call never dead-ends
 * on an address we haven't translated yet.
 */

#include <snesrecomp/snesrecomp.h>
#include <snesrecomp/platform.h>
#include <snesrecomp/cpu_ops.h>
#include <mp/functions.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* Global quit flag — set by mp_01E2CE when window is closed */
bool g_quit = false;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mario_paint.sfc>\n", argv[0]);
        return 1;
    }

    /* Initialize snesrecomp (LakeSnes + SDL2 + ImGui menu) */
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

    /* Anything not yet recompiled runs the original ROM code on the
     * LakeSnes CPU instead of silently doing nothing. */
    recomp_interp_set_enabled(true);

    /*
     * MP_INTERP_FUNCS="018000,0087EE" — hand specific addresses back to the
     * interpreter even though a recompiled version exists. Registering NULL
     * makes func_table_lookup miss, so dispatch falls through to the genuine
     * ROM code. This is the A/B lever for "is our translation of X wrong?":
     * run it interpreted and see if the symptom goes away.
     */
    {
        const char *list = getenv("MP_INTERP_FUNCS");
        while (list && *list) {
            char *end;
            unsigned long addr = strtoul(list, &end, 16);
            if (end == list) break;
            func_table_register((uint32_t)addr, NULL);
            printf("mp: $%06lX handed to the interpreter\n", addr);
            list = (*end == ',') ? end + 1 : end;
        }
    }

    /*
     * Real-frame mode (MP_REALFRAME=1): run the genuine ROM through LakeSnes's
     * full cycle-accurate frame instead of the recompiled boot chain. No
     * recompiled code participates. This is the ground truth to validate the
     * recomp against — capture the same frame both ways and diff. It is also
     * the only way to reach screens the recomp can't drive yet (the music
     * composer, Gnat Attack), since the real CPU handles their state machines.
     */
    if (getenv("MP_REALFRAME")) {
        printf("Mario Paint recomp: real-frame mode (genuine ROM via LakeSnes)\n");
        while (snesrecomp_realframe_begin())
            snesrecomp_realframe_end();
        snesrecomp_shutdown();
        return 0;
    }

    /*
     * Let interpreted ROM code reach the recompiled frame driver.
     *
     * The interpreter is a plain cpu_runOpcode loop: a JSL inside interpreted
     * code is executed by the emulated CPU and never routed through the
     * dispatch table, so a recompiled C function is unreachable from it. That
     * matters for $01E2CE, which *is* our frame driver (begin_frame ->
     * trigger_vblank -> NMI -> end_frame). Without this, any routine we hand to
     * the interpreter runs its whole vblank-waiting loop without presenting a
     * single frame — the title screen ran entirely invisibly and the window sat
     * frozen until control came back.
     *
     * The opcode-fetch hook that timed-recomp interception uses works here too
     * (LakeSnes calls it from cpu_runOpcode, which the interpreter drives), so
     * registering $01E2CE as an intercept makes interpreted code call the
     * native frame driver and present frames normally.
     *
     * Recomp path only — real-frame mode must run the genuine ROM untouched.
     *
     * $018260 is the important one. The ROM's title loop has no frame sync in
     * it at all — it spins polling the mouse bytes at $04C6/$04C8/$04CA and
     * bails to the demo after $800 idle iterations. On hardware an NMI drives
     * the frame underneath it; interpreted here, it burns all 2048 iterations
     * instantly with nothing drawn and no input possible, so the title screen
     * flashed past invisibly. Our recompiled mp_018260 drives a frame per
     * iteration, which is what makes the screen appear and respond.
     *
     * The fades are the same story: their ROM loops step brightness one step
     * per vblank, so interpreted they finish instantly with nothing drawn and
     * the screen left mid-fade. The recompiled versions drive a frame per step.
     */
    recomp_timed_add_intercept(0x018260, false);  /* title loop,  JSR -> RTS */
    recomp_timed_add_intercept(0x01E2CE, true);   /* frame sync,  JSL -> RTL */
    recomp_timed_add_intercept(0x01E794, true);   /* fade in,     JSL -> RTL */
    recomp_timed_add_intercept(0x01E7C9, true);   /* fade out,    JSL -> RTL */
    recomp_timed_recomp_enable();

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
