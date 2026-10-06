# Circuit Drift Labs — Shared Audio Plugin Standard

**Status:** Required shared baseline for Circuit Drift Labs audio plug-ins  
**Applies to:** VST3, Audio Unit (AU), CLAP, and companion standalone builds  
**Version:** 1.0  
**Last reviewed:** 2026-10-06

This is the common product, interaction, visual, host-integration, and quality baseline for CDL plug-ins. Each repository must link to this document and keep its own product specification as the source of truth for the plug-in’s unique purpose, signal flow, parameters, routing, and exceptions. This standard defines shared behavior; it does not require every plug-in to have the same feature set.

## 1. How to apply this standard

### 1.1 Requirement terms

- **MUST** means required for every applicable target.
- **SHOULD** means expected unless the project specification documents a good reason to differ.
- **MAY** means optional.
- **Conditional** means required only when the project includes the relevant capability or target.

The product specification may define stricter requirements. If a product requirement conflicts with this baseline, document the conflict and the chosen behavior in the product specification or a short decision record. Do not silently leave a requirement ambiguous.

### 1.2 Product profile

Every product specification MUST declare which profiles apply:

- **Effect plug-in:** processes host audio and declares its supported input/output layouts.
- **Instrument plug-in:** generates audio, receives MIDI or another declared host event format, and declares its output layouts.
- **MIDI effect / processor:** receives and transforms MIDI without claiming audio behavior it does not provide.
- **Standalone companion:** opens audio/MIDI devices directly and may provide device configuration and independent rendering features.
- **Offline renderer/exporter:** renders audio to a file outside the host’s normal real-time processing path.

A hybrid product may select more than one profile. Requirements for a profile apply only to targets that implement it. A plug-in MUST NOT expose standalone-only device controls inside a host plug-in unless the product specification gives a specific, host-safe reason.

### 1.3 Project compliance record

Each repository MUST include a short compliance table in its product specification or `docs/` index. Mark each baseline area **Implemented**, **Not applicable** with a reason, **Planned**, or **Exception** with a link to the decision. This keeps the shared standard testable without forcing irrelevant features into a product.

## 2. Shared product behavior

### 2.1 Predictable controls

- Every visible control MUST have a clear label and a defined relationship to the product specification’s parameter or action.
- A control MUST respond consistently to mouse, touch where supported, keyboard focus where practical, host automation, and MIDI mapping where implemented.
- Changing one parameter MUST NOT disable, reset, or alter another unless the product specification explicitly describes that dependency.
- Discrete modes MUST show their current state. Momentary actions MUST provide visible feedback and must not remain latched after focus changes unless designed as a latch.
- Destructive or state-clearing actions MUST be clearly named and provide an undo or confirmation path where practical.
- Continuous controls MUST use appropriate scaling (for example, logarithmic frequency/time ranges) and expose units in labels, readouts, or tooltips.

### 2.2 Parameter contract

- Automatable controls MUST have stable, unique parameter IDs. Once released, an ID MUST NOT be reused for a different meaning. Renames may change display text, not identity.
- Parameter ranges, defaults, units, formatting, and discrete choices MUST be documented and agree between code, GUI, presets, and host automation.
- Host-originated automation MUST update the GUI without feedback loops. GUI changes MUST notify the host using the plug-in framework’s supported parameter mechanism.
- Parameter changes received during audio processing MUST be applied safely and smoothly where abrupt changes could click, zipper, or destabilize the sound. Smoothing time and behavior are product-specific and MUST be documented for important controls.
- A parameter edit MUST not perform disk, network, blocking UI, or unbounded work on the audio thread.

### 2.3 Common control gestures

The project MUST document the gestures it supports. The following are the shared defaults where the framework and host permit them:

- Single click/drag changes the value; modifier keys MAY provide fine adjustment.
- Double-click resets to the documented default when reset behavior is safe and unambiguous.
- Hover/focus exposes a concise tooltip. Tooltips MUST be suppressible globally or follow the platform’s accessibility preference when feasible.
- A context menu SHOULD provide value entry and reset. Copy/paste is appropriate for scalar values, but MUST validate range, units, and parameter type before applying.
- MIDI Learn and unlink controls are conditional on the product implementing MIDI control mapping; do not show nonfunctional menu items.
- A direct numeric-entry editor MUST reject malformed or out-of-range input without silently changing to an unrelated value.

The right-click menu is not required to repeat every action on every widget. Context actions MUST fit the control type and remain consistent across the product.

### 2.4 Accessibility and usability

- Important information MUST not rely on color alone. Pair color states with text, shape, position, or another cue.
- Text and interactive controls MUST remain legible at supported display scaling and HiDPI settings.
- Keyboard navigation SHOULD cover primary controls, menus, and dialogs. Focus state MUST be visible.
- Animation MUST be nonessential to operating or understanding the audio. Where motion is prominent, provide a reduced-motion or disable-animation option.
- Tooltips MUST explain what a control affects in product terms, not merely repeat its name. Critical setup and safety information belongs in visible help or labels, not tooltip-only text.

## 3. Plug-in host and audio behavior

### 3.1 Host responsibilities and device settings

A plug-in uses the host for audio device selection, sample rate, buffer size, transport, and channel routing. It MUST NOT attempt to open or reconfigure the host’s audio device. Standalone companions MAY provide driver, device, sample-rate, buffer-size, and MIDI-device settings appropriate to the platform.

Standalone device settings MUST handle unavailable devices and failed opens clearly, remember valid choices safely, and allow recovery to a working device. A standalone test tone is optional, MUST require an explicit user action, and MUST start at a conservative level with a clear stop control.

### 3.2 Real-time safety

The real-time audio callback MUST avoid memory allocation/deallocation, locks, waits, file I/O, network I/O, logging, UI calls, and unbounded algorithms. Prepare buffers and state before processing; release resources outside the callback. Any cross-thread communication MUST have a defined ownership and synchronization strategy.

- The processor MUST handle host changes to sample rate, maximum block size, play state, and supported channel layout without using stale assumptions.
- Processing MUST remain valid for variable and unusually small block sizes, including zero-length blocks if the framework can deliver them.
- Outputs MUST remain finite for valid input and parameter states. NaN/Inf input or invalid state MUST not propagate uncontrolled corruption through the signal chain.
- Silence, bypass, and reset behavior MUST be intentional and documented. A product MUST not unexpectedly emit audio when idle, bypassed, stopped, or unconfigured.
- Denormal handling, tail reporting, latency reporting, and bypass semantics MUST be implemented where relevant to the DSP and format.
- If the plug-in introduces latency, it MUST report it to the host and keep the report accurate as operating modes change.

### 3.3 Format, buses, and transport

Each product specification MUST list supported plug-in formats, platforms, architectures, input/output bus layouts, MIDI/event behavior, latency, tail behavior, and known host limitations. The implementation MUST reject unsupported layouts safely and MUST NOT silently reinterpret channel meaning.

Where transport or tempo sync is offered, the plug-in MUST define behavior for stopped transport, missing tempo, tempo changes, loop boundaries, playhead jumps, and host seeks. Event-triggered behavior MUST be sample-accurate to the extent supported by the format and host; documented limitations are acceptable.

### 3.4 Bypass, levels, and meters

- Effects MUST provide the host’s expected bypass behavior or explain a deliberate product-specific alternative. Wet/dry and internal bypass controls MUST not produce clicks on transitions.
- Instruments MUST define what happens with no MIDI/event input and after note-off, including voice release and all-notes-off handling.
- Meters MUST represent a named signal point and unit. Input, output, gain reduction, and modulation meters MUST not be conflated.
- A digital output meter SHOULD indicate 0 dBFS or overs as clipping/over-range. Floating-point internal signals above 0 dBFS are not automatically proof of audible clipping; labeling MUST match the measured point.
- Peak hold, averaging, ballistics, and reset behavior MUST be consistent and documented where the meter is used for decisions.

## 4. State, presets, and files

### 4.1 Host state

The host’s save/restore state is the authoritative project-recall mechanism for plug-in instances. State MUST include all parameters and any non-parameter data required to reproduce the session, while excluding transient UI state unless the product specification says otherwise. State must be versioned or otherwise safely migratable.

- Loading valid saved state MUST restore the sound and meaningful product state without relying on the original machine’s temporary files.
- Missing, old, malformed, or future-version state MUST be handled safely. Do not crash, partially apply arbitrary values, or overwrite user data on parse failure.
- State restore MUST not perform long blocking work in the audio callback. Large assets SHOULD be loaded or prepared asynchronously with a safe fallback and clear status.
- Window size, selected tab, and similar view preferences MAY be saved separately from sound state.

### 4.2 Presets

Presets are conditional on the product having meaningful preset states. Preset names and categories SHOULD use a consistent browser pattern, but the preset content must suit the product. The product MUST define how presets interact with host automation and external assets.

JSON is not a universal required preset format. If JSON or another portable format is used, validate its schema, version, ranges, size, and paths before applying it. Presets MUST NOT execute code. Preset loading MUST be atomic from the user’s perspective: on failure, retain the current sound and explain the error. Factory presets MUST be documented and redistributable under the project’s stated terms.

A/B comparison is optional and SHOULD be provided when users frequently compare parameter states. If included, A and B states must be distinct, switching must be predictable, and saving/loading must clearly identify which state is affected.

### 4.3 Audio export and file loading

Offline WAV export is conditional. It applies when the product specification promises rendering, sample creation, or file export. If provided, export settings MUST state sample rate, bit depth or floating-point format, channel count, duration/source, normalization/limiting behavior, and destination. Export MUST not block or destabilize real-time playback unless explicitly offered as a documented offline operation.

File-loading products MUST state supported formats, size/duration limits, conversion behavior, failure messages, and whether paths or media are stored in host state. Validate untrusted files and handle cancellation, truncated files, unsupported codecs, and missing media without crashing.

## 5. MIDI, modulation, and automation

MIDI input, MIDI Learn, internal modulation, and macro controls are conditional on the product profile.

- Instrument and MIDI products MUST define note-on, note-off, velocity, pitch bend, sustain, all-notes-off, overlapping notes, voice-stealing, and channel behavior for the events they support.
- MIDI Learn MUST indicate learn mode, show the mapping result, handle timeout/cancel, and provide a reliable way to remove or change mappings. Conflicting mappings MUST be resolved predictably.
- MIDI CC and host automation MUST have defined precedence and scaling when controlling the same parameter.
- Modulation MUST not mutate the user’s base parameter value unless explicitly designed to do so. Users SHOULD be able to distinguish base value from modulated result.
- MIDI and automation input MUST be robust to dense event streams and invalid or unsupported messages.

## 6. Circuit Drift Labs visual identity

The interface should feel like one CDL family: dark, clear, technical, and audio-focused. Product-specific graphics and one product accent may distinguish instruments, effects, and tools, while common neutrals, typography, spacing, and interaction language remain recognizable.

### 6.1 Palette

| Role | Default |
|---|---|
| Background | `#0E1116` |
| Panel surface | `#171B22` |
| Border / separator | `#2A303A` |
| Primary accent | `#E8532A` |
| Secondary accent | `#4FB6C4` |
| Primary text | `#E6E8EC` |
| Muted text | `#8A929E` |
| Success / active signal | `#7BC96F` |
| Warning | `#F2C14E` |
| Error / clip | `#EF5350` |

A product MAY replace the primary accent with one documented identity color. Contrast, state meaning, and accessibility remain required. Avoid using red/orange alone to convey unrelated states.

### 6.2 Typography and layout

- Use a clean sans-serif for interface text; Inter is preferred where licensing and bundling allow, with a platform sans-serif fallback. Use a legible monospace with tabular numerals for values where alignment helps.
- Use compact labels, readable values, clear hierarchy, and consistent spacing. Avoid tiny text that becomes unreadable at common Windows scaling or macOS Retina scaling.
- Layout dimensions in any mockup are reference values, not hard-coded universal pixel sizes. The editor MUST fit its minimum supported size and handle resizing or clearly state a fixed-size design.
- Product title, preset/state controls, and product options SHOULD occupy a consistent header region when those functions exist. Version and product identity SHOULD be available in the interface or help/about view.
- A subtle CDL brand mark MAY be used. Decorative LED, matrix-text, circuit, or background animation is optional and MUST never obscure controls, consume significant audio-thread resources, or reduce legibility.

### 6.3 Shared control language

Use a consistent visual language for knobs, sliders, buttons, selectors, menus, meters, and value fields. Components may be resized or adapted for the job. A control MUST have sufficient hit area, clear active/disabled states, and a visible focus state. Do not force a rotary knob onto a control that is more naturally represented as a switch, curve editor, grid, or numeric field.

Suggested reference sizes: 36–48 px for a typical knob body, 4–6 px for a slider track, and 24–32 px for a compact button height. Adjust for product density, platform scaling, and accessibility. Values and units should be visible during interaction and persistently visible for critical settings.

Meters should use the product’s relevant units and a restrained color gradient. A meter is not required where it would not communicate a meaningful signal or measurement.

## 7. Shared options, help, and onboarding

### 7.1 Options

A plug-in MAY offer plug-in-specific options such as UI scaling, reduced motion, tooltip behavior, or product behavior. It MUST NOT duplicate host-owned audio driver, device, sample-rate, or buffer controls inside the plug-in.

A standalone companion MAY provide audio/MIDI device setup and an explicit, conservative-level test signal. A global animation switch is required only if the product includes optional motion beyond necessary meters or state feedback.

### 7.2 Help and support

Each product MUST provide accessible help documentation in its repository. The interface SHOULD include a Help/About entry with product name, version, essential usage guidance, supported formats/platforms, and a link to the CDL site: <https://djshellshoxxx.github.io/circuitdriftlabs/>. A support link or section must not imply a staffed service unless one exists. Keep help synchronized with shipped behavior.

### 7.3 Onboarding and Easter eggs

A first-run guided tour is optional. If included, it MUST be skippable, non-blocking, and restartable from Help. Core operation MUST remain discoverable without completing a tour. Easter eggs are optional, must not hide required functionality, change a user’s sound unexpectedly, or interfere with accessibility, licensing, or host automation.

## 8. Privacy, security, and reliability

- Products MUST describe network access, telemetry, analytics, licensing checks, and update checks accurately. If none are used, say so in the product documentation.
- Network access MUST be limited to a documented product purpose. Do not send audio, presets, project content, or diagnostics without clear user action and disclosure.
- File and preset parsers MUST validate input and bound memory/time use. File paths MUST be handled safely across platforms.
- Failures MUST leave audio processing in a safe state and preserve the user’s current project where practical. User-facing errors should identify the failed action and a recovery step.
- Logs MUST avoid recording private audio content, license secrets, or unnecessary identifying data. Diagnostic export, if offered, should be opt-in and reviewable before sharing.

## 9. Documentation and quality gates

Each plug-in repository MUST document its supported formats/platforms, install/build instructions, product-specific feature behavior, parameter IDs and ranges, state/preset format, file formats, known limitations, privacy/network behavior, and validation status. The shared baseline is not a substitute for a product specification, signal-flow description, parameter reference, or release notes.

Before a beta or release, complete and record applicable checks:

1. **Requirements and wiring:** map each product requirement and visible control to implementation or a documented non-applicability; verify no dead controls, orphan parameters, or incomplete signal paths.
2. **Build and format checks:** build every claimed target on its supported toolchain; run format validators such as pluginval where available and record the version/settings.
3. **DSP and state tests:** test representative edge cases, odd/small block sizes, sample-rate changes, automation, state round trips and migrations, invalid state, silence, extreme valid values, and finite output.
4. **Host checks:** load, save, restore, automate, bypass, resize, and close in a representative host for each claimed format/platform where available. Record untested combinations instead of implying universal compatibility.
5. **Performance and stability:** profile representative worst-case settings; verify the audio callback rules; run a soak test appropriate to project maturity and record duration and outcome.
6. **Combinatorial coverage:** use equivalence classes, boundary values, pairwise coverage, and targeted critical combinations. Exhaustive testing is required only when the finite state space is small enough to run meaningfully.
7. **Fuzzing:** fuzz parsers, preset/state loading, and event streams when practical. Randomize parameters and block sizes in DSP tests with fixed seeds for reproducibility. Record scope and duration.
8. **Release status:** use **BETA READY** only when the beta checklist is complete and known blockers are listed. Use **RELEASE READY** only when release-specific checks, packaging, licenses, and artifacts have also been verified.

Do not claim a test passed unless it was run and its result was checked. CI results, manual host tests, and known gaps should be linked or summarized in the repository’s QA document.

## 10. Required links in each applicable repository

Each applicable repository MUST:

1. Store this document at `docs/standards/CDL_PLUGIN_BASELINE.md` (or link to a single canonical copy if repository policy prevents duplication).
2. Link it from the README’s documentation or development section.
3. Link it from the product specification/index and mark it as the shared required baseline.
4. Maintain the profile selection and compliance record described in §1.3.
5. Document any exceptions beside the affected product requirement, not only in this shared document.

When this standard changes, update the shared copy and synchronize copies across applicable repositories. Keep product-specific requirements in each product’s own specification.
