# Building Chan Sampler

JUCE and [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions)
are downloaded automatically when you configure.

## Requirements

- CMake 3.22 or later, and Git
- Windows: Visual Studio 17 or 18 with "Desktop development with C++"
- macOS: Xcode
- Linux: GCC or Clang, plus these packages (Ubuntu / Debian names):

  ```bash
  sudo apt install libasound2-dev libjack-jackd2-dev libfreetype6-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libglu1-mesa-dev
  ```

`-DCHANSAMPLER_CLAP=OFF` leaves the CLAP version out (and skips its download).

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

Building without `--target` also makes the engine tests and the developer
tools; [CONTRIBUTING.md](../CONTRIBUTING.md) describes them, along with the
null tests against FL Studio and how releases are made.
