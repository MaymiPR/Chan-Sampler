<p align="center">
  <img src="docs/banner.svg" alt="Chan Sampler: FL Studio's Channel Sampler, in any DAW" width="100%">
</p>

<p align="center">
  <a href="https://github.com/MaymiPR/ChanSampler/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/MaymiPR/ChanSampler?color=4df1b1&label=release"></a>
  <a href="https://github.com/MaymiPR/ChanSampler/actions/workflows/release.yml"><img alt="Build" src="https://github.com/MaymiPR/ChanSampler/actions/workflows/release.yml/badge.svg"></a>
  <img alt="Formats" src="https://img.shields.io/badge/formats-VST3%20%7C%20AU%20%7C%20Standalone-2d3133">
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-Windows%20%7C%20macOS-2d3133">
  <a href="LICENSE"><img alt="License: AGPL v3" src="https://img.shields.io/badge/license-AGPL--3.0-ffa830"></a>
</p>

Ever wanted FL Studio's Sampler in Ableton Live, Logic, Reaper or Cubase? The
one with the declicking modes, the precomputed effects, the arpeggiator and the
echo fat mode, that you know by heart?

**Chan Sampler** brings it to every DAW. It is a free, open-source recreation of
the Channel Sampler from FL Studio 2026 as a **VST3**, **AU** and
**Standalone** plugin for Windows and macOS. It isn't a lookalike: every knob
uses FL's units, and the engine was measured against FL itself. Most of it
renders sample-identically to FL (see
[How close is it to FL?](#how-close-is-it-to-fl)). Your FL Sampler presets
(`.fst`) come along: drop one on the window and it loads with its sample.

> Chan Sampler is an independent project. It is not made, endorsed or supported
> by Image-Line, and it contains no Image-Line code, samples or presets.

![Sample page](docs/screenshots/sampler.png)

| Envelopes / LFOs / filter | Misc (levels, arpeggiator, echo) | Spectrum view |
|---|---|---|
| ![Envelopes](docs/screenshots/envelope.png) | ![Misc](docs/screenshots/misc.png) | ![Spectrum](docs/screenshots/spectrum.png) |

## Features

- **Samples:** WAV, AIFF, FLAC, Ogg Vorbis and MP3, including FL's compressed
  WAVs; drag and drop or the folder button. Region and slice markers in WAV
  files (cue / ACID), loop points from the file.
- **FL Studio Sampler presets (`.fst`):** drop one on the window or import it
  from the preset menu to get its sample and every setting.
- **Precomputed effects:** remove DC offset, normalize, reverse, reverse
  polarity, fade / swap stereo, sample start and length, fade in / out,
  crossfade, trim, reverb and stereo delay (rebuilt into the sample, not
  automatable, as in FL).
- **Playback:** start offset, loop points, ping-pong loop, FL's six declicking
  modes.
- **Time stretching:** PITCH / MUL / TIME with Resample, Stretch, Stretch Pro
  (with formant shift), the e3 / e2 modes, Auto, slice stretch and slice map.
  These use our own algorithms (FL's Elastique engine is proprietary).
- **Envelopes** for volume, Mod X / Mod Y and pitch and **LFOs** for those and
  panning, with tempo sync; filters Fast LP, LP, BP, HP, BS, LPx2, SVF LP and SVF LPx2.
- **Misc:** level adjustment, polyphony with mono / porta / slide, velocity and
  key tracking, cut groups between instances, arpeggiator with chords, echo
  delay / fat mode, root note and fine tune.
- **Sample view in FL's style:** zoom and scroll, Start / loop markers, file
  markers, spectrum view, multichannel view and colourful waveforms
  (Multiband or Color map). Right-click for Reload, Save as, Locate file,
  Copy, Paste, Detect tempo and Get properties.
- **Preset menu**, a resizable window (sharp on Retina / high-DPI screens), and
  every knob's right-click menu in FL's units (Reset, Copy value, Paste value,
  Type in value).

## How close is it to FL?

Every feature was compared with FL Studio 2026 by rendering the same notes
through FL's Sampler and through Chan Sampler inside FL and subtracting the
two (a "null test"; `nulltest/`). The precomputed effects, interpolation,
envelopes, LFOs, filters (with modulation), loops, tracking, glide, echo, most
of the arpeggiator and the compressed formats null against FL, and `.fst`
import reproduces FL's settings.
The sample view's colours, grid, spectrum and colourful waveforms were measured
from FL's screen.

Not identical: the time-stretch modes (FL's engine is proprietary, so they
match in behaviour, not sample for sample) and a few edge cases.
[docs/EVIDENCE.md](docs/EVIDENCE.md) lists every measurement and every known
gap.

## Install

Download from the [Releases](https://github.com/MaymiPR/ChanSampler/releases)
page:

- **Windows:** `ChanSampler-<version>-Windows-Setup.exe` installs the VST3 to
  `C:\Program Files\Common Files\VST3\` and the standalone app (Start menu),
  with an uninstaller. Windows SmartScreen may warn because the installer
  isn't code-signed: *More info* → *Run anyway*.
- **macOS** (universal, 10.13 or later): `ChanSampler-<version>-macOS.pkg`
  installs the AU, the VST3 and the app (you can choose which). It isn't
  notarized by Apple, so open it with right-click → *Open* (or System Settings →
  Privacy & Security → *Open Anyway*).

Then rescan plugins in your DAW. The `.zip` files hold the same plugins for
copying by hand (see their `INSTALL.txt`).

## Using it

- Drop an audio file or an `.fst` preset on the window, or click the folder
  button. Click the waveform to hear the sample at its root note.
- In the sample view: drag the Start flag and the loop markers, scroll to zoom,
  right-click for the view options. Drag the waveform out of the view to take
  the sample into your DAW, an audio editor or a folder; when the file is saved
  there, the sample reloads here (settings kept).
- Presets: the bar at the top saves and loads `.cspreset` files in
  `Documents/Chan Sampler/Presets` (folders become submenus); the arrows step
  through them. **Factory** presets are built in and hold settings only, so
  they keep your sample: *Chop* (the sample plays exactly as long as the note),
  *Pluck*, *Swell*, *Filter sweep*, *Wobble 1/8*, *Tremolo*, *Vibrato*,
  *808 glide*, *Echo ping-pong 1/8* and *Reverse*.
- Without the volume envelope, a note plays to the end of the sample, as in FL.

## Build

JUCE is downloaded automatically. Requirements: CMake ≥ 3.22 and Git, plus
Visual Studio 2022 / 2026 with "Desktop development with C++" on Windows, or
Xcode on macOS.

Windows (VST3 + Standalone):

```
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

(`"Visual Studio 17 2022"` with VS 2022.) Engine tests:
`build/ChanSamplerTests_artefacts/Release/ChanSamplerTests.exe`

macOS (universal AU + VST3 + Standalone):

```
cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
cmake --build build --config Release --target ChanSampler_AU ChanSampler_VST3 ChanSampler_Standalone
```

Developer tools (built with everything else):

- `ChanSamplerRender` renders notes offline (for the null tests), and has
  `bench <sample>` (CPU per stretch mode, 32 / 64 voices) and
  `compatstate <state.bin>` (checks that an older version's saved state loads
  unchanged).
- `ChanSamplerSnapshot` writes editor screenshots (used to compare with FL's
  screen and for the pictures above).

## Releases

GitHub Actions builds Windows and macOS (`.github/workflows/release.yml`) and
validates the plugins with pluginval at its strictest level and, on macOS,
Apple's `auval`:

- **Test build:** Actions → *Build and release* → *Run workflow*; the zips
  appear under the run's *Artifacts*.
- **Release:** push a version tag; the zips are attached to a new GitHub
  Release:

  ```
  git tag v1.0.0
  git push origin v1.0.0
  ```

Changes per version: [CHANGELOG.md](CHANGELOG.md).

## Working on another computer

```
git clone https://github.com/MaymiPR/ChanSampler.git
```

Build as above. Commit and push before switching computers, and pull on the
other one. Generated files (`build/`, FL's test renders in
`nulltest/renders*`) are not in the repository: rebuild, and re-render the null
tests with FL Studio when needed.

## License

Chan Sampler is free software under the
[GNU Affero General Public License v3](LICENSE). It is built with
[JUCE](https://juce.com) (used under the AGPLv3) and the VST3 SDK (MIT).

FL Studio is a trademark of Image-Line Software; other names are trademarks of
their owners and are used only to describe compatibility.

## How it was made

Chan Sampler was created entirely with Claude (Anthropic's AI) and
[REA](https://github.com/morluto/rea): every
line of code, the measurements behind it and this documentation. FL Studio
itself was the reference: its behaviour was measured from its renders and its
interface, as described in [docs/EVIDENCE.md](docs/EVIDENCE.md).
