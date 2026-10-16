# Chan Sampler

A JUCE sampler plugin modelled on FL Studio's Channel Sampler, built from the
public FL Studio manual. Builds **VST3** and **Standalone** on Windows (for FL
Studio, Reaper and others) and also **AU** on macOS, from the same code.

See [docs/EVIDENCE.md](docs/EVIDENCE.md) for how each feature maps to the
documentation, which parts are design guesses, and what remains to verify.

## Build (Windows)

Requirements: Visual Studio 2022 or 2026 (or Build Tools) with "Desktop
development with C++", CMake ≥ 3.22, and Git. JUCE is downloaded automatically.

```
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

(Use `"Visual Studio 17 2022"` with VS 2022.) Engine tests:
`build/ChanSamplerTests_artefacts/Release/ChanSamplerTests.exe`

Outputs:
- `build/ChanSampler_artefacts/Release/VST3/Chan Sampler.vst3`: copy to `C:\Program Files\Common Files\VST3\`.
  A `.vst3` plugin is a folder (a "bundle"); copy the whole folder. Writing
  to Program Files needs an administrator prompt.
- `build/ChanSampler_artefacts/Release/Standalone/Chan Sampler.exe`

Then rescan plugins in FL Studio (Options → Manage plugins) or Reaper
(Preferences → Plug-ins → VST → Re-scan).

## Build (macOS, AU)

Requirements: Xcode (or its command-line tools), CMake ≥ 3.22, Git.

```
cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
cmake --build build --config Release --target ChanSampler_AU ChanSampler_VST3 ChanSampler_Standalone
```

Copy `Chan Sampler.component` to `~/Library/Audio/Plug-Ins/Components/` and
`Chan Sampler.vst3` to `~/Library/Audio/Plug-Ins/VST3/`.

## Releases (downloads for Windows and macOS)

GitHub builds both platforms (`.github/workflows/release.yml`):

- **Test build:** GitHub → Actions → *Build and release* → *Run workflow*. The
  zips appear under the run's *Artifacts*.
- **Release:** tag a version and push the tag; the zips are attached to a new
  GitHub Release:

  ```
  git tag v0.1.0
  git push origin v0.1.0
  ```

Each zip contains the plugins, the standalone app and an `INSTALL.txt`
(drag-and-drop install; on macOS one Terminal command clears the download
quarantine, since the plugins are not notarized by Apple).

## Working on another computer

```
git clone https://github.com/MaymiPR/ChanSampler.git
```

Then build as above. Before switching computers, commit and `git push`; on the
other one, `git pull`. Generated files (`build/`, the FL test renders in
`nulltest/renders*`) are not in the repository: rebuild, and re-render the null
tests with FL Studio when needed.

## Using it

- Drop an audio file (WAV, AIFF, FLAC, Ogg, MP3) on the window or click **Load**. Click the waveform to preview.
- Drop or **Load** an FL Studio Sampler preset (`.fst`, from *Save channel state as…*) to get its sample and every setting. FL's own folders in the sample path are found automatically; otherwise the sample is looked for next to the preset.
- Drag the **S** marker (start offset) and the **L** markers (loop, when *Use Loop* is on). Shift-drag moves the loop as a whole.
- **Precomputed** settings rebuild the sample data. They are not automatable, matching FL.
- Without the volume envelope enabled, notes play to the end of the sample, as in FL.
