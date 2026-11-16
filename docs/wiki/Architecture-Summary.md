# Architecture Summary

From `ARCHITECTURE.md` and the README.

- **Gain-signal design.** The compressor and the curve each emit a `0..1` multiplier. Blending is multiplication, and the band stage is the only audio-path stage (README).
- **Layout.** `Source/DSP/` holds the engines (compressor, curve, band matrix and processor, crossover, envelope follower, delay line, latency manager, metering and analysis bus). See `ARCHITECTURE.md` for the module breakdown.
- **Latency reporting.** Quality modes report latency that the host compensates. An impulse test pins the reported latency to the real delay (README, "Testing").
- **Tests as contracts.** Tests compare with tolerances rather than by ear, and check parameter-to-control consistency (README).
