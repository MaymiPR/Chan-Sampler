# Working on Chan Sampler

Bug reports and ideas are welcome as
[issues](https://github.com/MaymiPR/ChanSampler/issues/new/choose). This page is
for changing the code: how it's built and tested, how a change is checked
against FL Studio, and how a release is made.

## The one rule

Chan Sampler follows FL Studio's Sampler, and FL is the reference for every
behaviour. A change that affects the sound is measured against FL's own render
of the same settings (see [Null tests](#null-tests-against-fl)), and what was
measured goes into [docs/EVIDENCE.md](docs/EVIDENCE.md), so the next person can
see why the code is the way it is. Knobs use FL's units and FL's ranges.

## Build

Requirements, and the commands per platform, are in the
[README](README.md#build). Besides the plugin, the build makes the developer
tools (all in `build/<tool>_artefacts/Release/`):

| Tool | What it does |
|---|---|
| `ChanSamplerTests` | The engine tests (no arguments). Run it after every change; it returns non-zero on a failure. |
| `ChanSamplerHostTest <Chan Sampler.vst3> nulltest/signals/sine.wav` | Loads the built VST3 the way a DAW does: automation, MIDI, save / reload, the editor size, host rate changes, offline rendering. It expects that 2-second sine. |
| `ChanSamplerRender` | Renders notes offline for the null tests; also `bench <sample>` (CPU per stretch mode, 32 / 64 voices), `stretchtest` (the stretcher alone on a file), `fftbench` (the stretcher's FFT against JUCE's), `compatstate <state.bin>` (an older version's saved state loads unchanged). |
| `ChanSamplerSnapshot` | Writes editor screenshots (to compare with FL's screen, and the README's pictures). |

GitHub Actions builds and runs the engine and host tests
(`.github/workflows/ci.yml`): on Linux for every push to `main`, on Windows
too for pull requests, and on both from Actions → *CI* → *Run workflow*.
Changes to docs or `nulltest/` alone don't start it. macOS is built and
validated at each release.

## Null tests against FL

`nulltest/` renders the same notes through FL's Sampler and through Chan
Sampler inside FL, and subtracts the two. Its [README](nulltest/README.md)
explains how to run them; they need FL Studio 2026 (closed while rendering),
Python 3 and numpy. The time-stretch modes can't null (FL's engine is
proprietary), so `nulltest/stretch/` compares their behaviour instead.

Never commit Image-Line's samples, presets or projects: the tests read FL's
own samples from the FL install.

## Code

- C++17 and JUCE; the sources are in `Source/`, listed once in `CMakeLists.txt`.
- Match the code around you: its naming, its comment density, and comments
  that say what was measured in FL rather than restating the code.
- Line endings are LF everywhere (`.gitattributes`); the installer script and
  the Windows install notes keep CRLF.
- Keep the audio thread allocation-free and lock-free.
- A behaviour change gets a line in `CHANGELOG.md` under `## Unreleased`,
  written for users.

## Making a release

1. Set the version in `CMakeLists.txt` (`project(ChanSampler VERSION x.y.z)`),
   `packaging/ChanSampler.iss` (both places), the tag example below and the
   placeholder in `.github/ISSUE_TEMPLATE/bug_report.yml`.
2. Rename `## Unreleased` in `CHANGELOG.md` to `## x.y.z`: that section
   becomes the release notes.
3. Commit, push, then tag:

   ```
   git tag v1.2.0
   git push origin v1.2.0
   ```

The release workflow (`.github/workflows/release.yml`) builds Windows, macOS
(universal) and Linux, validates the plugins with pluginval at its strictest
level and, on macOS, with Apple's `auval`, makes the installers, and publishes
the release with SHA-256 checksums. For a test build without releasing:
Actions → *Build and release* → *Run workflow*; the files appear under the
run's *Artifacts*.

## Working on another computer

Clone the repository and build as above. Generated files are not in the
repository: `build/`, FL's renders in `nulltest/renders*`, and the stretch
test signals (`python -m stretch.make_signals` in `nulltest/`).

## License

By contributing you agree that your contribution is licensed under the
[GNU AGPL v3](LICENSE), like the rest of Chan Sampler.
