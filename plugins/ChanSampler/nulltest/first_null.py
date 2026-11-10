"""The first null test: one in-FL pair (FL Studio's Sampler on channel 1, the built Chan Sampler VST3 on channel 2 of one
project, rendered once by FL from its command line) proves the harness, FL, the build and the render queue before
anything else is measured. Run it from nulltest/ after a build:

    python first_null.py          # renders the pair through the queue (FL Studio closed) and prints its residual
    python first_null.py --dry    # the checks and the queue's plan only: FL is not started

Checks (PASS / FAIL):
  1. the tools: CsRender and the Chan Sampler VST3 are built, FL Studio's executable is found
  2. the test signal exists (python render_signals.py writes it), and the plug-in channel's events that a project
     needs (local/vst_wrapper.bin, saved from an FL project; git-ignored)
  3. (--dry) the queue's plan: whether the pair is cached and why it would render
  4. (not --dry) the pair renders and nulls: below -60 dB, the scale's NULL (FL's 16-bit export floor is about -65
     to -95 dB)
"""
import argparse, os, sys
import channel_lib, cs_ref, paths_lib, queue_lib

CASE = dict(name="first_null", sample="signals/noise.wav", fl=(), ours={}, notes=None, vel=100, beats=8)


def check(name, ok, detail=""):
    print(f'{"PASS" if ok else "FAIL"}  {name}' + (f": {detail}" if detail else ""))
    return ok


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--dry", action="store_true", help="the checks and the render plan only (FL is not started)")
    a = ap.parse_args(argv)
    ok = check("CsRender built", os.path.isfile(paths_lib.RENDER_TOOL), os.path.relpath(paths_lib.RENDER_TOOL, paths_lib.REPO))
    ok &= check("Chan Sampler VST3 built", bool(paths_lib.PLUGIN_VST3) and os.path.isfile(paths_lib.PLUGIN_VST3),
                os.path.relpath(paths_lib.PLUGIN_VST3, paths_lib.REPO) if paths_lib.PLUGIN_VST3 else "none under build/")
    fl = os.path.isfile(paths_lib.FL_EXE)
    if a.dry:
        print(f'{"PASS" if fl else "note"}  FL Studio found' + ("" if fl else ": not on this machine (set FL_DIR); a render needs it"))
    else:
        ok &= check("FL Studio found", fl, paths_lib.FL_EXE if fl else "set FL_DIR")
    sample = paths_lib.signal(CASE["sample"])
    ok &= check("test signal", os.path.isfile(sample), os.path.relpath(sample, paths_lib.ROOT) + ("" if os.path.isfile(sample)
                else " missing: python render_signals.py"))
    ok &= check("the plug-in's channel events", os.path.isfile(channel_lib.WRAPPER),
                os.path.relpath(channel_lib.WRAPPER, paths_lib.ROOT) + ("" if os.path.isfile(channel_lib.WRAPPER)
                else " missing: save them from an FL project holding Chan Sampler (local/, never committed)"))
    if a.dry:
        if not ok:
            return 1
        q = queue_lib.queue_for([CASE], "pair", None, bool(paths_lib.PLUGIN_VST3))
        q.run(dry=True)
        return 0 if ok else 1
    if not ok:
        return 1
    fl_half, ours = cs_ref.render_pair(CASE["name"], CASE["sample"], CASE["fl"], CASE["ours"], vel=CASE["vel"], beats=CASE["beats"])
    d = cs_ref.null_db(fl_half, ours)
    return 0 if check("the pair nulls", d < -60, f"{d:.1f} dB re FL's half (limit -60)") else 1


if __name__ == "__main__":
    sys.exit(main())
