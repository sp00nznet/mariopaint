# Mario Paint Recomp

**A native PC port of Mario Paint (SNES, 1992) via static recompilation.**

You know what's cooler than emulating Mario Paint? *Running it natively.*

This project takes the original 65816 machine code from Mario Paint and converts it into equivalent C code that runs directly on your CPU — no emulator in the loop. The SNES hardware (PPU, SPC700 audio, DMA) is provided by [snesrecomp](https://github.com/sp00nznet/snesrecomp), which wraps a real SNES hardware backend. The result is Mario Paint running as a first-class native application.

```
 ╔══════════════════════════════════════════════════════╗
 ║  MARIO PAINT                           _ _          ║
 ║                                       |   |  click  ║
 ║  ┌──────────────────────────────────┐  | o |  click  ║
 ║  │                                  │  |   |  click  ║
 ║  │     your masterpiece goes here   │  |___|         ║
 ║  │                                  │   SNES         ║
 ║  │          (now in native C)       │   Mouse        ║
 ║  │                                  │  (now SDL2)    ║
 ║  └──────────────────────────────────┘                ║
 ║  [pencil] [fill] [stamp] [eraser] [undo] [music]    ║
 ╚══════════════════════════════════════════════════════╝
```

![Mario Paint Title Screen](title2.png)

## Why Mario Paint?

Because it's a weird, wonderful game that nobody expected to be recompiled:

- **The SNES Mouse.** Mario Paint was *the* killer app for the SNES Mouse peripheral. This project maps your real PC mouse to the SNES Mouse protocol, so every click and drag works exactly like it did in 1992 — but without the mouse ball getting gunked up.

- **Mario Paint Composer.** The music composition tool spawned an entire internet subculture. Thousands of covers, remixes, and original compositions have been made with it. Now it runs natively on your PC with real SPC700 audio.

- **Gnat Attack.** The hidden flyswatter minigame that was better than most full-price games. Swat those flies with your newly-nativized mouse.

- **It's a creativity tool, not just a game.** Drawing, animation, stamps, music — Mario Paint was a multimedia suite before multimedia suites were cool.

## Status

**Active recompilation** — ~175 functions recompiled across 16 source files. Canvas drawing works with click-and-drag, toolbar palette visible, SPC700 audio plays.

![Title Screen](title2.png)
![Clean Canvas](cleancanvas.png)

### What works
- **Full game flow**: boot → SPC700 audio upload → title screen → click → canvas mode
- **Title screen**: "MARIOPAINT" with cursor, "(c) 1992 Nintendo", sprite animations
- **Canvas drawing**: pencil tool with click-and-drag, visible pixel output
- **Drawing tools**: pencil, line (Bresenham), rectangle, ellipse, fill, stamp, spray can, undo
- **Toolbar**: 15-color palette bar across top of canvas
- **SPC700 audio**: direct RAM upload, 4-channel command queues — music plays!
- **Mouse input**: SNES Mouse mapped from SDL2, cursor tracks mouse movement
- **PPU rendering**: all 4 BG layers, sprites, palette, DMA transfers working
- **F12 screenshots**: PNG output with toast notification (stb_image_write + SDL2_ttf)
- NMI handler with OAM/VRAM/palette DMA, PPU register mirror writeback
- HDMA (3 channels), frame sync, fade in/out, sprite animation engine
- Full 4BPP bitplane canvas manipulation (B25E/B23C/B1C2 pipeline)

### Known issues
- Cursor shows prohibition icon (cursor sprite selection needs work)
- Mouse position offset (SDL mouse scaling needs tuning)
- Bottom toolbar/palette bar not yet visible
- Title screen fly-by sprite characters partially working (animation states 2-11)
- Title screen brightness pulsing during idle animation

### Recompilation progress
| Area | Functions | Source File |
|------|-----------|-------------|
| Boot/System | 9 | `mp_boot.c` |
| DMA/PPU Engine | 22 | `mp_bank01.c` |
| Input/Cursor | 7 | `mp_input.c` |
| Game Logic | 5 | `mp_gamelogic.c` |
| Graphics Init | 13 | `mp_gfxinit.c` |
| Sprite Engine | 3 | `mp_sprites.c` |
| Canvas/UI | 5 | `mp_canvas.c` |
| Audio Engine | 11 | `mp_audio.c` |
| Title Screen | 2 | `mp_title.c` |
| Boot Helpers | 13 | `mp_helpers.c` |
| Title Loop/Demo | 12 | `mp_titleloop.c` |
| Drawing Tools | 17 | `mp_tools.c` |
| Drawing Core | 9 | `mp_draw.c` |
| Canvas Interaction | 9 | `mp_interact.c` |
| Miscellaneous | 11 | `mp_misc.c` |
| Shapes/Drawing | 16 | `mp_shapes.c` |
| **Total** | **~175** | **~12,000 lines of C** |

### What's next
- Cursor icon fix (prohibition → correct tool cursor)
- Mouse position accuracy
- Bottom toolbar/palette display
- Title screen animation states 2-11 (fly-by characters)
- Canvas audio (SPC700 canvas-mode sample upload)
- Music composer
- Gnat Attack minigame

## Building

### Prerequisites
- CMake 3.16+
- A C17 compiler (MSVC 2022, GCC, Clang)
- SDL2, SDL2_ttf (via vcpkg on Windows, or your system package manager)

### Build

```bash
git clone --recursive https://github.com/sp00nznet/mariopaint.git
cd mariopaint

# Windows (MSVC + vcpkg)
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release

# Linux / macOS
cmake -B build && cmake --build build
```

### Run

```bash
./build/Release/mp_launcher "path/to/Mario Paint (JU).sfc"
```

You'll need to supply your own ROM file. We don't distribute copyrighted material.

**Controls:**
- Mouse: SNES Mouse (move cursor, left click to draw/select)
- F12: Save PNG screenshot
- Escape: Quit

## How it works

### Static Recompilation

Instead of interpreting SNES instructions one-by-one (like an emulator), we translate the entire game into equivalent C code ahead of time:

```
Original 65816:                    Recompiled C:

LDA #$80          ──────────►     op_lda_imm8(0x80);
STA $2100                         op_sta_abs8(0x2100);
                                  // writes to real PPU via LakeSnes
```

The recompiled code calls into [snesrecomp](https://github.com/sp00nznet/snesrecomp), which provides real SNES hardware emulation (LakeSnes) as a linkable library. PPU writes update real PPU state. APU writes go to a real SPC700. DMA transfers move real data. You get authentic behavior without writing a single line of hardware emulation.

### The Mouse Problem (solved!)

Mario Paint was designed for the SNES Mouse — a serial device that sends 32-bit position/button data through the controller port. Most emulators handle this internally, but for static recompilation we needed to:

1. **Track SDL2 mouse events** — accumulate motion deltas each frame
2. **Encode SNES Mouse protocol** — pack buttons, sensitivity, and displacement into the 32-bit serial format
3. **Feed it into LakeSnes** — populate auto-joypad registers and handle manual serial reads from `$4016`

The result: your PC mouse becomes an SNES Mouse. Move it, click it, draw with it — the recompiled Mario Paint code reads the same registers and gets the same data format as the original hardware.

## Project Structure

```
mariopaint/
├── CMakeLists.txt          # Build configuration
├── CLAUDE.md               # Technical design doc
├── README.md               # You are here
├── include/mp/
│   ├── cpu_ops.h           # 65816 instruction helpers
│   └── functions.h         # Recompiled function declarations
├── src/
│   ├── main/main.c         # Entry point (launches full boot chain)
│   └── recomp/
│       ├── mp_boot.c       # Bank 00 boot chain + NMI + main loop
│       ├── mp_bank01.c     # Bank 01 DMA/PPU/system helpers
│       ├── mp_input.c      # Mouse input + cursor + animations
│       ├── mp_gamelogic.c  # Game logic dispatch + cursor rendering
│       ├── mp_gfxinit.c    # Palette/tile/tilemap loading from ROM
│       ├── mp_draw.c       # Pixel plotting core (B051/B0D3/B1C2)
│       ├── mp_shapes.c     # Line/rect/ellipse + pen mask (B25E/B23C)
│       ├── mp_tools.c      # Tool handlers + pen tile transforms
│       └── ...             # 8 more source files
└── ext/
    └── snesrecomp/         # SNES hardware backend (submodule)
```

## Related Projects

- **[snesrecomp](https://github.com/sp00nznet/snesrecomp)** — The SNES hardware library that makes this possible
- **[Super Mario Kart Recomp](https://github.com/sp00nznet/mk)** — Sister project, same architecture
- **[Mario Paint Disassembly](https://github.com/Yoshifanatic1/Mario-Paint-Disassembly)** — Full 65816 disassembly (reference)
- **[LakeSnes](https://github.com/angelo-wf/LakeSnes)** — The SNES emulator powering the hardware backend

## Contributing

This is an active recompilation effort. The main work is translating 65816 assembly into C functions using the cpu_ops helpers. If you're familiar with SNES assembly or have experience with game reverse engineering, contributions are welcome!
