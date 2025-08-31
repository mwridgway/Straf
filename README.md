# Straf: Want jy moenie vloekie

A program that monitors mic audio on the computer to detect words that are expletives and penalises the user
when one is detected.  

## Getting Started

Run the app from the command line, or install it as a privileged Windows service.

## Build (Windows)

Prerequisites:
- Windows 11
- Visual Studio 2022 (Desktop development with C++) or MSVC Build Tools
- CMake 3.24+

Dependencies:
- nlohmann-json for config parsing (required)

### Default build (quick start)

If you just want a standard Debug build with MSVC:

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

Artifacts: `build/Debug/StrafAgent.exe`.

### Build and Run with Vosk STT (Windows)

1. Download and extract the Vosk SDK (prebuilt binaries recommended) to e.g. `C:\tools\vosk`.
   - Ensure you have:
     - `C:\tools\vosk\vosk_api.h`
     - `C:\tools\vosk\libvosk.lib`
     - `C:\tools\vosk\libvosk.dll` (not vosk.dll)
     - `C:\tools\vosk\libgcc_s_seh-1.dll`
     - `C:\tools\vosk\libstdc++-6.dll`
     - `C:\tools\vosk\libwinpthread-1.dll`

2. Download a Vosk model (e.g. English small model) and extract to e.g. `C:\models\vosk-model-small-en-us-0.15`.
   - Official models: https://alphacephei.com/vosk/models

3. Set environment variables for build and runtime:
   ```powershell
   $env:VOSK_INCLUDE_DIR = 'C:\tools\vosk'
   $env:VOSK_LIBRARY     = 'C:\tools\vosk\libvosk.lib'
   $env:PATH             = "C:\tools\vosk;$env:PATH"  # For libvosk.dll and dependencies
   $env:STRAF_VOSK_MODEL = 'C:\models\vosk-model-small-en-us-0.15'
   ```

4. Configure and build:
   ```powershell
   cmake -S . -B build
   cmake --build build --config Debug
   ```

5. Run with Vosk backend:
   ```powershell
   $env:STRAF_STT = 'vosk'
   $env:STRAF_AUDIO_SOURCE = 'wasapi'   # recommended for real mic capture
   ./build/Debug/StrafAgent.exe
   ```

6. To exit, use the tray icon's "Exit" menu or close from Task Manager if needed.

**Note:** If you see runtime errors about missing DLLs, ensure `libvosk.dll`, `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, and `libwinpthread-1.dll` are on your `PATH` or copied next to `StrafAgent.exe`.

Artifacts: `build/Debug/StrafAgent.exe`

### Vosk Troubleshooting
- Build error: `Cannot open include file: 'vosk_api.h'`
	- Set `VOSK_INCLUDE_DIR` to the folder containing `vosk_api.h`.
- Link error: `unresolved external symbol` or `cannot open file 'vosk.lib'`
	- Set `VOSK_LIBRARY` to the full path to `libvosk.lib`.
- Runtime error: `The code execution cannot proceed because libvosk.dll was not found`
	- Add the SDK folder to `PATH`, or copy `libvosk.dll` and dependencies next to `StrafAgent.exe`.
- No words recognized or very slow
	- Use the small model first, confirm audio is flowing, and verify `STRAF_VOSK_MODEL` points to the model folder (not a file).
- Constrained vocabulary
	- The app passes your configured words as a grammar to Vosk when available, improving accuracy/latency. Clear your word list to allow open dictation.

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

On first run, a config file is created at `%AppData%\Straf\config.json` (copied from `config.sample.json` if missing). Edit it to set your words and penalty timings.

Quick run after a Debug build (from repo root):

```powershell
./build/Debug/StrafAgent.exe
```

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
