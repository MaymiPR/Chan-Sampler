# Render regression

`scripts/render_regress.py` renders the cases in `<PluginTarget>/jobs.txt` through that plug-in's own offline
renderer and compares each output with the golden render in `<PluginTarget>/golden/<name>.f32`. CI runs it on
every push (ci.yml, all three systems) and the local check runs it when goldens exist; a case whose residual rises
above its threshold fails the build.

- The inputs are synthesized by the script with a fixed seed, so only the goldens are committed (0.5 s of stereo
  float32 is 192 KB; keep cases short).
- After an intended DSP change, run `python scripts/render_regress.py --build build --update` and commit the new
  goldens in the same commit as the change, with the reason in the message.
- This is not the null test. It says that our output did not change, not that it matches the original; the null
  tests in each plug-in's `nulltest/` say that, on the machine that has the original.
