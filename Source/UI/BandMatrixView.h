#pragma once
#include "Controls.h"
#include "../DSP/BandMatrix.h"
#include "../Parameters/BandMatrixParameters.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <functional>

namespace sf
{
class SideForgeProcessor;

/**
    The Band Matrix display: a frequency axis with the bands drawn on it, and a
    card for each band underneath.

    The spectrum is transformed **here**, on the message thread, from raw samples
    the audio thread pushed into `AnalysisBus::RawTap`. That is the whole reason
    the tap exists: a visualisation FFT has no business running in `processBlock`.

    Each band is drawn in its own colour, tinted across the frequency region it
    actually covers, and the region contracts as the band ducks — so "hard lows,
    light highs" is visible as a deep pull at the left and a small one at the
    right, rather than a number you have to read.

    Crossover handles are dragged directly. They write to the same parameters the
    Band panel exposes, so a drag is undoable and automatable like any other edit.
*/
class BandMatrixView : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    BandMatrixView (SideForgeProcessor&, const ControlContext&);
    ~BandMatrixView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Reads the meters and re-analyses. Called from the editor's timer. */
    void refresh();

    void setReducedAnimation (bool shouldReduce) { reduced = shouldReduce; }
    void setAnalysisEnabled (bool shouldAnalyse);

    /** The band whose advanced controls the lower panel is showing. */
    int  getSelectedBand() const noexcept { return selectedBand; }
    void setSelectedBand (int index);
    std::function<void (int)> onBandSelected;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** The colour a band is drawn in, shared with the cards and the panel. */
    static juce::Colour bandColour (int index);

private:
    struct Card
    {
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> enableButton, bypassButton, soloButton, muteButton;
    };

    void analyse();
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float> area,
                       const std::vector<float>& magnitudesDb,
                       juce::Colour colour, float fillAlpha) const;
    void drawBandRegions (juce::Graphics&, juce::Rectangle<float> area) const;
    void drawCrossoverHandles (juce::Graphics&, juce::Rectangle<float> area) const;
    void drawFrequencyGrid (juce::Graphics&, juce::Rectangle<float> area) const;
    void drawCard (juce::Graphics&, int band) const;

    float frequencyToX (float hz, juce::Rectangle<float> area) const;
    float xToFrequency (float x, juce::Rectangle<float> area) const;

    int crossoverHandleAt (juce::Point<int>) const;
    int cardAt (juce::Point<int>) const;
    void toggleBandParameter (int band, const char* parameterId);

    int activeBandCount() const;

    static constexpr int kFftOrder = 11;
    static constexpr int kFftSize  = 1 << kFftOrder;
    static constexpr int kNumBins  = kFftSize / 2 + 1;
    static constexpr float kMinHz = 20.0f;
    static constexpr float kMaxHz = 20000.0f;
    static constexpr float kTopDb = 0.0f;
    static constexpr float kFloorDb = -90.0f;

    SideForgeProcessor& processor;
    ControlContext context;

    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kFftSize,
                                                 juce::dsp::WindowingFunction<float>::hann };

    std::vector<float> scratch;          // 2 * kFftSize, the transform workspace
    std::vector<float> originalDb, processedDb, sidechainDb;

    std::array<float, kMaxMatrixBands> bandReduction {};
    std::array<float, kMaxCrossovers> shownCrossover { 120.0f, 2500.0f, 8000.0f };

    juce::Rectangle<int> spectrumArea, cardArea;
    std::array<Card, kMaxMatrixBands> cards;

    int selectedBand = 0;
    int draggingHandle = -1;
    int hoverHandle = -1;
    int hoverCard = -1;
    bool reduced = false;
    bool analysisEnabled = true;
    int refreshCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandMatrixView)
};
} // namespace sf
