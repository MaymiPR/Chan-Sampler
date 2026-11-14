# CLAUDE.md: instructions for working in Chan Sampler

Chan Sampler is a suite of open-source JUCE audio plug-ins (VST3, CLAP, AU and Standalone; AGPL-3.0). Each plug-in
recreates an original, its reference, from measurements of the reference's output and editor, and is checked against
it by null tests. Read this page first, then [CONTRIBUTING.md](CONTRIBUTING.md) (how to build, test, change and
release) and [STANDARDS.md](STANDARDS.md) (the rules in full).

## The check

One command proves a change, and it is the proof while the repository is private and no workflow runs:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check.ps1      (Windows)
bash scripts/check.sh                                                      (Linux, macOS)
```

It runs the repository checks and the lint tools, configures, builds `ci_targets`, counts warnings (zero allowed),
compares every VST3 with its sources, and runs `ctest` and the render regression; `-Pluginval`, `-FocusCheck` and
`-NullCheck` add the hand-over checks. It prints one `CHECK` line and exits 0 only when no step failed and none is
incomplete. Run it before every commit that is handed over, every push and every pull request. It never installs a
tool and never downloads a dependency.

## Where things are

| Path | Holds |
|---|---|
| the plug-in folder | `plugin` in a suite of one plug-in, `plugins/<Id>` per plug-in in a suite of several: `plugin.cmake`, `Source/` (`Params.h`, the processor, the editor, `Presets`, `dsp/Engine` and `dsp/Tuning.h`), `tests/`, `tools/` (the offline renderer), `nulltest/`, `ui/` (the control table and the behaviour checklist), README, RESEARCH, MEASUREMENTS, NULLTESTS |
| `shared/` | code every plug-in links: the theme, the controls, the host-keys hand-back, the deterministic math |
| `tools/` | the hosts for the reference plug-ins, the editor snapshot tool and the host test |
| `tests/regress/` | the render regression's cases and goldens |
| `scripts/` | the check scripts, the version and release scripts, the render regression |
| `packaging/` | the installers; `plugins.txt` lists the plug-ins |
| `local.yaml` | every machine path (git-ignored; `local.example.yaml` lists the keys) |

## Rules for a change

- Work on `feature/<slug>`, `fix/<slug>`, `chore/<slug>` or `release/x.y.z`; `main` is the only long-lived branch. A
  branch is squash-merged and deleted on both sides at merge.
- A commit has one imperative subject line of at most 72 characters and an optional body with what and why; no
  quotations, attributions, absolute paths or session names. Commits carry the no-reply identity that
  `scripts/bump_version.py` states.
- Measure before changing the sound: the reference's own render is the truth, and a DSP change is nulled against it
  and recorded. A case worse by more than 0.5 dB blocks the change.
- DSP code is plain C++ under `Source/dsp/` with every constant in `Tuning.h`; parameters are read once per block;
  the audio thread never allocates, locks or posts a message.
- Colours live only in the theme headers; every component takes no keyboard focus; the editor is drawn by our own
  code from measured geometry and colours.
- Parameter ids, the plug-in codes, the bundle ids, the state and preset tags, the preset extension and the
  installer's AppId never change once a release has shipped (CONTRIBUTING.md, Frozen identity).
- Versions are written by `scripts/bump_version.py`, never by hand. Documents follow the style of STANDARDS.md:
  the reference voice, no logs or dated notes, no em or en dashes.
- A deviation from STANDARDS.md is one sentence in CONTRIBUTING.md under "Deviations from the standard".

## Never

- Never write an absolute path, a user name or a machine name into a tracked file: a machine path is a key of
  `local.yaml`.
- Never use a word of the reference's vendor or product in a control label, a preset name, a plug-in name, a code
  identifier or a file name. The reference is named only where a document says what a plug-in recreates;
  `.provenance` lists the words and `scripts/check_repo.py` checks them. A name that a library or a standard
  defines passes only through a `uses:` line of `.provenance`.
- Never commit anything taken from or written by the reference or a DAW: renders, captures, screenshots of the
  original, its presets, images, fonts, samples or code, a project file, a state file, a rendered WAV. Test signals
  come from a script. `reference/`, the renders and the result files stay git-ignored.
- Never ship art fitted or traced from the original's pixels: measure, then redraw.
- Never push, tag, publish a release, change the repository's visibility or enable a workflow without the owner's
  approval, given for that action. A private repository runs no workflow.
- Never install or download a tool or a dependency without the owner's approval, and never silence a warning with a
  global flag.

## The procedure

The working procedure for recreating a plug-in (the stages from the first measurement to a release, with their
gates) is the owner's and is not part of this repository. This repository holds what
a contributor needs: the two documents named above and each plug-in's README, RESEARCH, MEASUREMENTS and NULLTESTS.
