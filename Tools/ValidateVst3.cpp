/*
    SideForge VST3 validation.

    pluginval is not installed on this machine, so this stands in for it: it
    loads the *built .vst3 bundle* through JUCE's own VST3 host code — the same
    path a DAW takes — rather than constructing the processor directly the way
    the unit tests do. That is the difference between "the code works" and "the
    plugin loads".

    It checks that the bundle scans, instantiates, reports the buses and latency
    it should, processes finite audio at several sample rates and block sizes
    with and without a sidechain, survives a state round trip, and opens and
    closes its editor without disturbing processing.

    Build and run:
        cmake --build build --config Release --target SideForgeValidate
        .\build\SideForgeValidate_artefacts\Release\SideForgeValidate.exe
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;
int checks = 0;

void check (bool condition, const juce::String& what)
{
    ++checks;

    if (condition)
    {
        std::printf ("  [ ok ] %s\n", what.toRawUTF8());
    }
    else
    {
        ++failures;
        std::printf ("  [FAIL] %s\n", what.toRawUTF8());
    }

    std::fflush (stdout);
}

void section (const juce::String& name)
{
    std::printf ("\n%s\n", name.toRawUTF8());
    std::fflush (stdout);
}

/** Looks for the built bundle relative to this executable.

    There can be more than one — a Debug tree next to a Release tree — so
    candidates whose path sits under the same configuration folder as this
    executable win, and anything that does not actually contain a binary is
    discarded rather than reported as a scanning failure. */
juce::File findBundle (const juce::StringArray& arguments)
{
    for (const auto& argument : arguments)
    {
        juce::File candidate { argument };

        if (candidate.exists())
            return candidate;
    }

    const auto executableDirectory =
        juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();

    // "Release" or "Debug", so the validator checks the build it was built with.
    const auto configuration = executableDirectory.getFileName();

    auto directory = executableDirectory;

    for (int level = 0; level < 6 && directory.exists(); ++level)
    {
        juce::Array<juce::File> candidates;

        for (const auto& file : directory.findChildFiles (juce::File::findFilesAndDirectories,
                                                          true, "SideForge.vst3"))
        {
            // A VST3 bundle is a directory with the binary inside it.
            const auto binary = file.getChildFile ("Contents")
                                    .getChildFile ("x86_64-win")
                                    .getChildFile ("SideForge.vst3");

            if (file.isDirectory() && binary.existsAsFile())
                candidates.add (file);
            else if (file.existsAsFile() && file.getSize() > 0)
                candidates.add (file);
        }

        for (const auto& candidate : candidates)
            if (candidate.getFullPathName().contains (juce::File::getSeparatorString()
                                                      + configuration
                                                      + juce::File::getSeparatorString()))
                return candidate;

        if (! candidates.isEmpty())
            return candidates.getFirst();

        directory = directory.getParentDirectory();
    }

    return {};
}

bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (data[i]))
                return false;
    }

    return true;
}

/** Finds a hosted parameter by its display name.

    A plugin loaded through the VST3 host API does not expose the APVTS string
    IDs - the host sees names and indices - so this is how the validator reaches
    a specific control from outside the plugin, the same way a DAW would. */
juce::AudioProcessorParameter* parameterNamed (juce::AudioPluginInstance& instance,
                                               const juce::String& name)
{
    for (auto* parameter : instance.getParameters())
        if (parameter->getName (64).equalsIgnoreCase (name))
            return parameter;

    return nullptr;
}

/** Sets a hosted parameter by name. Returns false if the host cannot see it. */
bool setParameterNamed (juce::AudioPluginInstance& instance, const juce::String& name,
                        float normalised)
{
    if (auto* parameter = parameterNamed (instance, name))
    {
        parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
        return true;
    }

    return false;
}

void fillTestSignal (juce::AudioBuffer<float>& buffer, int mainChannels, int scChannels,
                     double sampleRate, int offset)
{
    buffer.clear();

    for (int ch = 0; ch < mainChannels && ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] = 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                               * 220.0 * (offset + i) / sampleRate);
    }

    // A kick-like pulse train on the sidechain.
    const auto period = juce::jmax (1, (int) (sampleRate * 0.5));

    for (int ch = 0; ch < scChannels && mainChannels + ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (mainChannels + ch);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto position = (offset + i) % period;
            const auto length = (int) (sampleRate * 0.01);
            data[i] = position < length
                          ? 0.9f * (1.0f - (float) position / (float) length)
                          : 0.0f;
        }
    }
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray arguments;

    for (int i = 1; i < argc; ++i)
        arguments.add (juce::String::fromUTF8 (argv[i]));

    const auto bundle = findBundle (arguments);

    std::printf ("SideForge VST3 validation\n");

    if (bundle == juce::File() || ! bundle.exists())
    {
        std::printf ("\nCould not find SideForge.vst3.\n"
                     "Build it first, or pass the path as an argument.\n");
        return 2;
    }

    std::printf ("Bundle: %s\n", bundle.getFullPathName().toRawUTF8());

    // ---- scan ---------------------------------------------------------------
    section ("Scanning");

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile (descriptions, bundle.getFullPathName());

    check (! descriptions.isEmpty(), "the host can read the bundle's plugin list");

    if (descriptions.isEmpty())
    {
        std::printf ("\n1 check, 1 failure\n");
        return 1;
    }

    const auto& description = *descriptions.getFirst();
    std::printf ("  name=%s  manufacturer=%s  version=%s  uid=%s\n",
                 description.name.toRawUTF8(),
                 description.manufacturerName.toRawUTF8(),
                 description.version.toRawUTF8(),
                 description.createIdentifierString().toRawUTF8());

    check (description.name.containsIgnoreCase ("SideForge"), "the plugin reports its name");
    check (! description.isInstrument, "it is declared as an effect, not an instrument");

    // ---- instantiate --------------------------------------------------------
    section ("Instantiating");

    // Only VST3 is registered: it is the one format this project builds, and
    // adding it explicitly keeps the editor checks working (the headless helper
    // deliberately loads plugins without UI support).
    juce::AudioPluginFormatManager manager;
    manager.addFormat (std::make_unique<juce::VST3PluginFormat>());

    juce::String error;
    auto instance = manager.createPluginInstance (description, 48000.0, 512, error);

    check (instance != nullptr, "the host can instantiate it" + (error.isEmpty() ? juce::String()
                                                                                 : ": " + error));

    if (instance == nullptr)
    {
        std::printf ("\n%d checks, %d failures\n", checks, failures);
        return 1;
    }

    check (instance->getTotalNumOutputChannels() >= 1, "it has an output bus");
    check (instance->getBusCount (true) >= 2, "it exposes a sidechain input bus");
    check (instance->acceptsMidi(), "it accepts MIDI, for note-triggered curves");

    const auto numParameters = instance->getParameters().size();
    std::printf ("  parameters: %d\n", numParameters);
    check (numParameters > 60, "it publishes its full parameter set to the host");

    // ---- bus layouts --------------------------------------------------------
    section ("Bus layouts");

    auto trySidechain = [&instance] (const juce::AudioChannelSet& sc, const juce::String& name)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.inputBuses.add (sc);
        layout.outputBuses.add (juce::AudioChannelSet::stereo());

        const auto applied = instance->setBusesLayout (layout);
        check (applied, "stereo main with a " + name + " sidechain is accepted");
        return applied;
    };

    trySidechain (juce::AudioChannelSet::stereo(), "stereo");
    trySidechain (juce::AudioChannelSet::disabled(), "disabled");
    const auto stereoSidechain = trySidechain (juce::AudioChannelSet::stereo(), "stereo");

    // ---- processing ---------------------------------------------------------
    section ("Processing");

    const double sampleRates[] = { 44100.0, 48000.0, 96000.0 };
    const int blockSizes[] = { 64, 512, 2048 };

    auto sawNonFinite = false;
    auto sawRunaway = false;

    for (const auto sampleRate : sampleRates)
    {
        for (const auto blockSize : blockSizes)
        {
            instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
            instance->prepareToPlay (sampleRate, blockSize);

            juce::AudioBuffer<float> buffer { juce::jmax (instance->getTotalNumInputChannels(),
                                                          instance->getTotalNumOutputChannels()),
                                              blockSize };
            juce::MidiBuffer midi;

            const auto mainChannels = instance->getMainBusNumInputChannels();
            const auto scChannels = stereoSidechain ? 2 : 0;

            for (int block = 0; block < 24; ++block)
            {
                fillTestSignal (buffer, mainChannels, scChannels, sampleRate, block * blockSize);
                instance->processBlock (buffer, midi);

                if (! allFinite (buffer))
                    sawNonFinite = true;

                for (int ch = 0; ch < mainChannels; ++ch)
                    if (buffer.getMagnitude (ch, 0, blockSize) > 8.0f)
                        sawRunaway = true;
            }

            instance->releaseResources();
        }
    }

    check (! sawNonFinite, "output is finite at every sample rate and block size");
    check (! sawRunaway, "output never runs away");

    // ---- silence ------------------------------------------------------------
    instance->setRateAndBufferSizeDetails (48000.0, 512);
    instance->prepareToPlay (48000.0, 512);

    {
        juce::AudioBuffer<float> buffer { juce::jmax (instance->getTotalNumInputChannels(),
                                                      instance->getTotalNumOutputChannels()), 512 };
        juce::MidiBuffer midi;
        auto silent = true;

        for (int block = 0; block < 20; ++block)
        {
            buffer.clear();
            instance->processBlock (buffer, midi);

            for (int ch = 0; ch < instance->getMainBusNumOutputChannels(); ++ch)
                if (buffer.getMagnitude (ch, 0, 512) > 1.0e-6f)
                    silent = false;
        }

        check (silent, "silence in gives silence out");
    }

    // ---- the Band Matrix, driven the way a host would ------------------------
    //
    // Everything above runs in Full Range, because that is the default. The
    // feature a DAW is most likely to be left in has to be exercised through the
    // hosting layer too, not only in the unit tests, because the routing switch
    // changes the signal path and the reported latency.
    section ("Band Matrix");

    {
        const auto foundRouting = setParameterNamed (*instance, "Routing", 1.0f);
        check (foundRouting, "the host can see and set the Routing parameter");

        check (setParameterNamed (*instance, "Matrix Bands", 1.0f),
               "the host can see and set the band count");

        // Every band ducking, so all four processing chains are live at once.
        auto foundEveryBand = true;

        for (int band = 1; band <= 4; ++band)
        {
            const auto label = "Band " + juce::String (band) + " ";

            foundEveryBand &= setParameterNamed (*instance, label + "Mode", 1.0f / 6.0f);
            foundEveryBand &= setParameterNamed (*instance, label + "Depth", 1.0f);
            foundEveryBand &= setParameterNamed (*instance, label + "Enable", 1.0f);
        }

        check (foundEveryBand, "the host can see and set every band's controls");

        instance->setRateAndBufferSizeDetails (48000.0, 512);
        instance->prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> buffer { juce::jmax (instance->getTotalNumInputChannels(),
                                                      instance->getTotalNumOutputChannels()), 512 };
        juce::MidiBuffer midi;

        auto finite = true;
        auto runaway = false;

        for (int block = 0; block < 60; ++block)
        {
            fillTestSignal (buffer, instance->getMainBusNumInputChannels(),
                            stereoSidechain ? 2 : 0, 48000.0, block * 512);
            instance->processBlock (buffer, midi);

            if (! allFinite (buffer))
                finite = false;

            for (int ch = 0; ch < instance->getMainBusNumOutputChannels(); ++ch)
                if (buffer.getMagnitude (ch, 0, 512) > 8.0f)
                    runaway = true;
        }

        check (finite, "four ducking bands give finite output");
        check (! runaway, "four ducking bands never run away");

        std::printf ("  Band Matrix latency: %d samples\n", instance->getLatencySamples());
        check (instance->getLatencySamples() >= 0, "the matrix reports a sane latency");

        // Back to Full Range, so the state and editor checks below run against
        // the configuration a fresh instance is actually in.
        setParameterNamed (*instance, "Routing", 0.0f);
        instance->prepareToPlay (48000.0, 512);
    }

    // ---- several instances at once -------------------------------------------
    //
    // A session has more than one of these in it. Anything accidentally shared
    // between instances - a static buffer, a global FFT, a file handle - shows
    // up here and nowhere else.
    section ("Multiple instances");

    {
        juce::OwnedArray<juce::AudioPluginInstance> others;
        auto createdAll = true;

        for (int i = 0; i < 3; ++i)
        {
            juce::String instanceError;
            auto other = manager.createPluginInstance (description, 48000.0, 512, instanceError);

            if (other == nullptr)
            {
                createdAll = false;
                break;
            }

            other->setRateAndBufferSizeDetails (48000.0, 512);
            other->prepareToPlay (48000.0, 512);
            others.add (other.release());
        }

        check (createdAll, "three more instances can be created alongside the first");

        auto finite = true;

        if (createdAll)
        {
            // Each one configured differently, and interleaved block by block,
            // which is what a host's audio thread actually does.
            setParameterNamed (*others[0], "Routing", 1.0f);
            setParameterNamed (*others[1], "Routing", 0.0f);
            setParameterNamed (*others[2], "Routing", 1.0f);
            setParameterNamed (*others[2], "Matrix Bands", 1.0f);

            juce::MidiBuffer midi;

            for (int block = 0; block < 40; ++block)
            {
                for (auto* other : others)
                {
                    juce::AudioBuffer<float> buffer { juce::jmax (other->getTotalNumInputChannels(),
                                                                  other->getTotalNumOutputChannels()),
                                                      512 };

                    fillTestSignal (buffer, other->getMainBusNumInputChannels(), 0, 48000.0,
                                    block * 512);
                    other->processBlock (buffer, midi);

                    if (! allFinite (buffer))
                        finite = false;
                }
            }
        }

        check (finite, "several instances running at once all stay finite");

        for (auto* other : others)
            other->releaseResources();
    }

    // ---- latency ------------------------------------------------------------
    section ("Latency");
    std::printf ("  reported latency: %d samples\n", instance->getLatencySamples());
    check (instance->getLatencySamples() >= 0, "reported latency is not negative");

    // ---- state --------------------------------------------------------------
    section ("State");

    juce::MemoryBlock stateA;
    instance->getStateInformation (stateA);
    check (stateA.getSize() > 0, "it produces a state blob");

    // Move a parameter, restore, and confirm it came back.
    if (auto* parameter = instance->getParameters()[0])
    {
        const auto original = parameter->getValue();
        parameter->setValueNotifyingHost (original > 0.5f ? 0.1f : 0.9f);

        instance->setStateInformation (stateA.getData(), (int) stateA.getSize());
        check (std::abs (parameter->getValue() - original) < 1.0e-4f,
               "a restored state puts the parameters back");
    }

    juce::MemoryBlock stateB;
    instance->getStateInformation (stateB);
    check (stateB.getSize() == stateA.getSize(), "state round trips to the same size");

    // Malformed state must be survived, not crashed on.
    const char garbage[] = "this is not a SideForge state blob at all";
    instance->setStateInformation (garbage, (int) sizeof (garbage));
    check (true, "malformed state does not crash it");

    // ---- editor -------------------------------------------------------------
    section ("Editor");

    check (instance->hasEditor(), "it reports having an editor");

    {
        // The host owns the editor and must tell the processor before deleting
        // it, so this deliberately does not use a unique_ptr.
        auto* editor = instance->createEditorIfNeeded();
        check (editor != nullptr, "the host can create the editor");

        if (editor != nullptr)
        {
            std::printf ("  editor size: %d x %d, resizable=%d\n",
                         editor->getWidth(), editor->getHeight(), editor->isResizable() ? 1 : 0);
            check (editor->getWidth() > 0 && editor->getHeight() > 0, "the editor has a size");
            check (editor->isResizable(), "the editor is resizable");

            juce::AudioBuffer<float> buffer { juce::jmax (instance->getTotalNumInputChannels(),
                                                          instance->getTotalNumOutputChannels()), 512 };
            juce::MidiBuffer midi;
            auto finite = true;

            for (int block = 0; block < 20; ++block)
            {
                fillTestSignal (buffer, instance->getMainBusNumInputChannels(), 2, 48000.0, block * 512);
                instance->processBlock (buffer, midi);
                finite = finite && allFinite (buffer);
            }

            check (finite, "audio stays finite with the editor open");

            instance->editorBeingDeleted (editor);
            delete editor;
        }
    }

    check (instance->getActiveEditor() == nullptr, "the editor closes cleanly");

    instance->releaseResources();
    instance.reset();

    // ---- summary ------------------------------------------------------------
    std::printf ("\n%d checks, %d failure%s\n", checks, failures, failures == 1 ? "" : "s");

    return failures == 0 ? 0 : 1;
}
