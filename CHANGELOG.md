# Changelog

## 1.2.1

New
- Click CHAN SAMPLER in the top bar for a menu in FL's style: the version,
  About Chan Sampler (version, plugin format, license, credits), the project
  on GitHub, Report an Issue and the license.

Fixes
- With more than one Chan Sampler in a project, the loop start / end values
  the DAW shows (automation, generic editor) could be counted against
  another instance's sample; each now uses its own.

Downloads
- Each release has the Windows installer, the macOS installer and the Linux
  tarball (now with the version in its name); the Windows and macOS zips are
  gone. Every download includes the licenses of the third-party software in
  Chan Sampler (THIRD-PARTY-NOTICES.txt).

Other
- Every build is now checked to open a project saved by 1.2.0 with every
  setting unchanged; each release from here on adds its own.
- No compiler warnings on Linux (deprecated JUCE calls replaced, unused code
  removed).
- For contributors: changes are built and tested automatically, a
  contributor guide (CONTRIBUTING.md), the null-test tools in folders with a
  generator for the stretch-mode test signals, LF line endings.

## 1.2.0

Closer to FL's Sampler
- Every stretch mode was measured against FL's renders (clicks, noise, a
  kick, a Rhodes, a voice and two drum loops at -700 / +700 cents and x1.5 /
  x0.667) and rebuilt to match:
  - e3 mono and e2 mono splice the sound one pitch period at a time, as
    FL's do, and keep a voice's formants when PITCH moves.
  - e2 speech uses the same engine, with the pitch resampled like FL's.
  - Stretch, e2 generic and e2 transient handle attacks like e3; e2 generic
    plays its highs as pieces of the sound, as FL's does.
  - Every one of these modes is closer to FL's renders, by up to half (e3
    mono's spectrum 6.4 -> 3.8 dB from FL's, e2 transient's envelope 5.0 ->
    2.5 dB).
- e3 generic / Auto with PITCH: attacks are timed on FL's grid (all 128
  measured noise bursts land where FL puts them), counted from where an
  attack is under way, so a ramping drum hit or a swelling voice is timed
  like FL's. When stretching, an attack's low end is as much louder as FL
  plays it at that point.
- A note that starts softly lines up like FL's in Stretch, Stretch Pro, e2
  generic and e2 transient (each of FL's modes has its own latency there).
- Every knob's drag distance is now measured in FL.

Faster
- The stretch modes use about half the CPU, with the same sound: e3 generic
  / Auto about 16% of one core for 32 voices (1.1.0: 37%), Stretch Pro 25%
  (36%), the mono and speech modes about 5%. The stretcher has its own,
  faster FFT, and e3's spectral part only works on the band it keeps.

Fixes
- e3 mono and e2 mono: clicks could land up to ~190 samples off, or drop
  out when squeezing; every click now lands on time at full level.

Other
- Regression tests for the stretch modes: e3's attack timing, band split
  and pitched notes' start and length, every mode's alignment of a soft
  start, the mono modes' clicks and formants, the FFT, and a stress test of
  every mode at extreme, changing ratios.

## 1.1.0

New formats and platforms
- CLAP plugin (Windows, macOS, Linux), included in the installers.
- Linux builds: VST3, CLAP and the standalone app (x86-64, glibc 2.35 or
  later).

Closer to FL's Sampler
- e3 generic and Auto are built like FL's e3 (measured from FL renders, see
  nulltest/E3_NOTES.md): a spectral stretch below 1.485 kHz and
  pitch-synchronous grains of the sound above it. Attacks are timed like
  FL's (the first sample near an attack's peak lands on time) and held as
  long as FL holds them. Against FL's renders of a kick, a Rhodes, a voice
  and two drum loops at -1200 ... +1200 cents, nulls improved from -2.0 to
  -2.9 dB, envelopes are 18% closer and spectra 18% closer. A true null is
  not possible: FL's engine (elastique) is proprietary.
- Pitched drums keep their length: a kick an octave down no longer rings
  twice as long, and attacks are no longer smeared or doubled.
- The root note keyboard is FL's: all 128 notes behind a scroll bar (arrows,
  thumb, mouse wheel), FL's key colours and size, opening on FL's view.
- Knobs and sliders feel like FL's: FL's drag distance for each control, the
  hold at a two-sided control's centre (and PITCH's semitones), and a hidden
  cursor that drags past the screen's edges.
- The sample view draws the sample as PITCH, MUL and TIME play it, redrawn
  once a knob is let go; PITCH switches Resample to Auto when let go, and
  Auto reads e3 generic.
- Multiband waveform colours show a drum's click in its own colour, as FL
  does (707 Kick starts blue).

Fixes
- Stretched notes were up to 23 ms early or late depending on the pitch;
  steady sound in the spectral modes (e2, Stretch Pro) ran a few ms early.
- Pitched-down notes in the spectral modes could start late or fade in.

Other
- e3 generic uses more CPU than before (about 37% of one core for 32
  voices, was 14%).
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
