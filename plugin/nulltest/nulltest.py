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
    python nulltest.py --vst3       # the built VST3 (through RefRender) against the offline renderer: FL is not used
    python nulltest.py --vst3 --fs 96000        # the same at that rate (the check script's second pass)

--vst3 proves that the shipped binary is the renderer the numbers were measured with: a set of the offline cases
plays through the built Chan Sampler VST3 in the RefRender host and through CsRender, at the same rate, block and
tempo, and the two must agree below VST3_LIMIT. Exit code 0 when they do, 1 when a case differs, 2 when the VST3,
CsRender or RefRender is not built or no case matches. It writes no results file.
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


# ----------------------------------------------------------------------------- the built VST3 against the renderer

# One case per precomputed stage and per live path that the offline suite covers; each is a name of cases_offline.py
VST3_CASES = ("off_base", "off_boost64", "off_eq192", "off_fin256", "off_len50", "off_rev", "off_trim64",
              "off_rm128_64", "offL_pccut640_320", "off_env_tension", "off_flt1_128_0")
VST3_LIMIT = -100.0     # dB re the renderer's output: the same code in two binaries, so only rounding may differ
VST3_WORK = os.path.join(OURS, "vst3")
VST3_NAME = "Chan Sampler"
VST3_SETTLE = 500       # ms the host waits after the state: the plug-in rebuilds its sample on a background thread


def _built(pattern):
    import glob
    hits = glob.glob(os.path.join(paths_lib.REPO, "build", *pattern))
    return os.path.normpath(hits[0]) if hits else None


def _xml_block(xml):
    """JUCE's binary form of an XML text (AudioProcessor::copyXmlToBinary): a tag, the length, the text, a zero."""
    import struct
    data = xml.encode("utf-8") + b"\0"
    return b"VC2!" + struct.pack("<I", len(data)) + data


def host_state(sample, params):
    """The state file RefRender loads (state=): what a JUCE host saves for a VST3, holding our plug-in's own state
    (the sample's path and the parameters) as the component's part. The host stores that part as text, in JUCE's
    encoding of a memory block: the size, a dot, then six bits per character, lowest bits first."""
    import channel_lib
    own = _xml_block(channel_lib.state_xml(sample, params))
    table = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
    bits = int.from_bytes(own, "little")
    text = f"{len(own)}." + "".join(table[(bits >> (6 * i)) & 63] for i in range((len(own) * 8 + 5) // 6))
    return _xml_block(f'<?xml version="1.0" encoding="UTF-8"?> <VST3PluginState><IComponent>{text}</IComponent></VST3PluginState>')


def vst3_check(only=None, fs=44100, block=512):
    """The exit code of --vst3: every chosen case through the built VST3 and through CsRender, compared."""
    import cases_offline, refhost_lib
    from nulltest_lib import read_f32, write_f32
    exe = ".exe" if sys.platform == "win32" else ""
    host = _built(("RefRender_artefacts", "Release", "RefRender" + exe))
    bundle = _built(("*_artefacts", "Release", "VST3", "*.vst3"))
    for what, path in (("RefRender", host), ("the Chan Sampler VST3", bundle),
                       ("CsRender", RENDER if os.path.isfile(RENDER) else None)):
        if not path:
            print(f"{what} is not built: nothing compared")
            return 2
    by_name = {c["name"]: c for c in cases_offline.CASES}
    cases = [by_name[n] for n in VST3_CASES if n in by_name and (not only or any(o in n for o in only))]
    if not cases:
        print("no case matches: nothing compared")
        return 2
    os.makedirs(VST3_WORK, exist_ok=True)
    b2s = 60.0 / TEMPO
    jobs, lines, outs, temp = [], [], [], []
    try:
        for c in cases:
            notes = c.get("notes") or [(0, c.get("beats", 8), 60, c.get("vel", 100))]
            notes = [(k, st * b2s, ln * b2s, v) for st, ln, k, v in notes]
            n = int(round((max(s + d for _, s, d, _ in notes) + 1.5) * fs))
            x = np.zeros((2, n), np.float32)
            sample = paths_lib.signal(c["sample"])
            params = c.get("ours") or {}
            # the VST3 takes the sample and the parameters as its own state, as a host hands it over
            state = os.path.join(VST3_WORK, c["name"] + ".state"); temp.append(state)
            with open(state, "wb") as h:
                h.write(host_state(sample, params))
            jobs.append(dict(x=x, state=state, notes=notes, settle=VST3_SETTLE))
            # the renderer's job line: no quoting there, so every path is relative to nulltest/
            fin = os.path.join(VST3_WORK, c["name"] + "_in.f32"); fout = os.path.join(VST3_WORK, c["name"] + "_out.f32")
            temp += [fin, fout]
            write_f32(fin, x)
            rel = lambda q: os.path.relpath(q, HERE).replace(os.sep, "/")
            spec = ";".join(f"{k}:{s!r}:{d!r}:{v}" for k, s, d, v in notes)
            lines.append(" ".join([rel(fin), rel(fout), "sample=" + rel(sample), "notes=" + spec, f"bpm={TEMPO}"]
                                  + [f"{k}={v}" for k, v in params.items()]))
            outs.append(fout)
        jobfile = os.path.join(VST3_WORK, "jobs.txt"); temp.append(jobfile)
        with open(jobfile, "w") as h:
            h.write("\n".join(lines) + "\n")
        r = subprocess.run([RENDER, rel(jobfile), f"fs={fs}", f"block={block}"], cwd=HERE, capture_output=True, text=True)
        if r.returncode != 0:
            print("CsRender failed:\n" + r.stdout + r.stderr)
            return 2
        model = [read_f32(f) for f in outs]
        built = refhost_lib.render_jobs(jobs, lambda jf: [host, bundle, VST3_NAME, jf, f"fs={fs}", f"block={block}",
                                                          f"bpm={TEMPO}"], VST3_WORK)
    finally:
        for f in temp:
            if os.path.exists(f): os.remove(f)
    worst = -999.0
    for c, a, b in zip(cases, model, built):
        a, b = a.astype(np.float64), b.astype(np.float64)
        e = (a ** 2).sum()
        d = 10 * np.log10(max(((a - b) ** 2).sum(), 1e-30) / max(e, 1e-30))
        silent = e < 1e-12
        worst = max(worst, 0.0 if silent else d)
        print(f"{'FAIL ' if silent or d > VST3_LIMIT else 'same '} {d:7.1f} dB  {c['name']}  ({fs} Hz)"
              + ("  the renderer's output is silent" if silent else ""), flush=True)
    print(f"{len(cases)} cases at {fs} Hz, the VST3 against the renderer: worst {worst:.1f} dB (limit {VST3_LIMIT:g})")
    return 0 if worst <= VST3_LIMIT else 1


def main(argv=None):
    import argparse
    import cases_offline
    ap = argparse.ArgumentParser(description="The offline null tests (FL's Sampler alone against CsRender) and NULLTESTS.md.")
    ap.add_argument("--only", nargs="*", default=None, help="cases whose names contain one of these")
    ap.add_argument("--dry", action="store_true", help="the render plan only: what FL would render, and why (FL is not started)")
    ap.add_argument("--report", action="store_true", help="write NULLTESTS.md from the saved results (nothing is rendered)")
    ap.add_argument("--vst3", action="store_true", help="the built VST3 through RefRender against CsRender (FL is not used)")
    ap.add_argument("--fs", type=int, default=None, help="--vst3: the rate of the comparison (default 44100)")
    ap.add_argument("--results", default=RESULTS, help="the results file the report reads (default ours/offline_results.json)")
    a = ap.parse_args(argv)
    if a.fs is not None and not a.vst3:
        ap.error("--fs belongs to --vst3")
    if a.vst3:
        return vst3_check(a.only, a.fs or 44100)
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
