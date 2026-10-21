# Null tests against FL Studio

How Chan Sampler is checked against FL Studio's Sampler: the same notes go
through both, one is subtracted from the other, and the residual is reported
(below −60 dB counts as a match; FL's 16-bit export floor is about −65 to
−95 dB). The results are written up in [docs/EVIDENCE.md](../docs/EVIDENCE.md).

Requirements: FL Studio 2026 (closed while rendering: a running FL swallows the
command-line render), Python 3 with `numpy`, and Release builds of the plugin
and of `ChanSamplerRender` (see the main README).

## Layout

| Folder | What's in it |
|---|---|
| `lib/` | Shared code: FL project files (`flp`, `build_project`, `vstchannel`), FL renders (`flrender`: FL's Sampler alone; `nullfl`: FL's Sampler and Chan Sampler in one project), offline renders (`nulltest`), WAV files (`wavio`), paths (`paths`) |
| `cases/` | The null-test case lists: each module defines `CASES` |
| `probes/` | Measurement scripts: how each model in the plugin was found |
| `stretch/` | The stretch modes against FL (they can't null: FL's engine is proprietary), and the generator of their test signals |
| `signals/` | Test signals (inputs) |
| `data/` | Saved measurements (`.npy`, `.json`, `.txt`) and FL project data |
| `E3_NOTES.md` | What was measured about FL's e3 / elastique, round by round |

Everything runs from this folder as modules, for example
`python -m cases.untested_cases`. FL's renders are cached in `renders/` (FL's
Sampler alone) and `renders_fl/` (in-FL pairs), ours in `ours/`; none of them
are in the repository (`-f` re-renders where a script offers it).

## In-FL tests (the reference)

FL's Sampler on channel 1 and the Chan Sampler VST3 on channel 2 of one
project, rendered once by FL (`FL64.exe /R /Ewav`); the two halves are
subtracted as heard in FL, with no gain correction.

```
python -c "from lib import nullfl; from cases import fl_cases; nullfl.run(fl_cases.CASES)"
python -c "from lib import nullfl; from cases import fl_cases; nullfl.run(fl_cases.CASES, ['d_cut'])"   # names containing d_cut
python -m cases.untested_cases -f                                                                      # case files with a main; -f re-renders
```

| `cases/` | Covers |
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
| `e3_pitch_cases.py` | e3 generic at −1200 … +1200 cents (see `stretch/e3_in_fl.py`) |
| `cases.py` | the offline cases (below) |

Notes: cases with more than one note use a 26-beat gap between the halves (FL's
tick is 11025 / 52 samples at 130 BPM, so only then do both sit on the same
sub-sample positions; 26 beats also lines up at 65, 100, 120 and 160 BPM, set
per case with `bpm`); samples longer than the gap need the volume envelope,
or FL's half spills into ours.

## Offline tests (fast)

`lib/nulltest.py` with `cases/cases.py` renders FL's half from the command line
(FL's Sampler alone) and Chan Sampler's with `ChanSamplerRender`;
`probes/offline_vs_fl.py` compares an offline render with FL's half of an
in-FL case, for model tuning without new FL renders. The cases play at FL's
unity velocity, 100: FL passes a note's velocity to a plugin unchanged as the
MIDI velocity, so FL's 128 can't be reproduced outside FL. The in-FL tests
remain the reference for final numbers.

## Stretch modes

The stretch modes can't null against FL (its engine, elastique, is
proprietary), so these compare behaviour: where attacks land, how the output
is built, and envelope / band-spectrum distance. Make the test signals first:
`python -m stretch.make_signals` (they're generated, bit-identical to the ones
FL rendered).

| `stretch/` | Measures |
|---|---|
| `e3_in_fl.py` | e3 generic in FL: 30 pitch cases (707 Kick, a Rhodes, a voice, two drum loops at −1200 … +1200 cents) |
| `real_material.py` | every mode on the same material at ±700 cents and MUL ×1.5 / ×0.667 (our stretcher alone, fed FL's decode of each sample) |
| `mode_probes.py` | every mode on clicks, a saw and noise: click timing and gain, band levels, whether the output is pieces of the source |
| `e3_attacks.py` | e3 generic's attack timing on bursts stepped through FL's 512-sample grid (PITCH and MUL), shaped bursts, and the low band's gain while an attack is held |
| `soft_start.py` | where each mode puts a note that starts softly (no attack) |
| `e3_parity.py` | single-sample clicks at exactly ±1200 cents |
| `metrics.py`, `common.py` | the distances, the mode table, our stretcher run on its own |

FL's own samples are read from the FL install, never copied here.

## Measurement scripts

How each model in the plugin was found, kept for re-measuring when FL changes
and for working on the known gaps. Most render through FL, so close it first.

| Topic | `probes/` and data |
|---|---|
| Interpolation and sample-rate conversion | `kernel_extract.py`, `kernel_table.py`, `window_fit.py`, `interp_fit.py`, `src_extract.py`; `data/fl_kernel.npy`, `data/fl_window.npy`; signals `impulse1000.wav`, `impulses50.wav`, `imps22.wav`, `imps48.wav`, `imps96.wav` |
| SMP START fade and trim tail | `head_measure.py`, `tail_measure.py`, `tail_short.py`, `trim_probe.py` (they write their own signals); `data/head_matrix.npy`, `data/tail_matrix*.npy`, `data/jump_*.npy` |
| Envelopes, tempo switches | `tempo_probe.py`; `env_tick_probe.py` (FL's per-tick envelope: stage ticks, jump rate, at eight tempos), `env_time_probe.py` (the knob's real times from attacks at six tempos; `data/env_times.txt`), `env_hold_probe.py`; `data/attack_times.json`, `data/env_time_table.json` (the earlier 130 BPM table); signals `dc6.wav`, `dc20.wav`, `dc70.wav` (written by `env_time_probe.py`) |
| LFOs | `lfo_probe.py`, `lfo_speed_probe.py`; `data/lfo_periods.json`, `data/lfo_speed.json`, `data/lfo_pulse_ramp.npy` |
| Arpeggiator timing | `arp_probe.py`, `arp_time.py`, `arp_period.py`; `data/arp_ticks_dense.txt` |
| Filters | `filt.py` (biquads), `lf.py` (one-pole), `cascade.py`, `analysis.py` (transfer functions); `data/res_r.npy`, `data/svf_map.json` |
| Precomputed reverb | `rvb/` (impulse response decomposed into pulses, combs and allpasses) |
| Robustness | `robust_check.py` (host rates, mono / 16 / 24-bit / 48 kHz and very long files), `corrupt_files.py` (damaged and unusual files must not crash); both write their own test files |
| FL project format | `scanflp.py`, `scanev.py`, `flpdiff.py` (find where FL stores a setting by diffing saved projects); `data/t_base.flp`, `data/t_base.wav` |
| Sample view and formats | signals `freqsteps.wav`, `freqsweep.wav`, `view_tom.wav` (spectrum and colourful-waveform captures), `bits16_1ch.wav`, `bits24_2ch.wav` (format badge), `cue_tone.wav`, `strc_*.wav` (region / slice markers), `smpl_loop.wav` (embedded loop) |

`data/vst_wrapper.bin` holds the two FL channel events (Fruity Wrapper) that
`lib/vstchannel.py` turns into a Chan Sampler channel.
