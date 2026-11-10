# Contributing to Chan Sampler

Bug reports and null-test findings go to the [issues](https://github.com/MaymiPR/Chan-Sampler/issues/new/choose). This
page covers changing the code: how it is built, tested and checked, how a change reaches `main`, and how a release is
made. [STANDARDS.md](STANDARDS.md) holds the rules in full.

## The one rule

Chan Sampler follows FL Studio's Sampler, and FL is the reference for every behaviour. A change that affects the sound
is measured against FL's own render of the same settings ([Null tests](#null-tests)), and what was measured goes into
[MEASUREMENTS.md](plugins/ChanSampler/MEASUREMENTS.md), so the next person can see why the code is the way it is.
Knobs use FL's units and FL's ranges. Nothing of FL's (renders, samples, presets, projects, art) enters the
repository; a decision is recorded as the rule it produced, with the date of its approval, and nobody's words are
quoted in a file, a commit message, a tag or release notes.

## Build

Requirements and the commands per platform are in the [build guide](docs/BUILDING.md). The default configuration
(`-DCHANSAMPLER_TOOLS=ON`) also builds the offline renderer (`CsRender`) and the editor snapshot tool
(`ChanSamplerSnapshot`), under `build/<tool>_artefacts/Release/`. The target `ci_targets` builds what CI builds.

## Tests

`ctest --test-dir build -C Release --output-on-failure` runs every test program with its arguments; a fixture writes
the 2-second test sine first (`plugins/ChanSampler/nulltest/render_signals.py sine.wav`, into its `signals/`
folder). Each command returns non-zero on a failure.

| Test or tool | What it does |
|---|---|
| `ChanSamplerTests` | The engine tests (no arguments). |
| `ChanSamplerUiTests <sine.wav>` | Builds the editor off screen and checks its shared controls and the undo history: every drop-down list is the shared `chansampler::ui::ComboBox`, every choice reaches its parameter and is still shown after a sheet change and a rebuilt editor; gestures record single steps, unchanged values none, undo / redo restore values and the sample, the keys and the field work (one action per press, unused keys left to the host), a wheel notch eases the knob, a panel's menu arrow stays turned while its menu is open; as a plugin, every parameter is automatable, a preset, a sample load and a saved preset are each one edit for the host (the History step parameter, named after it, with their values off the message thread), the host's undo, redo and jumps over several steps put them back, and the editor has no undo arrows and leaves the keys to the host. Needs a display (`xvfb-run` on Linux). |
| `ChanSamplerHostTest <Chan Sampler.vst3> <sine.wav> <state-*.bin>` | Built from `tools/HostTest.cpp`; loads the built VST3 the way a DAW does: automation, MIDI, save / reload, the editor size, host rate changes, offline rendering, and projects saved by each earlier release (`plugins/ChanSampler/tests/data`). |
| Render regression | `python scripts/render_regress.py --build build`: `tests/regress/ChanSampler/jobs.txt` through `CsRender` against the committed goldens; our output does not change unless intended (`--update` after an intended change in the sound, committed with it and the reason in the message). |
| `CsRender` | Renders notes offline for the null tests and the render regression (a job file, or one render on the command line; `tune:` keys set the fitted constants of `dsp/Tuning.h`); `CsRenderDev` is a second copy for trying a change while a fit runs; also `bench <sample>` (CPU per stretch mode, 32 / 64 voices), `stretchtest` (the stretcher alone on a file), `fftbench` (the stretcher's FFT against JUCE's); the full list is at the top of `plugins/ChanSampler/tools/CsRender.cpp`. |
| `ChanSamplerSnapshot <out.png> [--scale=2]` | Writes an editor screenshot (the standard call: a synthesised drum hit, the Sample sheet); the long form takes a sample, parameters and a sheet (`plugins/ChanSampler/tools/Snapshot.cpp`). `plugins/ChanSampler/scripts/make_screenshots.py` makes the README's pictures with it. |
| Null tests | the same notes through FL's Sampler and through Chan Sampler; the residual in dB ([plugins/ChanSampler/nulltest/README.md](plugins/ChanSampler/nulltest/README.md)). |
| Repository checks | `python scripts/check_repo.py`: absolute paths, old names, attribution forms, document style, whitespace, vendor words in identifiers and file names, the harness and script names. |
| Reference hosts | `RefRender`, `ParamDump`, `RefCapture` and `FocusCheck` (`tools/`, with `-DCHANSAMPLER_TOOLS=ON`); `FocusCheck` checks that the space bar still reaches the host after clicks in the built VST3's editor. |

## The local check

`scripts/check.ps1` (Windows) or `scripts/check.sh` (Linux, macOS) is the one command before any build is handed over
and the proof on a private repository, where no workflow runs. It closes a running FL Studio, runs `check_repo.py` and
the lint tools it finds on the PATH (a missing tool is reported, never installed), configures when
`build/CMakeCache.txt` is missing, builds `ci_targets` into a log, counts warnings by the same rule as the workflows
and fails above zero, asserts the VST3 is newer than the build start, runs `ctest` and the render regression, and
prints one `CHECK` line with the counts. Switches: `-SkipBuild`, `-SkipTests`, `-KeepFl`.

## Changing the code

1. Branch `fix/<slug>`, `feature/<slug>` or `chore/<slug>` from `main`.
2. Make the change. A change in the sound is measured against FL's render of the same settings.
3. `scripts/check.ps1` (or `check.sh`) passes.
4. Open a pull request; Lint and CI run on a public repository, the local check stands in on a private one.
5. Squash merge; the branch is deleted on both sides at merge.
6. Add the line to `CHANGELOG.md` under `## Unreleased` (New, Improved, Fixed or Known issues, written for users,
   with before and after numbers in dB for a sound change).

Code rules:

- C++17 and JUCE; the plug-in's sources are in `plugins/ChanSampler/Source/` (`dsp/` the engine, `Engine.h/.cpp` its
  top class and `Tuning.h` its fitted constants; `ui/` the plug-in's own controls in `Controls.h/.cpp`; the processor,
  editor, parameters (`Params.h`, the IDs in `cs::id`), presets and host glue at the top), listed once in
  `plugins/ChanSampler/plugin.cmake`. What every editor of the suite uses is in `shared/` (the static library
  `ChanSamplerShared`): `ui/Theme.h` (the colours and the font), `ui/Widgets.h/.cpp` (knobs, switches, menus, dialogs,
  panels), `ui/HostFocus.h/.cpp` (the host's keys after a click), `dsp/Math.h` (deterministic math).
- Every C++ name of the plug-in is in the namespace `cs` (the tool abbreviation), the shared code in `chansampler`.
- Match the code around you: its naming, its comment density, and comments that say what was measured in FL rather
  than restating the code.
- Line endings are LF everywhere (`.gitattributes`); the installer script and the PowerShell scripts keep CRLF.
- Keep the audio thread allocation-free and lock-free.
- Dependencies are pinned to commits: JUCE by `CHANSAMPLER_JUCE_COMMIT` (the commit its tag pointed to; configuring
  stops if the tag moves), clap-juce-extensions by its `GIT_TAG`, the workflow actions by SHA with the version in a
  comment. Update the pin with the version.
- Configuring patches the fetched JUCE (`cmake/PatchJuceVst3Keys.cmake`, idempotent): its VST3 editor ignores the keys
  a host offers the view, so Ctrl+Z / Ctrl+Y would never reach the plugin in FL Studio.

## Making a release

[.github/RELEASE_CHECKLIST.md](.github/RELEASE_CHECKLIST.md) is the one list; the release checklist workflow opens it
as an issue for a `release/x.y.z` branch or by hand. In short:

1. `## Unreleased` in [CHANGELOG.md](CHANGELOG.md) holds everything user-facing, grouped under `### New`,
   `### Improved` and `### Fixed`. That section becomes the release notes, followed by a fixed footer (system
   requirements, where the install steps are, whether the files are signed: `scripts/release_notes.py`).
2. Optional: branch `release/x.y.z`; the checklist issue opens.
3. Save a project of the new version for the host test, so later versions are checked against it:

   ```text
   build/ChanSamplerHostTest_artefacts/Release/ChanSamplerHostTest "build/ChanSampler_artefacts/Release/VST3/Chan Sampler.vst3" --save-state plugins/ChanSampler/tests/data/state-x.y.z.bin
   ```

   If it is identical to the last one (no parameter changed), leave it out. A later change that alters how a saved
   setting reads back fails the host test; when it is intended (a range FL changed, say), the changelog says so and
   the `.txt` beside the old project is updated.
4. The local check passes with zero warnings; on a public repository Lint and CI are green on `main`.
5. `python scripts/bump_version.py x.y.z`: the version in `CMakeLists.txt` (`project(ChanSampler VERSION x.y.z)`: the
   plugin, its About menu, the tools and the installers take it from there), the dated CHANGELOG section, the commit
   `Release x.y.z`. Push `main`.
6. When packaging changed, run Build and release by hand once and check the artifacts.
7. On approval: `python scripts/bump_version.py x.y.z --tag` and `git push origin vx.y.z`. The workflow builds
   Windows, macOS (universal) and Linux, validates the plug-in with pluginval at strictness 10 (and `auval` on
   macOS), signs and notarizes when the repository has the secrets, and publishes the three installers with
   `SHA256SUMS.txt`, attestations and the notes from CHANGELOG.md. A tag with a hyphen (`vx.y.z-rc.1`) publishes a
   pre-release.
8. Check the release page, download one file on another machine, close the checklist issue.
9. A release that fails after the tag is fixed on `main` and shipped as the next patch version.

The signing secrets are named in `.github/workflows/release.yml`; without them the workflow publishes the same files
unsigned and the notes say so. Each download carries `packaging/THIRD-PARTY-NOTICES.txt`: when a dependency is added
or updated, its licence text is updated there.

## Repository protection

`main` and the `v*` release tags cannot be deleted or force-pushed, and release tags cannot be moved (GitHub rulesets).
Secret scanning with push protection, Dependabot alerts and private vulnerability reports are on (see `SECURITY.md`);
Actions may run GitHub's own actions only, with read-only tokens except the release job, and outside contributors'
pull requests wait for approval before CI runs.

## Repository layout

| Path | What it holds |
|---|---|
| `CMakeLists.txt` | the project, the JUCE pin and commit check, the options (`CHANSAMPLER_TOOLS`, `CHANSAMPLER_CLAP`, `CHANSAMPLER_DEV_COPY`), `ci_targets`, the reference hosts, `include(shared/shared.cmake)` and `include(plugins/ChanSampler/plugin.cmake)` |
| `shared/` | the code every plug-in of the suite links, as the static library `ChanSamplerShared`: `shared.cmake`, `ui/` (`Theme.h`, `Widgets`, `HostFocus`), `dsp/Math.h` |
| `plugins/ChanSampler/` | the plug-in: `plugin.cmake` (the plug-in, its tests and tools, the ctest registrations with the sine fixture), `Source/` (with `dsp/` and `ui/`), `tests/` (`ChanSamplerTests.cpp`, `ChanSamplerUiTests.cpp`, `data/`: projects saved by earlier releases for the host test), `tools/` (`CsRender.cpp`, and its own `Snapshot.cpp`: a sample, the sheets and the menus beyond the suite's generic tool), `scripts/` (`make_screenshots.py`), `nulltest/` (the null tests and measurement scripts against FL Studio, published; FL's renders and files stay in its git-ignored `renders*/` and `local/` folders), [README.md](plugins/ChanSampler/README.md), [RESEARCH.md](plugins/ChanSampler/RESEARCH.md), [MEASUREMENTS.md](plugins/ChanSampler/MEASUREMENTS.md) (every measurement), [NULLTESTS.md](plugins/ChanSampler/NULLTESTS.md) |
| `tools/` | C++ only: the reference hosts `RefRender.cpp`, `ParamDump.cpp`, `RefCapture.cpp`, `FocusCheck.cpp`, the suite's `Snapshot.cpp` and `HostTest.cpp` (built per plug-in as `<Stem>HostTest`) |
| `measurements/ChanSampler/` | the measured data (kernels, matrices, curves, tables), published (approval recorded 2026-11-09); the null-test scripts read and write it |
| `tests/regress/` | the render regression's cases and golden renders |
| `cmake/` | `PatchJuceVst3Keys.cmake` |
| `scripts/` | `bump_version.py`, `release_notes.py`, `render_regress.py`, `make_site.py`, `check_repo.py`, `check.ps1`, `check.sh` |
| `packaging/` | the installers; `plugins.txt` lists the plug-in they hold |
| `docs/` | the build guide, `branding/` (the banners, the social preview, `colours.json`), `screenshots/` |
| `.github/` | `workflows/` (Lint, CI, Build and release, Release checklist, Landing page), the issue forms, the pull request template, `RELEASE_CHECKLIST.md`, `dependabot.yml`, CODEOWNERS |
| `local.example.yaml` | the keys of `local.yaml` (git-ignored) for machine paths |

## Null tests

`plugins/ChanSampler/nulltest/` renders the same notes through FL's Sampler and through Chan Sampler inside FL, and
subtracts the two. Its [README](plugins/ChanSampler/nulltest/README.md) explains how to run them; they need FL Studio
(closed while rendering), Python 3.12 or newer with numpy (`pip install -r requirements.txt`) and the tools built. The
time-stretch modes cannot null (FL's engine is proprietary), so its stretch scripts (`compare_stretch*.py` and
others) compare their behaviour instead. Image-Line's samples, presets and projects are never committed: the tests read FL's own samples from the FL
install.

## Working on another computer

Clone the repository and build as above. Generated files are not in the repository: `build/`, FL's renders in
`plugins/ChanSampler/nulltest/renders*`, and every audio file. Write the test signals with
`python plugins/ChanSampler/nulltest/render_signals.py` and `python render_stretch_signals.py` (in
`plugins/ChanSampler/nulltest/`).

## Versions

One version (`project(ChanSampler VERSION ...)` in `CMakeLists.txt`). A new feature raises the middle number, a fix the
last. Tags are `vx.y.z`; `vx.y.z-rc.N` publishes a pre-release. `scripts/bump_version.py` writes every version.

## Frozen identity

What released users' projects, presets and installs depend on stays as every release has shipped it, whatever else is
renamed:

| Item | Value | Where it is set |
|---|---|---|
| Manufacturer and plug-in codes (and the VST3 class IDs derived from them) | `Chsm`, `Csm1` | `plugins/ChanSampler/plugin.cmake`, `packaging/plugins.txt` |
| Bundle ID | `com.ChanSampler.ChanSampler` (the naming rule today would give the lower-case form) | `BUNDLE_ID` in `plugin.cmake` |
| CLAP ID | `com.chansampler.chansampler` | `CLAP_ID` in `plugin.cmake` |
| Parameter IDs | the strings in `cs::id` | `plugins/ChanSampler/Source/Params.h` |
| Installer AppId | `8C1B5E2A-4F3D-4B7A-9E61-6A2C0D5F3B11` | `packaging/ChanSampler.iss` |
| Install locations | the root plug-in folders (see below) | `packaging/`, `SUBFOLDER: ''` in `release.yml` |

The company name hosts show changed from `ChanSampler` to `Chan Sampler` (approved 2026-11-09, shipped in the next
minor release): FL Studio and other hosts list the plug-in under the new vendor, and a plug-in rescan may be needed.

## Deviations from the standard

- Install locations: the plug-ins install to the root plug-in folders where every release since 1.0 has put them
  (macOS `/Library/Audio/Plug-Ins/Components`, `VST3` and `CLAP` with the app in `/Applications`, `SUBFOLDER: ''` in
  release.yml; Windows the root VST3 and CLAP folders under the earlier releases' AppId; Linux `~/.vst3` and
  `~/.clap`), not to a suite subfolder, because a moved location would leave installed users with two copies.

## License

By contributing you agree that your contribution is licensed under the [GNU AGPL v3](LICENSE), like the rest of Chan
Sampler.
