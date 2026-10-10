# SideForge — Developer Specs and Development Plan

Status: proposal, written 2026-10-10 against `main` @ `5f551df` plus PR #1
(CLAP, Linux, CI workflow). Nothing in this document is implemented yet.

How to read it:

- Part 1 — rules every work package follows (Definition of Done, contracts).
- Part 2 — the work packages, one spec each. IDs: **E** = essential, **V** = value
  feature, **I** = "makes it more interesting".
- Part 3 — the development plan: sequencing, branching, merge-conflict
  avoidance, test and CI gates, risk register.
- Part 4 — ways to maximise quality and speed.

Facts about the existing code that these specs depend on (all verified in the
source, not assumed):

| Fact | Where |
|---|---|
| Parameter IDs are a **public contract**; never rename, only add. Old trees are handled in `PresetState::migrate()`. | `Source/Parameters/ParamIDs.h` |
| Session state is a versioned `ValueTree` (`kStateVersion = 2`). Unknown content is ignored; malformed content is replaced by defaults. | `Source/Model/PresetState.h` |
| The APVTS is already constructed with an `UndoManager`. | `PluginProcessor.cpp` ctor |
| Buses: stereo main in/out + a **stereo sidechain input that is disabled by default**. Mono/stereo main only, input layout must equal output. | `PluginProcessor.cpp` ctor, `isBusesLayoutSupported` |
| Factory presets live in code, are applied as *complete* states (reset to default, then overrides), exposed through the host program API. User presets are files; directory from `MainView::getUserPresetDirectory()`. | `FactoryPresets.cpp`, `MainView.h` |
| `scSource` is a choice parameter `{External, Internal}`; `trigSource` is `{SC, Internal, MIDI, Transport, Manual}`; `trigThreshold`, `compThreshold` are dB parameters. | `ParamIDs.h`, `ParameterLayout.cpp` |
| `CurveModel` holds at most `kMaxNodes = 64` nodes, serialises to/from `ValueTree`. | `CurveModel.h` |
| `AnalysisBus.h` provides a lock-free `RawTap` (Original / Processed / Sidechain) for GUI analysis. | `Source/DSP/AnalysisBus.h` |
| Macros exist (`macroDepth`, `macroSeparation`, `macroTiming`, `macroCharacter`, `macrosEnabled`). | `ParamIDs.h`, `MacroMapping.cpp` |
| Tests: custom header-only framework, `Tests/{DSP,Rendering,State}`; 190 pass on Linux. Editor can be instantiated headless. | `Tests/` |
| `MainView.cpp` (928 lines) and `PluginProcessor.cpp` (726 lines) are the two largest shared files — the main merge-conflict risk. | `Source/UI`, `Source/` |
| JUCE 8.0.15 rejects MinGW. Windows binaries must come from MSVC. | `JUCE/modules/juce_core/system/juce_TargetPlatform.h` |

---

# Part 1 — Global rules

## 1.1 Definition of Done (applies to every work package)

A work package is done only when **all** of these hold:

1. Builds with no new warnings on Windows (MSVC) and Linux (GCC) in Release.
2. All existing tests pass; new tests listed in the spec exist and pass.
3. No allocation, lock, file I/O, logging, or `juce::String` construction on the
   audio thread in any new code path (see 1.3).
4. State compatibility: a session saved by the previous release loads and sounds
   identical (see 1.2). A test with a captured v2 state blob proves it.
5. New parameters are listed in `ParamIDs.h` with a comment, have a `migrate()`
   default, appear in the README parameter notes, and have a tooltip/help entry.
6. Editor opens, resizes and closes cleanly with the feature in every state
   (existing `EditorTests` extended).
7. CI is green on the PR head, including pluginval (once E1/E6 land).
8. `PROGRESS.md` updated; spec acceptance criteria ticked in the PR description.

## 1.2 Compatibility contract

- **Never rename or repurpose** a parameter ID or a choice index.
- **Choice parameters may only gain entries at the end.** (Example: V6 appends
  `"Send"` to `scSource`.)
- **New parameter → new ID**, default chosen so the old behaviour is unchanged.
- **State version bumps are batched.** Today: v2. All new persistent data in this
  document is additive child nodes (`<AB/>`, `<Style/>`), which the existing
  "ignore unknown" rule already tolerates. Plan: **one** bump to v3, done in
  work package E0 below, which adds the empty nodes and a `migrate(2→3)` stub.
  Every later package fills in its own node without bumping again. This removes
  the most likely merge conflict (everyone editing `kStateVersion`).
- Existing-session golden test: `Tests/State/GoldenStateTests.cpp` loads a
  checked-in v2 blob (`Tests/State/golden_v2.xml`) and asserts parameter values and
  a short rendered output hash. Created in E0, run by every PR.

## 1.3 Real-time rules

On the audio thread: no `new`/`delete`, no `std::vector` growth, no
`std::mutex`, no `juce::String`, no `DBG`, no file or network I/O, no
`MessageManager` calls. Cross-thread data uses `Utilities/LockFreeFifo.h`,
`std::atomic`, or the seqlock helper added in E0 (`Utilities/SeqLock.h`).
Prefer "audio thread writes a few atomics, GUI thread does the maths". Anything
that needs allocation is prepared in `prepareToPlay` or on the message thread.
Add `juce::ScopedNoDenormals` at the top of any new process entry point.

## 1.4 File-ownership convention (merge-conflict avoidance)

Each work package **adds new files** and touches shared files in only the
smallest possible, *append-only* places. Section 3.3 lists the shared files and
the rule for each. Do not reformat or reorder code in shared files.

---

# Part 2 — Work packages

Estimates are ideal developer-days for one engineer with the repo checked out,
excluding waiting on CI, reviews or DAW licences. Treat ±40 % as normal.

## E0 — Foundations (do first; unblocks parallel work)

**Goal.** Create the shared plumbing so later packages do not collide.

**Requirements.**
1. Bump `kStateVersion` to 3; add `migrate(2→3)` that adds empty `<AB/>` and
   `<Style/>` nodes if absent. Update the layout comment in `PresetState.h`.
2. Add `Source/Utilities/SeqLock.h`: single-writer / multi-reader copy of a
   trivially-copyable struct, wait-free for the writer, retrying reader. Unit
   tested with a stress thread.
3. Add `Source/UI/EditorSettings.h/.cpp`: non-session user settings (first-run
   flags, last-used folders) in `juce::PropertiesFile` under the user app-data
   folder. Pure message-thread API. Never read from the audio thread.
4. Add `Source/Parameters/ParamGroups.h`: a table mapping each parameter ID to
   `{group, randomisable, loudnessCritical}` — consumed by V5 (randomise) and V1
   (A/B and preset compare). One table, edited append-only.
5. Add the golden-state test described in 1.2.
6. Split CMake source lists: `SIDEFORGE_CORE_SOURCES` and `SIDEFORGE_UI_SOURCES`
   gain **one file per line, alphabetically sorted**, so concurrent additions
   merge cleanly (git merges separate lines; a shared line is a conflict).
7. Add `.clang-format` matching existing style and a CI format check that only
   reports (does not fail) on untouched files.

**Acceptance.** Existing 190 tests + golden test + SeqLock stress test pass; no
behavioural change in a rendered sample.
**Estimate.** 1.5 days.

---

## E1 — Working Windows build in CI

**Goal.** A pushed commit produces downloadable Windows (VST3, CLAP, Standalone)
and Linux artefacts, with tests run.

**Current state.** PR #1's workflow failed on both jobs after ~43 s with no
retrievable logs. Cause unknown. Do not guess; make failures observable first.

**Requirements.**
1. Reproduce: re-run the workflow with `ACTIONS_STEP_DEBUG=true`; open the run page
   and record the red step. Check repo settings: Actions enabled, workflow
   permissions, billing/spending limits, any allow-list of actions.
2. Make the workflow self-diagnosing: add `set -euxo pipefail` to bash steps;
   add a final `if: failure()` step that uploads `build/CMakeFiles/CMakeConfigureLog.yaml`,
   `build/CMakeFiles/*.log` and the cloned-dependency versions as an artefact.
3. Pin everything: JUCE tag `8.0.15` (already), clap-juce-extensions to a **commit
   SHA** (not default branch), runner images `windows-2022` / `ubuntu-22.04`,
   action versions to full SHAs for supply-chain safety.
4. Cache JUCE and the clap extension checkout (`actions/cache`, key = the pinned
   SHAs) and ccache/sccache for compilation; target < 15 min cold, < 6 min warm.
5. Windows job: build `SideForge_VST3`, `SideForge_Standalone`, `SideForge_CLAP`,
   `SideForgeTests`, `SideForgeValidate`; run tests; run the validator on the
   built `.vst3`; upload `SideForge-windows-x64`.
6. Linux job: same plus `xvfb-run`; upload `SideForge-linux-x64`.
7. pluginval: download the pinned release binary, run at strictness 5 (raise to 10
   when E6 is done) on the VST3 for both OSes. Failing pluginval fails the job.
8. `concurrency:` group per ref with `cancel-in-progress: true`.
9. Do not mark the CI job names as required checks until three consecutive green
   runs on `main`.

**Acceptance.** Three consecutive green runs on `main`; artefacts download and
the Windows `.exe` starts on a clean Windows 10/11 VM with no Visual C++
redistributable (the CMake sets the static CRT — verify, do not assume).
**Risks.** MSVC-only warnings or errors never seen on GCC (the code has only been
compiled with MSVC by the author and GCC by us). Budget a day for fixes.
**Estimate.** 1–2 days after the failing step is known.

---

## E2 — Sidechain compatibility in major DAWs

**Goal.** Know, per DAW, whether the sidechain input is reachable and works, and
make failure obvious to the user.

**Part A — plug-in side (code).**
1. **Sidechain status indicator** in the top bar: three states, computed on the
   message thread from values the audio thread publishes:
   - `Bus off` — `getBus(true, 1)->isEnabled() == false`.
   - `No signal` — bus enabled, detector input below −90 dBFS for > 2 s while the
     host is playing.
   - `Live` — bus enabled and signal present.
   Tooltip/click opens the relevant Help page (E5) with per-DAW routing steps.
2. Audio thread publishes: `std::atomic<bool> scBusEnabled`,
   `std::atomic<float> scPeakDb` (block peak, relaxed). Computed in
   `processBlock` from the sidechain buffer that already exists. Zero allocation.
3. When the bus is off **and** `scSource == External`, the detector must see
   silence (never uninitialised or stale data). Unit test: process with the
   sidechain bus disabled, assert output is finite and gain reduction is zero.
4. Mono sidechain handling: a host may give a mono sidechain even though the bus
   is declared stereo. Verify `isBusesLayoutSupported` accepts mono and stereo
   sidechain and that mono is duplicated to both detector channels. Test it.
5. Add `Tools/` offline test host run (`SideForgeValidate`) check: "sidechain bus
   present, optional, and defaulted off" so a regression is caught in CI.

**Part B — manual compatibility matrix (process).** For each DAW below, on
Windows, run the protocol and fill the table in `docs/DAW_COMPATIBILITY.md`.

| DAW | Format to test | Notes to check |
|---|---|---|
| Reaper (free eval) | VST3, CLAP | pin connector routing to inputs 3/4; 4-channel track |
| Ableton Live | VST3 | "Audio From" dropdown inside the device; sidechain in plug-in header |
| FL Studio | VST3 | sidechain via mixer routing "Sidechain to this track"; also needs wrapper behaviour check |
| Cubase / Nuendo | VST3 | Sidechain activation button on the plug-in; bus naming |
| Studio One | VST3 | sidechain dropdown; may require manual bus enable |
| Bitwig | VST3, CLAP | sidechain via device chain "Sidechain" input; plug-in sandboxing mode on/off |
| Standalone | — | no sidechain bus: confirm Internal source works and External shows "Bus off" |

**Protocol per DAW** (30 min each):
1. Build the test project: kick loop on track A, sustained saw bass on track B,
   SideForge on B with the preset "Kick → Bass Tight" (from E4).
2. Route A to the sidechain. Play. Record: does bass duck? Meter moves?
   Indicator state? Latency reported (check PDC alignment by nulling against a
   bounced dry copy)?
3. Automate `mix`, save project, close, reopen: parameters and curve restored?
4. Offline-render (bounce) and compare against realtime playback: identical to
   within −90 dB? (Quality mode `Render` vs `Live` differences are expected and
   must be listed.)
5. Toggle plug-in bypass and track mute; no clicks (listen + view waveform).
6. Record DAW version, plug-in build id, result, screenshots, workarounds.

**Acceptance.** Matrix filled for ≥ Reaper, Ableton Live, FL Studio plus one of
Cubase/Studio One/Bitwig; each cell is Pass or a documented workaround; the
status indicator shows the right state in each; README routing section updated
to match what was actually observed.
**Estimate.** Code 1.5 days; matrix 1 day per DAW (human, needs the DAW).

---

## E3 — Installers, signing, release automation

**Goal.** A user downloads one file, installs without SmartScreen scare screens
(or with the minimum), and can uninstall cleanly.

**Requirements.**
1. **Version single source of truth**: `project(SideForge VERSION x.y.z)` in
   `CMakeLists.txt` feeds the plug-in version (JUCE does this), the installer, the
   `moduleinfo.json`, and the About text. Bump script `Scripts/bump-version.ps1`.
2. **Windows installer** with Inno Setup (`installer/SideForge.iss`), built in CI:
   - Components (checkboxes): VST3 → `%CommonProgramFiles%\VST3`, CLAP →
     `%CommonProgramFiles%\CLAP`, Standalone → `%ProgramFiles%\SideForge`
     (+ Start Menu shortcut), Factory presets are inside the plug-in (no files).
   - Per-machine install, admin elevation; 64-bit only.
   - Uninstaller removes exactly what it installed. Never touches user presets in
     the user folder.
   - Silent switch `/VERYSILENT` for testing in CI.
3. **Portable zip** alongside the installer containing the three artefacts and
   `INSTALL.txt` (two paragraphs: where to copy what, how to rescan in the DAW).
4. **Code signing**: sign every `.exe`, `.dll`/`.vst3`, `.clap` and the installer
   with `signtool` using RFC 3161 timestamping. Options, best first for a small
   team: Azure Trusted Signing (no hardware token, works in CI), a cloud-HSM OV/EV
   certificate, or SignPath for open-source. Secrets live in repo/environment
   secrets; the signing step runs only on tags, from the protected environment.
   Without a certificate the release is marked "unsigned beta" in the release
   notes and the INSTALL text explains the SmartScreen "More info → Run anyway".
5. **Release workflow** `.github/workflows/release.yml`, triggered by a `v*` tag:
   build → test → sign → installer → zip → SHA-256 `checksums.txt` → create a
   **draft** GitHub Release with artefacts and auto-generated notes. A human
   publishes the draft; the workflow never publishes by itself.
6. Crash/diagnostic info: the About box shows version, build date, git SHA, JUCE
   version, and a "Copy diagnostics" button (OS, DAW host name from
   `PluginHostType`, sample rate, buffer, bus layout, latency) for bug reports.

**Acceptance.** On a clean Windows VM: installer installs all three, DAW finds the
VST3 and CLAP, uninstall leaves no files; `Get-AuthenticodeSignature` reports
Valid (when a certificate is configured); checksums verify.
**Risks.** Certificate procurement lead time (days to weeks) — start it
immediately, in parallel with everything else.
**Estimate.** 2–3 days engineering; certificate lead time is the real critical path.

---

## E4 — Default presets that work out of the box

**Goal.** The first ten presets cover the common jobs and sound right without
tweaking. Presets are verified by tests, not just by ear.

**Current state.** 23 factory presets exist (`FactoryPresets.cpp`, 346 lines).
They have not been audited against realistic material.

**Requirements.**
1. **Audit pass** (half a day): for each current preset record intent, mode,
   routing, depth, timing, and whether it needs an external sidechain. Output
   `docs/PRESET_AUDIT.md`. Anything unusable on first load (e.g. needs a
   sidechain but the bus is off, or extreme depth) is fixed or demoted.
2. **Preset metadata**: extend the factory table with `category`
   (`Essentials`, `Rhythmic`, `Spectral`, `Band Matrix`, `Style`, `Utility`),
   `tags` (string list), `needsSidechain` (bool), `description` (exists). This
   metadata is also what V1 uses; E4 defines it, V1 consumes it.
3. **Essentials set** (must exist, in this order, appear first in the list):
   1. `Kick → Bass (Tight)` — compressor, fast attack, 80–120 ms release, ~8 dB depth, sidechain HP ≈ 40 Hz.
   2. `Kick → Bass (Spectral Clean)` — Spectral mode, low-frequency range, preserve body high.
   3. `Kick → Pad (Gentle)` — slow release, 3–4 dB, wide stereo.
   4. `Vocal → Music (Pocket)` — duck 2–3 dB in the 1–4 kHz region only (Band Matrix or Spectral).
   5. `Pump (Classic EDM)` — Curve mode, 1/4, deep, with swing 0.
   6. `Gate Rhythm (1/8)` — Curve, step segments.
   7. `Internal Pump (no sidechain)` — Curve mode so it works with **no** sidechain routed. This is the preset that must always do something audible on first load.
   8. `Mono Bass Glue` — low band mono + light compression.
   9. `De-Mask Guitars (Spectral)` — vocal/guitar spectral separation.
   10. `Init (transparent)` — all neutral, bit-exact passthrough when `mix` is 100 %.
4. **Loudness safety**: no factory preset may raise output by > +1 dB over the
   input for the reference signal; presets that reduce level significantly use
   auto gain or a documented output gain.
5. **Automated preset tests** (`Tests/DSP/FactoryPresetRenderTests.cpp`): generate
   deterministic stimuli in the test (kick = 60 Hz decaying sine bursts every
   beat at 120 BPM; bass = 55 Hz saw + harmonics; vocal-like = band-limited
   noise 300 Hz–4 kHz; pink noise), render each preset offline and assert,
   per preset class:
   - output finite, peak ≤ +0 dBFS for −6 dBFS peak input;
   - for ducking presets: bass RMS in the 50 ms after each kick onset is lower
     than in the 50 ms before the next kick by the preset's declared range
     (store `expectedDepthDbMin/Max` in the table);
   - recovery: gain returns to within 1 dB of unity before the next kick for
     presets declaring `fullRecovery`;
   - `Init`: output equals input within −120 dB after latency alignment;
   - no preset output differs between block sizes 64/512/2048 by more than
     −80 dB (catches block-size dependence).
6. **Listening checklist** (human, once per preset, 5 min each): recorded in the
   audit doc — artefacts, pumping tails, mono compatibility (sum to mono and null
   test), CPU at 48 kHz/128 buffer.
7. First-instance default: new instances load preset index 0 only when the host
   supplies no state (see I2 for which preset). Parameter *defaults* must not
   change (compatibility contract).

**Acceptance.** Audit doc done; ten Essentials present and pass the render tests;
no preset triggers a pluginval or validator failure; CPU per instance for
Essentials < 3 % of one core at 48 kHz, 128 samples on the reference machine
(measure; record the machine).
**Estimate.** 3 days (1 audit, 1 table + tests, 1 tuning/listening).

---

## E5 — In-plugin Quick Start

**Goal.** A new user understands routing and the first three controls in under
two minutes without leaving the plug-in.

**Current state.** Full Help and configurable tooltips exist in `MainView.cpp`
(commit `5f551df`, +121 lines). This package extends them; it does not replace
them. First read `git show 5f551df` to see the existing structure.

**Requirements.**
1. **First-run overlay** (shown once per machine, flag in `EditorSettings`, not in
   the session): 4 coach-mark steps, each highlighting a real component with a
   short caption and Next / Back / Skip:
   1. Mode selector — "Pick how the gain is generated."
   2. Sidechain status chip (from E2) — "Route a track here; green means it hears you."
   3. Threshold/Depth (or Curve editor in Curve mode) — "This is the main control."
   4. Preset browser — "Start from a preset."
   Final screen: "Show this again" link and "Don't show again".
2. **Contextual empty states**: if mode needs the sidechain and status is
   `Bus off`/`No signal`, the visualiser area shows a one-line hint with a link
   to the DAW routing help instead of a flat line.
3. **Help page: "Routing by DAW"** with the rows from the E2 matrix (text only,
   maintained in one `HelpContent.cpp` string table so docs and UI cannot drift).
4. **Reopen** from the existing Help menu: "Quick start tour".
5. Overlay is purely cosmetic: it intercepts mouse only on its own buttons, never
   changes parameters, never blocks host keyboard focus beyond Esc to dismiss.
6. Respect the existing tooltip option; coach marks are independent of it.
7. Keyboard accessible: Tab order Next/Back/Skip, Esc skips.
8. Works at the minimum and maximum editor size; text uses the existing Theme.

**Tests.** Editor test: open, run overlay steps, assert each target component
exists and is on-screen at 3 window sizes; assert first-run flag is written and
the overlay does not reappear; assert no parameter changed.
**Acceptance.** Usability check with two people who have not used the plug-in:
each completes "make the bass duck under the kick" in < 3 minutes using only the
plug-in UI. Record the sessions' notes in the PR.
**Estimate.** 2.5 days.

---

## E6 — Host-edge-case robustness

**Goal.** Eliminate crashes, hangs and level spikes in situations hosts create
that unit tests on the author's machine do not.

**Requirements — automated (extend `ProcessorTests`, add `HostEdgeTests.cpp`).**
1. Layouts: mono main + no sidechain, mono main + mono sidechain, stereo main +
   mono sidechain, stereo + stereo, sidechain bus disabled. All finite output.
2. Sample rates: 22 050, 32 000, 44 100, 48 000, 88 200, 96 000, 176 400,
   192 000; sample rate change between `prepareToPlay` calls.
3. Block sizes: 1, 2, 7, 32, 64, 127, 512, 4096, 8192, plus **randomly varying**
   block sizes inside one stream; `prepareToPlay(maxBlock)` smaller than the
   actual block must not crash (hosts violate this; guard and process in chunks).
4. Hostile input: NaN, ±Inf, denormals, DC offset, full-scale square, silence for
   10 minutes (CPU must not climb: denormal protection), 0-length blocks.
5. Lifecycle: `prepareToPlay` called repeatedly with changing parameters;
   `releaseResources` then `processBlock` (some hosts do this); `setStateInformation`
   called *during* processing from another thread; `getStateInformation` while
   the editor is dragging a curve node.
6. Parameters: every parameter driven to min, max and random values each block
   for 60 s of audio, with and without smoothing; output finite, no single-sample
   jump larger than the configured limit except where the parameter is a switch.
7. Latency: changing `quality` / routing / lookahead updates reported latency via
   `updateHostDisplay` (message thread only) and the dry/wet alignment test
   (existing) still passes at the new value.
8. State fuzz: feed `setStateInformation` 10 000 randomly mutated copies of the
   golden state (byte flips, truncation, huge numbers, wrong types, deep nesting);
   must never crash, hang, or leave a NaN parameter. Run under ASan.
9. Bypass: toggling bypass, mix 0 %, mix 100 % are click-free (max step < −60 dBFS
   on a sine) and latency-aligned.
10. Transport: host without a playhead; playhead with no tempo; tempo changes
    mid-bar; looping; jumping position — curve phase stays PPQ-locked, no NaN.

**Requirements — tooling.**
- **Sanitiser builds** on Linux CI: ASan + UBSan job runs the unit tests;
  separate TSan job runs the editor-open tests and the state-fuzz while a
  background thread plays audio.
- **pluginval strictness 10** on Windows and Linux VST3.
- **Clap-validator** (the free-audio project's tool) on the `.clap`.
- **Memory growth test**: process 30 minutes of audio faster-than-realtime and
  assert resident memory is flat (< 1 MB drift).
- **Soak**: nightly workflow with a 2-hour randomised-parameter run.

**Acceptance.** All of the above green; any defect found is fixed in the same
package with a regression test.
**Estimate.** 4 days (tests 2, fixes 2; fixes are unknowable up front).

---

## E7 — macOS and Linux distribution

**Goal.** Ship the same plug-in on macOS (arm64 + x86_64) and Linux.

**Current state.** Linux compiles and passes tests/validation (done this
session). macOS has never been built. `ARCHITECTURE.md` and the README still say
"Windows only" and must be updated.

**Requirements.**
1. CMake: remove any remaining Windows-only assumptions; `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`,
   deployment target 11.0 (JUCE 8 minimum is 10.13+; choose 11.0 for arm64).
2. Formats: macOS = VST3, CLAP, **AU** (add `AU` to `FORMATS` on Apple only),
   Standalone. Linux = VST3, CLAP, Standalone (LV2 optional later).
3. CI job `macos-14`: build, run tests, `auval -v aufx Sdfg Sfrg` for the AU,
   pluginval, upload artefacts.
4. Signing/notarisation (needs an Apple Developer account, US$99/yr): hardened
   runtime, `codesign`, `notarytool submit --wait`, `stapler staple`; a `.pkg`
   installer built with `pkgbuild`/`productbuild`. Secrets in the protected
   environment, tags only.
5. Linux: tarball + `install.sh` (copies to `~/.vst3`, `~/.clap`); document
   required packages. Test on Ubuntu 22.04 and a Fedora container.
6. Platform code audit: grep for hard-coded `\\` path separators, `.exe`,
   `GetModuleHandle`, font assumptions; confirm `getUserPresetDirectory()` uses
   `juce::File::userApplicationDataDirectory` per platform.
7. Fonts/rendering: verify the editor at 1x and 2x (Retina) scale; the existing
   screenshot tests run on each OS and are compared per-platform (do not share
   goldens across OSes — font hinting differs).

**Acceptance.** Notarised macOS pkg opens without Gatekeeper warning; AU
validates; the plug-in loads in Logic, Reaper (mac), Ableton (mac).
**Estimate.** 3 days engineering + Apple account lead time + hardware for
real-host testing (a Mac).

---

## V1 — Preset browser with tags, search, and A/B compare

**Goal.** Finding, auditioning and comparing sounds is fast and safe.

### V1a — Preset library model

New files: `Source/Model/PresetLibrary.h/.cpp`, `Source/Model/PresetMeta.h`.

```cpp
struct PresetMeta {
    juce::String id;            // factory: "f:<index>"; user: relative file path
    juce::String name, category, author, description;
    juce::StringArray tags;
    bool factory = true, needsSidechain = false, favourite = false;
    juce::Time modified;
};
```

Requirements:
1. `PresetLibrary` scans the user preset directory (`*.sfpreset`, XML, same root
   as `PresetState::capture`) on a **background thread** (`juce::ThreadPool`, one
   job), reads only the root attributes (name, category, tags, author,
   description, needsSidechain) without parsing parameters, and caches an index
   file `index.xml` keyed by file size + mtime. Re-scan on demand and when the
   directory changes (`juce::FileSystemWatcher` is not in JUCE core; use a 5 s
   polling timer only while the browser is open).
2. Metadata is stored as **extra attributes on the root of the preset tree**
   (`category`, `tags` comma-separated, `author`, `description`) — additive, ignored
   by old versions (compatibility contract), so no format break.
3. Factory presets expose metadata from E4's table through the same interface.
4. Favourites and "last used" are user settings (`EditorSettings`), not preset data.
5. Save dialog: name, category (editable combo with the six standard ones), tags
   (chips), description. Overwrite asks for confirmation. Names are sanitised to a
   safe filename; no path separators; no reserved Windows names.
6. Delete/rename user presets move to the OS recycle bin (`File::moveToTrash`).
7. Import/export of a single `.sfpreset`; drag-drop a file onto the editor
   loads it. Loading is *always* through `PresetState::restore`, so a corrupt
   file cannot crash (existing guarantee).
8. Library API is message-thread only; scans never touch the processor.

### V1b — Browser UI

New files: `Source/UI/PresetBrowser.h/.cpp`. `MainView.cpp` gets **one** line to
instantiate it and one to lay it out; the existing `showPresetMenu`/`savePresetToFile`
stay as fallbacks until the browser is accepted, then are removed in a cleanup PR.

Requirements:
1. Top-bar preset field: shows current name, a `*` when modified, `◀ ▶` buttons
   (previous/next in the *current filtered list*), and opens the browser panel.
2. Panel: search box (matches name, tags, description, case-insensitive,
   debounced 150 ms); left column category list with counts; tag chips (multi-select
   = AND); sort (name, recently used, newest); "Favourites" and "Factory/User"
   filters; list rows show name, category, a sidechain icon if `needsSidechain`.
3. Keyboard: Up/Down to move, Enter to load, Esc to close, `/` focuses search,
   `F` toggles favourite.
4. **Audition hygiene**: loading a preset applies parameters through a single
   undo transaction (see V5) and does not change `bypass`, `monitor`, editor size,
   or host-visible `outputGain`/`inputGain` unless the user enabled "Presets load
   gain" in settings (default off; prevents sudden level jumps). The excluded set
   comes from `ParamGroups.h` (`loudnessCritical`).
5. Host program change: factory presets remain exposed through
   `getNumPrograms/setCurrentProgram` with **unchanged indices** (hosts store
   them). New factory presets are appended; reorder only in the *display* order
   via the `displayOrder` field.
6. Modified indicator: compare `PresetState::capture` against the loaded
   snapshot (ignoring `Editor` node and meter-only state) using a stable hash of
   sorted `(id, value)` pairs; recompute on a 200 ms message-thread timer, not on
   each parameter edit callback.

### V1c — A/B compare

New files: `Source/Model/AbCompare.h/.cpp`. Persisted in the `<AB/>` node (E0).

Requirements:
1. Two slots, each a full captured state (params + curve, no editor state).
   Active slot flag. On session load both slots restore.
2. Buttons: `A | B` toggle, `A→B` copy, `B→A` copy. Switching applies the stored
   state via `PresetState::restore` as one undo transaction.
3. First switch from an unsaved state stores the current state in the slot you
   leave, so you never lose edits.
4. Level-match hint: show "B is +2.1 dB louder" using the offline estimate
   (RMS of the gain-reduction average) — informational only; not a gain change.
5. Glitch control: parameter values are already smoothed in the DSP; the curve
   swap goes through `curveTables.publish` which is the existing lock-free path.
   Test: switch A/B while processing sine for 5 s at 1 kHz; output contains no
   non-finite values and no step > −40 dBFS beyond what the parameter delta
   implies.
6. A/B state is **not** automatable and not exposed as a host parameter (avoids
   a non-musical automation lane).

**Tests.** `PresetLibraryTests` (scan, index cache invalidation, metadata
round-trip, hostile filenames, corrupt files skipped, 5 000 presets scan < 1 s on
the background thread without blocking the message thread), `AbCompareTests`
(copy/switch/undo, persistence round-trip, golden v2 state unaffected),
`PresetBrowserEditorTests` (filters, keyboard navigation, no parameter change on
open/close).
**Acceptance.** Load a preset, tweak, press B, tweak differently, toggle A/B ten
times: no glitches, no lost edits, undo returns each step; save with tags, restart
the host, find it by tag search.
**Depends on.** E0, E4 (metadata). **Soft-depends on** V5 (undo transactions).
**Estimate.** 6 days (V1a 2, V1b 2.5, V1c 1.5).

---

## V2 — "Learn threshold" from the sidechain signal

**Goal.** One click sets a sensible threshold from the actual sidechain material.

**Behaviour.**
1. A `Learn` button beside `compThreshold` (Compress/Hybrid) and beside
   `trigThreshold` (audio-triggered curve). The button targets whichever the
   current mode uses; label says which ("Learn threshold").
2. Press → button shows a progress ring and "Play the sidechain…". The user plays
   the host; the plug-in listens up to **10 s** or until it has collected at least
   **2 s of non-silent** material, whichever comes first. Press again to cancel.
3. On completion the parameter animates to the learned value as **one undo
   transaction** (V5) and a small toast shows
   "Threshold −18.5 dB (peaks −9.2 dB)". If under 0.5 s of signal was heard:
   "No sidechain signal heard. Check routing." with link to Help.

**Algorithm (audio-thread safe).**
- Analyse the *detector signal* (after the sidechain high/low-pass and gain, i.e.
  exactly what the compressor sees) so the result accounts for filtering.
- Per block, the audio thread computes the block peak, converts to dB with a
  fast `log2` approximation, and increments one of **128 atomic counters**
  (histogram, 0.75 dB bins over −96…0 dB). Blocks below −70 dB are counted in a
  separate `silent` atomic. Allocation-free; the histogram is a fixed member.
- When learning completes, the message thread reads the counters and computes:
  `p95` = 95th-percentile non-silent block-peak level and `median`.
  `threshold = clamp(p95 − marginDb, paramMin, paramMax)` where `marginDb` defaults
  to 6 dB (a "Sensitivity" option in the Learn context menu: Tight 3 / Normal 6 /
  Loose 10).
- If `p95 − median < 3 dB` (steady tone, no transients) show a hint "Signal is
  steady; threshold may not react rhythmically" but still apply.
- Trigger-threshold variant uses the *trigger detector's* output instead (it has
  its own filter and detector type).

**Files.** New `Source/DSP/LevelHistogram.h` (the lock-free structure),
`Source/UI/LearnButton.h/.cpp`. Hook: one call in `processBlock` after the
detector, one `std::atomic<bool> learnActive` check — both appended, no
restructuring.

**Edge cases.** Host not playing; sidechain bus off (immediately fail with the
bus-off message); mode changed mid-learn (cancel); automation of the threshold
parameter during learn (apply anyway, last write wins); two instances (state is
per-instance); offline render (learn disabled, button greyed).

**Tests.** `LevelHistogramTests`: known inputs (constant −20 dB, kick pattern,
silence, one loud outlier) produce expected percentiles; atomic counters under a
concurrent writer thread; learned threshold for a −6 dBFS peak kick train equals
`−6 − 6 ± 1 dB`; silent input returns "no signal" without changing the
parameter; learn then undo restores the old value.
**Acceptance.** With the Essentials test project, Learn yields a threshold that
produces 4–10 dB gain reduction on the kick in at least 90 % of a set of 10
different kick loops (store the loops' synthetic generators in the test).
**Depends on.** E0 (EditorSettings for sensitivity), E2 (bus-off status);
soft on V5. **Estimate.** 3 days.

---

## V3 — Curve pattern library and MIDI import

**Goal.** Fill the curve with a good groove in one click; reuse grooves from MIDI.

### V3a — Pattern library

New: `Source/Model/CurvePatterns.h/.cpp`, `Source/UI/PatternMenu.h/.cpp`.

1. Patterns are **data**: `{id, name, category, division, loopBars, nodes[]}` where
   nodes are `(x, y, shape, tension)` in the existing `CurveModel` coordinates.
   Hard cap `CurveModel::kMaxNodes` (64): a pattern with more nodes is a build-time
   test failure.
2. Ship ≥ 30 patterns: Pump (¼, ⅛, 16th, triplet, dotted), Half-time bounce,
   Gate (⅛, 16th), Trance gate, Stutter, Swing groove, Sidechain "kick+snare",
   Dub swell, 2-bar offbeat, 4-bar build. Each has a one-line description.
3. Applying a pattern replaces the curve **and** sets the related parameters
   (division, loop length, mode = Loop) in one undo transaction; a checkbox
   "Keep my division/loop settings" (default off) applies only the shape.
4. Pattern menu in the curve editor toolbar: categories, hover preview (draws
   the shape ghosted without applying), click applies. "Save current as pattern"
   writes a user pattern (`*.sfpattern`, XML of `CurveModel::toValueTree()`).
5. Mirror/reverse/invert/scale-depth operations on the existing curve:
   `Flip horizontally`, `Invert`, `Scale depth 0–200 %`, `Shift by grid`.

### V3b — MIDI import

New: `Source/Model/MidiToCurve.h/.cpp`.

1. Entry points: toolbar button "Import MIDI…" and drag-drop of a `.mid` file onto
   the curve editor.
2. Use `juce::MidiFile`. Dialog: track picker (note counts shown), channel, note
   range filter (e.g. only C1 for kicks), length in bars (detected from the clip,
   rounded up to a power-of-two bar count, max 8), "Duck on: note-on / note-off".
3. Conversion: quantise onsets to the nearest 1/64 note; for each retained onset
   create `down` node (gain = 1 − depth·velocity/127 where depth is a dialog
   field, default 100 %) then a recovery node placed at the next onset minus a
   fraction or at `onset + recoverTime` (dialog; default "until next hit");
   merge onsets closer than one grid step; if the result exceeds 64 nodes, drop the
   weakest-velocity hits and tell the user how many were dropped.
4. Result sets division/loop length so the curve spans the clip length, shown in
   a preview before commit; Cancel leaves everything untouched; OK is one undo
   transaction.
5. Never import more than 10 MB / 16 tracks (reject larger files politely).
6. Export the curve as a `.mid` of note-on events at each duck point (round-trip
   for DAW editing) — optional stretch goal, excluded from acceptance.

**Tests.** Pattern table sanity (node counts, monotone x, y in range, deterministic
render); apply/undo round-trip; `MidiToCurve` on synthetic MIDI built in-test
(four-on-the-floor → 4 ducks per bar at x = 0, 0.25, …; swung hits; double hits;
> 64 hits; zero notes → friendly error; malformed files); the resulting curve
re-imports identically when serialised.
**Acceptance.** Every shipped pattern renders at 3 tempos without clicks (max
step < the curve's anti-click smoothing limit); importing a 4-on-the-floor MIDI
file yields a curve that, rendered against a kick audio file at the same tempo,
ducks within ±1 ms of the kick onsets in the test.
**Depends on.** E0; soft on V5. **Estimate.** V3a 3 days, V3b 3 days.

---

## V4 — Live spectral-ducking visualisation

**Goal.** Show which frequencies are being ducked, right now, so the unique
feature is visible.

**Current state.** `AnalysisBus.h` supplies raw sample taps; `SpectrumView.cpp`
draws spectra on the GUI thread. The spectral stage's per-bin gain is not yet
published.

**Requirements.**
1. **Publish per-bin gain.** In `SpectralDucker`, after computing the per-bin
   gain for a hop, copy it into a fixed-size `SpectralGainSnapshot`
   (`float gain[kMaxBins]` with `kMaxBins = 2049` for the largest FFT, plus
   `fftSize`, `sampleRate`, `hopCounter`) through `SeqLock` (E0). Cost: one
   memcpy of ≤ 8 KB per hop; measure and assert < 1 % CPU in the benchmark.
   Only publish when `spectralViewActive` (atomic set by the editor) is true;
   otherwise skip entirely (zero cost when the editor is closed).
2. **GUI mapping.** Message-thread timer 30 Hz reads the snapshot (retry on
   torn read), converts bins to log-frequency display columns (≤ 512) taking the
   *minimum* gain per column (so narrow notches are visible), converts to dB
   reduction, applies a short smoothing (attack instant, release 150 ms) for
   readability only.
3. **Display.** In `SpectrumView`: existing original (dim) and processed spectra,
   plus a translucent **reduction fill** between them coloured by depth
   (colour-blind-safe sequential palette from `Theme.h`), a thin line of the
   sidechain spectrum, and a hover read-out ("2.3 kHz: −5.1 dB"). Toggle buttons:
   Original / Processed / Sidechain / Reduction. Freeze (existing `freezeViz`).
4. **Collision map view** (optional second mode): a heat strip showing per-bin
   "sidechain share" before the power law (needs a second published array;
   only if profiling allows). Excluded from acceptance if it costs > 2 %.
5. **Band Matrix**: when routing is Band Matrix, draw crossover lines (existing)
   and shade each band's mean reduction in the band header.
6. Works at all quality modes (FFT size changes: the snapshot carries `fftSize`
   so the mapping rebuilds when it changes).
7. Reduced-motion/low-CPU option: 15 Hz refresh toggle in settings.

**Tests.** Synthetic: sidechain = 100 Hz tone, program = 100 Hz + 3 kHz tones →
published gain at ~100 Hz < 0.5, at 3 kHz > 0.9; snapshot is stable under a
concurrent reader (SeqLock stress); no allocation in the publish path (the
existing test allocator hook, or a counting `operator new` in the test);
rendering test draws the view with a canned snapshot and compares to a golden
PNG (Linux only, tolerance 2 %); CPU benchmark guard.
**Acceptance.** With a kick and a bass in the Spectral preset, the reduction fill
visibly concentrates under ~120 Hz and tracks the kick rhythm at 30 fps; the
editor closed costs nothing measurable.
**Depends on.** E0 (SeqLock). **Estimate.** 4 days.

---

## V5 — Undo/redo UI and constrained randomise

**Goal.** Make experimentation safe and fun.

### V5a — Undo / redo

The APVTS already owns an `UndoManager` (`undoManager`, passed in the processor
constructor). The work is plumbing and the non-APVTS curve.

1. **UI**: undo/redo buttons in the top bar with tooltips showing the next
   action's name; `Ctrl/Cmd+Z`, `Ctrl+Shift+Z`/`Ctrl+Y`. Editor already requests
   keyboard focus; ensure shortcuts do not steal keys from text fields.
2. **Transactions**: slider drags are one transaction (the JUCE attachment
   starts one on gesture begin — verify per control type in `Controls.cpp`);
   preset load, A/B switch, Learn, pattern apply, randomise and MIDI import each
   call `undoManager.beginNewTransaction("<name>")` once and then change values.
3. **Curve model**: add `CurveEditAction : juce::UndoableAction` storing
   before/after `ValueTree` snapshots (`CurveModel::toValueTree`), performed
   through `undoManager.perform`. Node drags coalesce into one action per drag.
4. **Limits**: `UndoManager(30000 units, 100 transactions)` — set in the
   constructor; cleared on `setStateInformation` and preset load *by user*? No:
   preset load must be undoable; clear only on session restore.
5. Undo must be applied on the message thread; parameter changes it produces are
   normal host-visible parameter changes (the host sees automation writes while
   in write mode — document this).
6. Never record meter-only state, editor size, or the open-panel index.

### V5b — Constrained randomise

New: `Source/Model/Randomiser.h/.cpp` (pure logic, no UI), `Source/UI/RandomiseButton.h/.cpp`.

1. Button with a dropdown: **Amount** 0–100 % (default 35 %: each randomised
   parameter moves that fraction of the way toward a random target), **Scope**
   checkboxes by group (Compressor, Curve shape, Spectral, Bands, Timing,
   Matrix), and **Lock** toggles (a small lock on each panel header freezes that
   group; locked groups persist in `EditorSettings`).
2. Metadata-driven: `ParamGroups.h` supplies for each parameter whether it is
   randomisable and its *musical range* `[lo, hi]` (normalised). Never randomised:
   `bypass`, `mix`, `inputGain`, `outputGain`, `monitor`,
   `quality`, `scSource`, `scAudition`, `trigAudition`, MIDI-learn mappings, routing
   mode, editor state. Randomisation can never produce silence, a level > +3 dB, or
   a threshold above the learned/known peak (when Learn data exists).
3. Curve randomise: generate N nodes (N from the current division: 2–12) on grid
   positions, y drawn from a distribution biased toward 0 and 1, shapes from the
   allowed set, ensure the curve has at least one duck ≥ 6 dB and returns to
   unity at least once per loop.
4. Seeded: the toast shows the seed; "Re-roll" cycles; "Previous roll" restores
   (undo does the same). Same seed + same state → same result (test).
5. One undo transaction per roll; the audio never sees a hard jump — values are
   written as ordinary parameter changes and the DSP smooths them.
6. Optional "Mutate" (amount ≤ 15 %) for small variations.

**Tests.** Undo: 50 random edits then 50 undos restore the exact captured state
(golden compare incl. curve); curve drag coalescing; preset load undo; randomise
determinism, bounds (10 000 rolls never leave `[lo,hi]` nor touch excluded IDs,
never produce NaN), locked groups unchanged, safe-level property (render each of
200 random rolls with the reference signal; peak < +3 dB; not silent).
**Acceptance.** 100 consecutive random rolls with the Essentials stimulus never
produce clipping, silence or an audible click (max step test) and each can be
undone in one step.
**Depends on.** E0 (ParamGroups). **Estimate.** V5a 2 days, V5b 3 days.

---

## V6 — Companion "SideForge Send" plug-in (sidechain over a shared bus)

**Goal.** Let any track's audio be used as a SideForge sidechain even in hosts
whose native sidechain routing is awkward or absent (FL Studio users especially).

> This is the largest and riskiest package. It starts with a time-boxed
> **spike** (V6.0). Do not start V6.1+ until the spike result is recorded.

### V6.0 — Spike (2 days, throwaway code allowed)

Questions to answer with a minimal prototype in Reaper, Ableton, FL Studio and one
sandboxing host (Bitwig):
1. Are Send and Receive instances in the **same process**? (Ableton/FL/Reaper
   default yes; Bitwig sandbox can be per-plug-in.) Therefore the transport
   must work **across processes**.
2. In what order does each host run the audio callbacks of different tracks in one
   block? Log `(instanceId, hostTimeInSamples, wallClock)` to a file; analyse
   whether Send ever runs after Receive in the same block.
3. Is `AudioPlayHead::PositionInfo::getTimeInSamples()` available and identical
   in both instances for the same block?
4. Measured jitter of a memory-mapped ring between instances.
Output: a one-page decision record. If ordering is unreliable and the timestamp
approach below cannot align audio exactly, **scope V6 down** to "sidechain with a
declared one-block latency and a warning in offline renders", or cancel.

### V6.1 — Shared transport (library `SideForgeBus`)

New static library `Source/Bus/` (header + cpp, no JUCE GUI dependency).

```cpp
struct BusHeader {            // lives at offset 0 of the shared region
    uint32_t magic = 0x53464231;   // "SFB1"
    uint32_t version = 1;
    uint32_t channelCount = 2;
    uint32_t sampleRate;           // advertised by the latest writer
    uint32_t capacityFrames;       // power of two, 1 << 17 (≈ 2.7 s at 48 kHz)
    std::atomic<uint64_t> writeFrame;   // total frames ever written
    std::atomic<uint64_t> writerHostPos;// host sample position of frame 0 of last block
    std::atomic<uint32_t> writerAlive;  // heartbeat counter
};
// followed by float data[channelCount][capacityFrames]
```

1. One named region per **channel number 1–16** (not per name, to avoid
   dictionary races), file-backed memory map at
   `<temp>/SideForge/bus-<channel>.sfb` via `juce::MemoryMappedFile` (read/write).
   Created on first use (message thread / `prepareToPlay`), never on the audio thread.
2. Sole writer per channel; if a second Send claims the same channel, the second
   shows "Channel in use" and mixes nothing (first writer wins, heartbeat timeout
   3 s releases the claim). Receivers are unlimited and read-only.
3. Writer audio-thread path: copy block into ring, then release-store `writeFrame`
   and `writerHostPos`. No locks, no allocation.
4. Reader audio-thread path: compute the desired start frame from its own host
   position (`readFrame = writerFrameAtHostPos(hostPos)`), copy out; if the data
   is not yet written (Send ran later) use the previous block (**late** counter++),
   else exact. A "Align" setting adds 0–2 blocks of deliberate delay so the sender
   is always ahead; the Receiver reports this extra delay as plug-in latency so
   the host's PDC keeps the output aligned (only possible because the receiver's
   own audio is delayed by the same amount — the detector sees on-time audio).
5. Sample-rate mismatch between instances: Send advertises its rate; Receiver
   resamples with a small polyphase/linear fallback only if rates differ, else
   direct copy. Document: all tracks share the project rate in practice.
6. Security/robustness: the file lives in a per-user temp dir with owner-only
   permissions; magic/version validated; all indices masked; reader never trusts
   `writeFrame` beyond `capacity` (ring-wrap safe); stale file (heartbeat > 3 s old)
   treated as "no sender".
7. Cross-platform: `MemoryMappedFile` on Windows/macOS/Linux; tested on all three.

### V6.2 — "SideForge Send" plug-in

New CMake target `SideForgeSend` (VST3, CLAP, Standalone off): stereo in →
stereo out passthrough (bit-exact), publishes the input to the chosen channel.

1. Parameters (IDs prefixed `send`): `sendChannel` int 1–16 (default 1),
   `sendGain` dB −24…+12 (default 0), `sendMode` {Pre-fader copy only} — keep to
   one mode in v1; `sendMute` bool (stop publishing).
2. UI: tiny editor (≈ 300×140): channel selector, level meter, "Channel in use"
   warning, link to Help. Name/colour field optional (stored in state only; not
   used for matching).
3. Zero latency, no processing of the main signal; bypass still publishes (document;
   hosts differ) — behaviour selectable with `sendWhenBypassed`.
4. Shares `Theme.h`, LookAndFeel and `EditorSettings` with the main plug-in.

### V6.3 — Receiver side in SideForge

1. Append `"Send"` to the **end** of `scSource`'s choices (`External, Internal, Send`;
   compatibility contract — old index values unchanged).
2. New parameter `scSendChannel` int 1–16 (default 1); visible only when
   `scSource == Send`.
3. Detector input selection in `processBlock`: when `Send`, the sidechain buffer
   is filled from the bus reader instead of the sidechain bus; everything
   downstream (filters, meters, delta/audition monitoring) is unchanged. This is
   exactly the "source switch" extension point noted in `PROGRESS.md`.
4. Latency: add the Align setting (0–2 blocks, default 1) to the existing
   `LatencyManager`; changes call `updateHostDisplay` from the message thread
   (never from `processBlock`; mirror the existing pattern at
   `PluginProcessor.cpp` lines ~146–160).
5. Status chip (E2) gains states `Send: live`, `Send: waiting`, `Send: late (n)`.
6. Offline render caveat: if `late > 0` during a bounce, show a persistent warning
   chip and log to the diagnostics copy (E3); recommend native sidechain for final
   bounces.

**Tests.** `BusTests`: single-thread ring wrap, concurrent writer/reader stress
(1 hour of audio compressed into seconds) bit-exact when ordered, no torn
frames, stale-file handling, two writers on one channel, corrupt header,
mismatched rates; `SendPluginTests`: bit-exact passthrough, meter, state;
`ReceiverIntegrationTests`: Send → SideForge in-process offline harness
reproduces native-sidechain behaviour within −90 dB when Align ≥ 1 and ordered;
pluginval and clap-validator for the new plug-in; **manual** DAW matrix
(Reaper/Ableton/FL/Bitwig) recorded alongside E2.
**Acceptance.** In FL Studio: put Send on the kick channel, SideForge on the bass,
`scSource = Send`, bass ducks in time (null test against native routing in Reaper
shows ≤ one-sample or exactly the declared latency offset); no clicks over a
30-minute run; survives project reload with channel assignments intact.
**Risks.** Host processing order and sandboxing (spike); two products to test and
sign; user confusion — mitigated by the status chip and Help.
**Depends on.** E0, E2, E3 (shared installer), E6 (validators). **Estimate.**
Spike 2 days; V6.1 4 days; V6.2 2 days; V6.3 3 days; hardening 3 days = ~14 days.

---

## I1 — Style macros ("EDM pump", "tight gate", "dub swing"…)

**Goal.** One-knob character on top of the existing macro system.

**Current state.** Four macros (`macroDepth`, `macroSeparation`, `macroTiming`,
`macroCharacter`) and `macrosEnabled` already map to underlying parameters
(`MacroMapping.cpp`). No new engine work is needed.

**Requirements.**
1. Add the category `Style` to presets (E4 metadata) with eight styles:
   `EDM Pump`, `Tight Gate`, `Dub Swing`, `Subtle Glue`, `Vocal Pocket`,
   `Trance Gate`, `Hip-Hop Bounce`, `Ambient Breath`.
2. Each style is a factory preset that sets: mode, routing, a curve pattern id
   (V3a) where relevant, `macrosEnabled = true`, and the four macro positions, plus
   the few base parameters the macros do not cover. Styles must leave the macros
   in a useful middle position so the user can then turn them.
3. A **Style** dropdown in the macro panel lists these eight (reads the library;
   no separate data), plus "Custom" when the macros or base parameters have moved
   away from the style.
4. `<Style/>` session node stores the last chosen style id and the macro
   positions at selection time (used for the Custom detection). Additive.
5. Tuning is by ear and recorded: for each style, three audio demos rendered by
   the test harness (kick+bass, drums+pad, vocal+music) kept as WAVs in
   `docs/audio/` (≤ 3 MB total, mono 16-bit 44.1 kHz) so regressions in character
   are audible in review.

**Tests.** Each style loads, is finite, passes the E4 render checks with its own
declared depth range; macro sweeps 0→1→0 produce monotonic depth change (no
dead zones) for each style (test: depth metric increases with `macroDepth`).
**Acceptance.** Blind A/B by two listeners confirms the eight styles are
distinguishable and each sounds appropriate on the three demos.
**Depends on.** E4, V1 (browser category), V3a (patterns). **Estimate.** 3 days
(1 engineering, 2 sound design/tuning).

---

## I2 — Lead with spectral ducking

**Goal.** Make the differentiating feature the first impression, without breaking
any contract.

**Constraints.** The `mode` choice order and parameter defaults cannot change
(compatibility). Everything below is presentation and preset choice.

**Requirements.**
1. First-instance preset (E4 item 7) = `Kick → Bass (Spectral Clean)` when a
   sidechain is detected enabled at first `prepareToPlay`; otherwise
   `Internal Pump (no sidechain)` so there is always an audible result.
2. Editor: the spectral view (V4) is the default visualiser tab on first open in
   Spectral/Hybrid modes; a **"Compare with plain ducking"** button temporarily
   switches to a compressor-only render of the same material using the existing
   A/B mechanism (V1c) with a prepared pair — the audible demonstration of why
   the feature matters.
3. Mode selector tooltips and Help describe Spectral first in prose (visual order
   unchanged).
4. Marketing assets generated from the build: a `SideForgeRender` console tool
   (new CMake target in `Tools/`) that offline-renders the E4 stimuli through
   named presets to WAV and exports the V4 view as PNG — reproducible demo
   material for the README, store pages and videos.
5. README top section rewritten: one paragraph on collision-map ducking with the
   V4 screenshot and a before/after audio link.

**Tests.** First-instance logic unit test (bus enabled → spectral preset; bus off →
internal pump); `SideForgeRender` determinism (hash of output stable across runs
on the same OS).
**Acceptance.** A new user loading the plug-in in a host with a sidechain wired
hears ducking and sees the reduction fill within 10 seconds, without touching a
control.
**Depends on.** E2, E4, V1c, V4. **Estimate.** 2 days.

---

## I3 — Loudness-matched bypass ("Match level")

**Goal.** Let users judge the effect without the "louder sounds better" bias, in
a way that is predictable.

**Why it was deferred.** `PROGRESS.md`: integrated-loudness settling makes a
toggle feel unpredictable. This design removes that by **freezing** the offset
at the moment of comparison.

**Requirements.**
1. New parameter `matchLevel` (bool, default off, **not** MIDI-learn-excluded) and
   read-only display `matchOffsetDb`.
2. Measurement (always-on only while `matchLevel` is true, otherwise skipped):
   K-weighted (BS.1770 stage-1 shelf + stage-2 high-pass) mean-square of the
   **dry (latency-aligned)** and **wet** paths, each over a 3 s exponential window,
   gated: samples below −70 LKFS absolute gate are ignored. Reuse the K-weighting
   filter already implemented for the trigger detector (`TriggerEngine`,
   "Weighted" detector) via a shared header `Source/DSP/KWeighting.h`; do not
   copy it.
3. Offset = `clamp(L_dry − L_wet, −12, +12) dB`, smoothed with a 1 s time
   constant. Applied to the **wet path only**, after `outputGain`, with a 50 ms
   ramp.
4. **Predictability rules:** when the user engages `bypass` while `matchLevel` is
   on, the *current* offset is frozen and applied as the compensating gain to the
   dry output for as long as bypass is on; on un-bypass the offset resumes
   tracking. This makes "bypass ↔ active" an immediate, consistent level-matched
   switch.
5. **Never active in non-realtime render**: when `isNonRealtime()` the feature is
   disabled and the UI chip reads "Match off during render" so it cannot alter a
   bounce. A persistent warning chip shows while `matchLevel` is on: "Level
   match is an audition aid — turn off before bouncing."
6. Does not interact with `autoGain`: autoGain stays as is; the display shows
   both numbers ("Auto +1.2 dB, Match −0.8 dB").
7. State: `matchLevel` is a normal parameter, but its default is off in all
   presets and A/B and Randomise ignore it (`ParamGroups`).

**Tests.** Known signals: wet = dry − 6 dB → offset converges to +6 ± 0.3 dB within
6 s; silence → offset holds, no NaN; steady-state accuracy vs a reference K-weighted
loudness calculation on speech-like noise (±0.5 dB); bypass toggle with freeze is
click-free and the level step across the toggle is < 0.5 dB after convergence;
`isNonRealtime()` disables; CPU overhead < 0.7 % when on, 0 when off.
**Acceptance.** Engaging bypass during a steady program produces a level change of
less than 0.5 dB while the spectral/dynamic character change remains audible.
**Depends on.** E0. **Estimate.** 3 days.

---

# Part 3 — Development plan

## 3.1 Phases and order

Dependencies drive the order; independent packages run in parallel lanes.

```
Phase 0  (wk 1)    E0 Foundations ──┬── E1 CI fixed (needs your run-page error)
                                    └── start in parallel: E3 certificate purchase,
                                        E7 Apple account, DAW licences/VMs

Phase 1  (wk 2-3)  Lane A: E2 (A: code)  → E5 Quick start
                   Lane B: E4 audit + metadata → E4 tests/tuning
                   Lane C: E6 automated tests + sanitiser CI
                   Lane D: E3 installer + release workflow (unsigned first)

Phase 2  (wk 3-5)  Lane A: V1 (a→b→c)         Lane B: V5a → V5b
                   Lane C: V2                 Lane D: V3a → V3b
                   Lane E: V4

Phase 3  (wk 5-7)  I1 (needs E4,V1,V3a)  I2 (needs E2,E4,V1c,V4)  I3
                   E7 macOS (hardware dependent)   E2 Part B matrix (human time)

Phase 4  (wk 6-9)  V6 spike → (decision) → V6.1 → V6.2 → V6.3 → hardening

Release  beta 0.2   = Phase 0+1+ V1,V2,V5      (first real beta to testers)
         beta 0.3   = + V3,V4,I1,I2,I3,E7
         1.0        = + V6 (if the spike passes) and a clean DAW matrix
```

Rationale: ship a stable, installable, validated plug-in with usability features
(0.2) before the large risky item (V6). Weeks assume 2–3 engineers (or Claude
sessions) in parallel; a single engineer should expect roughly the sum of
estimates (~85 working days) and should follow the same order.

Estimates summary (ideal days): E0 1.5 · E1 1.5 · E2 1.5+matrix · E3 2.5 · E4 3 ·
E5 2.5 · E6 4 · E7 3 · V1 6 · V2 3 · V3 6 · V4 4 · V5 5 · V6 ~14 · I1 3 · I2 2 · I3 3
= **~65 days engineering** plus human-only time (DAW matrix ~5 days, listening and
sound design ~4 days, certificates and accounts, lead times).

## 3.2 Branching and release flow

- `main` is always releasable. Protect it: PR required, CI green (E1) required,
  linear history (squash merge) so each package is one revertable commit.
- One branch per package: `feat/<id>-<slug>` (e.g. `feat/v2-learn-threshold`).
  Branch from latest `main`, never from another feature branch, **except** where
  the spec says "depends on" — then wait for the dependency to merge.
- PRs are small and stackable: split a package into the sub-packages above
  (V1a/b/c, V3a/b, V5a/b, V6.0–V6.3), each its own PR (< 600 changed lines of
  non-test code where possible).
- Tag `v0.x.y-beta.N` on `main` → release workflow (E3) → draft release.
- Hotfixes: branch from the release tag, cherry-pick to `main`.

## 3.3 Merge-conflict hot spots and the rule for each

| Shared file | Conflict risk | Rule |
|---|---|---|
| `CMakeLists.txt` | everyone adds sources | One path per line, alphabetically sorted lists (E0). New feature = new files only. Never reformat. Targets for new products go in separate `cmake/*.cmake` files included with one `include()` line. |
| `Parameters/ParamIDs.h` | new IDs | Append inside a clearly delimited block per package: `// ---- <package id> ----` … `// ---- end <package id> ----`. Never edit lines outside your block. |
| `Parameters/ParameterLayout.cpp` | new params | Same: add a `addXxxParams(layout)` function in **your own file** (e.g. `Parameters/LevelMatchParams.cpp`); `ParameterLayout.cpp` gets one call line, appended last. |
| `Parameters/ParamGroups.h` | new rows | Append-only table; sorted by ID; one row per line. |
| `Model/PresetState.cpp/.h` | state nodes | Only E0 touches the version and layout. Later packages add their own node serialiser in their own file and one registration line. |
| `PluginProcessor.cpp/.h` | hooks in `processBlock` and members | Introduce in E0 a small `ProcessorExtensions` pattern: new features register an object implementing `onPrepare / onBlock(begin|end) / onStateSave / onStateLoad`, constructed in one list. Each package adds its own file and one line in the list, not edits to `processBlock`. Where a hook must sit mid-pipeline (V2 histogram, V6 receiver, I3 meters) the line is added in the exact spot named in its spec; keep it to one call. |
| `UI/MainView.cpp/.h` (928 lines) | all UI additions | E0 introduces a `TopBarSlot`/`PanelSlot` registry; each package contributes a component in its own file and one `addSlot(...)` line. Do not edit existing layout code in feature PRs; layout tweaks happen in dedicated PRs by one owner. |
| `Source/UI/Panels.cpp` | shared panels | Same slot rule; new panels get new files. |
| `README.md`, `PROGRESS.md` | everyone documents | Each package adds a section in its own file under `docs/features/<id>.md`; README links to them. `PROGRESS.md` updates happen at merge time by the merger. |
| `Tests/` | new tests | New test files only (GLOB picks them up with `CONFIGURE_DEPENDS`). Never edit another package's tests. |
| `.github/workflows/*` | CI edits | Single owner (E1/E3). Others request changes via issue. |

If E0's slot/extension pattern is judged too heavy, the fallback is a
**merge train**: only one UI-touching PR in flight at a time. The pattern is
preferred because it keeps lanes parallel.

## 3.4 Quality gates in CI (what each PR must pass)

1. Format check (reporting).
2. Windows build + Linux build, Release, warnings-as-errors for new files
   (`-Werror` via a per-file property list, to avoid failing on legacy warnings).
3. Unit + golden-state + preset-render tests.
4. VST3 validator, pluginval (strictness 5 → 10), clap-validator.
5. Linux ASan+UBSan unit run; TSan run on editor/state-fuzz tests (from E6).
6. Allocation guard: tests run `processBlock` under a counting allocator hook and
   fail if any allocation occurs (add in E0; enable per test).
7. Performance guard: a benchmark test renders 10 s of audio for four canonical
   configurations and fails if CPU time regresses > 15 % vs a stored baseline for
   that runner type (store baselines in a file; update deliberately).
8. Size guard: plug-in binary growth > 10 % between PRs needs an explanation.
9. PR template with the Definition of Done checklist (1.1).

Nightly: soak run (2 h randomised), full pluginval at max strictness, build with
`-DSIDEFORGE_ENABLE_LTO=ON` to catch LTO-only issues (the README notes a ~15 min
link).

## 3.5 Test strategy summary

- **DSP truth first.** Every audio claim in a spec is a numeric assertion on a
  deterministic signal built in the test (no checked-in WAVs for tests; WAVs are
  only for human listening in `docs/audio/`).
- **Layering.** Pure-logic units (Randomiser, MidiToCurve, PresetLibrary,
  LevelHistogram, Bus ring) are tested without JUCE audio/GUI; integration tests
  use `SideForgeProcessor` offline; editor tests use the headless editor
  already in `EditorTests`; DAW behaviour is the manual matrix.
- **Regression anchors.** Golden v2 state (compatibility), preset render
  expectations (E4), screenshot goldens per-OS.
- **Fuzz** every parser we add: preset files, pattern files, MIDI import, bus
  header, state blobs.
- **Human gates** are explicit tasks with owners and a place to record results
  (`docs/DAW_COMPATIBILITY.md`, `docs/PRESET_AUDIT.md`, listening notes in PRs).

## 3.6 Risk register

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R1 | CI failure cause unknown | now | blocks Windows build | E1: make failures observable first; need the run-page error. |
| R2 | MSVC-only compile errors | medium | schedule | First Windows CI run early (E1); fix before feature work lands. |
| R3 | Host sidechain quirks (FL, Ableton) | high | usability | E2 indicator + matrix; V6 as an alternative path. |
| R4 | V6 host ordering/sandbox makes alignment impossible | high | V6 cancelled or reduced | Spike V6.0 with explicit go/no-go; scoped fallback with declared latency. |
| R5 | Parameter/state compatibility break | low if rules followed | severe (broken sessions) | Contract 1.2, golden test, one batched version bump. |
| R6 | Audio-thread allocation/lock slips in | medium | glitches in hosts | Allocation-guard test, code review checklist, sanitisers. |
| R7 | Merge conflicts in `MainView`, `PluginProcessor` | high | schedule | Slot/extension patterns (E0), append-only blocks, small PRs, merge train fallback. |
| R8 | Certificate / Apple account lead time | high | release date | Start in Phase 0; unsigned beta path documented. |
| R9 | Preset quality is subjective | medium | perceived quality | Numeric render tests + listening checklist + two-listener sign-off. |
| R10 | Scope creep in V1/V4 | medium | schedule | Acceptance criteria are the scope; optional items are labelled and excluded. |
| R11 | No access to some DAWs / OSes | medium | untested claims | Say so in the compatibility doc; mark as "untested", never "supported". |
| R12 | CPU growth from the new features | medium | usability | Publish-when-visible rule (V4), histogram only while learning, K-weighting only when on; performance guard in CI. |

---

# Part 4 — Maximising quality and speed

1. **Fix observability before features.** One red unexplained CI job costs more
   than any feature. E1 and E0 first.
2. **Make small PRs and merge daily.** Long branches are the source of merge
   pain. Use the sub-packages; behind-a-flag code that is not user-visible can
   merge early (a hidden `EditorSettings` developer flag per feature).
3. **Write the failing test first** for every numeric claim in a spec; the specs
   are written so each acceptance bullet is already a test.
4. **Worktrees for parallel lanes.** `git worktree add ../sf-v2 -b feat/v2-learn`
   gives each lane its own checkout and build directory; use a separate build dir
   per lane and `ccache` so incremental builds take seconds, not minutes. Keep
   `JUCE/` shared via a single clone and `git worktree` or a symlink.
5. **One owner per shared file** (CI workflows, `MainView` layout, `ParamIDs.h`
   header). Others request changes through the owner or the slot mechanism.
6. **Use parallel agents deliberately.** Good fits for separate AI sessions:
   independent, well-bounded packages with pure logic and tests (V2 histogram,
   V3b MIDI conversion, V5b randomiser, I3 K-weighting meter, E6 test suite).
   Poor fits: anything editing `MainView.cpp` or `PluginProcessor.cpp`, anything
   needing a real DAW, and sound design. Give each session this document's spec,
   the file-ownership rule, and the Definition of Done; review diffs, not
   summaries.
7. **Keep the DSP/UI split.** Every package puts logic in a non-GUI class with
   a test and a thin component; this is what makes parallel work and fast tests
   possible, and it matches the existing architecture ("everything is a gain
   signal", engines testable alone).
8. **Profile early.** Add the CPU benchmark in E0 so every package sees its own
   cost; set a CPU budget per feature (V4 < 1 %, I3 < 0.7 % when on, V2 0 % when
   idle).
9. **Automate the boring human work.** DAW test projects as templates (kick/bass
   stems generated by the test tool), a checklist issue template per DAW, a
   diagnostics-copy button (E3) so bug reports arrive with the facts.
10. **Beta feedback loop.** Release 0.2 to 5–10 testers with the installer, ask for
    the diagnostics text and the DAW name; keep a public known-issues list; triage
    weekly. Real-world reports will outrank any internal guess about what to
    build next (and will tell you whether V6 is even needed).
11. **Decide with data at gates.** Gate 1 (after Phase 1): does it install, load,
    duck, and survive pluginval in three DAWs? Gate 2 (after Phase 2): do testers
    use the browser, Learn and A/B? Gate 3 (V6.0): can alignment work? Do not start
    the next phase until the gate result is written down.
12. **Documentation as part of done.** The README is already thorough; keep it
    that way by requiring `docs/features/<id>.md` in each PR and a Help entry,
    generated from the same string table (E5) so UI and docs cannot disagree.
13. **Protect the contract.** Add a CI step that diffs `ParamIDs.h` and the choice
    lists against the last release tag and fails if an ID disappeared or a choice
    moved — a 20-line script that prevents the worst possible bug class.
14. **Security hygiene.** Pin action SHAs, least-privilege workflow permissions,
    signing only from protected environments on tags, never put certificates in
    the repo, treat every file we parse (presets, MIDI, bus files, states) as
    untrusted input — fuzz it.

---

# Appendix A — Parameter and state additions (all packages)

| Package | New parameter IDs (all new, defaults preserve old behaviour) | State additions |
|---|---|---|
| E0 | — | `kStateVersion = 3`, empty `<AB/>`, `<Style/>` |
| V6 | `scSendChannel` (int 1–16, 1); `scSource` gains choice index 2 `"Send"` | — |
| I3 | `matchLevel` (bool, off) | — |
| V1 | — (no automatable parameters) | `<AB active slotA slotB/>` |
| I1 | — | `<Style id macros=…/>` |
| V6 Send plug-in | `sendChannel`, `sendGain`, `sendMute`, `sendWhenBypassed` (separate plug-in, own IDs) | own state |

Everything else (V2, V3, V4, V5, E-packages) adds **no** persisted parameters.
User settings (first-run flags, favourites, sensitivity, locks, developer flags)
live in `EditorSettings`, outside the session.

# Appendix B — New files by package (for ownership and review)

```
E0  Utilities/SeqLock.h  UI/EditorSettings.{h,cpp}  Parameters/ParamGroups.h
    Tests/State/GoldenStateTests.cpp  Tests/State/golden_v2.xml  .clang-format
E1  .github/workflows/build.yml (edited)
E2  docs/DAW_COMPATIBILITY.md  UI/ScStatusChip.{h,cpp}
E3  installer/SideForge.iss  .github/workflows/release.yml  Scripts/bump-version.ps1
E4  docs/PRESET_AUDIT.md  Tests/DSP/FactoryPresetRenderTests.cpp
E5  UI/QuickStartOverlay.{h,cpp}  UI/HelpContent.{h,cpp}
E6  Tests/DSP/HostEdgeTests.cpp  Tests/State/StateFuzzTests.cpp  nightly workflow
E7  cmake/Apple.cmake  installer/macos/*  Scripts/install-linux.sh
V1  Model/PresetMeta.h  Model/PresetLibrary.{h,cpp}  Model/AbCompare.{h,cpp}
    UI/PresetBrowser.{h,cpp}
V2  DSP/LevelHistogram.h  UI/LearnButton.{h,cpp}
V3  Model/CurvePatterns.{h,cpp}  Model/MidiToCurve.{h,cpp}  UI/PatternMenu.{h,cpp}
V4  DSP/SpectralGainSnapshot.h  (SpectrumView.cpp extended)
V5  Model/Randomiser.{h,cpp}  Model/CurveEditAction.{h,cpp}  UI/RandomiseButton.{h,cpp}
V6  Bus/SideForgeBus.{h,cpp}  Send/*  Parameters/SendReceiveParams.cpp
I1  (presets + UI/StyleSelector.{h,cpp})
I2  Tools/SideForgeRender.cpp
I3  DSP/KWeighting.h  DSP/LevelMatch.{h,cpp}
```
