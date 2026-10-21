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
| `declick_cases.py` | the six declicking modes (start offset, sample end, cuts) with a constant, noise and a sine |
| `tempo_env_cases.py` | the volume envelope at 65, 100 and 160 BPM (Tempo off and on), decay tension |

Notes: cases with more than one note use a 26-beat gap between the halves (FL's
tick is 11025 / 52 samples at 130 BPM, so only then do both sit on the same
sub-sample positions; 26 beats also lines up at 65, 100, 120 and 160 BPM, set
per case with `bpm`); samples longer than the gap need the volume envelope,
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

## Measurement scripts

How each model in the plugin was found, kept for re-measuring when FL changes
and for working on the known gaps. They run from this folder (they share the
helpers above and the test signals here); most render through FL, so close it
first. `.npy` / `.json` files are their saved measurements.

| Topic | Scripts and data |
|---|---|
| Interpolation and sample-rate conversion | `kernel_extract.py`, `kernel_table.py`, `window_fit.py`, `interp_fit.py`, `src_extract.py`; `fl_kernel.npy`, `fl_window.npy`; signals `imp*.wav`, `impulse*.wav`, `impw*.wav` |
| SMP START fade and trim tail | `head_measure.py`, `tail_measure.py`, `tail_short.py`, `trim_probe.py`; `head_matrix.npy`, `tail_matrix*.npy`, `jump_*.npy`; signals `noise_q*.wav` |
| Envelopes, tempo switches | `tempo_probe.py`; `env_tick_probe.py` (FL's per-tick envelope: stage ticks, jump rate, at eight tempos), `env_time_probe.py` (the knob's real times from attacks at six tempos; `env_times.txt`); `attack_times.json`, `env_time_table.json` (the earlier 130 BPM table); signals `step*.wav`, `stairs*.wav`, `dc*.wav` |
| LFOs | `lfo_probe.py`, `lfo_speed_probe.py`; `lfo_periods.json`, `lfo_speed.json`, `lfo_pulse_ramp.npy` |
| Arpeggiator timing | `arp_probe.py`, `arp_time.py`, `arp_period.py`; `arp_ticks_dense.txt` |
| Filters | `filt.py` (biquads), `lf.py` (one-pole), `cascade.py`, `analysis.py` (transfer functions); `res_r.npy`, `svf_map.json` |
| Precomputed reverb | `rvb/` (impulse response decomposed into pulses, combs and allpasses) |
| Stretch modes | `stretch_check.py` (offline sanity check, no FL), `stretch_vs_fl.py` (spectral distance to FL) |
| Robustness | `robust_check.py` (host rates, mono / 16 / 24-bit / 48 kHz and very long files); `loud*.wav` |
| FL project format | `scanflp.py`, `scanev.py`, `flpdiff.py` (find where FL stores a setting by diffing saved projects); `t_base.flp`, `t_base.wav` |
| Sample view and formats | `freqsteps.wav`, `freqsweep.wav`, `view_tom.wav` (spectrum and colourful-waveform captures), `bits16_1ch.wav`, `bits24_2ch.wav` (format badge), `cue_tone.wav`, `strc_*.wav` (region / slice markers), `smpl_loop.wav` (embedded loop) |
| Results | `results.json`, `results_table.md` (offline null results) |

The other `.wav` files are the test signals of the null-test cases (tones,
clicks, noise, decays).
