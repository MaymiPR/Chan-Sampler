## What and why

<!-- What changes, and what in the original it follows (or what is ours, marked "(ours)"). -->

## How it was checked

<!-- Tests (ctest), the render regression (scripts/render_regress.py), null tests against the original
     (nulltest/), or what was measured. A sound change needs the original's render of the same settings and the
     before and after residual in dB. -->

- [ ] `ctest` passes and the render regression is green (CI runs both)
- [ ] A user-facing change has a line in `CHANGELOG.md` under `## Unreleased`
- [ ] A sound change has its measurement in the plug-in's MEASUREMENTS.md and its numbers in NULLTESTS.md
