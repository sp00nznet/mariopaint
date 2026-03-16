# Mario Paint — Static Recompilation

## Project Overview
Static recompilation of Mario Paint (SNES, 1992) from 65816 assembly to native C code.
Mario Paint is notable for being one of the few SNES games that requires the SNES Mouse
peripheral, and features a music composition tool (the famous "Mario Paint Composer").

## Architecture
- **Source CPU**: WDC 65C816 (16-bit, 3.58 MHz) + SPC700 audio
- **ROM Layout**: LoROM, 512 KB (Mario Paint (JU) [!])
- **Target**: Native x86-64 C code with SDL2 for windowing/rendering/audio
- **Hardware Backend**: [snesrecomp](https://github.com/sp00nznet/snesrecomp) library (LakeSnes-powered)
- **Special Hardware**: SNES Mouse on port 1 (mapped from SDL2 mouse input)

## Hardware Backend (snesrecomp)
All SNES hardware emulation is provided by the snesrecomp library (ext/snesrecomp/),
which wraps LakeSnes — a real, cycle-accurate SNES emulator written in pure C.
- Real PPU rendering (Mode 0-7, sprites, windows, color math, hi-res)
- Real SPC700 + DSP audio (BRR, echo, noise, 8 channels)
- Real DMA (GPDMA + HDMA, all 8 channels)
- Full memory bus routing (LoROM/HiROM auto-detection)
- SNES Mouse support (SDL mouse → SNES mouse serial protocol)

## Key Challenges
1. **SNES Mouse**: Mario Paint requires mouse input. snesrecomp now has
   SNES Mouse support that maps SDL2 mouse events to the SNES mouse protocol.
   Port 1 is configured as SNES_INPUT_MOUSE.
2. **Music Composer**: The music composition screen is the most complex interactive
   feature. Sequencer state, note playback, and the SPC700 audio engine all need
   to work together.
3. **Stamp/Drawing Tools**: Multiple drawing modes with undo, stamps, animations,
   and the flyswatter minigame.

## Key References
- Yoshifanatic1/Mario-Paint-Disassembly — Full 65816 disassembly using Asar
- The Cutting Room Floor (tcrf.net/Mario_Paint) — Hidden features and debug modes
- sp00nznet/mk — Sister project (Super Mario Kart recomp) for structural reference

## Build System
- CMake 3.16+, MSVC (Visual Studio 2022) primary
- snesrecomp (ext/snesrecomp/) added as CMake subdirectory
- SDL2 via vcpkg

## ROM Details
- Internal name: MARIO PAINT
- ROM type: LoROM
- ROM file not included — user must supply their own copy

## Code Conventions
- C17 standard for recompiled code
- 65816 register state in `SnesCpu g_cpu` (from snesrecomp)
- Memory access via `bus_read8()` / `bus_write8()` (routes to real LakeSnes hardware)
- Function naming: mp_XXXXXX (where XXXXXX = SNES address in hex)
- Function dispatch via `func_table_register()` / `func_table_call()`
