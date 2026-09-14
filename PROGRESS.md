# SideForge — Progress

## Current phase

**Phase 7 — presets, compatibility and hardening.** Phases 1 to 6 are complete
and green.

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

Last run: **122 passed, 0 failed** in both Debug and Release, plus
**25 checks, 0 failures** from `SideForgeValidate`.

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
  (Waveform / Curve / Spectrum), meter column, seven parameter panels, status footer.
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
- Eight factory presets, exposed as host programs and in the preset menu.
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

## Active problems

- None.

## Next actions

1. Run `pluginval` at strictness 8 if it can be installed, and record the result
   here. In its absence the project has its own `SideForgeValidate` host, which
   loads the built bundle through JUCE's VST3 hosting code.
2. Test in a host with real sidechain routing (Reaper / Cubase) and confirm the
   footer's connection indicator matches what the host reports.

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

## Architectural decisions

See `ARCHITECTURE.md` — the decision table lives there so it sits next to the
signal flow and threading rules it explains.
