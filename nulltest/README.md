# Null tests against FL Studio

How Chan Sampler is checked against FL Studio's Sampler: the same notes go
through both, one is subtracted from the other, and the residual is reported
(below −60 dB counts as a match; FL's 16-bit export floor is about −65 to
−95 dB). The results are written up in [docs/EVIDENCE.md](../docs/EVIDENCE.md).

Requirements: FL Studio 2026 (closed while rendering: a running FL swallows the
command-line render), Python 3 with `numpy`, and Release builds of the plugin
and of `ChanSamplerRender`.

## In-FL tests (the reference)

FL's Sampler on channel 1 and the Chan Sampler VST3 on channel 2 of one
project, rendered once by FL (`FL64.exe /R /Ewav`); the two halves are
subtracted as heard in FL, with no gain correction.

```
python -c "import nullfl, fl_cases; nullfl.run(fl_cases.CASES)"
python -c "import nullfl, fl_cases; nullfl.run(fl_cases.CASES, ['d_cut'])"   # cases whose name contains d_cut
python untested_cases.py -f                                                  # files with a main; -f re-renders
```

Each file below defines `CASES`; FL's renders are cached in `renders_fl/`.

| File | Covers |
|---|---|
| `fl_cases.py` | precomputed effects, interpolation, envelopes, declicking |
| `fx_sweep.py` | the precomputed effects over their whole range |
| `trim_sweep.py`, `trim_fit_cases.py` | SMP START / LENGTH, trim |
| `lfo_cases.py` | LFOs and filter modulation |
| `filter_x2_cases.py` | LPx2 and SVF LPx2 |
| `echo_cases.py`, `arp_cases.py` | echo delay, arpeggiator |
| `untested_cases.py` | loops, echo Mod X / Y and fat mode, tracking, glide, polyphony, root note, FLAC / Ogg / MP3 |
| `fst_cases.py` | FL Sampler preset (`.fst`) import |
| `stretch_time_cases.py` | TIME with Resample |

Notes: cases with more than one note use a 26-beat gap between the halves (FL's
tick is 11025 / 52 samples at 130 BPM, so only then do both sit on the same
sub-sample positions); samples longer than the gap need the volume envelope,
or FL's half spills into ours.

## Offline tests (fast)

`nulltest.py` with `cases.py` renders FL's half from the command line and
Chan Sampler's with `ChanSamplerRender`; `offline_vs_fl.py` compares an offline
render with FL's half of an in-FL case, for model tuning without new FL
renders. The offline renderer is 1.25× the in-FL level and maps FL velocity v
to MIDI round(v·127/128), so trust the in-FL tests for final numbers.

## Building blocks

- `flp.py` reads and writes FL project events; `build_project.py`,
  `flrender.py` and `vstchannel.py` build test projects (FL's Sampler patched
  per case, Chan Sampler's state from the case's parameters).
- `wavio.py` reads and writes WAV files.
- The `.wav` files are test signals (tones, clicks, noise, decays; `cue_tone`,
  `strc_*` carry region / slice markers, `smpl_loop` a loop).
- The other scripts are the measurements behind specific models cited in
  docs/EVIDENCE.md (interpolation kernel, reverb, trim tail, stretch timing).
