"""Conformance harness: does each recompiled function match the ROM, call by call?

1. Oracle: the genuine ROM (MP_REALFRAME=1) runs the canvas tour — title click,
   then every bottom-bar button. MP_PROFILE lists every JSR/JSL target it
   executed, and whether a recompiled body is registered for it.
2. The same tour runs again with all of those under lockstep validation
   (MP_VALIDATE, snesrecomp's recomp_timed_add_validate): on each call the
   native body runs on a saved copy of the machine, the genuine routine then
   runs for real, and WRAM/VRAM/CGRAM/OAM are compared when it returns.
   Up to 3000 calls per function are checked: every call of a per-frame
   routine across the whole tour (the toolkit default of 300 stopped before the
   eraser was clicked and hid its bomb-icon bug), while the SPC upload helper's
   125k calls stay affordable.
   Execution stays genuine throughout, so one wrong function cannot hide or
   cause another's failure, and the run's snapshots must equal the oracle's.

PASS = called at least once, no memory or I/O-register mismatch on any checked call, and no
call skipped (a skipped call - a jump exit, a body that drives frames, or one
that falls back to the interpreter for a callee - was not checked, so the
function is not verified). A routine that returns with M/X
widths different from the ROM fails (its caller would run at the wrong width).
Other register/flag mismatches are reported, not failed:
tools/intercept_bisect.py is the check for callers that read them.

  py tools/conformance.py <rom> [--update]

Prints "conformance: P/N pass". Per-function lines go to scratch/conf/results.txt.
tools/conformance_baseline.txt holds the pass count; the run exits 1 if it drops
(--update rewrites it).
"""
import os, sys, re

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import canvas_tour  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "scratch", "conf")
BASELINE = os.path.join(HERE, "conformance_baseline.txt")

# Frame drivers: their native bodies present a frame (begin/end_frame), which
# is how the native-driven build paces itself, not a translation of the ROM
# routine. Run natively they would advance the capture clock mid-check.
FRAME_DRIVERS = {"018260", "01E2CE", "01E794", "01E7C9"}


def snaps(d):
    return {f: open(os.path.join(d, f), "rb").read() for f in sorted(os.listdir(d)) if f.endswith(".snap")}


def main():
    args = sys.argv[1:]
    update = "--update" in args
    rom = [a for a in args if a != "--update"][0]

    oracle = os.path.join(OUT, "oracle")
    r = canvas_tour.run(rom, canvas_tour.BOTTOM, oracle, real=True, extra_env={"MP_PROFILE": "5000"})
    prof = re.findall(r"^PROF (\w{6}) +\d+ +\w\w (JSR|JSL) .*recompiled", r.stderr, re.M)
    spec = ",".join(a + ("L" if k == "JSL" else "") for a, k in prof if a not in FRAME_DRIVERS)

    checked = os.path.join(OUT, "validated")
    r = canvas_tour.run(rom, canvas_tour.BOTTOM, checked, real=True, extra_env={"MP_VALIDATE": spec,
                                                                              "SNESRECOMP_VALIDATE_MAX": "3000"})
    if snaps(oracle) != snaps(checked):
        print("WARNING: validation changed the run (snapshots differ from the oracle)")

    rows = re.findall(r"^VALIDATE (\w{6}) calls=(\d+) mem_fail=(\d+) reg_fail=(\d+) mx_fail=(\d+) skipped=(\d+)(.*)$",
                      r.stderr, re.M)
    kind = dict(prof)
    passed, verified = 0, []
    with open(os.path.join(OUT, "results.txt"), "w") as f:
        for addr, calls, mem, reg, mx, skip, first in rows:
            ok = int(calls) > 0 and int(mem) == 0 and int(mx) == 0 and int(skip) == 0
            passed += ok
            if ok:
                verified.append(addr + ("L" if kind.get(addr) == "JSL" else ""))
            f.write(f"{addr} {'PASS' if ok else 'FAIL'} calls={calls} mem_fail={mem} reg_fail={reg} mx_fail={mx} "
                    f"skipped={skip}{first}\n")
    # MP_TIMED format: what main.c's k_verified should hold.
    with open(os.path.join(OUT, "verified.txt"), "w") as f:
        f.write(",".join(sorted(verified)) + "\n")
    print(f"conformance: {passed}/{len(rows)} pass (details: scratch/conf/results.txt)")

    base = int(open(BASELINE).read().split()[0]) if os.path.exists(BASELINE) else 0
    if update or not os.path.exists(BASELINE):
        open(BASELINE, "w").write(f"{passed} {len(rows)}\n")
    elif passed < base:
        print(f"REGRESSION: {passed} < baseline {base}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
