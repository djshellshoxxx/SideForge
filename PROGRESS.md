# SideForge — Progress

## Current phase
**Phase 1 — Foundation** (in progress)

## Toolchain (verified 2026-09-14)
| Tool | Version / path |
|---|---|
| CMake | 4.4.3 |
| Compiler | MSVC 14.44.35207 (VS 2022 **Build Tools**, `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`) |
| Windows SDK | 10.0.26100.0 |
| Generator | `Visual Studio 17 2022`, `-A x64` |
| JUCE | 8.0.15, shallow clone in `JUCE/` (git-ignored) |
| Git | 2.54.0 |

> Note: only the VS **Build Tools** are installed — there is no full IDE. Build from the
> command line with the documented `cmake --build` commands.

## Exact build / test commands
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target SideForge SideForgeTests
.\build\SideForgeTests_artefacts\Release\SideForgeTests.exe
```
Debug: swap `Release` for `Debug` in both lines.

## Completed
- (nothing marked complete yet — Phase 1 is being built)

## Active problems
- none

## Next actions
1. Finish Phase 1: configure + build + green tests, then commit.
2. Phase 2 — sidechain detector + compressor engine.

## Architectural decisions
| # | Decision | Why |
|---|---|---|
| 1 | JUCE **8.0.15**, not 9.x | 9.x is very new; 8.0.15 is the last well-settled 8 release and every API used here is stable in it. Upgrading later is a contained change. |
| 2 | Core sources compiled into both the plugin and the test runner rather than a shared static lib | JUCE module targets inject their sources into every consumer; putting them in a static lib and also linking them into the plugin risks duplicate symbols on MSVC. Recompiling ~15 small files is cheaper than that class of bug. |
| 3 | The test runner links the *real* `SideForgeProcessor` and the GUI modules | Lets the deterministic tests cover bus layouts, state recall and editor-open/closed behaviour instead of a stand-in. |
| 4 | Every engine emits a **gain signal**, not audio | Compressor and curve both produce a broadband `0..1` multiplier; spectral produces a per-bin multiplier. Makes "Hybrid" mode a defined combination and each engine independently testable. |
| 5 | Curve evaluation goes through a rendered lookup table published via a 3-slot lock-free exchange | The node list is a `std::vector` edited on the message thread; the audio thread must never walk it. Table lookup is also branch-free and cheap at sample rate. |
| 6 | Parameter IDs carry a version hint (`juce::ParameterID{id, 1}`) and are declared in one header | IDs are a public contract with host automation lanes; centralising them makes accidental renames obvious. |
| 7 | Curve segment types are `Curve` / `SCurve` / `Step` | "Straight", "ramp" and "curved" are all the `Curve` type at different tension values, so three types cover the required four behaviours without redundant UI. |
| 8 | State is a versioned wrapper tree (`SideForgeState`) around the APVTS tree | Allows non-parameter data (curve nodes, editor size) in the same blob and gives `PresetState::migrate` somewhere to hook in. A bare APVTS tree is still accepted on load. |

## Deferred / not yet exposed
- Nothing deferred yet.
