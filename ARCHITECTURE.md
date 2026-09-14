# SideForge — Architecture

## Overview
SideForge is a Windows-only (x64) VST3/Standalone plugin built with JUCE 8 + CMake.
It combines four processing concepts behind one unified model:

1. **Compressor engine** — classic feed-forward sidechain compression.
2. **Curve engine** — a drawable, tempo-syncable rhythmic gain envelope (LFOTool-style).
3. **Spectral ducking engine** — STFT collision analysis + per-bin attenuation.
4. **Multiband / channel matrix** — 1 or 3 bands, stereo-linked / L-R / M-S.

## Unifying idea: everything is a gain signal
The compressor and curve engines both emit a **broadband gain multiplier** in `[0, 1]`
per sample, per channel. They are combined according to the active *Mode*, then handed
to the multiband/channel stage which applies them per band with per-band depth and
ballistics. The spectral engine is a separate STFT stage that emits a **per-bin gain**
and runs after the broadband stage. This keeps every engine independently testable and
lets "Hybrid" mode be a simple, well-defined combination rather than a special case.

## Signal flow (per block)

```
main in ──┬─────────────────────────── dry delay line ───────────────┐ (mix / bypass)
          │                                                          │
          │   sidechain in ──> SidechainDetector (HP/LP + detector)   │
          │                        │                                 │
          │                        ├──> CompressorEngine ──┐         │
          │                        ├──> TriggerEngine ─────┤         │
          │                        │        └──> CurveEngine┤         │
          │                        │                       ▼         │
          │                        │              broadband gain g[n] │
          ├──> lookahead delay ──> MultibandProcessor (1 or 3 bands)  │
          │                        applies g[n] per band, per channel │
          │                                  │                        │
          │                                  ▼                        │
          └────────────────────> SpectralDucker (STFT, collision map) │
                                             │                        │
                                             ▼                        ▼
                                          wet ────────> mix ───> output gain ──> out
```

## Threading / real-time rules
- `processBlock` never allocates, locks, logs, or touches the GUI.
- Everything sized in `prepareToPlay`; worst-case buffers are preallocated there.
- Structural changes that need reallocation (FFT size, band count) are requested by the
  message thread into an atomic "pending config" and applied by the audio thread only when
  it can swap into a **preallocated** alternative — see `LatencyManager` + `SpectralDucker`.
- The GUI reads audio state only through `Utilities/LockFreeFifo.h` (SPSC ring) and atomics.

## Latency
`LatencyManager` owns the single source of truth for reported latency:
`latency = lookaheadSamples + spectralLatencySamples`. The dry path is delayed by the same
amount so mix/bypass stay phase-coherent. Latency changes are pushed to the host from the
message thread (`AudioProcessor::setLatencySamples`) after the audio thread flags a change.

## Quality modes
| Mode     | Lookahead | STFT size | Hop | Notes |
|----------|-----------|-----------|-----|-------|
| Live     | 0 ms      | 512       | 1/4 | spectral stage only if mode requires it |
| Studio   | 3 ms      | 1024      | 1/4 | default |
| Spectral | 5 ms      | 2048      | 1/4 | higher frequency resolution |
| Render   | 10 ms     | 4096      | 1/8 | offline / bounce |

## State
`APVTS` holds every automatable parameter. Non-automatable structured data (the curve
node list, A/B slots, editor size) lives in a sibling `ValueTree` child. The whole tree is
wrapped in a versioned root (`SideForgeState` with `version` attribute) so older sessions
can be migrated forward in `PresetState::migrate`.

## Source map
- `Source/Parameters/` — parameter IDs, ranges, APVTS layout, macro mapping.
- `Source/DSP/` — all audio processing; no JUCE GUI includes.
- `Source/Model/` — curve data model and preset/state serialization; no audio, no GUI.
- `Source/UI/` — all rendering; reads only snapshots produced by the audio thread.
- `Source/Utilities/` — lock-free primitives and smoothing helpers.
- `Tests/` — console test runner; links the same core sources as the plugin.
