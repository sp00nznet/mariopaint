"""Generate gen/mp_gen.c from your ROM with snesrecomp's autogen.

1. Runs the canvas tour on the genuine ROM with MP_PROFILE, which lists every
   JSR/JSL target executed, the M/X flags it was entered with, and JSR vs JSL.
2. Feeds those to ext/snesrecomp/tools/autogen.py, which lifts each routine it
   can to C in gen/mp_gen.c (gitignored: derived from the ROM, never committed).

  py tools/gen.py <rom>
  cmake --build build --config Release     # picks gen/ up (re-run cmake once)

Skipped: routines entered with more than one M/X combination (MULTI-MX) and
the frame drivers, which the native-driven mode replaces on purpose. Then run
tools/conformance.py: generated routines are validated like hand-ports.
"""
import os, re, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import canvas_tour  # noqa: E402
from conformance import FRAME_DRIVERS  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")


def main():
    rom = sys.argv[1]
    out = os.path.join(ROOT, "scratch", "gen_profile")
    r = canvas_tour.run(rom, canvas_tour.BOTTOM, out, real=True, extra_env={"MP_PROFILE": "100000"})
    rows = re.findall(r"^PROF (\w{6}) +\d+ +(\w\w) (JSR|JSL) (MULTI-MX )?", r.stderr, re.M)
    lst = os.path.join(out, "routines.txt")
    with open(lst, "w") as f:
        for addr, p, kind, multi in rows:
            if not multi and addr not in FRAME_DRIVERS:
                f.write(f"{addr} {p} {kind}\n")
    os.makedirs(os.path.join(ROOT, "gen"), exist_ok=True)
    return subprocess.call([sys.executable, os.path.join(ROOT, "ext", "snesrecomp", "tools", "autogen.py"),
                            rom, lst, os.path.join(ROOT, "gen", "mp_gen.c")])


if __name__ == "__main__":
    sys.exit(main())
