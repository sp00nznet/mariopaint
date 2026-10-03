# Roadmap

## Next

- **Fix the failing hand-ports.** `tools/conformance.py` lists each with the
  first byte it gets wrong. Several share one cause (frame-wait helpers that
  drive the native frame loop), so a handful of fixes should clear many.
- **Translate the reached-but-unrecompiled routines** the canvas tour executes,
  hottest first (`MP_PROFILE`), including `$01DFC1` (125k calls per tour).
- **Jump-table handlers.** The profiler sees direct `JSR`/`JSL` only; the
  toolbar and tool dispatch go through tables (`$008683`, `$00A22D`), so their
  handlers need listing from the tables themselves.
- **Validate the generated routines.** snesrecomp's `autogen.py` lifts 111 of
  the 119 routines the tour reaches (`tools/gen.py`); none has been through a
  completed conformance run yet. Passing ones join `k_verified`, replacing the
  failing hand-ports. The 8 it refuses (stack tricks, a routine reached at two
  widths) need generator support or hand-ports.
- **Extend the tour** past the bottom bar: palette clicks, drawing strokes,
  stamp and page modes, then the Music Composer and Gnat Attack.
- **Quick-start `Setup.cmd`** per the house rules (prerequisite checks, ROM
  prompt, build, launcher shortcut).

## Deferred

- The native-driven boot chain (`MP_NATIVE=1`) is kept but not the default
  until its hand-ports pass conformance; fixing it routine by routine is the
  same work as above.
- Canvas border palette and page-turn tile differences seen in the native-driven
  build (`docs/ref/README.md`); they come from the failing routines above.

## Out of scope

- Distributing the ROM, any part of it, or code generated from it.
- Online features, save sharing, or enhancements that change how the game plays.
