"""Find the first frame where an intercepted function makes WRAM diverge.

Runs the title-click script on the genuine ROM and again with MP_TIMED=<fn>,
snapshotting every frame in [start, end), and prints the first frame whose WRAM
(stack page masked) differs, with the differing bytes. Narrows a conformance
FAIL down to the frame where the native body first did something different.

  py tools/first_diff.py <rom> <fn[L]> [start] [end]
"""
import os, sys, glob, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import canvas_tour as ct  # noqa: E402
import conformance as cf  # noqa: E402


def frames(rom, d, start, end, extra):
    os.makedirs(d, exist_ok=True)
    for p in glob.glob(os.path.join(d, "*")):
        os.remove(p)
    env = dict(os.environ, SNESRECOMP_MOUSE_SCRIPT=" ".join(ct.TITLE), MP_REALFRAME="1",
               SNESRECOMP_SHOT_FRAMES=",".join(map(str, range(start, end))),
               SNESRECOMP_DUMP_PREFIX="s", SNESRECOMP_EXIT_FRAME=str(end), **extra)
    subprocess.run([os.path.abspath(ct.EXE), "--headless", os.path.abspath(rom)], cwd=d, env=env,
                   capture_output=True)
    return {p[-11:-5]: cf.masked(open(p, "rb").read()) for p in sorted(glob.glob(os.path.join(d, "s_f*.snap")))}


if __name__ == "__main__":
    rom, fn = sys.argv[1], sys.argv[2]
    start = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    end = int(sys.argv[4]) if len(sys.argv) > 4 else 1500
    base = os.path.join(ct.HERE, "..", "scratch", "first_diff")
    # SNESRECOMP_SHOT_FRAMES takes at most 32 frames, so walk 32-frame windows.
    for lo in range(start, end, 32):
        hi = min(lo + 32, end)
        a = frames(rom, os.path.join(base, "oracle"), lo, hi, {})
        b = frames(rom, os.path.join(base, fn), lo, hi, {"MP_TIMED": fn})
        for f in a:
            x, y = a[f][20:20 + 0x20000], b.get(f, b"")[20:20 + 0x20000]
            if x != y:
                d = [i for i in range(len(x)) if i >= len(y) or x[i] != y[i]]
                print(f"frame {int(f)}: {len(d)} bytes differ (oracle/native)")
                print("  " + " ".join(f"{i:05X}:{x[i]:02X}/{y[i]:02X}" for i in d[:32] if i < len(y)))
                sys.exit(1)
    print(f"no WRAM difference in frames {start}..{end - 1}")
