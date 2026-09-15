#include "../TestHelpers.h"
#include "Model/FactoryPresets.h"
#include "Parameters/BandMatrixParameters.h"
#include <array>
#include <string>
#include <vector>

// =============================================================================
//  Cross-feature tests.
//
//  Every other test file checks one feature in isolation. This one checks what
//  happens when features meet: the engine mode against the routing mode, the
//  quality setting against the band count, a soloed band against Delta
//  monitoring, and so on.
//
//  An exhaustive product of every discrete setting is astronomically large - the
//  discrete dimensions alone multiply out past 10^11 - so the sweep here is
//  **all-pairs**: for every pair of settings, and every combination of values
//  that pair can take, one configuration is run with those two pinned and every
//  other parameter randomised. That is the standard combinatorial-testing
//  result: the large majority of interaction faults involve only two factors,
//  and all-pairs catches all of them for a few thousand runs rather than 10^11.
//
//  On top of that, the cubes that matter most - routing x engine x quality x
//  band count, and per-band mode x detector source - are swept exhaustively,
//  because those are the interactions the Band Matrix actually introduced.
//
//  Each configuration asserts the invariants that must hold no matter what is
//  set: the output stays finite, it never runs away, silence in gives silence
//  out, and the reported latency is a sane non-negative number.
// =============================================================================

using namespace sft;

namespace
{
constexpr int kBlock = 128;

// ---------------------------------------------------------------------------
//  Parameters that stay at unity during the sweep.
//
//  Gain trims are multiplicative and have their own dedicated tests; letting the
//  fuzzer stack +24 dB of input trim on +24 dB of output trim on +24 dB of band
//  makeup would make the "did the level run away" check meaningless, because a
//  +72 dB level would be correct.
// ---------------------------------------------------------------------------
bool isGainTrim (const juce::String& id)
{
    if (id == sf::pid::inputGain || id == sf::pid::outputGain || id == sf::pid::scGain)
        return true;

    for (int b = 0; b < sf::kMaxMatrixBands; ++b)
        if (id == sf::pid::band[b].gain || id == sf::pid::band[b].makeup)
            return true;

    return false;
}

/** One axis of the sweep: a parameter and the discrete values it can take. */
struct Dimension
{
    const char* name;
    const char* id;
    std::vector<float> values;
};

const std::vector<Dimension>& dimensions()
{
    static const std::vector<Dimension> dims = []
    {
        std::vector<Dimension> d;

        auto add = [&d] (const char* name, const char* id, std::vector<float> values)
        {
            d.push_back ({ name, id, std::move (values) });
        };

        auto range = [] (int count)
        {
            std::vector<float> v;

            for (int i = 0; i < count; ++i)
                v.push_back ((float) i);

            return v;
        };

        // ---- global ------------------------------------------------------
        add ("routing",      sf::pid::routingMode,   range (2));
        add ("bypass",       sf::pid::bypass,        range (2));
        add ("engine",       sf::pid::mode,          range (4));
        add ("quality",      sf::pid::quality,       range (4));
        add ("monitor",      sf::pid::monitor,       range (3));
        add ("channelMode",  sf::pid::channelMode,   range (3));
        add ("bandMode",     sf::pid::bandMode,      range (2));
        add ("scSource",     sf::pid::scSource,      range (2));
        add ("autoGain",     sf::pid::autoGain,      range (2));
        add ("macros",       sf::pid::macrosEnabled, range (2));
        add ("monoLow",      sf::pid::monoLowEnable, range (2));
        add ("scAudition",   sf::pid::scAudition,    range (2));
        add ("viz",          sf::pid::vizEnabled,    range (2));

        // ---- the full-range engines --------------------------------------
        add ("compDetector", sf::pid::compDetector,   range (3));
        add ("compAutoRel",  sf::pid::compAutoRelease, range (2));
        add ("curveSync",    sf::pid::curveSyncMode,  range (2));
        add ("curveTrig",    sf::pid::curveTrigMode,  range (4));
        add ("trigSource",   sf::pid::trigSource,     range (5));
        add ("trigDetector", sf::pid::trigDetector,   range (4));

        // ---- the Band Matrix ---------------------------------------------
        add ("bandCount",    sf::pid::bmBandCount,      range (sf::kMaxMatrixBands));
        add ("b1Mode",       sf::pid::band[0].mode,     range (sf::kNumBandModes));
        add ("b2Mode",       sf::pid::band[1].mode,     range (sf::kNumBandModes));
        add ("b3Mode",       sf::pid::band[2].mode,     range (sf::kNumBandModes));
        add ("b4Mode",       sf::pid::band[3].mode,     range (sf::kNumBandModes));
        add ("b1ScSource",   sf::pid::band[0].scSource, range (sf::kNumBandScSources));
        add ("b2ScSource",   sf::pid::band[1].scSource, range (sf::kNumBandScSources));
        add ("b1ScFilter",   sf::pid::band[0].scFilter, range (2));
        add ("b1ScListen",   sf::pid::band[0].scListen, range (2));
        add ("b1Solo",       sf::pid::band[0].solo,     range (2));
        add ("b2Solo",       sf::pid::band[1].solo,     range (2));
        add ("b1Mute",       sf::pid::band[0].mute,     range (2));
        add ("b1Bypass",     sf::pid::band[0].bypass,   range (2));
        add ("b1Enable",     sf::pid::band[0].enable,   range (2));

        return d;
    }();

    return dims;
}

/** A single point in the sweep. */
struct Configuration
{
    std::vector<std::pair<const char*, float>> pinned;
    unsigned seed = 0;

    std::string describe() const
    {
        std::string s = "seed " + std::to_string (seed);

        for (const auto& [id, value] : pinned)
            s += std::string ("  ") + id + "=" + num (value);

        return s;
    }
};

/** Randomises every parameter, then forces the pinned ones on top. */
void apply (sf::SideForgeProcessor& p, const Configuration& config)
{
    std::mt19937 rng { config.seed };
    std::uniform_real_distribution<float> unit { 0.0f, 1.0f };

    for (auto* parameter : p.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);

        if (ranged == nullptr)
            continue;

        if (isGainTrim (ranged->paramID))
            ranged->setValueNotifyingHost (ranged->convertTo0to1 (0.0f));
        else
            ranged->setValueNotifyingHost (unit (rng));
    }

    for (const auto& [id, value] : config.pinned)
        if (auto* parameter = p.getAPVTS().getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

/** Music on the main bus, a kick on the sidechain. */
void fillSignal (juce::AudioBuffer<float>& buffer, int mainChannels, int scChannels,
                 double sampleRate, int offset)
{
    buffer.clear();

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto n = offset + i;
        const auto t = (double) n / sampleRate;

        // Three partials, so every crossover band has something in it. Kept
        // quiet on purpose: the "did the level run away" check below only means
        // something if a legitimate chain of makeup gains cannot reach the bound
        // on its own.
        const auto tone = 0.1f * (float) (std::sin (2.0 * kPi * 60.0 * t)
                                          + std::sin (2.0 * kPi * 700.0 * t)
                                          + std::sin (2.0 * kPi * 6000.0 * t));

        for (int ch = 0; ch < mainChannels && ch < buffer.getNumChannels(); ++ch)
            buffer.getWritePointer (ch)[i] = ch == 0 ? tone : tone * 0.8f;

        const auto period = juce::jmax (1, (int) (sampleRate * 0.25));
        const auto length = juce::jmax (1, (int) (sampleRate * 0.02));
        const auto position = n % period;
        const auto pulse = position < length
                               ? 0.9f * (1.0f - (float) position / (float) length)
                               : 0.0f;

        for (int ch = 0; ch < scChannels && mainChannels + ch < buffer.getNumChannels(); ++ch)
            buffer.getWritePointer (mainChannels + ch)[i] = pulse;
    }
}

/** Runs one configuration and asserts the universal invariants. */
void exercise (sf::SideForgeProcessor& p, const Configuration& config,
               int blocks = 6, double sampleRate = 48000.0)
{
    apply (p, config);

    juce::AudioBuffer<float> buffer { p.getTotalNumInputChannels(), kBlock };
    juce::MidiBuffer midi;

    for (int b = 0; b < blocks; ++b)
    {
        fillSignal (buffer, 2, 2, sampleRate, b * kBlock);
        p.processBlock (buffer, midi);

        SF_CHECK_MSG (allFinite (buffer),
                      ("non-finite output | " + config.describe()).c_str());

        for (int ch = 0; ch < 2; ++ch)
            SF_CHECK_MSG (buffer.getMagnitude (ch, 0, kBlock) < 12.0f,
                          ("runaway level " + num (buffer.getMagnitude (ch, 0, kBlock))
                           + " | " + config.describe()).c_str());
    }

    const auto latency = p.getReportedLatencySamples();

    SF_CHECK_MSG (latency >= 0 && latency < (int) (sampleRate * 0.5),
                  ("implausible reported latency " + std::to_string (latency) + " | "
                   + config.describe()).c_str());

    // Silence in, silence out. Everything in the plugin is multiplicative or a
    // filter, so no setting has any business generating signal from nothing -
    // and a config that rings or self-oscillates shows up right here.
    //
    // The flush has to outlast the reported latency *and* the slowest release
    // the fuzzer can pick, or the delay lines and envelopes would still be
    // handing back the signal from above and every config would "fail". A probe
    // over the worst configuration this sweep found showed the tail at -57 dB
    // after 48 blocks and at true zero by 130, so 160 leaves real headroom.
    const auto flush = latency / kBlock + 160;

    for (int b = 0; b < flush; ++b)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
    }

    for (int ch = 0; ch < 2; ++ch)
        SF_CHECK_MSG (buffer.getMagnitude (ch, 0, kBlock) < 1.0e-4f,
                      ("silence produced output " + num (buffer.getMagnitude (ch, 0, kBlock))
                       + " | " + config.describe()).c_str());
}
} // namespace

// ---------------------------------------------------------------------------
//  All-pairs across every discrete setting
// ---------------------------------------------------------------------------

SF_TEST (cross_everyPairOfDiscreteSettings)
{
    const auto& dims = dimensions();

    auto p = makeProcessor (48000.0, kBlock);
    unsigned seed = 1;
    auto configurations = 0;

    for (size_t i = 0; i < dims.size(); ++i)
    {
        for (size_t j = i + 1; j < dims.size(); ++j)
        {
            for (const auto a : dims[i].values)
            {
                for (const auto b : dims[j].values)
                {
                    Configuration config;
                    config.pinned = { { dims[i].id, a }, { dims[j].id, b } };
                    config.seed = seed++;

                    exercise (*p, config);
                    ++configurations;
                }
            }
        }
    }

    // A guard against the dimension table silently shrinking: if someone deletes
    // an axis, the sweep gets quietly weaker and nothing else notices.
    SF_CHECK_MSG (configurations > 2000,
                  ("the pairwise sweep only ran " + std::to_string (configurations)
                   + " configurations").c_str());
}

// ---------------------------------------------------------------------------
//  Exhaustive products where the interactions actually live
// ---------------------------------------------------------------------------

SF_TEST (cross_routingByEngineByQualityByBandCount)
{
    auto p = makeProcessor (48000.0, kBlock);
    unsigned seed = 900000;

    for (int routing = 0; routing < 2; ++routing)
    {
        for (int engine = 0; engine < 4; ++engine)
        {
            for (int quality = 0; quality < 4; ++quality)
            {
                for (int bandCount = 0; bandCount < sf::kMaxMatrixBands; ++bandCount)
                {
                    for (int bandMode = 0; bandMode < 2; ++bandMode)
                    {
                        Configuration config;
                        config.pinned = { { sf::pid::routingMode, (float) routing },
                                          { sf::pid::mode, (float) engine },
                                          { sf::pid::quality, (float) quality },
                                          { sf::pid::bmBandCount, (float) bandCount },
                                          { sf::pid::bandMode, (float) bandMode },
                                          { sf::pid::bypass, 0.0f },
                                          { sf::pid::monitor, 0.0f } };
                        config.seed = seed++;

                        exercise (*p, config);
                    }
                }
            }
        }
    }
}

SF_TEST (cross_everyBandModeAgainstEveryDetectorSource)
{
    auto p = makeProcessor (48000.0, kBlock);
    unsigned seed = 950000;

    for (int mode = 0; mode < sf::kNumBandModes; ++mode)
    {
        for (int source = 0; source < sf::kNumBandScSources; ++source)
        {
            for (int bandCount = 0; bandCount < sf::kMaxMatrixBands; ++bandCount)
            {
                Configuration config;
                config.pinned = { { sf::pid::routingMode, (float) sf::RoutingMode::BandMatrix },
                                  { sf::pid::bmBandCount, (float) bandCount },
                                  { sf::pid::bypass, 0.0f },
                                  { sf::pid::monitor, 0.0f } };

                // Every band set the same way, so each mode is exercised in
                // every position in the crossover chain.
                for (int b = 0; b < sf::kMaxMatrixBands; ++b)
                {
                    config.pinned.push_back ({ sf::pid::band[b].mode, (float) mode });
                    config.pinned.push_back ({ sf::pid::band[b].scSource, (float) source });
                    config.pinned.push_back ({ sf::pid::band[b].enable, 1.0f });
                    config.pinned.push_back ({ sf::pid::band[b].bypass, 0.0f });
                    config.pinned.push_back ({ sf::pid::band[b].solo, 0.0f });
                    config.pinned.push_back ({ sf::pid::band[b].mute, 0.0f });
                }

                config.seed = seed++;
                exercise (*p, config);
            }
        }
    }
}

SF_TEST (cross_bandModesMixedAcrossTheChain)
{
    // The same modes again, but *different* in each band at once - which is the
    // configuration a real "hard lows, light highs" session is in, and the one
    // where a shared buffer or a shared latency figure would break.
    auto p = makeProcessor (48000.0, kBlock);
    unsigned seed = 960000;

    for (int rotation = 0; rotation < sf::kNumBandModes; ++rotation)
    {
        for (int source = 0; source < sf::kNumBandScSources; ++source)
        {
            Configuration config;
            config.pinned = { { sf::pid::routingMode, (float) sf::RoutingMode::BandMatrix },
                              { sf::pid::bmBandCount, (float) (sf::kMaxMatrixBands - 1) },
                              { sf::pid::bypass, 0.0f },
                              { sf::pid::monitor, 0.0f } };

            for (int b = 0; b < sf::kMaxMatrixBands; ++b)
            {
                config.pinned.push_back ({ sf::pid::band[b].mode,
                                           (float) ((rotation + b) % sf::kNumBandModes) });
                config.pinned.push_back ({ sf::pid::band[b].scSource,
                                           (float) ((source + b) % sf::kNumBandScSources) });
                config.pinned.push_back ({ sf::pid::band[b].enable, 1.0f });
                config.pinned.push_back ({ sf::pid::band[b].bypass, 0.0f });
            }

            config.seed = seed++;
            exercise (*p, config, 12);
        }
    }
}

SF_TEST (cross_soloMuteBypassEnableInEveryCombination)
{
    // 4 bands x 4 independent switches = 256 states, all of them reachable by a
    // user clicking around, and several of them contradictory on purpose.
    auto p = makeProcessor (48000.0, kBlock);
    unsigned seed = 970000;

    for (int state = 0; state < 256; ++state)
    {
        Configuration config;
        config.pinned = { { sf::pid::routingMode, (float) sf::RoutingMode::BandMatrix },
                          { sf::pid::bmBandCount, (float) (sf::kMaxMatrixBands - 1) },
                          { sf::pid::bypass, 0.0f },
                          { sf::pid::monitor, 0.0f } };

        for (int b = 0; b < sf::kMaxMatrixBands; ++b)
        {
            config.pinned.push_back ({ sf::pid::band[b].mode,
                                       (float) sf::BandProcessorMode::Ducker });
            config.pinned.push_back ({ sf::pid::band[b].enable, (float) ((state >> (b * 2)) & 1) });
            config.pinned.push_back ({ sf::pid::band[b].mute,   (float) ((state >> (b * 2 + 1)) & 1) });
            config.pinned.push_back ({ sf::pid::band[b].solo,   (float) ((state >> b) & 1) });
            config.pinned.push_back ({ sf::pid::band[b].bypass, (float) ((state >> (7 - b)) & 1) });
        }

        config.seed = seed++;
        exercise (*p, config, 4);
    }
}

// ---------------------------------------------------------------------------
//  The same crossings under awkward host conditions
// ---------------------------------------------------------------------------

SF_TEST (cross_pairsSurviveEverySampleRateAndLayout)
{
    struct Host { double sampleRate; int block; int mainChannels; int scChannels; };

    const Host hosts[] = { { 44100.0,   32, 2, 2 },
                           { 48000.0,   64, 1, 1 },
                           { 88200.0,  128, 2, 1 },
                           { 96000.0,  256, 2, 2 },
                           { 176400.0, 512, 2, 0 },
                           { 192000.0, 1024, 2, 2 } };

    // A reduced pairwise set - the first eight dimensions against each other -
    // repeated on every host layout the plugin claims to support.
    const auto& dims = dimensions();
    const size_t reduced = juce::jmin ((size_t) 8, dims.size());

    unsigned seed = 300000;

    for (const auto& host : hosts)
    {
        auto p = makeProcessor (host.sampleRate, host.block,
                                host.mainChannels, host.scChannels);

        juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), host.block };
        juce::MidiBuffer midi;

        for (size_t i = 0; i < reduced; ++i)
        {
            for (size_t j = i + 1; j < reduced; ++j)
            {
                for (const auto a : dims[i].values)
                {
                    for (const auto b : dims[j].values)
                    {
                        Configuration config;
                        config.pinned = { { dims[i].id, a }, { dims[j].id, b } };
                        config.seed = seed++;
                        apply (*p, config);

                        const auto where = std::string (" at ") + num (host.sampleRate)
                                           + " Hz / " + std::to_string (host.block)
                                           + " samples / " + std::to_string (host.mainChannels)
                                           + "ch | " + config.describe();

                        for (int n = 0; n < 4; ++n)
                        {
                            fillSignal (buffer, host.mainChannels, host.scChannels,
                                        host.sampleRate, n * host.block);
                            p->processBlock (buffer, midi);

                            SF_CHECK_MSG (allFinite (buffer), ("non-finite" + where).c_str());

                            for (int ch = 0; ch < host.mainChannels; ++ch)
                                SF_CHECK_MSG (buffer.getMagnitude (ch, 0, host.block) < 12.0f,
                                              ("runaway level" + where).c_str());
                        }
                    }
                }
            }
        }
    }
}

SF_TEST (cross_settingsChangingEveryBlockNeverDestabilises)
{
    // The sweep above changes settings between runs. This one changes them
    // *during* one, on every single block, which is what a host does when a
    // dozen automation lanes are moving at once.
    auto p = makeProcessor (48000.0, kBlock);

    juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), kBlock };
    juce::MidiBuffer midi;

    const auto& dims = dimensions();
    std::mt19937 rng { 4242 };
    std::uniform_real_distribution<float> unit { 0.0f, 1.0f };

    for (int b = 0; b < 3000; ++b)
    {
        // Two random discrete settings and a handful of continuous ones move.
        for (int n = 0; n < 2; ++n)
        {
            const auto& dim = dims[(size_t) (unit (rng) * (float) dims.size()) % dims.size()];
            const auto value = dim.values[(size_t) (unit (rng) * (float) dim.values.size())
                                          % dim.values.size()];

            if (auto* parameter = p->getAPVTS().getParameter (dim.id))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        }

        for (const auto* id : { sf::pid::mix, sf::pid::macroDepth, sf::pid::macroTiming,
                                sf::pid::bmXover1, sf::pid::bmXover2, sf::pid::bmXover3,
                                sf::pid::band[0].depth, sf::pid::band[1].threshold })
            if (auto* parameter = p->getAPVTS().getParameter (id))
                parameter->setValueNotifyingHost (unit (rng));

        fillSignal (buffer, 2, 2, 48000.0, b * kBlock);
        p->processBlock (buffer, midi);

        SF_CHECK_MSG (allFinite (buffer),
                      ("non-finite output while automating, block " + std::to_string (b)).c_str());

        for (int ch = 0; ch < 2; ++ch)
            SF_CHECK_MSG (buffer.getMagnitude (ch, 0, kBlock) < 12.0f,
                          ("runaway level while automating, block " + std::to_string (b)).c_str());
    }
}

SF_TEST (cross_everyPresetAgainstEveryRoutingAndQuality)
{
    // A preset is a whole configuration at once. Loading each one and then
    // overriding the two settings a user is most likely to reach for afterwards
    // crosses the factory content against the global controls.
    for (int preset = 0; preset < sf::FactoryPresets::getNumPresets(); ++preset)
    {
        auto p = makeProcessor (48000.0, kBlock);

        for (int routing = 0; routing < 2; ++routing)
        {
            for (int quality = 0; quality < 4; ++quality)
            {
                p->setCurrentProgram (preset);
                setParam (*p, sf::pid::routingMode, (float) routing);
                setParam (*p, sf::pid::quality, (float) quality);

                juce::AudioBuffer<float> buffer { p->getTotalNumInputChannels(), kBlock };
                juce::MidiBuffer midi;

                const auto where = "preset '" + sf::FactoryPresets::getName (preset).toStdString()
                                   + "' routing " + std::to_string (routing)
                                   + " quality " + std::to_string (quality);

                for (int b = 0; b < 24; ++b)
                {
                    fillSignal (buffer, 2, 2, 48000.0, b * kBlock);
                    p->processBlock (buffer, midi);

                    SF_CHECK_MSG (allFinite (buffer), ("non-finite: " + where).c_str());

                    for (int ch = 0; ch < 2; ++ch)
                        SF_CHECK_MSG (buffer.getMagnitude (ch, 0, kBlock) < 12.0f,
                                      ("runaway level: " + where).c_str());
                }
            }
        }
    }
}

SF_TEST (cross_stateRoundTripsFromEveryPairedConfiguration)
{
    // Whatever the sweep can reach, the session file has to be able to carry.
    const auto& dims = dimensions();

    auto source = makeProcessor (48000.0, kBlock);
    auto restored = makeProcessor (48000.0, kBlock);

    unsigned seed = 500000;

    for (size_t i = 0; i < dims.size(); ++i)
    {
        for (size_t j = i + 1; j < dims.size(); j += 3)
        {
            Configuration config;
            config.pinned = { { dims[i].id, dims[i].values.front() },
                              { dims[j].id, dims[j].values.back() } };
            config.seed = seed++;

            apply (*source, config);

            juce::MemoryBlock state;
            source->getStateInformation (state);
            restored->setStateInformation (state.getData(), (int) state.getSize());

            for (auto* parameter : source->getParameters())
            {
                auto* withID = dynamic_cast<juce::RangedAudioParameter*> (parameter);

                if (withID == nullptr)
                    continue;

                auto* other = restored->getAPVTS().getParameter (withID->paramID);
                SF_CHECK_MSG (other != nullptr,
                              ("parameter lost in a round trip: "
                               + withID->paramID.toStdString()).c_str());
                // Compare the value, not the normalised float: a bool set to
                // 0.03 legitimately reads back as 0.0, and both mean "Off".
                SF_CHECK_MSG (other->getCurrentValueAsText() == parameter->getCurrentValueAsText(),
                              (withID->paramID.toStdString() + " did not survive a round trip: "
                               + other->getCurrentValueAsText().toStdString() + " instead of "
                               + parameter->getCurrentValueAsText().toStdString() + " | "
                               + config.describe()).c_str());
            }
        }
    }
}
