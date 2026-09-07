# Ground-truth reference captures

Screens from the **genuine ROM** running under LakeSnes (`MP_REALFRAME=1`), not
from recompiled code. Use these as the target when validating the recomp.

Reproduce (no window interaction needed — the title auto-advances into the demo):

```bash
MP_REALFRAME=1 SNESRECOMP_SHOT_FRAMES=2900,7600 SNESRECOMP_EXIT_FRAME=7700 \
  build/Debug/mp_launcher.exe "Mario Paint (JU) [!].smc"
```

| File | What it shows |
|---|---|
| `real_canvas.png` | Art mode. **Top bar**: current-colour swatch, 15-colour palette, creature icon. **Bottom bar**: drawing tools (pencils, eraser, stamp, spray, shapes, fill, hand, page, Undodog) and the page-turn arrow at far right. |
| `real_toolbar_page2.png` | Bottom bar after the page-turn arrow. Contains the **piano-keys icon (Music Composer)** and the **coffee-cup icon (Gnat Attack)**. |
| `real_stamp_mode.png` | Stamp mode — the top bar becomes a stamp picker instead of the colour palette. |

## Comparing the recomp against it

`recomp_canvas.png` is the recompiled build at the same screen. Capture it with:

```bash
SNESRECOMP_SHOT_FRAMES=400 SNESRECOMP_EXIT_FRAME=410   build/Debug/mp_launcher.exe "Mario Paint (JU) [!].smc"
```

Add `SNESRECOMP_DUMP_PREFIX=rec` to also write `rec_f000400.ppu.txt` (BG geometry,
scroll, screen enables, BG3 tilemap rows) and `rec_f000400.snap` (WRAM + VRAM +
CGRAM). Do the same with `MP_REALFRAME=1 SNESRECOMP_DUMP_PREFIX=ref` and diff the
two — that is how the toolbar bug below was found.

## Fixed

Both bars now render, and **BG1 tilemap rows 0-2 are byte-identical to the real
ROM**. Three compounding mistakes were involved:

1. `mp_018000` skipped the title->canvas transition (Phases 5-6), so BG12NBA /
   BG34NBA kept the title screen's `$04/$44` instead of `$06/$66`. Every BG layer
   read its tile data from the wrong character base — the source of the garbage.
2. `mp_0089C3` blanket-filled BG1's toolbar rows with a transparent tile instead
   of building the palette bar, and BG3 was force-enabled (`TM=$17`) to
   compensate. The real ROM runs the canvas with BG3 **off** (`TM=$13`); the
   toolbar is BG1.
3. The bottom bar was built correctly in WRAM by `mp_00A22D` but never DMA'd to
   VRAM, and a hack zeroed BG1 rows 25-31 outright.

`$018000` is now left unregistered so the interpreter runs the genuine routine —
the recompiled body is kept as the starting point for finishing it.

## Still different from the real ROM

- **Canvas border palette.** Rows 3+ come straight from ROM `$02:8000` as
  palette 0; the real game ORs in palette 5 somewhere later (`$00:BEC7` does this
  for the stamp-mode top bar, but the canvas-frame equivalent has not been
  found). Cosmetic: black border instead of red.
- **Toolbar tile data**, VRAM `$7400-$77FF`, differs in 322/1024 words — the
  page-turn cell at the far right of the bottom bar renders wrong.
- **Phantom sprites** occasionally appear on the canvas; OBJ character data
  differs in 963/8192 words.
