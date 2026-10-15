# Null tests against FL Studio

Renders the same note through FL Studio's Sampler and through Chan Sampler,
flips the polarity of one, sums them, and reports the residual.

Requirements: FL Studio 2026 installed, Python 3 with `numpy`, and a Release
build of `ChanSamplerRender` (`cmake --build build --config Release --target ChanSamplerRender`).

```
python -c "import nulltest, cases; nulltest.run(cases.CASES)"
```

Run a subset by name: `nulltest.run(cases.CASES, only=['boost', 'eq'])`.

- `build_project.py` builds a one-note project from FL's Basic template.
- `flrender.py` patches channel 1's settings and calls `FL64.exe /R /Ewav`. FL renders are cached in `renders/`; delete a file to re-render it.
- `nulltest.py` renders Chan Sampler with `ChanSamplerRender.exe` into `ours/` and compares the two.
- `cases.py` lists every case: the FL patch next to the matching Chan Sampler parameters.
- `flp.py` is a minimal FLP event reader/writer. `wavio.py` reads and writes WAV files.

Residual below −60 dB counts as a match. FL's 16-bit export floor is about −65 to −95 dB.
