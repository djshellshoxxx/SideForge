#include "MidiLearn.h"

namespace sf
{
const juce::Identifier MidiLearnManager::stateTag { "MidiLearn" };

namespace
{
const juce::Identifier kMapTag { "Map" };
const juce::Identifier kCcProp { "cc" };
const juce::Identifier kParamProp { "param" };
const juce::Identifier kChannelProp { "channel" };
} // namespace

void MidiLearnManager::processBlock (const juce::MidiBuffer& midi) noexcept
{
    const auto channel = listenChannel;

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (! message.isController())
            continue;

        if (channel != kOmniChannel && message.getChannel() != channel)
            continue;

        const auto cc = message.getControllerNumber();

        if (! juce::isPositiveAndBelow (cc, kNumControllers))
            continue;

        controllerValue[(size_t) cc].store ((float) message.getControllerValue() / 127.0f,
                                            std::memory_order_relaxed);
        controllerDirty[(size_t) cc].store (true, std::memory_order_release);

        if (learnPending.load (std::memory_order_acquire))
            capturedController.store (cc, std::memory_order_release);
    }
}

bool MidiLearnManager::applyPendingChanges()
{
    if (apvts == nullptr)
        return false;

    auto changed = false;

    // --- complete a learn -----------------------------------------------------
    if (learnPending.load (std::memory_order_acquire))
    {
        const auto cc = capturedController.exchange (-1, std::memory_order_acq_rel);

        if (juce::isPositiveAndBelow (cc, kNumControllers))
        {
            juce::String target;

            {
                const juce::ScopedLock lock (learnTargetLock);
                target = learnTarget;
                learnTarget.clear();
            }

            learnPending.store (false, std::memory_order_release);

            if (target.isNotEmpty())
            {
                setMapping (target, cc);
                changed = true;

                // Do not also apply the controller's current value: the movement
                // that taught the mapping should not jump the parameter.
                controllerDirty[(size_t) cc].store (false, std::memory_order_release);
            }
        }
    }

    // --- apply moved controllers ---------------------------------------------
    for (int cc = 0; cc < kNumControllers; ++cc)
    {
        if (! controllerDirty[(size_t) cc].exchange (false, std::memory_order_acq_rel))
            continue;

        const auto& parameterID = mappings[(size_t) cc];

        if (parameterID.isEmpty())
            continue;

        if (auto* parameter = apvts->getParameter (parameterID))
        {
            const auto value = juce::jlimit (0.0f, 1.0f,
                                             controllerValue[(size_t) cc].load (std::memory_order_relaxed));
            parameter->setValueNotifyingHost (value);
            changed = true;
        }
    }

    return changed;
}

void MidiLearnManager::startLearn (const juce::String& parameterID)
{
    {
        const juce::ScopedLock lock (learnTargetLock);
        learnTarget = parameterID;
    }

    capturedController.store (-1, std::memory_order_release);
    learnPending.store (true, std::memory_order_release);
}

void MidiLearnManager::cancelLearn()
{
    learnPending.store (false, std::memory_order_release);
    capturedController.store (-1, std::memory_order_release);

    const juce::ScopedLock lock (learnTargetLock);
    learnTarget.clear();
}

juce::String MidiLearnManager::getLearningParameterID() const
{
    const juce::ScopedLock lock (learnTargetLock);
    return learnTarget;
}

int MidiLearnManager::getControllerFor (const juce::String& parameterID) const
{
    if (parameterID.isEmpty())
        return -1;

    for (int cc = 0; cc < kNumControllers; ++cc)
        if (mappings[(size_t) cc] == parameterID)
            return cc;

    return -1;
}

juce::String MidiLearnManager::getParameterForController (int controller) const
{
    if (! juce::isPositiveAndBelow (controller, kNumControllers))
        return {};

    return mappings[(size_t) controller];
}

void MidiLearnManager::setMapping (const juce::String& parameterID, int controller)
{
    if (! juce::isPositiveAndBelow (controller, kNumControllers) || parameterID.isEmpty())
        return;

    // One controller drives one parameter, and one parameter answers to one
    // controller: re-learning moves the assignment rather than stacking another.
    clearMapping (parameterID);
    mappings[(size_t) controller] = parameterID;
}

void MidiLearnManager::clearMapping (const juce::String& parameterID)
{
    for (auto& entry : mappings)
        if (entry == parameterID)
            entry.clear();
}

void MidiLearnManager::clearAllMappings()
{
    for (auto& entry : mappings)
        entry.clear();
}

int MidiLearnManager::getNumMappings() const
{
    int count = 0;

    for (const auto& entry : mappings)
        if (entry.isNotEmpty())
            ++count;

    return count;
}

juce::ValueTree MidiLearnManager::toValueTree() const
{
    juce::ValueTree tree { stateTag };
    tree.setProperty (kChannelProp, listenChannel, nullptr);

    for (int cc = 0; cc < kNumControllers; ++cc)
    {
        if (mappings[(size_t) cc].isEmpty())
            continue;

        juce::ValueTree entry { kMapTag };
        entry.setProperty (kCcProp, cc, nullptr);
        entry.setProperty (kParamProp, mappings[(size_t) cc], nullptr);
        tree.appendChild (entry, nullptr);
    }

    return tree;
}

void MidiLearnManager::fromValueTree (const juce::ValueTree& tree)
{
    clearAllMappings();

    if (! tree.isValid() || ! tree.hasType (stateTag))
        return;

    listenChannel = juce::jlimit (0, 16, (int) tree.getProperty (kChannelProp, kOmniChannel));

    for (const auto& child : tree)
    {
        if (! child.hasType (kMapTag))
            continue;

        const auto cc = (int) child.getProperty (kCcProp, -1);
        const auto parameterID = child.getProperty (kParamProp, juce::String()).toString();

        // A mapping to a parameter this build no longer has is dropped rather
        // than kept as a dangling entry.
        if (juce::isPositiveAndBelow (cc, kNumControllers)
            && parameterID.isNotEmpty()
            && (apvts == nullptr || apvts->getParameter (parameterID) != nullptr))
        {
            mappings[(size_t) cc] = parameterID;
        }
    }
}
} // namespace sf
