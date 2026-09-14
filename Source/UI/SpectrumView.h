#pragma once
#include "../DSP/SpectralDucker.h"
#include "Theme.h"
#include <vector>

namespace sf
{
/**
    The spectral half of the display: what the two sources contain, where they
    collide, and what the plugin is taking out as a result.

    Everything comes from the `SpectralSnapshot` the ducker publishes through a
    triple buffer at ~50 Hz. Nothing here transforms anything; the FFT has already
    been done on the audio thread as part of the processing, so displaying it is
    free rather than a second analysis.
*/
class SpectrumView : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    SpectrumView();

    void setSource (SpectralDucker* ducker) { source = ducker; }

    /** Reads the newest snapshot and repaints if it changed. */
    void refresh();

    void setFrozen (bool shouldFreeze) { frozen = shouldFreeze; repaint(); }
    void setReducedAnimation (bool shouldReduce) { reduced = shouldReduce; }
    void setShowDelta (bool shouldShow) { showDelta = shouldShow; repaint(); }
    bool getShowDelta() const noexcept { return showDelta; }

    /** Draws the band edges the spectral stage is limited to. */
    void setFrequencyRange (float lowHz, float highHz);
    /** Draws the crossover points when the band stage is in three-band mode. */
    void setCrossovers (bool threeBand, float xover1Hz, float xover2Hz);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    float frequencyToX (float hz, juce::Rectangle<float> area) const;
    float xToFrequency (float x, juce::Rectangle<float> area) const;
    float dbToY (float db, juce::Rectangle<float> area) const;

    void drawFrequencyGrid (juce::Graphics&, juce::Rectangle<float>) const;
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>, const std::vector<float>& dbValues,
                       juce::Colour colour, float fillAlpha) const;
    void drawCollision (juce::Graphics&, juce::Rectangle<float>) const;
    void drawAttenuation (juce::Graphics&, juce::Rectangle<float>) const;
    void drawRangeMarkers (juce::Graphics&, juce::Rectangle<float>) const;
    void drawReadout (juce::Graphics&, juce::Rectangle<float>) const;

    static constexpr float kMinHz = 20.0f;
    static constexpr float kMaxHz = 20000.0f;
    static constexpr float kTopDb = 6.0f;
    static constexpr float kFloorDb = -84.0f;

    SpectralDucker* source = nullptr;

    // Locally smoothed copies, so the display glides instead of flickering.
    std::vector<float> mainDb, sideDb, collision, gainDb;
    int numBins = 0;
    float sampleRate = 48000.0f;
    int fftSize = 1024;

    bool frozen = false;
    bool reduced = false;
    bool showDelta = false;
    int refreshCounter = 0;
    float hoverX = -1.0f;

    // Overlays driven by the parameters rather than by the analysis.
    float rangeLow = 20.0f, rangeHigh = 20000.0f;
    bool  showCrossovers = false;
    float crossover1 = 180.0f, crossover2 = 2500.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumView)
};
} // namespace sf
