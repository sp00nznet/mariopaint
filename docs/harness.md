# Headless harness and conformance

Everything here runs without a window, so it works over RDP and in CI
(section 13 of the house rules). The tools live in `tools/`; their output lands
in `scratch/`, which is gitignored.

## Headless runs

```bash
build/Debug/mp_launcher.exe --headless "Mario Paint (JU) [!].smc"
build/Debug/mp_launcher.exe --record out.mp4 "Mario Paint (JU) [!].smc"
```

`--headless` creates no window, renderer or audio device, and does not pace to
60 Hz, so a run is as fast as the host allows (about 120 frames a second in a
Debug build). `--record` implies `--headless` and pipes every frame to
`ffmpeg` (must be on `PATH`) as H.264. Both come from snesrecomp
(`snesrecomp_parse_args`), so every snesrecomp title has the same flags.

The capture knobs are snesrecomp environment variables:

| Variable | Effect |
|---|---|
| `SNESRECOMP_SHOT_FRAMES=400,900` | PNG after those presented frames (`screenshot_NNNN.png`) |
| `SNESRECOMP_EXIT_FRAME=910` | exit once that frame has presented |
| `SNESRECOMP_DUMP_PREFIX=s` | with each shot, also `s_fNNNNNN.snap` (WRAM + VRAM + CGRAM) and `.ppu.txt` |
| `SNESRECOMP_MOUSE_SCRIPT="..."` | scripted mouse, below |

## Scripted mouse

`frame:dx:dy:btn[:count]` sends a relative delta; `frame:@x:@y:btn[:count]`
parks a virtual pointer at SNES pixel (x, y). `main.c` registers Mario Paint's
cursor (`$04DC`/`$04DE`) with `recomp_input_set_mouse_cursor_addr`, so
snesrecomp reports whatever delta walks the cursor onto the pointer. That works
the same in recompiled and real-frame mode, which is what lets one script drive
both.

```bash
SNESRECOMP_MOUSE_SCRIPT="600:@40:@212:0:30 630:@40:@212:L:3"
```

Things that cost time to find:

- **Starting the game means clicking Mario.** The title only accepts a left
  click inside a window around the walking sprite (`$01:8CEC`: cursor within
  [x-24, x+24) of `$0792/2` and [y-32, y+4) of `$0794/2`). Mario walks along
  y = 169 at about a pixel a frame, so `canvas_tour.py` parks the cursor at
  (128, 168) and clicks every 4 frames. Clicking every 16 frames skipped
  straight over the hit box.
- **Mouse buttons were swapped in LakeSnes.** The serial packet's 9th bit is
  Right and the 10th Left. LakeSnes had them the other way round, so a left click
  reached the genuine ROM as a right click and the title never started. Mario
  Paint's own decode (`$01:DA88`) is the proof: `$04CA` bit 5 (left pressed)
  comes from bit 6 of `$4218`, the 10th bit read.
- **`$04CA` layout** (from `$01:DA88`/`$01:DABF`): bits 4/5/6 = left
  held/pressed/double-click, bits 0/1/2 = the same for right. The old
  hand-written reader set both groups on a left click.
- **Script steps cap at 4096.** Clicking every 4 frames uses about 500 steps;
  the old cap of 256 silently dropped everything after the title.

## Canvas tour

```bash
py tools/canvas_tour.py "Mario Paint (JU) [!].smc"          # recompiled build
py tools/canvas_tour.py "Mario Paint (JU) [!].smc" --real   # genuine ROM
py tools/canvas_tour.py "Mario Paint (JU) [!].smc" 40,212 150,212
```

The script enters the canvas, clicks each point (every bottom-bar slot by
default), and saves `NN_x_y.png` plus a `.snap` after each click. `run.log`
holds the console output.

## Conformance

```bash
py tools/conformance.py "Mario Paint (JU) [!].smc"
```

1. The genuine ROM (`MP_REALFRAME=1`) runs the tour as the oracle.
   `MP_PROFILE` lists every `JSR`/`JSL` target it executed, and whether a
   recompiled body is registered for it.
2. Each registered function is then tested on its own: the same tour with only
   that function intercepted (`MP_TIMED=addr`), so the native C body runs in
   place of the ROM routine and everything else stays genuine.
3. A function passes when every snapshot (WRAM, VRAM, CGRAM) after every click
   is byte-identical to the oracle.

Testing one function at a time matters. With the whole recompiled boot chain
running, one wrong routine corrupts state that a dozen later ones read, and
the screen tells you nothing about which one was wrong.

Per-function results go to `scratch/conf/results.txt`. A failure names the first
differing shot and region, for example `03_62_212.snap: wram 12B @004CA`.
`tools/conformance_baseline.txt` holds the pass count; the run exits 1 if it
drops below it, and `--update` raises it.

The ROM is not in the repo, so CI builds and skips this step; run it locally
before committing a recompiled function.

## Generated routines

```bash
py tools/gen.py "Mario Paint (JU) [!].smc"     # writes gen/mp_gen.c (gitignored)
cmake -B build && cmake --build build --config Release
```

`gen.py` profiles the genuine ROM through the tour, then hands every reached
routine to snesrecomp's `tools/autogen.py`. The routines it can lift are built in
in automatically, and with `MP_GEN=1` they are registered over the hand-ports
at the same addresses. They are opt-in because no validation run over them has
completed yet. The file is derived from your ROM and is never committed.

Generated routines go through the same conformance run as hand-ports, and only
passing ones join `k_verified`.

## When intercepts break together

Validation proves each call's effect on memory and I/O, but an intercepted
routine also hands registers back to a genuine caller. If the default build
misbehaves while every function passes:

```bash
py tools/intercept_bisect.py "Mario Paint (JU) [!].smc" "$(cat scratch/conf/verified.txt)"
BISECT_THRESH=0.001 py tools/intercept_bisect.py ...   # catch single-sprite differences
```

It runs the tour with groups of intercepts, scores each against the oracle's
screens, and splits failing groups down to single routines. Two finds so far:
sound-queue pushes that returned in 8-bit mode (now caught by validation as
`mx_fail`), and the eraser's bomb icon, wrong only after the 300th call (the
reason conformance now checks up to 3000 calls per function).
