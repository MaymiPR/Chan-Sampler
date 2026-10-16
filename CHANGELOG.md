# Changelog

## 1.1.0 (in progress)

- CLAP plugin (Windows, macOS, Linux), in the installers.
- Linux builds: VST3, CLAP and the standalone app (x86-64, glibc 2.35 or
  later).
- The root note keyboard is FL's: all 128 notes behind a scroll bar (arrows,
  thumb, mouse wheel), FL's key colours and size, opening on FL's view.
- Knobs and sliders feel like FL's: FL's drag distance for each control,
  the hold at a two-sided control's centre (and PITCH's semitones), and a
  hidden cursor that drags past the screen's edges.
- The sample view draws the sample as PITCH, MUL and TIME play it, as FL does;
  PITCH switches Resample to Auto when let go, and Auto reads e3 generic.
- Stretch modes keep attacks sharp when pitch-shifting (the frames over an
  attack are rebuilt 1:1, on time), and pitched notes are no longer early or
  late: the stretcher's output lines up with the source at any pitch (it ran
  up to 23 ms off). The sample view redraws once a knob is let go.
- Releases list SHA-256 checksums; the install notes explain the security
  warnings for the unsigned downloads (SmartScreen, macOS's Open Anyway).

## 1.0.0

First stable release. Saves from 0.1.0 load unchanged: every parameter, the
sample and the window size (checked with `ChanSamplerRender compatstate`).
From now on, parameter IDs and the plugin codes stay fixed so projects keep
loading.

Sound (all null-tested against FL Studio 2026):
- Filter modulation (envelopes, LFOs, Mod X / Y) matches FL, including LPx2 and
  SVF LPx2.
- All six declicking modes match FL, including Crossfade's fades and
  Transient (no bleeding)'s fade-out (a sweeping high-pass, not a volume fade).
- Envelopes match FL at any tempo, not only 130 BPM: they run on the song's
  ticks as FL's do, with FL's own stage times (re-measured at six tempos), and
  positive decay tension has FL's shape.
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
- Factory presets (settings only, keep the loaded sample): Chop, Pluck, Swell,
  Filter sweep, Wobble 1/8, Tremolo, Vibrato, 808 glide, Echo ping-pong 1/8,
  Reverse. A saved preset without a sample also keeps the loaded one.
- The sample in and out of other programs: drag the waveform out of the view
  into a DAW, an audio editor or Explorer / Finder; Copy / Paste through the
  system clipboard (the file and its audio); a sample file changed on disk (say,
  saved from the audio editor it was dragged into) reloads with the settings
  kept.
- Sample view right-click menu in FL's order: Reload, Save as, Locate file,
  Copy, Paste, Detect tempo, Get properties, Spectrum view,
  Multichannel waveform view, Colorful waveforms (Multiband / Color map).
- Detect tempo: FL's Tempo detection dialog (estimate, ranges, type in BPM or
  beats, project tempo, no tempo); TIME > Autodetect... sets TIME to the
  sample's length in beats, so a loop plays at the project's tempo. A WAV's
  ACID tempo is read when loaded.
- Panel menus as in FL: the echo delay's presets (Echo - classic / ping pong,
  Fat - detuned / more detuned / detuned + time offset / 8 voices), and the
  envelope's presets (Save as, then pick from the list).
- Resizable window, sharp on Retina / high-DPI screens.

Look (measured from FL 2026's screen):
- Sample view: FL's colours, grid, smooth waveform, format badge (16 / 32 with
  mono / stereo icon), file markers.
- Spectrum view modelled on FL's (sharp in time and frequency, FL's palette).
- Colourful waveforms measured on FL's own drum, loop and vocal samples.
- FL-styled menus, message dialogs, Properties sheet and file buttons.
- Tracking opens on the Key tab; Content shows only the options that do
  something.

Robustness:
- Offline rendering (export / bounce) never starts on an outdated sample, also
  right after loading a project or changing a precomputed setting.
- Changing the host's sample rate mid-session keeps every sample's pitch.
- Damaged or hostile files can't crash or hang the plugin (1306 corrupted
  files tested); a file that is too long or damaged gets a clear message.
- Long samples use a third of the memory (a 30-minute stereo file: 3.1 GB to
  0.8 GB).
- The spectrum and colourful waveforms look the same at 96 kHz as at 44.1 kHz.

Fixes:
- Loading a sample with loop points sets the loop markers but no longer
  switches Use loop points on (FL leaves the switch as it was).
- Trackpad zoom in the sample view no longer triggers by accident when clicking
  (macOS).
- macOS font widths match the Windows layout.

Builds: Windows installer and macOS .pkg (plus zips) with VST3, AU (macOS) and
the standalone app, validated with pluginval (strictness 10) and `auval`.
License: AGPLv3.

## 0.1.0

First public build.
