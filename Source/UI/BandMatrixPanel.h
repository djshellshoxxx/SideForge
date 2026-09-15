#pragma once
#include "Panels.h"
#include "../Parameters/BandMatrixParameters.h"
#include <array>
#include <functional>

namespace sf
{
/**
    The Band Matrix's advanced controls.

    It holds a full set of controls for **all four** bands and shows one at a
    time. Keeping the hidden ones alive rather than rebuilding on selection means
    every parameter always has a live control — which is what the editor's
    coverage test checks — and makes switching bands instant.

    Above them sits the matrix-wide row: routing, band count, the crossovers and
    the two matrix macros.
*/
class BandMatrixPanel : public PanelBase
{
public:
    BandMatrixPanel (const ControlContext&, std::function<void (int)> onBandSelected);

    void updateRelevance (const EngineSettings&) override;

    /** Shows one band's controls and hides the rest. */
    void setSelectedBand (int index);
    int getSelectedBand() const noexcept { return selectedBand; }

    /** True once something has been copied and can be pasted. */
    static bool hasCopiedBand() noexcept;

    /** Copy, paste and reset for one band. The parameter list lives next to the
        controls, so the three can never drift apart from what the panel shows. */
    static void copyBand (const ControlContext&, int band);
    static void pasteBand (const ControlContext&, int band);
    static void resetBand (const ControlContext&, int band);

    /** Every parameter ID belonging to one band, in a stable order. Solo and
        mute are excluded: they are monitoring state, not settings. */
    static std::vector<const char*> bandParameterIds (int band);

private:
    void buildBandControls (int band);
    void applyVisibility();

    struct BandSection
    {
        std::vector<juce::Component*> controls;
        std::vector<juce::Component*> compressorOnly;   // ratio, makeup
        std::vector<juce::Component*> filterOnly;       // detector high/low pass
        std::vector<juce::Component*> dynamicsOnly;     // threshold, knee, ballistics
    };

    std::function<void (int)> bandSelected;
    std::array<BandSection, kMaxMatrixBands> sections;
    std::vector<juce::Component*> globalControls;
    std::vector<juce::Component*> bandButtons;

    int selectedBand = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandMatrixPanel)
};
} // namespace sf
