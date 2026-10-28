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
[build guide](docs/BUILDING.md). Building without `--target` also makes the
developer tools (all in `build/<tool>_artefacts/Release/`):

| Tool | What it does |
|---|---|
| `ChanSamplerTests` | The engine tests (no arguments). Run it after every change; it returns non-zero on a failure. |
| `ChanSamplerHostTest <Chan Sampler.vst3> nulltest/signals/sine.wav Tests/data/state-*.bin` | Loads the built VST3 the way a DAW does: automation, MIDI, save / reload, the editor size, host rate changes, offline rendering, and projects saved by each earlier release (`Tests/data`). It expects that 2-second sine. |
| `ChanSamplerRender` | Renders notes offline for the null tests; also `bench <sample>` (CPU per stretch mode, 32 / 64 voices), `stretchtest` (the stretcher alone on a file), `fftbench` (the stretcher's FFT against JUCE's); the full list is at the top of `Tests/RenderTool.cpp`. |
| `ChanSamplerSnapshot` | Writes editor screenshots (to compare with FL's screen, and the README's pictures). |
| `ChanSamplerUiTests nulltest/signals/sine.wav` | Builds the editor off screen and checks its shared controls and the undo history: every drop-down list is the shared `fl::ComboBox`, every choice reaches its parameter and is still shown after a sheet change and a rebuilt editor; gestures record single steps, unchanged values none, undo / redo restore values and the sample, the keys and the field work (one action per press, unused keys left to the host), a wheel notch eases the knob, a panel's menu arrow stays turned while its menu is open. Needs a display (CI runs it under xvfb on Linux). |

GitHub Actions builds and runs the engine, UI and host tests
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
- Dependencies are pinned to commits: JUCE by `CHANSAMPLER_JUCE_COMMIT` (the
  commit its tag pointed to; configuring stops if the tag moves),
  clap-juce-extensions by its `GIT_TAG`, the workflow actions by SHA with the
  version in a comment. Update the pin with the version.
- Configuring patches the fetched JUCE once (`cmake/PatchJuceVst3Keys.cmake`):
  its VST3 editor ignored the keys a host offers the view, so Ctrl+Z / Ctrl+Y
  never reached the plugin in FL Studio. A JUCE of your own given with
  `FETCHCONTENT_SOURCE_DIR_JUCE` is not touched (the keys then go to FL).

## Repository protection

`main` and the `v*` release tags can't be deleted or force-pushed, and release
tags can't be moved (GitHub rulesets). Secret scanning with push protection,
Dependabot alerts and private vulnerability reports are on (see
`SECURITY.md`); Actions may run GitHub's own actions only, with read-only
tokens except the release job, and outside contributors' pull requests wait
for approval before CI runs.

## Making a release

1. Set the version in `CMakeLists.txt` (`project(ChanSampler VERSION x.y.z)`):
   the plugin, its About menu, the tools and the installers take it from
   there.
2. Rename `## Unreleased` in `CHANGELOG.md` to `## x.y.z`: that section
   becomes the release notes.
3. Save a project of the new version for the host test, so later versions are
   checked against it:

   ```
   build/ChanSamplerHostTest_artefacts/Release/ChanSamplerHostTest "build/ChanSampler_artefacts/Release/VST3/Chan Sampler.vst3" --save-state Tests/data/state-x.y.z.bin
   ```

   If it's identical to the last one (no parameter changed), leave it out.
   A later change that alters how a saved setting reads back fails CI; if
   it's intended (a range FL changed, say), say so in the changelog and update
   the `.txt` beside the old project.
4. Commit, push, then tag:

   ```
   git tag vx.y.z
   git push origin vx.y.z
   ```

The release workflow (`.github/workflows/release.yml`) builds Windows, macOS
(universal) and Linux, validates the plugins with pluginval at its strictest
level and, on macOS, with Apple's `auval`, and publishes the Windows installer,
the macOS installer and the Linux tarball with SHA-256 checksums. Each carries
`packaging/THIRD-PARTY-NOTICES.txt`: when a dependency is added or updated,
update its license text there. For a test build without releasing:
Actions → *Build and release* → *Run workflow*; the files appear under the
run's *Artifacts*.

## Working on another computer

Clone the repository and build as above. Generated files are not in the
repository: `build/`, FL's renders in `nulltest/renders*`, and the stretch
test signals (`python -m stretch.make_signals` in `nulltest/`).

## License

By contributing you agree that your contribution is licensed under the
[GNU AGPL v3](LICENSE), like the rest of Chan Sampler.
