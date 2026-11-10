# Standards

The rules every repository of this kind follows: layout, naming, branches, versions, commits, documents, hygiene,
the CI and release standard and the two development flows. CONTRIBUTING.md is the short form for a change;
this page is the reference. A deviation is listed in the last section with its reason.

## Tree

| Path | Holds |
|---|---|
| `plugins/<Id>/` | one plug-in: `plugin.cmake`, `Source/` (`Params.h`, `PluginProcessor`, `PluginEditor`, `Presets`, a generated `PresetTable.h`; `dsp/` with `Engine` and `Tuning.h`; `ui/` with the plug-in's own `Theme.h` and `Controls`), `tests/` (`<Stem>Tests.cpp`, `<Stem><Area>Tests.cpp`, `data/state-<x.y.z>.bin`), `tools/` (`<Ab>Render.cpp`, `<Ab><Part>Render.cpp`), a flat `nulltest/` (with its README), README, RESEARCH, MEASUREMENTS, NULLTESTS; optional `scripts/` (every script that writes under `Source/` or `resources/`, the editor-measuring scripts) and `resources/` (its binary resources) |
| `shared/` | code shared by the plug-ins, built as the static library `<SuiteId>Shared`: `shared.cmake`, `ui/` (`Theme.h`, `Widgets`, `HostFocus`), `dsp/` (`Math.h`); a suite of one plug-in has it too |
| `tools/` | C++ only: the reference hosts RefRender, ParamDump, RefCapture, FocusCheck, and `Snapshot.cpp` and `HostTest.cpp`, built per plug-in (Python goes to `scripts/`) |
| `measurements/<Id>/` | measurement data per plug-in, the id's exact case; published by a public suite with the approval recorded, git-ignored while the suite is private |
| `packaging/` | `plugins.txt` (`Target|Product name|PluginCode|ManufacturerCode|AU type`), the Inno Setup script, `distribution.xml`, `install-linux.sh`, the INSTALL notes, THIRD-PARTY-NOTICES, ABOUT; the standalone apps unticked by default |
| `tests/regress/` | the render regression: `<Id>/jobs.txt` and `<Id>/golden/*.f32` per plug-in target, the only place `scripts/render_regress.py`, CI and the local check read |
| `docs/` | `BUILDING.md`, `branding/` (banners, icon, social preview, `colours.json`), `screenshots/` |
| `scripts/` | `bump_version.py`, `release_notes.py`, `render_regress.py`, `make_site.py`, `check_repo.py`, `check.ps1`, `check.sh`; `build_native.py` when a plug-in has native null-test helpers; `nulltest/` for a host the plug-ins share and `ref_state_lib.py` |
| `.github/` | the five workflows, `dependabot.yml`, the issue forms, the pull request template, `RELEASE_CHECKLIST.md`, CODEOWNERS |

Root files: `README.md` (purpose, contents, quick start, links), `LICENSE` (AGPL-3.0), `CONTRIBUTING.md`,
`CHANGELOG.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md`, `STANDARDS.md`, `CMakeLists.txt`, `.provenance`,
`local.example.yaml` (the keys of the git-ignored `local.yaml`, which holds every machine path; the suite's own keys
in its closing "Suite keys" block), `.editorconfig`,
`.gitattributes`, `.gitignore`, `.markdownlint-cli2.yaml`, `.yamllint.yaml`, `requirements.txt`.

## Naming

| Item | Rule | Example |
|---|---|---|
| Suite | the repository's name; the id without spaces; the company name hosts show | Example, Example |
| Plug-in id | PascalCase, no spaces; the folder, the CMake target, the state tag | ThePlugin |
| Class stem | the id without a leading The | PluginProcessor, PluginEditor, PluginTests |
| Tool abbreviation | two letters, the initials without The, or the first two letters of one word; a renderer of one part is `<Ab><Part>Render` | Tp: TpRender, TpMeterRender |
| Namespace | the abbreviation in lower case; one suite namespace for `shared/` (the suite name's first word in lower case) | `tp`, `example` |
| Four-character codes | first upper case, then lower case or digits; the manufacturer code is the suite's | Exmp, Expl |
| Bundle and CLAP id | `com.<suiteid>.<pluginid>` in lower case | com.example.theplugin |
| Preset extension | the plug-in name without The, lower case, no spaces, at most 8 characters | `.plugin` |
| Release files | `<SuiteId>-<version>-<OS>[-<Format>].<ext>` | Example-1.2.0-Windows-Setup.exe |
| Look and controls | suite colours `shared/ui/Theme.h`, a plug-in's own look `Source/ui/Theme.h`; shared controls `shared/ui/Widgets`, a plug-in's own `Source/ui/Controls`; no forwarding headers | `Source/ui/Controls.cpp` |
| Test programs | `tests/<Stem>Tests.cpp`, a further one `tests/<Stem><Area>Tests.cpp`; `<Stem>HostTest` from `tools/HostTest.cpp` | `PluginUiTests.cpp` |
| Harness module | `<ab>_ref.py`, the tool abbreviation in lower case (never the reference's initials or name) | `tp_ref.py` |
| Null-test scripts | lower_snake in a flat `nulltest/`; `nulltest.py` and `first_null.py`; others start with their purpose (`measure_`, `probe_`, `fit_`, `train_`, `null_`, `render_`, `compare_`, `check_`, `read_`, `cases_`), helpers are `<topic>_lib.py`, a plug-in is named by its id; no numbered iterations (the script whose output ships keeps the name); native helpers in `nulltest/native/`, built into `build/native/` | `probe_release.py` |
| Plug-in scripts | `plugins/<Id>/scripts/`: `make_`, `export_`, `pack_` for a script that writes under `Source/` or `resources/`; `measure_ui.py`, `compare_ui.py`, `fit_<part>.py` for the editor | `make_presets.py` |
| Null-test report | one generated `NULLTESTS.md` per plug-in, a section per kind of case (Settings, Stages, Presets, Grid); the data as JSON in `measurements/<Id>/` | NULLTESTS.md |
| Documents | lower-kebab under `docs/` (`BUILDING.md` excepted); root and plug-in documents in upper case | `docs/null-testing.md` |
| Branding | `docs/branding/<suiteid>-` banner dark and light (SVG), icon (SVG and 512 px PNG), social (SVG and PNG) | `example-banner-light.svg` |
| Screenshots | `docs/screenshots/hero.png`; `<pluginid>.png` and `<pluginid>-<view>.png`, ids in lower case; `<suiteid>-<view>.png` for a view the plug-ins share | `theplugin.png`, `example-toolbar.png` |

Parameter ids are lowerCamelCase and never renamed after a release; parameter names are the names the reference
shows in a host. No vendor word in a control label, a preset name, a plug-in name, a code identifier, a tracked file
name or a local file a script reads (`.provenance` lists the words; `scripts/check_repo.py` checks them); the
reference is named only where a document says what a plug-in recreates.

The frozen identity never changes once a release has shipped: the manufacturer and plug-in codes (and the VST3 class
IDs from them), the bundle and CLAP IDs, the parameter IDs, the company name, the installer's AppId and the install
folders. CONTRIBUTING.md records the values under "Frozen identity".

## Branches

`main` is the only long-lived branch, on GitHub and locally. Work happens on `feature/<slug>`, `fix/<slug>`,
`chore/<slug>` or `release/x.y.z`; a branch is squash-merged by pull request and deleted on both sides at merge.
Archive tags and release tags (`vx.y.z`, `vx.y.z-rc.N`) are created only on approval, recorded with its date.

## Versions

One version for the suite, in `project(<SuiteId> VERSION x.y.z)`. `0.1.0` until the first release, then `1.0.0`;
a new plug-in raises the middle number, a fix the last. `scripts/bump_version.py` writes every version; a version
is never edited by hand.

## Commits

One imperative subject line of at most 72 characters; an optional body stating what and why. No quotations, no
attributions, no absolute paths, no session names. The no-reply account on every commit.

## Documents

1. Official reference voice: present tense, imperative or declarative, impersonal.
2. Each document states its purpose, scope and what a reader needs, and nothing else.
3. No logs, diaries, session notes, progress updates, dated entries, narrative of work done, brainstorms or
   conversation history. A date appears only as part of a fact a reader needs (a release date, a pin's date).
4. One authoritative place per fact; other documents link to it.
5. Short sections, tables and numbered steps for procedures.
6. No filler, hedging, repeated explanation, outdated sections or commented-out instructions.
7. No absolute paths, no old names, no quotations of or attributions to a person. A decision is stated as the rule
   it produced, with the date of its approval where the date matters.
8. Behaviour-driving files (checklists, templates, workflows, policy files) keep every rule, constraint and
   instruction when reworded; required front matter and schemas stay valid.

## Hygiene

LF line endings everywhere, CRLF for `.iss` and `.ps1`; four-space indentation (two in YAML); actions pinned by
commit SHA; no em or en dashes; no vendor internals in a public document (install paths, resource names, internal
parameter ids, internal class names); nothing of the original ships (code, art, fonts, presets, samples, weights,
or art fitted or traced from its pixels); reference renders, captures and result files stay git-ignored; before
every release the provenance gate passes: `.provenance` names the vendor words and every allowed file with its
reason and approval date, and `scripts/check_repo.py --release` passes.

## CI and release standard

### Workflows

| Workflow | Trigger | Does |
|---|---|---|
| Lint (`lint.yml`) | push to main, pull request, by hand | Markdown, YAML, actionlint with shellcheck, the shell scripts, `scripts/check_repo.py`; one Ubuntu job |
| CI (`ci.yml`) | push to main and pull request (documents, `docs/`, null-test harnesses ignored), by hand | Linux, Windows, macOS: configure with the tools on and CLAP off, build `ci_targets` with zero warnings, `ctest`, the render regression; Windows uploads editor screenshots |
| Build and release (`release.yml`) | tag `v*`, by hand (artifacts only) | every format on three systems, `ctest`, pluginval at strictness 10, FocusCheck where built, signing when the secrets exist, one installer per OS; on a tag: checksums, attestations, notes, the GitHub release |
| Release checklist (`release-checklist.yml`) | creation of `release/x.y.z`, by hand with the version | opens the issue from `.github/RELEASE_CHECKLIST.md` once |
| Landing page (`pages.yml`) | a published release, by hand | `scripts/make_site.py` to GitHub Pages (public repositories with Pages enabled) |

### Rules

1. The header comment of every workflow states purpose, trigger, runners and what the workflow does not do, in the
   documentation style above, and names a procedure by its page in this repository.
2. GitHub-owned actions only, pinned by commit SHA with the version in a comment; Dependabot proposes the bumps.
3. `permissions: contents: read` at the top; `contents: write` only on the job that publishes a release; `pages` and
   `id-token` only on the Pages deploy; `persist-credentials: false` on every checkout.
4. `workflow_dispatch` on every workflow a person may run by hand; one concurrency group per workflow and ref; Lint
   and CI cancel the superseded run, Build and release never cancels.
5. The pins of the table below; JUCE and clap-juce-extensions are pinned in CMake only.
6. No secret value is echoed; signing and notarization run only when the secrets exist, the output is otherwise the
   same files unsigned, and the signing state is recorded as an artifact.
7. A private repository keeps its push- and tag-triggered workflows disabled (`gh workflow disable`; Dependabot
   stays) and proves the same steps locally with `scripts/check.ps1` or `scripts/check.sh`. The scripts report a
   tool they cannot find instead of installing it. The workflows are enabled when the repository becomes public.
8. A workflow whose trigger can never fire in its repository is deleted, not disabled.
9. A workflow beyond the five is a deviation, listed below with its trigger and purpose.
10. Zero warnings. A CI or release run passes only when every job on every platform reports no compiler, linker,
    CMake, lint, actionlint, pluginval or workflow annotation warning. The build jobs count warnings by rule from
    the build log (lines matching a compiler warning or a `warnings generated` summary; annotation and hint lines
    excluded; nothing filtered by path) and fail on a count above zero. A third-party false positive is silenced
    per file with a comment naming the cause, never by a global flag. The local check scripts apply the same count.

### Pins

| Item | Pin |
|---|---|
| Runners | `ubuntu-24.04`, `windows-latest`, `macos-15`; the Linux release build inside the `ubuntu:22.04` container (glibc 2.35 floor) |
| pluginval | 1.0.4 |
| Inno Setup | 7.1.1, checked by SHA-256 |
| markdownlint-cli2 | 0.24.0 |
| yamllint | 1.38.0 |
| actionlint | 1.7.12 |
| Python for the lint job | 3.12 |
| JUCE, clap-juce-extensions | the tag and commit in `CMakeLists.txt` |

### Release flow

1. `CHANGELOG.md` holds the version's entries under `## Unreleased` (New, Improved, Fixed, Known issues).
2. Optional: branch `release/x.y.z`; the checklist issue opens. The branch is deleted after the release.
3. `scripts/check.ps1` (or `check.sh`) passes with zero warnings; on a public repository Lint and CI are green on
   main with zero warnings on every platform.
4. `python scripts/bump_version.py x.y.z` writes the version into `CMakeLists.txt`, dates the CHANGELOG section and
   commits. The version is never edited by hand.
5. Push main. When packaging changed since the last release, run Build and release by hand first and check the
   artifacts (the installers, the signing state files).
6. On approval, `bump_version.py x.y.z --tag` and push the tag. Build and release publishes
   `<SuiteId>-x.y.z-Windows-Setup.exe`, `<SuiteId>-x.y.z-macOS.pkg`, `<SuiteId>-x.y.z-Linux.tar.gz`,
   `SHA256SUMS.txt`, the attestations and the notes; a `-rc.N` tag publishes a pre-release. A release run with any
   warning is a failed release: fix on main and re-run before the tag is pushed, or the tag is not pushed.
7. The landing page deploys; the repository settings are confirmed; the checklist issue is closed.
8. A release that fails after the tag is fixed on main and shipped as the next patch version. A tag or a release is
   deleted only on approval, and never once a file has been downloaded.

## Development flows

A change to the suite:

1. Branch `fix/<slug>`, `feature/<slug>` or `chore/<slug>` from main.
2. Make the change. A DSP change is recorded with the null regression; a case worse by more than 0.5 dB blocks it.
3. `scripts/check.ps1` (or `check.sh`) passes. Open a pull request; Lint and CI run (public) or the local check
   stands in (private).
4. Squash merge; the branch is deleted on both sides. Add the CHANGELOG line under `## Unreleased`.
5. Release when due, by the release flow.

A new plug-in in the suite:

1. The reference's parameter dump and one rendered block; the names chosen by the naming table; the plan approved.
2. The generator adds `plugins/<Id>/` with `plugin.cmake`, the include line in `CMakeLists.txt`, the line in
   `packaging/plugins.txt`, the regression jobs and the local keys for its reference.
3. `first_null.py` on day one; the local check before every build that is handed over; the null regression after
   every DSP change.
4. Before its first release: the name review, the provenance and public gates, the plug-in's four documents, the
   screenshots, the CHANGELOG section. The minor version bumps.

## Deviations from the standard

None.
