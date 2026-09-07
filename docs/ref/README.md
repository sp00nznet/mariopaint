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

## What the recomp currently gets wrong

The recomp draws garbled tiles where the top palette bar belongs and nothing at
all along the bottom. The tile *graphics* are loading correctly (real "SAVE" /
"LOAD" text is visible in the garbage), so this is a BG3 tilemap addressing /
palette problem, not missing art. Pre-dates the snesrecomp migration — the same
bug is present at 5b7705e, just with different garbage.
