# Developer Guide

- Windows x64, JUCE 8. Build with `Scripts/build.ps1` or CMake (see README "Building").
- CTest targets: `SideForgeUnitTests` (189 deterministic tests covering DSP, state, editor, MIDI learn and the Band Matrix) and `SideForgeVst3Validation`. The README lists what the tests cover.
- The editor test checks that every parameter has a control and every control points at an existing parameter. Keep that invariant when adding parameters.
- Not built or run in this documentation pass (Windows and JUCE build).
