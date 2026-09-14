#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

namespace sf
{
/**
    MIDI CC -> parameter mapping, with a learn mode.

    Split across the two threads the way everything else here is:

      - The **audio thread** only records incoming controller values into an array
        of atomics and raises a dirty flag. It never touches the mapping table and
        never calls into the parameter objects, because `setValueNotifyingHost` is
        not safe to call from `processBlock`.
      - The **message thread** drains those flags on a timer and applies them, so
        MIDI control keeps working with the editor closed.

    Mappings are part of the plugin's saved state, so a learned controller survives
    a session reload.
*/
class MidiLearnManager
{
public:
    static constexpr int kNumControllers = 128;
    static constexpr int kOmniChannel = 0;   // 0 = respond on any channel

    void attach (juce::AudioProcessorValueTreeState& state) { apvts = &state; }

    // -- audio thread --------------------------------------------------------
    /** Records controller movements. Allocation-free and lock-free. */
    void processBlock (const juce::MidiBuffer& midi) noexcept;

    // -- message thread ------------------------------------------------------
    /** Applies everything the audio thread recorded since the last call, and
        completes a pending learn. Returns true if anything changed. */
    bool applyPendingChanges();

    void startLearn (const juce::String& parameterID);
    void cancelLearn();
    bool isLearning() const noexcept { return learnPending.load (std::memory_order_acquire); }
    juce::String getLearningParameterID() const;

    /** -1 when the parameter has no controller assigned. */
    int getControllerFor (const juce::String& parameterID) const;
    /** Empty when the controller is unassigned. */
    juce::String getParameterForController (int controller) const;

    void setMapping (const juce::String& parameterID, int controller);
    void clearMapping (const juce::String& parameterID);
    void clearAllMappings();
    int  getNumMappings() const;

    /** Which channel mappings respond to; 0 means every channel. */
    void setChannel (int channel) { listenChannel = juce::jlimit (0, 16, channel); }
    int  getChannel() const noexcept { return listenChannel; }

    // -- state ---------------------------------------------------------------
    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);

    static const juce::Identifier stateTag;

private:
    juce::AudioProcessorValueTreeState* apvts = nullptr;

    // Message-thread-owned: controller index -> parameter ID ("" = unassigned).
    std::array<juce::String, kNumControllers> mappings;
    int listenChannel = kOmniChannel;

    // Audio-thread -> message-thread hand-off.
    std::array<std::atomic<float>, kNumControllers> controllerValue {};
    std::array<std::atomic<bool>, kNumControllers>  controllerDirty {};

    std::atomic<bool> learnPending { false };
    std::atomic<int>  capturedController { -1 };

    juce::CriticalSection learnTargetLock;   // message thread only; guards a String
    juce::String learnTarget;
};
} // namespace sf
