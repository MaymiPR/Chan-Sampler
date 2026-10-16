# Changelog

## 1.0.0

First stable release. Saves from 0.1.0 load unchanged: every parameter, the
sample and the window size (checked with `ChanSamplerRender compatstate`).
From now on, parameter IDs and the plugin codes stay fixed so projects keep
loading.

Sound (all null-tested against FL Studio 2026):
- Filter modulation (envelopes, LFOs, Mod X / Y) matches FL, including LPx2 and
  SVF LPx2.
- Loops, echo Mod X / Y and fat mode, velocity / key tracking, mono / porta
  glide match FL. Max polyphony is ignored, as in FL's renders.
- FLAC, Ogg and MP3 samples match FL's decoding.
- Stretch Pro and the other phase-vocoder modes use half the CPU (Stretch Pro
  at 64 voices: 118% to 54% of a core) and stay steadier on long notes.

New:
- FL Studio Sampler presets (`.fst`): drop or import to get the sample and every
  setting.
- Content panel: Load regions / Load slice markers from WAV files (cue / ACID).
- Preset menu: save, load and browse `.cspreset` files, import `.fst`.
- Sample view right-click menu: Reload, Save as, Locate file, Get properties,
  Spectrum view, Multichannel waveform view, Colorful waveforms (Multiband /
  Color map).
- Resizable window, sharp on Retina / high-DPI screens.

Look (measured from FL 2026's screen):
- Sample view: FL's colours, grid, smooth waveform, format badge (16 / 32 with
  mono / stereo icon), file markers.
- Spectrum view modelled on FL's (sharp in time and frequency, FL's palette).
- Colourful waveforms measured on FL's own drum, loop and vocal samples.
- FL-styled menus, message dialogs, Properties sheet and file buttons.
- Tracking opens on the Key tab; Content shows only the options that do
  something.

Fixes:
- Trackpad zoom in the sample view no longer triggers by accident when clicking
  (macOS).
- macOS font widths match the Windows layout.

Builds: Windows VST3 + Standalone, macOS universal AU + VST3 + Standalone,
validated with pluginval (strictness 10) and `auval`. License: AGPLv3.

## 0.1.0

First public build.
