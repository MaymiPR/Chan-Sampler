# Building Chan Sampler

JUCE and [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions)
are downloaded automatically when you configure.

## Requirements

- CMake 3.22 or later, and Git
- Windows: Visual Studio 17 or 18 with "Desktop development with C++"
- macOS: Xcode
- Linux: GCC or Clang, plus these packages (Ubuntu / Debian names):

  ```bash
  sudo apt install libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libxi-dev libglu1-mesa-dev mesa-common-dev
  ```

- Python 3 for the tests (`ctest` writes their test signal with `plugins/ChanSampler/nulltest/make_base_signals.py`,
  standard library only; set `CHANSAMPLER_PYTHON` to choose the interpreter).

| Option | Default | Effect |
|---|---|---|
| `CHANSAMPLER_CLAP` | `ON` | the CLAP version (and its download) |
| `CHANSAMPLER_TOOLS` | `ON` | the developer tools: `CsRender` and `CsRenderDev` (the null tests and the render regression), `ChanSamplerSnapshot` (editor screenshots) and the reference hosts `RefRender`, `ParamDump`, `RefCapture`, `FocusCheck` |
| `CHANSAMPLER_DEV_COPY` | `ON` | a copy of the VST3 at `build/plugins/Chan Sampler.vst3` after each build, the folder a DAW's plug-in junction points at |

## Windows (VST3, CLAP, Standalone)

```bash
cmake -B build -A x64
cmake --build build --config Release --target ChanSampler_VST3 ChanSampler_CLAP ChanSampler_Standalone
```

CMake picks the newest Visual Studio installed. To choose one, add its generator,
for example `-G "Visual Studio 17 2022"`.

Keep the build folder's path short (for example `C:\src\ChanSampler\build`).
JUCE's checkout holds deeply nested files, and under a long path Git stops
with "Filename too long" while configuring.

## macOS (universal AU, VST3, CLAP, Standalone)

```bash
cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
cmake --build build --config Release --target ChanSampler_AU ChanSampler_VST3 ChanSampler_CLAP ChanSampler_Standalone
```

## Linux (VST3, CLAP, Standalone)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ChanSampler_VST3 ChanSampler_CLAP ChanSampler_Standalone
```

## Output

The plugins and the app land in `build/ChanSampler_artefacts/Release/`
(`VST3/`, `CLAP/`, `AU/`, `Standalone/`). Copy the plugins to your plugin
folders, or run the standalone app from there.

## Tests

The target `ci_targets` builds what CI builds: the VST3, the three test programs, the renderer and, on Windows, the
snapshot tool. Then `ctest` runs every test:

```bash
cmake --build build --config Release --target ci_targets
ctest --test-dir build -C Release --output-on-failure
python scripts/render_regress.py --build build
```

On Linux the UI and host tests need a display (`xvfb-run -a ctest ...` without one). `scripts/check.ps1` (Windows) or
`scripts/check.sh` (Linux, macOS) runs the whole local check; [CONTRIBUTING.md](../CONTRIBUTING.md) describes the
tools, the null tests against FL Studio and how releases are made.
