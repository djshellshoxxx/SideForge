#include "TestFramework.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UI/MainView.h"
#include "Parameters/ParamIDs.h"
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <algorithm>
#include <cstring>
#include <string>

namespace sft
{
std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}
} // namespace sft

namespace
{
/** Renders every page of the interface to PNG, so the layout can be inspected
    without a host. Not part of the test run: `--screenshots=<dir>` only. */
int writeScreenshots (const juce::String& directoryPath)
{
    auto directory = juce::File::isAbsolutePath (directoryPath)
                         ? juce::File (directoryPath)
                         : juce::File::getCurrentWorkingDirectory().getChildFile (directoryPath);

    directory.createDirectory();

    sf::SideForgeProcessor processor;

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    processor.setBusesLayout (layout);
    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    // Push some audio through first so the meters and the visualiser have
    // something to draw rather than rendering their empty state.
    {
        juce::AudioBuffer<float> buffer { processor.getTotalNumInputChannels(), 512 };
        juce::MidiBuffer midi;

        for (int block = 0; block < 400; ++block)
        {
            buffer.clear();

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);

                for (int i = 0; i < 512; ++i)
                {
                    const auto n = block * 512 + i;
                    data[i] = 0.45f * (float) std::sin (2.0 * 3.14159265358979 * 180.0 * n / 48000.0);

                    // A kick-like pulse train on the sidechain.
                    const auto position = n % 24000;
                    buffer.getWritePointer (2 + ch)[i] =
                        position < 480 ? 0.9f * (1.0f - (float) position / 480.0f) : 0.0f;
                }
            }

            processor.processBlock (buffer, midi);
        }
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor { processor.createEditor() };

    if (editor == nullptr)
        return 1;

    editor->setSize (sf::SideForgeEditor::kDesignWidth, sf::SideForgeEditor::kDesignHeight);

    auto* mainView = dynamic_cast<sf::MainView*> (editor->getChildComponent (0));

    if (mainView == nullptr)
        return 1;

    const char* const pageNames[]  = { "waveform", "curve", "spectrum" };
    const char* const panelNames[] = { "compressor", "trigger", "curve", "spectrum",
                                       "bands", "routing", "output" };

    auto save = [&editor, &directory, mainView] (const juce::String& name)
    {
        // No message loop is running here, so the editor's timer has to be driven
        // by hand before the meters, the visualiser and the footer have anything
        // to draw.
        for (int tick = 0; tick < 12; ++tick)
            mainView->refreshNow();

        juce::Image image { juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true };
        {
            juce::Graphics g { image };
            editor->paintEntireComponent (g, true);
        }

        auto file = directory.getChildFile (name + ".png");
        file.deleteFile();

        juce::PNGImageFormat format;
        std::unique_ptr<juce::FileOutputStream> stream { file.createOutputStream() };

        if (stream != nullptr && format.writeImageToStream (image, *stream))
            std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
    };

    for (int page = 0; page < 3; ++page)
    {
        mainView->setDisplayPage ((sf::MainView::DisplayPage) page);

        for (int panel = 0; panel < 7; ++panel)
        {
            mainView->setActivePanelForScreenshot (panel);
            save (juce::String (pageNames[page]) + "-" + panelNames[panel]);
        }
    }

    // ...one in Hybrid mode with the spectral stage actually running, so the
    // collision map and the spectra are exercised rather than the "not in this
    // mode" placeholder...
    {
        auto set = [&processor] (const char* id, float value)
        {
            if (auto* parameter = processor.getAPVTS().getParameter (id))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        };

        set (sf::pid::quality, (float) sf::Quality::Spectral);
        set (sf::pid::mode, (float) sf::Mode::Hybrid);
        set (sf::pid::specDepth, 24.0f);

        juce::AudioBuffer<float> buffer { processor.getTotalNumInputChannels(), 512 };
        juce::MidiBuffer midi;

        for (int block = 0; block < 300; ++block)
        {
            buffer.clear();

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* main = buffer.getWritePointer (ch);
                auto* side = buffer.getWritePointer (2 + ch);

                for (int i = 0; i < 512; ++i)
                {
                    const auto n = block * 512 + i;
                    const auto t = 2.0 * 3.14159265358979 * n / 48000.0;

                    // A sustained chord against a kick that shares its low end.
                    main[i] = 0.22f * (float) (std::sin (t * 220.0) + std::sin (t * 330.0)
                                               + std::sin (t * 550.0));

                    const auto position = n % 24000;
                    side[i] = position < 3000
                                  ? 0.85f * (float) (std::sin (t * 55.0) + 0.4 * std::sin (t * 180.0))
                                        * (1.0f - (float) position / 3000.0f)
                                  : 0.0f;
                }
            }

            processor.processBlock (buffer, midi);
        }

        mainView->setDisplayPage (sf::MainView::DisplayPage::Spectrum);
        mainView->setActivePanelForScreenshot (3);
        save ("hybrid-spectral-analysis");

        mainView->setDisplayPage (sf::MainView::DisplayPage::Wave);
        save ("hybrid-waveform");
    }

    // ...and one at the minimum size, where the layout is tightest.
    editor->setSize (sf::SideForgeEditor::kMinWidth, sf::SideForgeEditor::kMinHeight);
    mainView->setDisplayPage (sf::MainView::DisplayPage::Wave);
    mainView->setActivePanelForScreenshot (4);   // Bands: the busiest panel
    save ("minimum-size-bands");

    std::fflush (stdout);
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    for (int i = 1; i < argc; ++i)
        if (std::strncmp (argv[i], "--screenshots=", 14) == 0)
            return writeScreenshots (juce::String::fromUTF8 (argv[i] + 14));

    // --filter=a,b,c runs every test whose name contains any of the patterns.
    std::vector<std::string> filters;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strncmp (argv[i], "--filter=", 9) != 0)
            continue;

        std::string list { argv[i] + 9 };
        size_t start = 0;

        while (start <= list.size())
        {
            const auto comma = list.find (',', start);
            const auto piece = list.substr (start, comma == std::string::npos ? std::string::npos
                                                                             : comma - start);
            if (! piece.empty())
                filters.push_back (piece);

            if (comma == std::string::npos)
                break;

            start = comma + 1;
        }
    }

    auto matches = [&filters] (const char* name)
    {
        if (filters.empty())
            return true;

        for (const auto& f : filters)
            if (std::strstr (name, f.c_str()) != nullptr)
                return true;

        return false;
    };

    auto& tests = sft::registry();
    std::stable_sort (tests.begin(), tests.end(),
                      [] (const sft::TestCase& a, const sft::TestCase& b)
                      { return std::strcmp (a.name, b.name) < 0; });

    int passed = 0, failed = 0, skipped = 0;

    for (const auto& t : tests)
    {
        if (! matches (t.name))
        {
            ++skipped;
            continue;
        }

        std::printf ("[ run  ] %s\n", t.name);
        std::fflush (stdout);

        try
        {
            t.fn();
            ++passed;
            std::printf ("[  ok  ] %s\n", t.name);
        }
        catch (const sft::Failure& f)
        {
            ++failed;
            std::printf ("[ FAIL ] %s\n           %s\n", t.name, f.what());
        }
        catch (const std::exception& e)
        {
            ++failed;
            std::printf ("[ FAIL ] %s\n           unexpected exception: %s\n", t.name, e.what());
        }
        catch (...)
        {
            ++failed;
            std::printf ("[ FAIL ] %s\n           unknown exception\n", t.name);
        }

        std::fflush (stdout);
    }

    std::printf ("\n%d passed, %d failed", passed, failed);

    if (skipped > 0)
        std::printf (", %d skipped", skipped);

    std::printf ("\n");

    return failed == 0 ? 0 : 1;
}
