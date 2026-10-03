# Architecture

Mario Paint is C (the recompiled routines and the entry point) on top of
[snesrecomp](https://github.com/sp00nznet/snesrecomp), which supplies the SNES
hardware (LakeSnes: PPU, SPC700/DSP, DMA, the mouse), the SDL2 window and audio,
the menu overlay, and the 65816 interpreter used for code that has not been
recompiled. Python tools in `tools/` drive and check it headlessly.

## Parts

| Part | Owns |
|---|---|
| `src/main/main.c` | Mode selection, mouse setup, which recompiled functions run |
| `src/recomp/mp_*.c` | Recompiled routines, one C function per ROM routine (`mp_XXXXXX` = SNES address) |
| `ext/snesrecomp` | Hardware, frame loop, function table, interpreter, timed-recomp hook, validation |
| `tools/canvas_tour.py` | Scripted headless run: title click, then clicks on the canvas |
| `tools/conformance.py` | Per-function verdicts against the genuine ROM |

## Execution modes

The same binary runs three ways, chosen in `main.c`.

**Timed recomp (default).** LakeSnes runs its real, cycle-timed frame loop on
the genuine ROM, and at the entry of every function in the verified list the
opcode hook runs the native C body instead, then returns as the routine's own
`RTS`/`RTL` would. Timing, NMI, HDMA and the SPC700 stay exact; anything not
verified yet runs on the emulated CPU. This is the model the Mario Kart recomp
settled on (`mk/docs/recomp_frame_model_plan.md`): the interpreter's share
shrinks as functions are verified, and at every step the game still plays.

**Real frame (`MP_REALFRAME=1`).** No native code at all. This is the oracle the
harness compares against, and the reference captures in `docs/ref/` come from it.

**Native-driven (`MP_NATIVE=1`).** The original approach: `mp_008000` runs the
boot chain in C, `mp_01E2CE` drives one frame per call (begin frame, vblank, NMI
handler, end frame), and unrecompiled routines fall back to the interpreter
without timing. It reaches the title and the canvas, but it calls every
hand-port whether or not it is correct, and 46 of the 103 it reaches in the
canvas tour are not (see
`tools/conformance.py`). It stays for that work, and becomes the default again
only if it can pass the same checks.

## Why the timed model

Two problems with driving everything from C until every routine is right:

1. **One wrong routine poisons the rest.** The toolbar showed a mix of the main
   bar and the eraser bar because several hand-ports wrote the wrong bytes;
   nothing on screen said which.
2. **The ROM keeps its own time.** Routines spin on `$4212`, step a fade once
   per vblank, or spread work over several frames. Without a timed loop each of
   these needs a band-aid (the frame-driver intercepts on `$01E2CE`, `$018260`
   and the fades exist for this reason).

Inside the timed loop a native body that polls hardware calls
`recomp_timed_spin()` so time passes while it waits, exactly where the ROM
spins.

## Verifying a function

`recomp_timed_add_validate` (snesrecomp) checks a native body against the ROM on
every call without changing the run: save the machine, run the native body,
keep the result, restore, let the genuine routine run, compare WRAM, VRAM, CGRAM
and OAM when it returns. [harness.md](harness.md) covers the tools around it.

A function joins the default list in `main.c` only when it passes. Register
mismatches (A/X/Y left different) are reported but do not fail a function: most
callers ignore them, and hand-ports rarely reproduce dead register values. The
risk is a genuine caller that does read one, so the tour is also run in the
default mode with the whole list intercepted and its screens compared with the
oracle's.
