#include "TestHelpers.h"
#include "Model/PresetState.h"

using namespace sft;

SF_TEST (state_roundTripsParametersAndCurve)
{
    auto a = makeProcessor();

    setParam (*a, sf::pid::compThreshold, -33.5f);
    setParam (*a, sf::pid::compRatio, 8.0f);
    setParam (*a, sf::pid::mix, 42.0f);
    setParam (*a, sf::pid::mode, 2.0f);

    a->getCurveModel().setPresetShape (6);
    a->getCurveModel().addNode (0.42f, 0.17f);
    const auto expectedNodes = a->getCurveModel().getNumNodes();
    const auto expectedMid   = a->getCurveModel().evaluate (0.42f);

    juce::MemoryBlock blob;
    a->getStateInformation (blob);
    SF_CHECK (blob.getSize() > 0);

    auto b = makeProcessor();
    b->setStateInformation (blob.getData(), (int) blob.getSize());

    SF_CHECK_NEAR (getParam (*b, sf::pid::compThreshold), -33.5f, 0.01f);
    SF_CHECK_NEAR (getParam (*b, sf::pid::compRatio), 8.0f, 0.01f);
    SF_CHECK_NEAR (getParam (*b, sf::pid::mix), 42.0f, 0.01f);
    SF_CHECK_NEAR (getParam (*b, sf::pid::mode), 2.0f, 0.01f);
    SF_CHECK (b->getCurveModel().getNumNodes() == expectedNodes);
    SF_CHECK_NEAR (b->getCurveModel().evaluate (0.42f), expectedMid, 1.0e-5f);
}

SF_TEST (state_survivesGarbageInput)
{
    auto p = makeProcessor();
    const auto before = getParam (*p, sf::pid::compThreshold);

    const char junk[] = "this is definitely not a plugin state blob";
    p->setStateInformation (junk, (int) sizeof (junk));
    SF_CHECK_NEAR (getParam (*p, sf::pid::compThreshold), before, 0.001f);

    p->setStateInformation (nullptr, 0);
    SF_CHECK_NEAR (getParam (*p, sf::pid::compThreshold), before, 0.001f);

    // Well-formed XML, wrong content.
    juce::XmlElement wrong { "SomethingElse" };
    juce::MemoryBlock mb;
    p->copyXmlToBinary (wrong, mb);
    p->setStateInformation (mb.getData(), (int) mb.getSize());
    SF_CHECK_NEAR (getParam (*p, sf::pid::compThreshold), before, 0.001f);
}

SF_TEST (state_sanitiserDropsNonFiniteValues)
{
    juce::ValueTree params { sf::PresetState::paramsTag };

    juce::ValueTree good { "PARAM" };
    good.setProperty ("id", "compRatio", nullptr);
    good.setProperty ("value", 4.0, nullptr);
    params.appendChild (good, nullptr);

    juce::ValueTree bad { "PARAM" };
    bad.setProperty ("id", "compThreshold", nullptr);
    bad.setProperty ("value", std::numeric_limits<double>::quiet_NaN(), nullptr);
    params.appendChild (bad, nullptr);

    juce::ValueTree worse { "PARAM" };
    worse.setProperty ("id", "mix", nullptr);
    worse.setProperty ("value", std::numeric_limits<double>::infinity(), nullptr);
    params.appendChild (worse, nullptr);

    sf::PresetState::sanitiseParameters (params);
    SF_CHECK (params.getNumChildren() == 1);
    SF_CHECK (params.getChild (0).getProperty ("id").toString() == "compRatio");
}

SF_TEST (state_acceptsBareApvtsTree)
{
    auto a = makeProcessor();
    setParam (*a, sf::pid::outputGain, -6.0f);

    // Simulate a session saved by a build that stored only the APVTS tree.
    const auto bare = a->getAPVTS().copyState();
    auto xml = bare.createXml();
    SF_CHECK (xml != nullptr);

    juce::MemoryBlock mb;
    auto b = makeProcessor();
    b->copyXmlToBinary (*xml, mb);
    b->setStateInformation (mb.getData(), (int) mb.getSize());

    SF_CHECK_NEAR (getParam (*b, sf::pid::outputGain), -6.0f, 0.01f);
}

SF_TEST (state_editorSizeIsClampedOnRestore)
{
    auto p = makeProcessor();

    juce::ValueTree root { sf::PresetState::rootTag };
    root.setProperty (sf::PresetState::versionProp, 1, nullptr);

    juce::ValueTree ed { sf::PresetState::editorTag };
    ed.setProperty ("width", 99999, nullptr);
    ed.setProperty ("height", -5, nullptr);
    root.appendChild (ed, nullptr);

    sf::EditorState state;
    sf::CurveModel curve;
    SF_CHECK (sf::PresetState::restore (root, p->getAPVTS(), curve, state));
    SF_CHECK (state.width <= 4096 && state.width >= 820);
    SF_CHECK (state.height >= 560 && state.height <= 2600);
}

SF_TEST (state_allParametersHaveStableIdsAndDefaults)
{
    auto p = makeProcessor();
    const auto& all = p->getParameters();
    SF_CHECK (all.size() > 60);

    juce::StringArray seen;

    for (auto* param : all)
    {
        auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (param);

        if (withID == nullptr)
            continue; // the wrapper-provided bypass parameter, if any

        SF_CHECK_MSG (! seen.contains (withID->paramID),
                      ("duplicate parameter id: " + withID->paramID).toStdString());
        seen.add (withID->paramID);

        const auto v = param->getValue();
        SF_CHECK_FINITE (v);
        SF_CHECK (v >= 0.0f && v <= 1.0f);
    }
}
