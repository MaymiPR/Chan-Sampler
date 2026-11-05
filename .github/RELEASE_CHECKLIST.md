Release checklist for ChanSampler {{VERSION}} (branch `{{BRANCH}}`). One list for every release: tick each item
as it happens and close this issue when the release is live. The flow in short is in CONTRIBUTING.md under
"Making a release"; the rules are in STANDARDS.md.

## Before the version bump

- [ ] The local check passes with zero warnings (`scripts/check.ps1` or `scripts/check.sh`); on a public repository
  Lint and CI are green on `main` with zero warnings on every platform (`gh run list --branch main --limit 5`).
- [ ] Since the last DSP change: `ctest`, pluginval at strictness 10 on every VST3, FocusCheck, the 44.1 / 96 kHz null
  spot check, VST3 equals renderer (`nulltest.py --vst3`), the render regression with goldens updated in the same
  commit, tests with a clean settings file.
- [ ] The four documents of every plug-in are current; NULLTESTS.md is regenerated from the result files (`nulltest.py --report`); the README's How close rows equal it; no number typed by hand.
- [ ] Screenshots regenerated with the Snapshot tool where the editor changed; `docs/screenshots/hero.png` rebuilt.
- [ ] `packaging/plugins.txt` lists every plug-in that ships; `packaging/THIRD-PARTY-NOTICES.txt` names every dependency and font.
- [ ] A saved-state fixture exists for this version if a parameter or its reading changed.
- [ ] `CHANGELOG.md` has everything user-facing under `## Unreleased`, with before and after numbers in dB for sound changes; `### Highlights` for a new plug-in.
- [ ] The provenance and public gates pass: `python scripts/check_repo.py --release` (the Provenance section in every
  MEASUREMENTS.md, `## Unreleased` first in the CHANGELOG, no absolute path, old name, attribution form or dated
  entry), and the `.provenance` file names the vendor words and every allowed file with its reason.
- [ ] First release of a plug-in only: the name review (the vendor's catalogue and the trademark registers searched for the name and its stem), approval recorded with its date.
- [ ] The Windows test installer builds from PowerShell and installs and uninstalls cleanly: `pwsh
  packaging\make_iss_include.ps1 build`, then `ISCC /DTestRoot=<scratch> /DAppVersion={{VERSION}}
  packaging\ChanSampler.iss`.
- [ ] Hygiene: no real email, no em or en dash, no `TODO`, no vendor internals, no version string where a plain product name is wanted; the VST3 junction still points at the build.

## The bump and the tag

- [ ] `python scripts/bump_version.py {{VERSION}} --dry`, then without `--dry`: CMakeLists.txt and CHANGELOG.md updated, commit `Release {{VERSION}}`, `main` pushed.
- [ ] Packaging changed since the last release, or a first release: Build and release run by hand on `main` (artifacts
  only), the installers and the two signing state files checked, the Windows installer installed and uninstalled.
- [ ] Approval for the tag recorded with its date.
- [ ] `python scripts/bump_version.py {{VERSION}} --tag`, then `git push origin v{{VERSION}}` (a `-rc.N` tag first for a pre-release).
- [ ] The release run is green on all four jobs with zero warnings; the log says how each file was signed. A run with
  a warning is a failed release: fix on `main`, re-run, and push the tag again only then.

## After

- [ ] The release page is not a draft; the title is `ChanSampler {{VERSION}}`; the files are
  `ChanSampler-{{VERSION}}-Windows-Setup.exe`, `ChanSampler-{{VERSION}}-macOS.pkg`, `ChanSampler-{{VERSION}}-Linux.tar.gz`
  and `SHA256SUMS.txt`; the notes are the CHANGELOG section plus the footer with the signing state.
- [ ] One file downloaded on another machine; its digest equals its line in `SHA256SUMS.txt`; `gh attestation verify <file> --repo MaymiPR/Chan-Sampler` passes on a public repository.
- [ ] The README's badges show the new version.
- [ ] First release only: the social preview uploaded and the repository settings applied (description, website,
  topics, licence, squash merge, delete head branches on merge, the rulesets Protect main and Protect release tags,
  secret scanning, Dependabot alerts, private vulnerability reporting, GitHub-owned actions only).
- [ ] `release/{{VERSION}}` deleted; this issue closed.
