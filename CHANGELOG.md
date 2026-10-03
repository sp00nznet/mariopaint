# Changelog

All notable changes to this project. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow SemVer once the first one is tagged.

## [Unreleased]

### Added
- `--headless` and `--record out.mp4`: run with no window, audio or pacing, and
  optionally pipe frames to ffmpeg.
- Needs snesrecomp #1-#5 (stacked: headless/record, mouse pointer steering,
  lockstep validation, LakeSnes mouse-button bump, autogen) and LakeSnes #1.
- `tools/canvas_tour.py`: drives the title click and every canvas bottom-bar
  button headlessly, on the recompiled build or the genuine ROM (`--real`).
- `tools/conformance.py`: lockstep-validates every recompiled function the tour
  reaches against the genuine ROM and reports a pass count, with a committed
  baseline that fails the run on regression.
- `tools/first_diff.py`: finds the first frame an intercepted function makes
  WRAM diverge.
- `MP_TIMED`, `MP_VALIDATE`, `MP_PROFILE` in `main.c` (timed-recomp intercepts,
  lockstep validation, call profile).
- `tools/intercept_bisect.py`: finds which intercepted functions break the
  tour when run together (callers reading registers a hand-port left wrong).
- `tools/gen.py`: profiles the genuine ROM through the tour and lifts every
  reached routine with snesrecomp's `autogen.py` into `gen/mp_gen.c`
  (gitignored; built in when present, used with `MP_GEN=1`; not yet validated).
- `docs/harness.md`, `CHANGELOG.md`, `ROADMAP.md`, `CONTRIBUTING.md`.

### Changed
- Default mode is now timed recomp: the genuine ROM in LakeSnes's timed frame
  loop, with every function that passes conformance (`k_verified` in `main.c`)
  running natively. The previous native-driven boot chain is `MP_NATIVE=1`.
- `$01:D9E1` (mouse read) is now a straight translation of `$01:D9E1-$01:DB30`:
  serial clocking from `$4016`, two's-complement displacements, the ROM's
  `$04CA` layout and double-click logic, and the `$4212` auto-read wait.
- `$00:8B48` (cursor move) translated from the ROM: two's-complement deltas,
  `$1B26`/`$1B28` both written.
- Cursor locking to the host pointer moved out of the mouse reader into
  snesrecomp (`recomp_input_set_mouse_cursor_addr`).
- Root screenshots moved to `docs/screenshots/`.

### Fixed
- APU command-queue pushes (`$01:D308/D328/D348/D368`, `$01:D2BF`) returned in
  8-bit mode: the ROM wraps them in PHP/PLP, the hand-ports did `SEP #$30` and
  left callers running at the wrong width. They also return the advanced
  index in A now, as the ROM does.
- Eraser bomb icon (`$00:81CA`) used tile bytes in the wrong order; it and
  `$00:823C` now read their frames from the ROM tables.
- `$01:DE2D` (APU command sender) did not clear the port after a command was
  echoed.
- `$01:E747` and `$01:D9E1` now wait out the auto-joypad read (`$4212`) as the
  ROM does, instead of reading `$4218` mid-read in the timed loop.
- Left and right mouse buttons were swapped in LakeSnes's mouse packet, so a
  left click reached the genuine ROM as a right click and the title screen
  could not be started in real-frame mode.
- The old mouse reader set both buttons' bits on a left click and treated
  displacements as sign-magnitude after the ROM had already converted them.

### Removed
- `smk_config.ini` from the repo (per-machine menu settings; now gitignored).

## Before this file

History up to September 2026 (boot chain, title screen, canvas drawing,
interpreter fallback, real-frame reference captures, stack pointer and title
handover fixes) is in `git log`.
