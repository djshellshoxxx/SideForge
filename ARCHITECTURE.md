# SideForge — Architecture

## Overview

SideForge is a Windows-only (x64) VST3 / Standalone plugin built with JUCE 8 and
CMake. It combines four processing concepts behind one model:

1. **Compressor engine** — classic feed-forward sidechain compression.
2. **Curve engine** — a drawable, tempo-syncable rhythmic gain envelope.
3. **Spectral ducking engine** — STFT collision analysis with per-bin attenuation.
4. **Multiband / channel matrix** — 1 or 3 bands, stereo-linked / L-R / M-S.

and one of two top-level **routings**:

- **Full Range** — the four engines above over the whole spectrum. This is the
  original signal path, and it is untouched by everything below.
- **Band Matrix** — the destination split into 1-4 Linkwitz-Riley bands, each
  with its own processor, timing and detector source, latency-aligned and
  recombined.

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

## The Band Matrix

`BandMatrix` owns two `LinkwitzRileyCrossover` networks - one for the
destination, one for the sidechain - four `BandProcessor`s, and four `DelayLine`
aligners.

```
wet ──> destination crossover ──> band[0..n) ──> BandProcessor ──> DelayLine ──┐
                                      ▲                                        ├──> sum
sc  ──> sidechain crossover ──────────┘   (detector source select)            ─┘
```

Three things are worth knowing about how it is put together.

**The crossover is a cascade with allpass correction.** Splitting four ways means
each band passes through a different number of crossovers, so the bands arrive
with different phase. Every band that skips a split is run through an allpass at
that split's frequency instead, which is what makes the four bands sum back flat.
`CrossoverTests` measures the reconstruction rather than trusting the topology.

**Crossover frequencies glide in the log domain, rate-limited.** A one-pole on
the frequency looked right and was not: on a large jump it moved the cutoff by a
factor of 1.38 in the first block, which is audible. The limit is now an explicit
12 octaves per second, and `maxRatioPerBlock()` states the guaranteed bound so the
test can assert the real contract instead of a tolerance someone picked.

**Latency is aligned inside the matrix, not outside it.** Each `BandProcessor`
reports `lookahead + (Spectral ? one STFT frame : 0)`, the matrix pads every band
up to the deepest, and the processor reports that total. Summing bands that are
not aligned would comb-filter the recombination, and it would do it silently.

Detector routing is per band and resolved in `detectorSourceFor()`: the full
sidechain, the matching band of the sidechain, the full destination, or the
matching band of the destination - each optionally through that band's own
detector high-pass and low-pass.

Solo and mute are resolved at recombination, not in the band processors, so
`heard = anySolo ? solo[b] : !mute[b]` is evaluated once per band per block and a
soloed band still runs its processor (and so its meters and envelopes stay live
rather than jumping when you un-solo).

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
Full Range   latency = lookaheadSamples + spectralLatencySamples
Band Matrix  latency = max over bands of (band lookahead + band spectral)
```

`LatencyManager::updateExplicit` is how the Band Matrix path reports its figure:
the matrix has already worked out its own alignment, so it hands over a total
rather than the two components.

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
  `LinkwitzRileyCrossover.h`, `BandProcessor.*` and `BandMatrix.*` are the Band
  Matrix; they depend on the existing engines rather than duplicating them, so a
  band in Compressor mode is the same `CompressorEngine` the full-range path uses.
- `Source/Model/` — curve model, preset/state serialisation, factory presets.
- `Source/UI/` — all rendering; reads snapshots produced by the audio thread.
  The lower panels live in one `Panels.cpp` rather than a `Panels/`
  directory: each is a constructor listing its controls plus a short relevance
  rule, and a pair of ten-line files each would be harder to read, not easier.
  `BandMatrixPanel` is the exception and has its own file: it is the only panel
  that holds four bands' worth of controls and shows one of them.
- `Source/Utilities/` — lock-free primitives and smoothing helpers.
- `Tests/` — a console runner that links the same core and UI sources as the plugin.
  `Tests/DSP/CrossFeatureTests.cpp` is the all-pairs sweep: for every pair of
  discrete settings and every combination of values that pair can take, one run
  with those two pinned and everything else randomised.

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
| 16 | Band Matrix is a **routing switch**, not a replacement | Full Range had to stay bit-for-bit identical, and a test asserts it: moving all 107 matrix parameters leaves the Full Range output sample-identical. Users with existing sessions lose nothing. |
| 17 | Band solo / mute / Listen are non-automatable | A soloed band recalled from an automation lane six months later is a bug. They are still saved with the session, like the editor size. |
| 18 | The Band Matrix display runs its own FFT on the **message thread** | The matrix does not compute a spectrum as part of processing the way the spectral stage does, so the display would have had to add one to `processBlock`. Instead the audio thread pushes raw samples into `AnalysisBus::RawTap` (three stores per sample) and the GUI transforms them, only while that page is on screen. |
| 19 | Per-band enum is `BandProcessorMode`, not `BandMode` | `BandMode` already meant "1 band or 3 bands" in the multiband stage. Two different meanings for one name in one namespace is how a wrong-enum bug gets written. |
| 20 | Monitoring modes suspend Mix and auto makeup | An audition replaces the signal rather than processing it, so blending it back with the dry track or scaling it by compressor makeup is never what was asked for. Found by the cross-feature sweep as a +23 dB sidechain audition. |
