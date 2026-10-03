"""Which intercepts break the game when run together?

Lockstep validation proves each function's memory effects call by call, but an
intercepted function also hands registers and timing back to genuine callers.
This runs the canvas tour with groups of MP_TIMED intercepts and scores each
group by the fraction of pixels that differ from the oracle's screens
(scratch/conf/oracle, from conformance.py), splitting failing groups until
single functions are left.

  py tools/intercept_bisect.py <rom> ADDR[L],ADDR[L],...

Prints each culprit. ponytail: pixel threshold, not an exact match - the cursor
and Mario land a frame or two apart once native bodies skip cycles.
"""
import os, sys
from concurrent.futures import ThreadPoolExecutor
from PIL import Image, ImageChops

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import canvas_tour  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ORACLE = os.path.join(HERE, "..", "scratch", "conf", "oracle")
THRESH = float(os.environ.get("BISECT_THRESH", "0.02"))


def score(rom, group, tag):
    d = os.path.join(HERE, "..", "scratch", "bisect", tag)
    canvas_tour.run(rom, canvas_tour.BOTTOM, d, extra_env={"MP_TIMED": ",".join(group)})
    worst = 0.0
    for f in sorted(os.listdir(ORACLE)):
        if not f.endswith(".png"):
            continue
        p = os.path.join(d, f)
        if not os.path.exists(p):
            return 1.0
        a, b = Image.open(os.path.join(ORACLE, f)).convert("RGB"), Image.open(p).convert("RGB")
        diff = ImageChops.difference(a, b).convert("L").point(lambda v: 255 if v > 24 else 0)
        worst = max(worst, sum(diff.histogram()[255:]) / (a.width * a.height))
    return worst


def bisect(rom, groups0):
    bad, todo, n = [], list(groups0), 0
    while todo:
        groups = [g for g in todo if g]
        todo = []
        with ThreadPoolExecutor(6) as ex:
            scores = list(ex.map(lambda gi: score(rom, gi[1], f"g{n}_{gi[0]}"), enumerate(groups)))
        n += 1
        for g, s in zip(groups, scores):
            print(f"{s:.3f} {','.join(g)}", flush=True)
            if s <= THRESH:
                continue
            if len(g) == 1:
                bad.append(g[0])
            else:
                todo += [g[:len(g) // 2], g[len(g) // 2:]]
    return bad


if __name__ == "__main__":
    rom, items = sys.argv[1], sys.argv[2].split(",")
    # Six slices up front so the first round already runs in parallel.
    culprits = bisect(rom, [items[i::6] for i in range(6)])
    print("CULPRITS:", ",".join(culprits) or "none")
