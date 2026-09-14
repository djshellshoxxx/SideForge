# SideForge

A Windows VST3 sidechain processor that combines four things most mixes need at
once: a classic sidechain compressor, a drawable rhythmic gain curve, multiband
and mid/side control, and spectral collision ducking.

Every engine produces a **gain signal** rather than audio. That is the idea the
whole plugin is built on: the compressor and the curve both emit a `0..1`
multiplier, so blending them is multiplication rather than a special case, the
band stage is the only thing that touches the audio, and each engine can be
tested on its own.

![SideForge](Resources/screenshots/waveform.png)

Compress mode: the sidechain in its own lane at the top, the gain-reduction
envelope over the destination and the output, and a tick at every trigger.

---

## Contents

- [What it does](#what-it-does)
- [Requirements](#requirements)
- [Building](#building)
- [Installing](#installing)
- [Routing the sidechain](#routing-the-sidechain)
- [Using it](#using-it)
- [Right-click, MIDI learn and automation](#right-click-midi-learn-and-automation)
- [Latency and quality modes](#latency-and-quality-modes)
- [Factory presets](#factory-presets)
- [Troubleshooting](#troubleshooting)
- [Testing](#testing)
- [Project layout](#project-layout)
- [Licence](#licence)

---

## What it does

**Four modes**, chosen with the selector at the top:

| Mode | What runs |
|---|---|
| **Compress** | Feed-forward sidechain compressor only. |
| **Curve** | The drawn rhythmic envelope only. No sidechain input needed. |
| **Spectral** | STFT collision ducking only. |
| **Hybrid** | Compressor × curve for the broadband gain, plus the spectral stage. The **Blend** control divides the work between them. |

**Compressor** — threshold, ratio (up to inf:1), soft knee, attack, hold,
release, range, peak/RMS/hybrid detection, program-dependent auto release,
detector high-pass and low-pass with audition, stereo link, lookahead and
optional auto gain.

**Curve** — a drawable envelope with movable nodes, per-segment curve / S-curve /
step shapes with adjustable tension, grid snapping, tempo divisions from 1/64
through 8 bars (straight, dotted and triplet), free-running millisecond mode,
loop / one-shot / audio-triggered / MIDI-triggered modes, swing, phase, pre- and
post-delay, attack / hold / recover rate limits and anti-click smoothing. The
phase is locked to the host's PPQ position, so starting playback in the middle of
a bar lands on the right part of the curve.

**Spectral** — an STFT that builds a **collision map** rather than inverting the
sidechain spectrum. Per bin it asks how much of the combined energy belongs to
the sidechain, gates that by whether the sidechain is meaningfully present there
at all, then raises the result to a power set by **Preserve Body** so only
decisive collisions duck. Frequency range, tilt, frequency and time smoothing,
attack / release, tonal-versus-transient sensitivity and an artifact guard that
pulls isolated notches back towards their neighbours.

**Bands and channels** — one broadband stage or three bands split by
Linkwitz-Riley 4th-order crossovers (allpass-corrected, so the bands recombine
flat to within a fraction of a dB), each with its own depth, attack, release and
bypass. Stereo, Left/Right with an adjustable link amount, or Mid/Side where the
link control decides how much of the ducking reaches the sides. Optional mono-low
stage.

**Triggers** — external sidechain, internal, MIDI note, host transport or a
manual test button, with peak, RMS, transient or K-weighted detection, its own
filter with audition, threshold, hysteresis and a minimum interval.

**Monitoring** — normal, sidechain listen, delta (only what was removed),
detector audition, trigger audition, input / sidechain / output / gain-reduction
meters with peak, RMS and latching clip indicators.

---

## Requirements

| | |
|---|---|
| OS | Windows 10 or 11, x64 |
| Compiler | Visual Studio 2022, or the 2022 Build Tools |
| CMake | 3.22 or newer |
| JUCE | 8.0.15 |

There are no other dependencies, no network access and no copy protection.
macOS, Linux, AU, AAX and VST2 are deliberately not targeted.

---

## Building

JUCE is expected in a `JUCE/` directory next to `CMakeLists.txt` and is not
checked in:

```powershell
git clone --branch 8.0.15 --depth 1 https://github.com/juce-framework/JUCE.git JUCE
```

Then either run the build script, which does everything including the tests:

```powershell
.\Scripts\build.ps1                       # Release build + tests
.\Scripts\build.ps1 -Config Debug
.\Scripts\build.ps1 -Clean -Install       # rebuild and install the VST3
```

...or drive CMake directly:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target SideForge_All
```

The first configure takes a few minutes because JUCE builds its `juceaide` helper.

Link-time optimisation is off by default — it adds several percent to the DSP but
around a quarter of an hour to every link, which is not a sensible trade while
iterating. Turn it on for the binaries you actually ship:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DSIDEFORGE_ENABLE_LTO=ON
```

Artefacts land in:

```
build\SideForge_artefacts\Release\VST3\SideForge.vst3
build\SideForge_artefacts\Release\Standalone\SideForge.exe
```

Swap `Release` for `Debug` in both commands for a debug build.

### Tests

```powershell
cmake --build build --config Release --target SideForgeTests
.\build\SideForgeTests_artefacts\Release\SideForgeTests.exe
```

`--filter=name` runs a subset; several comma-separated patterns are allowed, for
example `--filter=spectral_,curveEngine_`.

The same runner can render the interface to PNG, which is how the screenshots
above were made and how three of the bugs listed in `PROGRESS.md` were found:

```powershell
.\build\SideForgeTests_artefacts\Release\SideForgeTests.exe --screenshots=shots
```

The VST3 bundle itself is checked separately, by a small host — see
[Troubleshooting → Validation](#troubleshooting).

---

## Installing

Copy the whole `SideForge.vst3` **folder** (not just the file inside it) to:

```
C:\Program Files\Common Files\VST3\
```

Then rescan plugins in your host. The standalone build needs no installation —
run `SideForge.exe` and pick your audio device under **Options → Audio/MIDI
Settings**.

---

## Routing the sidechain

SideForge exposes **one** external sidechain bus. That is deliberate: host
support for multiple sidechain buses is inconsistent enough that a multi-bus
design would work in some DAWs and quietly fail in others.

The footer always tells you where you stand — a red dot for *not connected*,
amber for *connected but silent*, green for *receiving*.

**Reaper** — put SideForge on the destination track, add two channels to the
track (4 total), route the trigger track to channels 3/4 of the destination, then
in the FX pin connector map track channels 3/4 to the plugin's sidechain inputs.

**Cubase / Nuendo** — activate the side-chain input in the plugin header, then
add a send from the trigger track to that side-chain destination.

**FL Studio** — route the trigger channel to the destination's mixer track with
the sidechain arrow, then choose that sidechain source in the plugin wrapper's
**Processing** settings.

**Ableton Live** — Live does not expose VST3 sidechain buses on every version. If
the sidechain input is unavailable, use **Routing → SC Source → Internal**, which
keys the plugin from the track it is sitting on, or use **Curve** mode, which is
tempo-locked and needs no sidechain at all.

**Studio One / Bitwig / Waveform** — enable the side-chain input in the plugin
header and send the trigger track to it.

No sidechain at all? **Curve** mode runs entirely from the host transport.

> **A future Sender plugin.** The trigger routing is a single switch over a
> `TrigSource` enum fed by a signal the processor chooses (`TriggerEngine`).
> A companion Sender plugin would add one more case to that switch and one shared
> lock-free buffer keyed by a bus id — the engine itself would not change. That
> is the reason the routing layer is a switch rather than a hard-wired bus read.

---

## Using it

The four **macros** at the top left are modulation over the panel parameters,
never a separate set of hidden values, so A/B and session recall stay exact:

| Macro | What it moves |
|---|---|
| **Depth** | How much gain reduction every active engine asks for. |
| **Separation** | Trades broadband ducking for spectral ducking, and makes the collision map more selective as it does. |
| **Timing** | Scales every attack and release together. |
| **Character** | Soft and transparent (wide knee, gentler ratio, slower onset) through to tight and aggressive. |

Turn **Macros Active** off to hear the panel parameters exactly as they are set.

The central display has three pages:

- **Waveform** — destination (cyan), sidechain (orange), gain reduction (violet),
  output (green) and a tick at every trigger. Click it to freeze; double-click to
  reset.
- **Curve** — the editable envelope. Drag nodes; double-click to add or remove
  one; drag the middle of a segment to bend it; right-click for segment type and
  shape presets; `Ctrl+Z` / `Ctrl+Shift+Z` to undo and redo. Hold `Shift` while
  dragging to bypass the grid.
- **Spectrum** — both spectra, the collision map in yellow through red, and the
  attenuation the spectral stage is applying. Hover for exact values. **Delta**
  overlays the spectrum of what is being removed.

![Spectrum](Resources/screenshots/spectrum.png)

Hybrid mode. The wash marks where the kick and the chord genuinely compete, and
the violet line is what the spectral stage is taking out as a result.

![Curve](Resources/screenshots/curve.png)

![Bands](Resources/screenshots/bands.png)

The Bands panel at the minimum window size. Controls the active setup does not
use — the crossovers in one-band mode, bands 2 and 3, the channel link in Stereo
— are greyed rather than hidden, so nothing moves around under the cursor.

Controls that the active engine does not use are greyed rather than hidden, so
the layout never jumps around and you can still see what a mode ignores.

---

## Right-click, MIDI learn and automation

**Right-click any control** for:

- the **host's own** parameter menu, where the DAW offers one — in VST3 hosts
  this is where "add automation", the host's MIDI CC binding and "show in
  automation lane" live,
- **MIDI learn** — pick it, then move a controller. The control shows a small
  `CC n` badge once bound, and `LEARN` while it is waiting,
- **Assign MIDI CC** directly, either from the named common controllers or from
  the full 0–127 list,
- **Type value**, **Reset to default**, **Copy value** and **Paste value**.

MIDI bindings are saved with the session and keep working with the editor closed.
Controller movements are recorded on the audio thread into lock-free atomics and
applied on the message thread, so nothing calls into the host from `processBlock`.

Other interactions, on every control:

- **Double-click** — reset to default.
- **Shift-drag** — fine adjustment.
- **Double-click the value readout** — type a number directly.

**Lookahead** and **Quality** change the reported latency, so they are marked as
non-automatable. Automate them and the host would be re-negotiating latency
mid-bar; use them as setup controls instead.

---

## Latency and quality modes

`latency = lookahead + spectral`, reported to the host and applied to the dry
path as well, so mix and bypass stay phase-coherent.

| Quality | STFT | Overlap | Max lookahead | Spectral latency |
|---|---|---|---|---|
| **Live** | — | — | 0 ms | none — the spectral stage is out of the path |
| **Studio** | 1024 | 4x | 5 ms | 1024 samples |
| **Spectral** | 2048 | 4x | 10 ms | 2048 samples |
| **Render** | 4096 | 8x | 20 ms | 4096 samples |

The spectral stage only reports latency while it is actually processing;
switching back to Compress or Curve gives the host its samples back immediately.

Because Live runs no STFT, **Spectral** and **Hybrid** are disabled while Live is
selected. Clicking one anyway switches the quality to Studio and selects it,
rather than doing nothing.

---

## Factory presets

| Preset | For |
|---|---|
| Kick vs Bass | Fast broadband duck keyed from the kick, detector listening to the low end only. |
| Vocal vs Music | Spectral ducking that opens space only where the music collides with the vocal. |
| Transparent Spectral Space | Gentle and wide. Almost inaudible until you bypass it. |
| Four On The Floor Pump | Tempo-locked quarter-note pump. No sidechain required. |
| Irregular Live Kick | Curve re-triggered by the sidechain, for a drummer who does not play to a grid. |
| Mid/Side Vocal Clearing | Ducks the centre and leaves the sides open. |
| Aggressive Stutter | Sixteenth-note gate, hard steps, minimal smoothing. |
| Sub Only Ducking | Three-band split with only the low band moving. |

Your own presets save to
`%APPDATA%\SideForge\Presets\*.sideforge` — the preset menu has **Save**,
**Load** and **Open preset folder**.

---

## Troubleshooting

**Nothing happens.** Check the footer. A red dot means the sidechain is not
connected: either route one (see above), switch **SC Source** to *Internal*, or
use **Curve** mode. In Compress mode also check that the sidechain is actually
crossing the threshold — the gain-reduction meter tells you immediately.

**The sidechain is connected but the meter never moves.** The detector filter may
be excluding it. Turn on **Routing → Audition** to hear exactly what the detector
hears, and widen the high-pass / low-pass.

**Spectral mode does nothing.** It needs Studio quality or higher. Check the
mode buttons are not greyed; clicking one switches the quality for you.

**The track is out of time with the rest of the mix.** Your host is not applying
delay compensation. Either turn it on, or set Quality to **Live** and Lookahead
to 0 ms for a zero-latency configuration.

**Clicks when toggling bypass.** There should not be any — bypass crossfades over
about 15 ms. If you hear one, it is more likely the host hard-switching the
plugin; try the plugin's own Bypass control rather than the host's.

**The curve is not in time.** In Tempo sync the phase follows the host's PPQ
position, so check the host is actually sending a playhead. In Free mode the
cycle is a fixed rate and is not tempo-locked by design.

**High CPU.** Render quality runs a 4096-point STFT at 8x overlap. Drop to Studio
for mixing. **Output → Visualiser → Reduced** halves the repaint rate, and
turning the visualiser off entirely leaves processing untouched.

**Validation.** The project ships its own VST3 host check, which loads the built
bundle through JUCE's VST3 hosting code — the same path a DAW takes — and
verifies scanning, instantiation, bus layouts, processing at several sample rates,
latency reporting, state round trips and the editor:

```powershell
cmake --build build --config Release --target SideForgeValidate
.\build\SideForgeValidate_artefacts\Release\SideForgeValidate.exe
```

pluginval is not installed on the machine this was built on, so it has not been
run. If you have [pluginval](https://github.com/Tracktion/pluginval):

```powershell
pluginval.exe --validate-in-process --strictness-level 8 `
  ".\build\SideForge_artefacts\Release\VST3\SideForge.vst3"
```

---

## Testing

117 deterministic tests cover the DSP, the state format and the editor. They
generate their own signals and compare with tolerances rather than by ear:

- silence in gives silence out, in every mode;
- a missing sidechain bus never crashes;
- bypass is sample-exact and click-free, and stays aligned with the reported latency;
- compressor attack, release, hold, knee continuity and range clamping;
- curve timing at five sample rates and six block sizes, mid-bar starts, tempo
  changes and transport jumps;
- trigger hysteresis, minimum interval and block-size independence;
- crossover reconstruction within 0.5 dB across the audible range, and exact
  mid/side round-tripping;
- STFT reconstruction to better than 1e-4 at every quality setting, and an
  impulse test that pins the reported latency to the actual delay;
- state round trips, malformed state, and parameter boundary values;
- mono and stereo layouts, oversized and varying block sizes, rapid automation,
  extreme and non-finite input, and a long-run stability pass;
- the editor paints at every size in its range, in every mode and on every page;
- **every parameter has a control in the editor, and every control points at a
  parameter that exists** — so the UI cannot drift away from the DSP;
- MIDI learn binds, follows, re-binds, survives a state round trip and drops
  mappings to parameters that no longer exist.

---

## Project layout

```
Source/
  PluginProcessor.*        block flow only; no DSP maths lives here
  PluginEditor.*           look and feel, window size, hosts MainView
  Parameters/              IDs, APVTS layout, macro mapping, MIDI learn
  DSP/                     every engine; no GUI includes
  Model/                   curve model, versioned state, factory presets
  UI/                      everything that draws; reads snapshots only
  Utilities/               lock-free primitives, smoothing helpers
Tests/
  DSP/  State/  Rendering/
Resources/                 icon
```

`ARCHITECTURE.md` explains the signal flow and the threading rules;
`PROGRESS.md` records what is done, what was deferred and why.

---

## Licence

SideForge is built on [JUCE](https://juce.com), which is dual-licensed under the
JUCE licence and the AGPLv3. Using it under the AGPLv3 means this project is
AGPLv3 as well; using it commercially needs a JUCE licence from Raw Material
Software. No other third-party code is included.
