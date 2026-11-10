# Chan Sampler: research

What the original is and does, where the knowledge comes from, and what Chan Sampler does not match. Chan Sampler is
an open-source recreation of the Sampler of FL Studio (Image-Line's DAW): the channel that plays one sample per note
with FL's precomputed effects, time stretching, envelopes, LFOs, filter, arpeggiator and echo. The reference is FL
Studio 26.1.7. How each behaviour was measured and the laws that resulted are in [MEASUREMENTS.md](MEASUREMENTS.md);
how close it comes is in [NULLTESTS.md](NULLTESTS.md).

Nothing of FL's (renders, samples, presets, projects, art) is in this repository: FL's renders and files stay in the
git-ignored folders under `nulltest/`. Behaviour is found by measuring FL from the outside.

## Sources

| Id | Source |
|---|---|
| **M** | Image-Line's online manual (`chansettings_sampler`, `_ins` and `_misc`, screenshots for colours) |
| **PYFLP** | the open-source PyFLP project's description of FL's project format and raw parameter ranges |
| **NULL** | runtime measurements and null tests in `nulltest/` |

## UI layout (FL's window)

Controls sit at the positions and sizes of FL's controls on the Sampler, Instrument and Functions sheets, each 561×399
under a 45 px top panel. The canvas is scaled to the window. Option lists use FL's own wording, e.g. the declicking
modes and the filter types. Panels for FL-only features are left out, and the arpeggiator / echo panels are rearranged
to fit.

## Group (Cut / By) between instances

FL's channel Group is a host feature; Chan Sampler rebuilds it across its own instances (`Source/CutGroups.*`): every
note start of an instance with Cut = n is posted on a process-wide lock-free bus with its host timeline position, and
instances with By = n stop their voices there (release, or the declick fade without a volume envelope, as Cut self). An
instance never cuts itself through its group (Cut self does that). Sample-exact when the cut instance is processed after
the cutting one, otherwise at the start of its next block (`CsRender cuttest dc6.wav`: 0 and 144 samples late
at 512-sample blocks). With the transport stopped the cut happens at the next block. Bridged / sandboxed instances in
other processes cannot see each other. FL's Time panel (gate / shift / swing / full porta) edits FL's note events and is
not reproduced. FL has no Panning envelope (LFO only), so neither does Chan Sampler.

## Known gaps (diff / approximate)

- **Time-stretch modes:** our own algorithms (pitch-synchronous grains, phase vocoder, e3's band split; see [Time
  stretching](MEASUREMENTS.md#time-stretching-against-fls-renders)). FL's elastique is proprietary, so they match FL
  in duration, pitch, attack timing and character, not sample for sample. Resample with TIME nulls.
- **LPx2:** −47…−63 dB, SVF LPx2 bit-exact (nulltest/cases/filter_x2_cases.py): SVF LPx2 = two Chamberlin stages with
  q1 = 1 − 0.8 res, q2 = 1 − 0.6 res; LPx2 = two identical LP biquads (no Q floor) times a level compensation linear
  in the stage's Q times its frequency, 1 − Q(a − ω)b (a and b fitted to the 8 × 8 compensation measured on noise,
  within 0.001 everywhere; worst in-FL case −47 dB). A note starts the compensation at unity and moves it tick by
  tick, linearly within each tick, by at most the envelopes' slew per tick (full range per 8.83 ms): at maximum
  resonance the first tick only gets from 1 to 0.455 (target 0.39) and the second finishes it (read from FL's output
  over our uncompensated filter, Mod X 96 / Mod Y 256): those cases −59.5 / −59.3 dB in FL.
- **SMP trim, segments under 200 frames:** −59…−69 dB (lengths match FL's; the end filter is a model fitted to FL's
  click matrices at seven lengths; LENGTH 0 and LENGTH 0 with a start are at FL's 16-bit export floor, a few one-step
  differences on a quiet segment). Not matched: a START tie (exactly 0.5) landing where a sound starts out of digital
  silence (loud.wav, −52.5 dB: the start filter comes out a frame early).
- **LFO:** see the LFO row in [MEASUREMENTS.md](MEASUREMENTS.md#measured-laws). FL's pan envelope had no audible
  effect in CLI renders (amount ±128, sustain 128), so Chan Sampler has a panning LFO and no panning envelope.
- **Sample view:** the spectrum is a model of FL's: 0.4 to 1.6 dB per pixel on the four synthetic captures; on six
  real samples (FL captures of the spectrum view, the file reloaded in a scratch project) 1.4 to 3.4 dB per 4 ×
  8-pixel block, 2.37 on average, and 4 to 6 dB per pixel, mostly the speckle of noisy spectra; colourful waveforms
  match within 0.4 to 2.5 RGB steps on solid fills (a stereo loop about 4), Color map's column-to-column flicker
  matches in character only.
- **Not implemented** (FL host features or FL-only storage): Keep on disk and Resample (how FL holds the file), gate /
  shift / swing (FL's Time panel), FL's channel VOL / PAN / PITCH / RANGE / TRACK (host-side), Edit in audio editor /
  pitch corrector / time warper and Open in new channel. Max polyphony is shown but, as in FL's renders, does nothing.
