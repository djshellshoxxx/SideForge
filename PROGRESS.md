# SideForge — Progress

## Current phase

**Complete.** All seven phases of the original plugin are done, and so is the
Band Matrix, which was specified afterwards and ran through the same seven
phases of its own.

## Toolchain (verified 2026-09-14)

| Tool | Version / path |
|---|---|
| CMake | 4.4.3 |
| Compiler | MSVC 14.44.35207 (VS 2022 **Build Tools**, `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`) |
| Windows SDK | 10.0.26100.0 |
| Generator | `Visual Studio 17 2022`, `-A x64` |
| JUCE | 8.0.15, shallow clone in `JUCE/` (git-ignored) |
| Git | 2.54.0 |
| Python | 3.x with Pillow — used once, offline, to generate `Resources/` |

> Only the VS **Build Tools** are installed; there is no full IDE. Build from the
> command line with the commands below.

## Exact build / test commands

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target SideForge_All
cmake --build build --config Release --target SideForgeTests
.\build\SideForgeTests_artefacts\Release\SideForgeTests.exe
```

Debug: swap `Release` for `Debug` in both build lines and in the test path.

Link-time optimisation is behind `-DSIDEFORGE_ENABLE_LTO=ON` and off by default;
with it on, the plugin link takes roughly fifteen minutes.

`SideForge_All` is the target that produces the VST3 and the standalone; the bare
`SideForge` target only builds the shared code library.

Last run: **190 passed, 0 failed** in both Debug and Release, plus
**25 checks, 0 failures** from `SideForgeValidate`.

The full suite takes about three minutes in Release and rather longer in Debug;
`--filter=<substring>` runs a subset, and `--screenshots=<dir>` renders every
page and panel of the interface to PNG instead of running tests.

## Completed

### Phase 1 — Foundation
- CMake project, VST3 + Standalone targets, console test runner linking the same sources.
- Parameter registry with version-hinted stable IDs, full APVTS layout, cached raw pointers.
- Versioned state wrapper that accepts a bare APVTS tree and drops non-finite values.
- Editable curve model with a lock-free rendered-table exchange.

### Phase 2 — Sidechain and compressor
- Bus layout: stereo main + optional stereo/mono sidechain; mono and stereo both supported.
- `SidechainDetector`: gain, high-pass, low-pass, peak / RMS / hybrid magnitude, stereo link.
- `CompressorEngine`: soft knee, range clamp, attack / hold / release on the gain
  reduction, program-dependent auto release, auto makeup.
- Metering: peak, RMS, latching clip, gain reduction with peak hold.

### Phase 3 — Curve engine
- `CurveEngine`: host-locked phase, free-run mode, swing, phase offset, depth,
  pre/post delay, attack / hold / recover rate limits, anti-click smoothing.
- `TriggerEngine`: sidechain / internal / MIDI / transport / manual sources, peak,
  RMS, transient and K-weighted detection, own filter with audition, threshold,
  hysteresis, minimum interval.
- `CurveEditor`: node add / move / remove, per-segment tension and type, grid
  snapping, undo/redo, shape presets, playhead.

### Phase 4 — Multiband and stereo
- LR4 crossovers with an allpass correction on the low band so three bands
  recombine flat; verified within 0.5 dB from 30 Hz to 15 kHz.
- Per-band depth, attack, release and enable.
- Stereo / Left-Right with adjustable link / Mid-Side with a link that controls
  how much ducking reaches the sides. Mono-low stage.

### Phase 5 — Spectral engine
- STFT with Hann analysis *and* synthesis windowing and a normalisation factor
  measured from the window, so reconstruction is exact to better than 1e-4 at
  every quality setting, sample rate and block size.
- Collision map with gating, preserve-body exponent, frequency range with tapered
  edges, tilt, frequency and time smoothing, attack / release, tonal-versus-
  transient sensitivity and an artifact guard.
- Quality mode switching without allocation, applied at a block boundary so the
  reported latency never disagrees with the signal path.

### Phase 6 — Interface and visualisation
- Full resizable interface: header, four macros, tabbed central display
  (Waveform / Curve / Spectrum / Matrix), meter column, eight parameter panels,
  status footer.
- Custom look and feel; colour language shared between the visualiser and the panels.
- Lock-free visualisation: `AnalysisBus` (SPSC ring of decimated columns) and a
  triple-buffered `SpectralSnapshot`. No UI component reads DSP working state.
- One 45 Hz timer for the whole editor. Reduced-animation and visualiser-off options.
- Right-click menu on every control: host parameter commands, MIDI learn, direct
  CC assignment, type value, reset, copy, paste.
- MIDI learn with per-session persistence, applied on the message thread.
- Tooltips, exact numeric readouts, editable value fields, double-click reset,
  shift-drag fine adjustment, undo/redo for curve edits, A/B slots, click-free bypass.
- Controls are disabled, not hidden, when the active engine does not use them.

### Phase 7 — Presets, compatibility, hardening
- 23 factory presets, exposed as host programs and in the preset menu.
- User preset save / load to `%APPDATA%\SideForge\Presets\*.sideforge`.
- Icon generated into `Resources/` and wired into the build via `ICON_BIG` / `ICON_SMALL`.
- README with build, install, routing, operation and troubleshooting sections.
- `--screenshots=<dir>` on the test runner renders every page of the interface,
  in every mode and at the minimum window size, so the layout can be inspected
  without a host. Three of the bugs above were found this way.
- `SideForgeValidate`: a small JUCE-based VST3 host that loads the built bundle
  the way a DAW would and checks scanning, instantiation, bus layouts, processing
  at three sample rates and three block sizes, silence, latency, state round
  trips, malformed state and the editor. This stands in for pluginval, which is
  not installed here.

### The Band Matrix

Specified after the plugin was finished, and built in its own seven phases.

- `LinkwitzRileyCrossover`: 1 to 4 bands from a cascade of LR4 splits, with an
  allpass on every band that skips a split so the bands recombine flat at any
  band count. Frequencies are kept ordered and at least 1.25x apart, and glide
  in the log domain at a rate-limited 12 octaves per second.
- `BandProcessor`: one band's worth of processing — Off, Ducker, Compressor,
  Limiter, Curve, Spectral or Listen, each with its own depth, range, timing,
  gain, pan, mix and makeup, its own detector source and detector filter, and
  its own lookahead.
- `BandMatrix`: two crossover networks (destination and sidechain), four band
  processors, four alignment delays. Every band is padded to the deepest so the
  recombination stays phase-coherent; solo and mute are resolved at the sum.
- 107 new parameters, all in state version 2. Nothing existing was renamed or
  repurposed, so a version-1 session simply has no values for them and loads in
  Full Range.
- Display page with three spectra (original, processed, detector), per-band
  tinted regions that contract by the reduction each band is applying, draggable
  crossover handles and four band cards with enable / bypass / solo / mute.
- Matrix panel with routing, band count, crossovers, two matrix macros and the
  selected band's full control set, greying what the current configuration does
  not use.
- 15 Band Matrix factory presets, 23 in total.

### Cross-feature testing

Asked for explicitly: every feature tested against every other feature. An
exhaustive product of the discrete settings is past 10^11 combinations, so
`Tests/DSP/CrossFeatureTests.cpp` does **all-pairs** — for every pair of settings
and every combination of values that pair can take, one run with those two
pinned and every other parameter randomised. 5402 configurations, plus:

- the cubes the Band Matrix introduced, swept exhaustively;
- all 256 solo / mute / bypass / enable states across four bands;
- the same pairs on six host layouts from 44.1 to 192 kHz, block sizes 32 to
  1024, mono and stereo, with and without a sidechain bus;
- 3000 blocks with settings moving on every single block.

Each configuration asserts finite output, no runaway level, a plausible reported
latency, and silence in giving silence out.

## Bugs the tests caught

Recorded because each one would have been invisible by ear:

1. **STFT latency was wrong by one hop.** The reported latency was
   `fftSize - hopSize`; an impulse test showed the true delay is a full
   `fftSize`. Dry-path alignment and host delay compensation were both off by
   256–512 samples until this was fixed.
2. **Auto makeup returned silence.** `Decibels::decibelsToGain (x, 0.0f)` treats
   `0 dB` as the minus-infinity floor and returns 0, so auto gain multiplied the
   output by zero whenever the compressor was not reducing.
3. **The test harness was disabling the sidechain bus.** `setPlayConfigDetails`
   calls `disableNonMainBuses()`, so every "sidechain" test had been running
   against a bus that was not there — and writing to its channels was a buffer
   overrun that crashed the runner.
4. **Crossover response looked wrong at 8 kHz.** It was the measurement: at
   exactly `fs/6` a sine is sampled at six fixed phases, so the peak *sample*
   sits below the true peak. Measuring RMS instead showed the crossover was
   flat all along.

The next three came out of rendering the interface to PNG and looking at it,
which is why `--screenshots=<dir>` is now part of the test runner:

5. **The spectral noise floor was an absolute constant.** At `1e-6` it sat far
   below the transform's own numerical noise, so above the destination's content
   two bins of rounding error compared as "the sidechain owns this band" and the
   whole top end was ducked whenever the sidechain was between hits. The floor
   now scales with the FFT size, which puts it near -90 dBFS. The existing
   silent-sidechain test missed this because it used noise as the destination
   (so the main magnitude was never small) and an exactly-zero sidechain.
6. **The artifact guard borrowed the collision array as scratch.** Harmless for
   the audio, which had finished with it — but the snapshot published to the
   visualiser is taken afterwards, so the collision map on screen was actually a
   set of gain values, and read as a solid red wash across the spectrum.
7. **Every choice control rendered blank.** `ComboBoxAttachment` matches on item
   index and does not populate the box itself.

From the Band Matrix work:

8. **Crossover glide moved the cutoff 1.38x in the first block.** A one-pole on
   the frequency looks like smoothing and is not: its first step is proportional
   to the distance, so a large jump is nearly instant. Replaced with an explicit
   log-domain rate limit, and the test now asserts the guaranteed bound the
   class states rather than a tolerance someone picked.
9. **The screenshot renderer overflowed the stack.** The processor is about
   2.5 MB once four bands each preallocate a worst-case FFT, and it was a local.
   Heap-allocated.

From the cross-feature sweep:

10. **Mix and auto makeup were applied to the monitoring paths.** Auditioning the
    sidechain at 50% Mix handed back half the sidechain blended with half the
    user's own track, and auto gain then scaled that by whatever makeup the
    compressor wanted — a +23 dB audition in the configuration that failed.
    Both now stand down while any audition or monitor mode is engaged. Four
    regression tests pin it.
11. **Most controls had no tooltip of their own.** The tooltip was set on a
    knob's inner slider and value label, so hovering the name or the padding
    between them showed nothing, and the segmented controls had none at all
    until the mouse had already moved across them. `editor_everyControlHasATooltip`
    now walks the editor and requires four lines from every control: what it is,
    what it does, how to work it, and the right-click hint.

## Active problems

- None.

## Next actions

1. Run `pluginval` at strictness 8 if it can be installed, and record the result
   here. In its absence the project has its own `SideForgeValidate` host, which
   loads the built bundle through JUCE's VST3 hosting code.
2. Test in a host with real sidechain routing (Reaper / Cubase) and confirm the
   footer's connection indicator matches what the host reports, and that a
   Band Matrix session recalls with the right routing.
3. Listen to the four-band configurations on real material. The tests prove the
   matrix is correct, phase-coherent and stable; they cannot say whether four
   bands of ducking is a good idea on a given mix.

## Known and accounted for

- The VST3 publishes ~2167 parameters. About ninety are SideForge's; the rest
  are JUCE's MIDI CC parameters (128 controllers x 16 channels), which is how a
  VST3 receives CC at all, and therefore how the plugin's own MIDI learn works.
  Documented in the README so it does not read as a bug.
- Right-click menus are wired on every control type and the MIDI learn behind
  them is tested end to end, but the menu *appearing* is not automated — a
  `PopupMenu` needs a running message loop, which the headless runner has not
  got. Verified by hand instead.

## Deferred / not exposed

- **Multiple sidechain buses.** One dependable external bus only; host support
  for more is inconsistent. `TriggerEngine`'s source switch is the extension
  point a future companion Sender plugin would use. No control is exposed for it.
- **Loudness-matched bypass.** Not implemented. Doing it honestly needs an
  integrated-loudness measurement of both paths with a settling time that would
  make the bypass button feel unpredictable; the plain crossfade is exact and
  latency-matched instead. No control is exposed for it.
- **Double-precision processing.** Not advertised and not implemented; the
  `double` overload asserts and is never called.
- **More than four matrix bands.** Four covers sub / low / mid / high, which is
  the split people actually reach for, and each band is a full processing chain.
  `kMaxMatrixBands` is the single constant that would have to change, but the
  parameter IDs and the band cards are sized from it deliberately rather than
  generated, so adding a fifth is a decision, not a constant bump.
- **Per-band crossover slopes.** LR4 throughout. Mixed slopes would break the
  allpass correction that makes the bands recombine flat, which is a larger
  change than it sounds.

## Architectural decisions

See `ARCHITECTURE.md` — the decision table lives there so it sits next to the
signal flow and threading rules it explains.
