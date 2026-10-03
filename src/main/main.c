/*
 * Mario Paint — Static Recompilation
 *
 * Entry point: loads the ROM, configures SNES Mouse on port 1, registers
 * recompiled functions, and picks one of three modes (docs/architecture.md):
 *
 *   default          timed recomp: the genuine ROM in LakeSnes's cycle-timed
 *                    frame loop, with every function in k_verified running as
 *                    native C in place of its ROM routine
 *   MP_REALFRAME=1   no native code: the oracle the harness compares against
 *   MP_NATIVE=1      the native-driven boot chain below, interpreter fallback
 *
 * Native-driven frame architecture:
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

/*
 * Recompiled functions that pass tools/conformance.py: every checked call
 * byte-identical to the ROM (WRAM, VRAM, CGRAM, OAM), none skipped. These run
 * natively by default. Regenerate with conformance.py (scratch/conf/verified.txt)
 * and paste; never add a function here that has not passed.
 */
static const char k_verified[] =
    "00815B,008187,0081CA,00823C,00833B,00837D,00849D,008683L,"
    "0089B1,0089C3,008A39,008B48,0091C7,0096AB,009D7D,00B051,"
    "00B0D3,00B305,00B66C,00BA78,00C414,00F921,0182B1,0182F6,"
    "019DFEL,01D2BFL,01D308L,01D348L,01D368L,01D56DL,01D9E1L,01DCB9L,"
    "01DDB8L,01DDE1L,01DE2DL,01DECDL,01DFD3L,01E042L,01E06FL,01E09BL,"
    "01E103L,01E1ABL,01E20CL,01E238L,01E2F3L,01E30EL,01E429L,01E460L,"
    "01E500L,01E59BL,01E60CL,01E66BL,01E747L,01E87BL,01E88AL,01E8F6L,"
    "0FC000L";

/* "01D9E1L,008B48" -> fn(addr, is_long) for each; suffix L = returns with RTL. */
static void add_addr_list(const char *list, void (*fn)(uint32_t, bool)) {
    while (list && *list) {
        char *end;
        unsigned long addr = strtoul(list, &end, 16);
        if (end == list) break;
        bool is_long = (*end == 'L' || *end == 'l');
        if (is_long) end++;
        fn((uint32_t)addr, is_long);
        list = (*end == ',') ? end + 1 : end;
    }
}

#ifdef MP_HAVE_GEN
void gen_register_all(void);
#endif

static void dump_profile(void) {
    recomp_timed_profile_dump(atoi(getenv("MP_PROFILE")));
}

int main(int argc, char *argv[]) {
    argc = snesrecomp_parse_args(argc, argv);   /* --headless, --record out.mp4 */
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [--headless] [--record out.mp4] <mario_paint.sfc>\n", argv[0]);
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
    /* Cursor X/Y, so scripted "@x:@y" mouse steps steer the genuine ROM too. */
    recomp_input_set_mouse_cursor_addr(0x04DC, 0x04DE);

    /* Register all recompiled functions */
    mp_register_all();
#ifdef MP_HAVE_GEN
    /* Generated routines (gen/, from your ROM) are not validated yet, so they
     * are opt-in: MP_GEN=1 registers them over the hand-ports at the same
     * addresses. */
    if (getenv("MP_GEN")) gen_register_all();
#endif

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
     * Timed loop (default, MP_REALFRAME=1, MP_TIMED, MP_VALIDATE): LakeSnes runs
     * the genuine ROM cycle-accurately; native bodies replace ROM routines at
     * their entry through the opcode hook. MP_REALFRAME=1 alone is the ground
     * truth the recomp is validated against.
     */
    if (!getenv("MP_NATIVE")) {
        bool real = getenv("MP_REALFRAME") != NULL;
        printf("Mario Paint recomp: %s\n", real ? "real-frame mode (genuine ROM, no native code)"
                                                : "timed recomp (verified functions native)");
        if (!real && !getenv("MP_VALIDATE") && !getenv("MP_TIMED"))
            add_addr_list(k_verified, recomp_timed_add_intercept);
        /*
         * MP_TIMED="01D9E1L,008B48" — run exactly these natively instead of
         * k_verified (suffix L = returns with RTL). Add MP_REALFRAME=1 to start
         * from no native code at all.
         *
         * MP_VALIDATE="..." (same format) — lockstep validation instead: every
         * call runs the native body on a saved copy of the machine and compares
         * it with the genuine routine, which is what actually runs. Reports one
         * VALIDATE line per function at exit.
         *
         * MP_PROFILE=N — at exit, dump the N hottest JSR/JSL targets with the
         * M/X flags they are entered with (what to recompile next).
         */
        add_addr_list(getenv("MP_TIMED"), recomp_timed_add_intercept);
        add_addr_list(getenv("MP_VALIDATE"), recomp_timed_add_validate);
        if (getenv("MP_VALIDATE")) atexit(recomp_timed_validate_report);
        if (!real || getenv("MP_TIMED") || getenv("MP_VALIDATE") || getenv("MP_PROFILE"))
            recomp_timed_recomp_enable();
        if (getenv("MP_PROFILE")) {
            recomp_timed_profile_enable();
            atexit(dump_profile);
        }
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
