"""Null tests: FL Studio's Sampler vs Chan Sampler.

Each case renders the same sample and note through FL (command-line render of a
generated project) and through Chan Sampler (offline renderer), scales ours by
FL's channel gain (volume 78.125% + centre pan = 0.389612, measured), flips
its polarity, sums, and reports the residual relative to FL's signal.
Residual < -60 dB = null (FL's 16-bit export floor is about -70 dB here).

    python nulltest.py              # every offline case (cases_offline.py); FL renders through the queue
    python nulltest.py --only eq    # cases whose names contain eq
    python nulltest.py --dry        # the render plan only: FL is not started
    python nulltest.py --report     # NULLTESTS.md from the saved results (ours/offline_results.json)
"""
import json, os, subprocess, sys
import numpy as np
import ref_solo_lib as F; import wav_lib; import paths_lib

HERE = paths_lib.ROOT
RENDER = paths_lib.RENDER_TOOL
OURS = os.path.join(HERE, "ours"); os.makedirs(OURS, exist_ok=True)
CHANNEL_GAIN = 0.389612
OUT_DEFAULT = 0.5   # a default Chan Sampler's total output (OUT at FL's 78%)
TEMPO = 130.0

def ours(name, sample, params, key=60, velocity=100, beats=8, seconds=None, notes=None):
    pfile = os.path.join(OURS, name + ".txt")
    # OUT stays at its default (0.5 in total); FL's channel gain is applied below in its place
    with open(pfile, "w") as f:
        for k, v in params.items(): f.write(f"{k} {v}\n")
    out = os.path.join(OURS, name + ".wav")
    notes = notes or [(0, beats, key, velocity)]
    b2s = 60.0 / TEMPO
    seconds = seconds or max(st + ln for st, ln, _, _ in notes) * b2s + 1.5
    spec = ";".join(f"{k}:{st * b2s}:{ln * b2s}:{v}" for st, ln, k, v in notes)
    subprocess.run([RENDER, paths_lib.signal(sample), out, str(seconds), pfile, spec], check=True)
    return wav_lib.read(out)[0] * (CHANNEL_GAIN / OUT_DEFAULT)

def null(fl, us):
    n = max(len(fl), len(us))
    a = np.zeros((n, 2)); a[:len(fl)] = fl
    b = np.zeros((n, 2)); b[:len(us)] = us
    r = a - b
    e = max((a ** 2).sum(), 1e-30)
    return 10 * np.log10(max((r ** 2).sum(), 1e-30) / e)

def run(cases, only=None):
    results = []
    for c in cases:
        name = c["name"]
        if only and not any(o in name for o in only): continue
        fl = F.render(name, c["sample"], c.get("fl", []), velocity=c.get("vel", 100), beats=c.get("beats", 8), notes=c.get("notes"))
        us = ours(name, c["sample"], c.get("ours", {}), velocity=c.get("vel", 100), beats=c.get("beats", 8), notes=c.get("notes"))
        r = null(fl, us)
        results.append((name, r))
        print(f"{F.verdict(r):5s} {r:7.1f} dB  {name}", flush=True)
    return results


# ----------------------------------------------------------------------------- results and the report

RESULTS = os.path.join(OURS, "offline_results.json")   # [[case, dB], ...] of the last full run (git-ignored)
REPORT = os.path.join(os.path.dirname(HERE), "NULLTESTS.md")


def save_results(results, path=RESULTS):
    with open(path, "w", encoding="utf-8") as h:
        json.dump([[n, float(d)] for n, d in results], h)


def load_results(path=RESULTS):
    with open(path, encoding="utf-8") as h:
        return [(n, float(d)) for n, d in json.load(h)]


def _db(v):
    return f"−{-v:.1f}" if v < 0 else f"{v:.1f}"


def report(results):
    """NULLTESTS.md from the offline suite's results: the summary and one row per case."""
    r = dict(results)
    verdicts = [F.verdict(d) for _, d in results]
    counts = {k: verdicts.count(k) for k in ("NULL", "close", "DIFF")}
    filters = [r[n] for n in ("off_flt0_128_0", "off_flt1_128_0", "off_flt2_96_160", "off_flt5_128_0") if n in r]
    lines = [
        "# Null tests",
        "",
        "The same notes through FL Studio's Sampler and through Chan Sampler, subtracted. The residual is in dB re FL's",
        "output; lower is better. How the renders are made (in-FL pairs, offline cases, the render queue, FL's 16-bit export,",
        "velocity, tempo and rates) and the scale (NULL, close, diff) are in [MEASUREMENTS.md](MEASUREMENTS.md#setup). The",
        "in-FL null tests of each feature are given beside its law there; the time-stretch modes cannot null (FL's engine is",
        "proprietary) and are compared by behaviour.",
        "",
        "## Offline suite",
        "",
        "At velocity 100 FL's 16-bit floor sits about 6 dB higher than at velocity 128. The EQ and ring-mod cases, three pogo",
        "cases, the precomputed cut at 512 and the SVF LPx2 filter case, which reach −60 to −66 dB at velocity 128, read",
        "\"close\" at velocity 100 because of the export alone. The small real differences in the offline suite are the",
        f"precomputed cut at its lowest cutoff ({_db(r['offL_pccut256_0'])} dB), the envelope with tensions ({_db(r['off_env_tension'])}) "
        "and the Fast LP, LP, BP and LPx2",
        f"filter cases (−{int(-max(filters))} to −{int(-min(filters))}). The in-FL tests in [MEASUREMENTS.md](MEASUREMENTS.md) "
        "are the reference.",
        "",
        f"Offline suite: **{len(results)} cases: {counts['NULL']} NULL, {counts['close']} close, {counts['DIFF']} diff** "
        "([full table](#offline-cases)).",
        "",
        "## Offline cases",
        "",
        "Offline cases (`python nulltest.py` in `nulltest/`; this page: `python nulltest.py --report`), FL velocity 100.",
        "",
        "| Case | Residual (dB) | Result |",
        "|---|---|---|",
    ]
    lines += [f"| {n} | {d:.1f} | {F.verdict(d)} |" for n, d in results]
    return "\n".join(lines) + "\n"


def main(argv=None):
    import argparse
    import cases_offline
    ap = argparse.ArgumentParser(description="The offline null tests (FL's Sampler alone against CsRender) and NULLTESTS.md.")
    ap.add_argument("--only", nargs="*", default=None, help="cases whose names contain one of these")
    ap.add_argument("--dry", action="store_true", help="the render plan only: what FL would render, and why (FL is not started)")
    ap.add_argument("--report", action="store_true", help="write NULLTESTS.md from the saved results (nothing is rendered)")
    ap.add_argument("--results", default=RESULTS, help="the results file the report reads (default ours/offline_results.json)")
    a = ap.parse_args(argv)
    if a.report:
        text = report(load_results(a.results))
        with open(REPORT, "w", encoding="utf-8", newline="\n") as h:
            h.write(text)
        print(f"wrote {os.path.relpath(REPORT, paths_lib.REPO)} from {a.results}")
        return 0
    if a.dry:
        import queue_lib
        queue_lib.queue_for(cases_offline.CASES, "fl", a.only).run(dry=True)
        return 0
    results = run(cases_offline.CASES, a.only)
    if not a.only:
        save_results(results)
    return 0


if __name__ == "__main__":
    sys.exit(main())
