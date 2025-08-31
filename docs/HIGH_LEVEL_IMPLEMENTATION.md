# High-Level Implementation Plan

This document outlines how to implement the app described in `README.md`: listen to microphone audio, detect expletives, and apply on-screen penalties (GTA-style overlay). It’s intentionally concise so you can adjust before we start coding.

## Goals & Scope
- Detect a user-defined list of words from live mic audio (offline, low-latency).
- Enforce penalties with a visible, above-all overlay; queue up to 5 active penalties; each has its own cooldown.
- Run as a user app (CLI/background). Optional service-based startup support on Windows 11.

## Architecture Overview
- Language: C++17/20 (Windows 11).
- Packaging: CMake project; single executable for user session; optional Windows Service + User Agent.
- Processes:
  - User Agent (primary): captures mic, detects words, renders overlay, manages penalties, reads config.
  - Optional Service: starts with OS and spawns/monitors the User Agent in the interactive session. Service does not draw UI.
- IPC: Named pipe or Windows App Service (only if Service is used) to forward start/stop commands or status.

## Core Modules
1) Audio Capture (WASAPI)
- Capture default microphone with loop: 16 kHz mono PCM (downsample if needed).
- Voice Activity Detection (VAD) to skip silence and reduce CPU.

2) Speech/Keyword Detection
- Small vocabulary/grammar from user-provided word list.
- Two offline options (pluggable):
  - Microsoft Speech SDK (C++): grammar-constrained recognition; offline models required.
  - Vosk (C++): offline ASR; configure grammar for listed words to improve accuracy/latency.
- Post-processing: normalize words (case, punctuation), exact match to configured list.

3) Profanity Matcher
- Holds normalized set of trigger words.
- Optional simple stemming/variants (e.g., plural forms) as a second pass.

4) Penalty Manager
- State machine/queue: up to 5 penalties enqueued when triggers occur during active penalties.
- Each penalty: duration + cooldown. When duration ends, cooldown must elapse before another identical penalty can be applied.
- Ticks on a high-resolution timer; persists minimal state for resilience (optional).

5) Overlay Renderer
- Always-on-top, borderless, click-through layered window: WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT.
- Rendering via Direct2D or Direct3D11 with alpha blending for GTA-style visuals.
- Initial scope: reliable over desktop and borderless-fullscreen apps.
- Stretch goal (later): exclusive fullscreen/game overlay via DX hook (out of MVP).

6) Config & Storage
- Location: %AppData%/Straf/config.json.
- Contents: word list, penalty visuals (theme), durations/cooldowns, queue limit, device IDs.
- Hot reload: watch file for changes to update vocab and visuals on the fly.

## Data Flow
- WASAPI frames → VAD → ASR/KWS (grammar = word list) → normalized tokens → Profanity Matcher → Penalty Manager → Overlay Renderer.

## CLI & Service
- CLI: run, install-service, uninstall-service, start, stop, open-config.
- Service (optional): installs with admin rights, launches User Agent in active session using a helper (no UI in session 0).

## Performance & Reliability
- Real-time audio pipeline threads at ABOVE_NORMAL priority.
- ASR/KWS configured for low-latency (short window, partial results).
- Backpressure/overflow safeguards: drop oldest frames if CPU spikes.

## Privacy & Permissions
- Mic consent via Windows privacy settings; prompt user instructions if denied.
- All processing local/offline; no recordings saved unless explicitly enabled in config.

## Edge Cases
- No mic / access denied → surface non-intrusive toast/overlay message; retry policy.
- Ambiguous detections → thresholding; require confidence > configurable cutoff.
- Fullscreen exclusive apps → documented limitation for MVP.

## Tech Choices (proposed)
- Build: CMake.
- Audio: WASAPI (IMMDeviceEnumerator, IAudioClient3, IAudioCaptureClient).
- Rendering: Direct3D11 (future‑proof for effects), compliant with CS2, VAC, and Trusted Mode. No process injection or Present hooks.
- ASR/KWS: Prefer Vosk (C++) for fully offline; abstract behind `IDetector` interface so we can swap to Microsoft Speech SDK.

## MVP Definition
- Run as a user app.
- Load word list from config, detect and display overlay penalty with queueing/cooldown.
- Works over desktop and borderless games.

## Open Decisions (to confirm)
- ASR engine choice (Vosk vs Microsoft Speech SDK).
  - Decision: Use Vosk (free, offline). Keep swappable via `IDetector`.
- Default durations/cooldown values and queue limit (README says up to 5; confirm durations).
  - Decision: Make configurable. Ship sensible defaults (e.g., duration 10s, cooldown 60s, queue limit 5). “GTA‑style” refers to visuals, not timings.

## Rendering technical note
Use D3D11 (+ DirectComposition for window presentation) without injection or hooks.

- CS2 uses D3D11 on Windows. A D3D11 overlay aligns with the game’s swap chain and minimizes CPU overhead.
- For text, use DirectWrite/Direct2D on a shared D3D11 surface; present via D3D11. This keeps D3D11 performance with D2D ergonomics.
- Avoid Present hooks or DLL injection; these can violate VAC/Trusted Mode policies. Use a separate, topmost, click‑through, layered window presented via DirectComposition. Prefer borderless‑windowed mode for games.
- Anti‑cheat posture: Stay out of game processes. Consider a “game‑safe mode” that disables overlay for known anti‑cheat titles. No guarantees for ESEA or FACEIT; document limitations.


## Next Steps
- Confirm open decisions and tweak scope.
- Generate project scaffold (CMake, modules, skeleton interfaces, config loader).
- Add a tiny console demo that prints detected words before building overlay.
