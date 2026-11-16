# Codebase Map

| I want to... | Look in |
|---|---|
| Compressor | `Source/DSP/CompressorEngine.*` |
| Gain curve | `Source/DSP/CurveEngine.*` |
| Multiband and mid/side | `Source/DSP/BandMatrix.*`, `BandProcessor.*`, `Crossover.h`, `LinkwitzRileyCrossover.h` |
| Envelope and delay | `Source/DSP/EnvelopeFollower.h`, `DelayLine.h` |
| Latency and metering | `Source/DSP/LatencyManager.h`, `Metering.*`, `AnalysisBus.h` |
| Plugin processor and editor | `Source/` (see `ARCHITECTURE.md`) |
| Tests | `Source/` test targets via `CMakeLists.txt` (`SideForgeUnitTests`) |
| Build | `CMakeLists.txt`, `Scripts/build.ps1` |
| Architecture and progress | `ARCHITECTURE.md`, `PROGRESS.md` |
