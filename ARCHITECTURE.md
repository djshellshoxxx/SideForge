# SideForge — Architecture

## Overview

SideForge is a Windows-only (x64) VST3 / Standalone plugin built with JUCE 8 and
CMake. It combines four processing concepts behind one model:

1. **Compressor engine** — classic feed-forward sidechain compression.
2. **Curve engine** — a drawable, tempo-syncable rhythmic gain envelope.
3. **Spectral ducking engine** — STFT collision analysis with per-bin attenuation.
4. **Multiband / channel matrix** — 1 or 3 bands, stereo-linked / L-R / M-S.

## The unifying idea: everything is a gain signal

The compressor and the curve engines both emit a **broadband gain multiplier** in
`[0, 1]`, per sample and per channel. They are combined according to the active
Mode and handed to the multiband stage, which is the only place that touches the
audio. The spectral engine is a separate STFT stage emitting a **per-bin gain**
and runs after the broadband stage.

Three things follow from that, and they are the reason for the design:

- "Hybrid" mode is multiplication, not a special case;
- each engine is testable without any audio plumbing around it;
- band depth, channel linking and mid/side all apply to *one* signal, so they
  behave identically whichever engine produced it.

## Signal flow (per block)

```
main in ──┬──> input gain ──┬──> dry copy ──────> dry delay (= total latency) ──┐
          │                 │                                                  │
          │                 └──> wet copy ──> lookahead delay ──┐               │
          │                                                     │               │
          │  sidechain in ──> SidechainDetector (HP/LP, gain,    │               │
          │                     peak/RMS/hybrid, stereo link)    │               │
          │                        │                             │               │
          │                        ├──> CompressorEngine ──┐     │               │
          │                        │                       │     │               │
          │                        └──> TriggerEngine ─────┤     │               │
          │                               (own filter)     │     │               │
          │                                   └──> CurveEngine   │               │
          │                                            │         │               │
          │                                   broadband gain g[n]│               │
          │                                            │         ▼               │
          │                                            └──> MultibandProcessor   │
          │                                                 (1 or 3 bands, per   │
          │                                                  band depth + timing,│
          │                                                  stereo / LR / MS,   │
          │                                                  mono-low)           │
          │                                                        │             │
          │                        filtered sidechain ──> sc delay │             │
          │                                                   └──> SpectralDucker│
          │                                                        (STFT)        │
          │                                                          │           │
          │                                          monitor / audition select   │
          │                                                          │           ▼
          └──────────────────────────────────────────────> mix ──> output gain ──> bypass
                                                                                 crossfade
                                                                                    │
                                                                                    ▼
                                                                                   out
```

## Threading and real-time rules

- `processBlock` never allocates, locks, logs, reads a file, resizes a container
  or calls into the GUI.
- Everything is sized in `prepareToPlay`, to the worst case rather than the
  current setting: the spectral stage allocates for a 4096-point FFT whatever
  quality mode is selected, so switching mode later is an index change.
- Hosts that hand over a block larger than they promised are handled by slicing
  in `processBlock`, so no buffer is ever resized on the audio thread.
- `setLatencySamples` talks to the host, so the audio thread only raises a flag
  in `LatencyManager` and an `AsyncUpdater` publishes the value.
- MIDI CC values are recorded into atomics on the audio thread and applied to
  parameters by a 30 Hz timer on the message thread — `setValueNotifyingHost` is
  never called from `processBlock`.
- The GUI reads audio state only through `Utilities/LockFreeFifo.h` (an SPSC ring
  and a triple buffer) and through plain atomics.

## Latency

`LatencyManager` owns the single source of truth:

```
latency = lookaheadSamples + spectralLatencySamples
```

The dry path is delayed by the same amount, so mix and bypass stay
phase-coherent and bypass is sample-exact.

The STFT's latency is a **full FFT frame**, not a frame minus a hop. A sample is
only finished once every analysis window overlapping it has been transformed and
overlap-added, and the last such window begins on that sample and runs a whole
window past it. `spectral_reportedLatencyMatchesTheActualDelay` pins this down
with an impulse rather than trusting the arithmetic.

The spectral stage reports latency only while it is in the path, so leaving
Spectral or Hybrid mode returns those samples to the host.

## Quality modes

| Mode | Lookahead cap | STFT | Hop | Notes |
|---|---|---|---|---|
| Live | 0 ms | — | — | broadband engines only; zero latency |
| Studio | 5 ms | 1024 | 1/4 | default |
| Spectral | 10 ms | 2048 | 1/4 | higher frequency resolution |
| Render | 20 ms | 4096 | 1/8 | offline / bounce |

## The collision map

The spectral stage does not invert the sidechain spectrum. Per bin it computes:

```
share = sideMag / (sideMag + mainMag + eps)     // the sidechain's share of the bin
gate  = side² / (side² + floor²)                // is the sidechain really there
c     = (share * gate * bandWeight * tilt) ^ (1 + 3 * preserveBody)
```

`share` is high only where the two sources actually compete; `gate` stops a
near-silent sidechain from carving anything; the exponent is what **Preserve
Body** controls, so raising it leaves decisive collisions alone and relaxes
marginal ones instead of thinning the track everywhere.

## State

`APVTS` holds every automatable parameter. Non-automatable structured data (the
curve node list, editor size, MIDI mappings) lives in sibling `ValueTree`
children. The whole tree is wrapped in a versioned root (`SideForgeState` with a
`version` attribute) so older sessions can be migrated forward in
`PresetState::migrate`. A bare APVTS tree is still accepted on load, and any
non-finite parameter value is dropped rather than restored.

## UI

The editor is thin: it owns the look and feel and the saved window size, and
hands everything else to `MainView`.

- **One timer** drives every animated element, at 45 Hz. Relevance, MIDI badges
  and status text refresh at a quarter of that, because they change rarely.
- No UI component reads a DSP object's working state. The visualiser drains
  `AnalysisBus` (an SPSC ring of decimated columns the audio thread fills), the
  spectrum view reads a `SpectralSnapshot` from a triple buffer, and the meters
  read atomics.
- The spectral display costs nothing extra: the FFT it shows was already computed
  as part of processing.
- `ParameterMenu` gives every control the same right-click menu — the host's own
  parameter commands where available, plus MIDI learn, type, reset, copy, paste.
- Controls the active engine does not use are disabled rather than hidden, so the
  layout never moves under the user's cursor.

## Source map

- `Source/Parameters/` — parameter IDs, ranges, APVTS layout, macro mapping, MIDI learn.
- `Source/DSP/` — all audio processing; no JUCE GUI includes.
- `Source/Model/` — curve model, preset/state serialisation, factory presets.
- `Source/UI/` — all rendering; reads snapshots produced by the audio thread.
  The seven lower panels live in one `Panels.cpp` rather than a `Panels/`
  directory: each is a constructor listing its controls plus a short relevance
  rule, and seven pairs of ten-line files would be harder to read, not easier.
- `Source/Utilities/` — lock-free primitives and smoothing helpers.
- `Tests/` — a console runner that links the same core and UI sources as the plugin.

## Architectural decisions

| # | Decision | Why |
|---|---|---|
| 1 | JUCE **8.0.15**, not 9.x | 9.x is very new; 8.0.15 is the last well-settled 8 release and every API used here is stable in it. |
| 2 | Core sources compiled into both the plugin and the test runner, not a shared static lib | JUCE module targets inject their sources into every consumer; a static lib that is also linked into the plugin risks duplicate symbols on MSVC. |
| 3 | The test runner links the *real* processor and editor | Lets the tests cover bus layouts, state recall, editor painting and parameter-to-control coverage rather than a stand-in. |
| 4 | Every engine emits a gain signal, not audio | See above. Makes Hybrid a defined combination and each engine independently testable. |
| 5 | Curve evaluation goes through a rendered lookup table published via a 3-slot exchange | The node list is a `std::vector` edited on the message thread; the audio thread must never walk it. Table lookup is also branch-free at sample rate. |
| 6 | Parameter IDs carry a version hint and live in one header | IDs are a public contract with host automation lanes; centralising them makes accidental renames obvious. |
| 7 | Curve segment types are `Curve` / `SCurve` / `Step` | "Straight", "ramp" and "curved" are all `Curve` at different tensions, so three types cover the four required behaviours without redundant UI. |
| 8 | State is a versioned wrapper around the APVTS tree | Allows non-parameter data in the same blob and gives `PresetState::migrate` somewhere to hook in. |
| 9 | Ballistics live in the engines, not in the detector | So that "Attack" on the compressor means attack of the gain reduction, which is what a mixing engineer expects. The detector is a fast magnitude only. |
| 10 | Curve Attack / Hold / Recover are **rate limits**, not filters | At the default of 0 ms the drawn curve plays back exactly; the controls shape it rather than blurring it. |
| 11 | One sidechain bus, behind a `TrigSource` switch | Multi-bus support is inconsistent across hosts. The switch is the extension point a future Sender plugin would use. |
| 12 | Lookahead and Quality are non-automatable | Both change reported latency; automating them would have the host renegotiating latency mid-bar. |
| 13 | MIDI learn is applied on a timer, not in `processBlock` | `setValueNotifyingHost` is not real-time safe. The audio thread only records controller values into atomics. |
| 14 | Macros are modulation over real parameters | Nothing in the UI is a dead end, and A/B and session recall stay exact. |
| 15 | Auto makeup is capped at +24 dB | Past that the user wants the output trim, not an automatic 40 dB boost from an extreme threshold/ratio pair. |
