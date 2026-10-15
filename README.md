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

```
cmake -B build -G Xcode
cmake --build build --config Release
```

Copy `Chan Sampler.component` to `~/Library/Audio/Plug-Ins/Components/`.

## Using it

- Drop an audio file on the window or click **Load**. Click the waveform to preview.
- Drag the **S** marker (start offset) and the **L** markers (loop, when *Use Loop* is on). Shift-drag moves the loop as a whole.
- **Precomputed** settings rebuild the sample data. They are not automatable, matching FL.
- Without the volume envelope enabled, notes play to the end of the sample, as in FL.
