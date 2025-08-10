# Project Title

A program that monitors mic audio on the computer to detect words that are expletives and penalises the user
when one is detected.  

## Getting Started

Run the app from the command line, or install it as a privileged Windows service.

## Build (Windows)

Prerequisites:
- Windows 11
- Visual Studio 2022 (Desktop development with C++) or MSVC Build Tools
- CMake 3.24+
- Git (only if using vcpkg)

Dependencies:
- nlohmann-json (header-only) for config parsing.
	- Recommended (via vcpkg)
	- Alternate: drop single header `nlohmann/json.hpp` into your include path
	- Note: The project also has a small built-in fallback parser for basic configs, so it will build without nlohmann-json, but nlohmann-json is preferred.

### Option A: Build with vcpkg (recommended)

1) Install vcpkg (one time):

```powershell
# Choose an install folder (example):
cd $env:USERPROFILE ; git clone https://github.com/microsoft/vcpkg.git ; cd vcpkg
./bootstrap-vcpkg.bat
./vcpkg integrate install
```

2) Install nlohmann-json:

```powershell
# For 64-bit builds
./vcpkg install nlohmann-json:x64-windows
```

3) Configure and build (from repo root):

```powershell
# Ensure VCPKG_ROOT points to your vcpkg folder if not integrated automatically
$env:VCPKG_ROOT = "$HOME/vcpkg"
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Debug
```

### Option B: Build without vcpkg

You have two choices:
- Place `nlohmann/json.hpp` somewhere in your compiler's include path (e.g., `third_party/nlohmann/json.hpp`). The CMake setup will discover it automatically.
- Do nothing and rely on the project’s built-in minimal JSON parser (sufficient for the provided sample config). You can add nlohmann-json later without code changes.

Build commands:

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

Artifacts will be under `build/Debug/StrafAgent.exe` (for Debug config).

### Enable clang-tidy (optional)

If you have clang-tidy installed, you can run it automatically during builds.

- One-off configure flag:

```powershell
cmake -S . -B build -DSTRAF_ENABLE_CLANG_TIDY=ON
cmake --build build --config Debug
```

- Or use the provided CMake Preset (separate build dir `build-tidy`):

```powershell
cmake --preset vs2022-debug-tidy
cmake --build --preset vs2022-debug-tidy --config Debug
```

## Running

On first run, a config file is created at `%AppData%\Straf\config.json` (copied from `config.sample.json` if missing). Edit it to set your words, penalty timings, and logging level.

## Features

- The user can provide a list of words to detect that will result in a penalty.
- Penalties are enabled when a word in the list is detected. Each penalty has a cooling off period.
- If another word is detected while a penalty is active, another penalty is added that must be served after any current penalty is being served — up to a limit of 5 active penalties.
- Penalties are in the form of a screen overlay visible above all other user interface elements, even during a game or other full screen application. This should be in the style of GTA wanted levels visually.

## Technical implementation

- The app is written in C++ for performance.
- Compatible with Windows 11.
- Can run in the background as a service (future work).

## Troubleshooting

- If CMake warns that `nlohmann_json` is not found, either:
	- Install it via vcpkg (Option A), or
	- Place `nlohmann/json.hpp` in your include path (Option B).
- Ensure you are using the `x64` MSVC toolchain and have the Windows 11 SDK installed.

## License

This project is licensed under the MIT License - see the LICENSE file for details.
