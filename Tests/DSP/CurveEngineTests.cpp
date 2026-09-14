#include "../TestHelpers.h"
#include "DSP/CurveEngine.h"
#include "Model/CurveModel.h"

using namespace sft;

namespace
{
/** A curve model rendered into a table, ready for the engine to read. */
struct PreparedCurve
{
    explicit PreparedCurve (int shapeIndex)
    {
        model.setPresetShape (shapeIndex);
        model.renderTo (table);
    }

    sf::CurveModel model;
    sf::CurveTable table;
};

sf::TransportInfo stoppedTransport (double bpm = 120.0)
{
    sf::TransportInfo t;
    t.valid = true;
    t.isPlaying = false;
    t.bpm = bpm;
    return t;
}

sf::TransportInfo playingTransport (double ppq, double bpm = 120.0)
{
    auto t = stoppedTransport (bpm);
    t.isPlaying = true;
    t.ppqPosition = ppq;
    return t;
}

/** Renders `numSamples` of the engine in blocks of `blockSize`. */
std::vector<float> render (sf::CurveEngine& engine, int numSamples, int blockSize,
                           const sf::TransportInfo& transport, double sampleRate,
                           const sf::TriggerEvents& triggers = {})
{
    std::vector<float> out ((size_t) numSamples, 0.0f);
    auto t = transport;
    int done = 0;

    while (done < numSamples)
    {
        const auto n = juce::jmin (blockSize, numSamples - done);
        const auto quarters = t.ppqPerSample (sampleRate) * (double) n;

        sf::TriggerEvents blockTriggers;

        for (int i = 0; i < triggers.count; ++i)
            if (triggers.positions[i] >= done && triggers.positions[i] < done + n)
                blockTriggers.add (triggers.positions[i] - done);

        engine.process (n, t, quarters, blockTriggers, out.data() + done);

        t.ppqPosition += quarters;
        done += n;
    }

    return out;
}
} // namespace

SF_TEST (curveEngine_flatCurveIsUnityGain)
{
    PreparedCurve curve { 0 };   // Flat
    sf::CurveEngine engine;
    engine.prepare (48000.0, 512);
    engine.setTable (&curve.table);

    sf::CurveEngine::Settings s;
    s.syncMode = sf::SyncMode::Free;
    s.rateHz = 2.0f;
    s.trigMode = sf::TrigMode::Loop;
    s.smoothMs = 0.0f;
    engine.setSettings (s);

    const auto out = render (engine, 4800, 128, stoppedTransport(), 48000.0);

    for (auto v : out)
        SF_CHECK_NEAR (v, 1.0, 1.0e-5);
}

SF_TEST (curveEngine_freeRunSawFollowsTheDrawnShape)
{
    constexpr double sr = 48000.0;
    constexpr float rate = 2.0f;                 // 0.5 s per cycle
    const int cycle = (int) (sr / rate);         // 24000 samples

    PreparedCurve curve { 3 };   // Saw Down: 1 -> 0 across the cycle
    sf::CurveEngine engine;
    engine.prepare (sr, 512);
    engine.setTable (&curve.table);

    sf::CurveEngine::Settings s;
    s.syncMode = sf::SyncMode::Free;
    s.rateHz = rate;
    s.trigMode = sf::TrigMode::Loop;
    s.smoothMs = 0.0f;   // no anti-click filter, so the drawn curve is exact
    s.depth = 1.0f;
    engine.setSettings (s);

    const auto out = render (engine, cycle, 256, stoppedTransport(), sr);

    for (const int probe : { 0, cycle / 4, cycle / 2, (3 * cycle) / 4, cycle - 1 })
    {
        const auto expected = 1.0 - (double) probe / (double) cycle;
        SF_CHECK_NEAR (out[(size_t) probe], expected, 0.01);
    }
}

SF_TEST (curveEngine_timingIsIndependentOfBlockSize)
{
    constexpr double sr = 48000.0;
    PreparedCurve curve { 2 };   // Sine dip
    const int samples = 24000;

    std::vector<std::vector<float>> renders;

    for (const int blockSize : { 1, 7, 64, 333, 512, 2048 })
    {
        sf::CurveEngine engine;
        engine.prepare (sr, 2048);
        engine.setTable (&curve.table);

        sf::CurveEngine::Settings s;
        s.syncMode = sf::SyncMode::Free;
        s.rateHz = 2.0f;
        s.trigMode = sf::TrigMode::Loop;
        s.smoothMs = 0.0f;
        engine.setSettings (s);

        renders.push_back (render (engine, samples, blockSize, stoppedTransport(), sr));
    }

    for (size_t r = 1; r < renders.size(); ++r)
        for (size_t i = 0; i < renders[0].size(); ++i)
            SF_CHECK_NEAR (renders[r][i], renders[0][i], 1.0e-5);
}

SF_TEST (curveEngine_timingIsCorrectAtEverySampleRate)
{
    PreparedCurve curve { 3 };   // Saw Down

    for (const double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        sf::CurveEngine engine;
        engine.prepare (sr, 1024);
        engine.setTable (&curve.table);

        sf::CurveEngine::Settings s;
        s.syncMode = sf::SyncMode::Free;
        s.rateHz = 4.0f;             // 0.25 s per cycle at every rate
        s.trigMode = sf::TrigMode::Loop;
        s.smoothMs = 0.0f;
        engine.setSettings (s);

        const auto cycle = (int) (sr / 4.0);
        const auto out = render (engine, cycle, 256, stoppedTransport(), sr);

        // Half a cycle in, a saw-down curve is at 0.5 regardless of sample rate.
        SF_CHECK_NEAR (out[(size_t) (cycle / 2)], 0.5, 0.01);
    }
}

SF_TEST (curveEngine_tempoSyncLocksToTheHostPosition)
{
    constexpr double sr = 48000.0;
    PreparedCurve curve { 3 };   // Saw Down

    sf::CurveEngine engine;
    engine.prepare (sr, 512);
    engine.setTable (&curve.table);

    sf::CurveEngine::Settings s;
    s.syncMode = sf::SyncMode::Tempo;
    s.divisionIndex = 13;        // 1/4 note
    s.trigMode = sf::TrigMode::Loop;
    s.smoothMs = 0.0f;
    engine.setSettings (s);

    // 120 bpm, quarter-note cycle: one cycle is 0.5 s = 24000 samples.
    const auto out = render (engine, 24000, 256, playingTransport (0.0), sr);

    SF_CHECK_NEAR (out[0], 1.0, 0.01);
    SF_CHECK_NEAR (out[12000], 0.5, 0.02);
}

SF_TEST (curveEngine_startingMidBarLandsAtTheRightPhase)
{
    constexpr double sr = 48000.0;
    PreparedCurve curve { 3 };   // Saw Down

    // Playback starting at PPQ 1.25 with a 1/4 cycle is 25 % into the cycle, so a
    // saw-down curve must begin at 0.75 rather than at 1.0.
    for (const auto [ppq, expected] : { std::pair<double, double> { 0.25, 0.75 },
                                        std::pair<double, double> { 1.25, 0.75 },
                                        std::pair<double, double> { 2.50, 0.50 },
                                        std::pair<double, double> { 7.75, 0.25 } })
    {
        sf::CurveEngine engine;
        engine.prepare (sr, 512);
        engine.setTable (&curve.table);

        sf::CurveEngine::Settings s;
        s.syncMode = sf::SyncMode::Tempo;
        s.divisionIndex = 13;
        s.trigMode = sf::TrigMode::Loop;
        s.smoothMs = 0.0f;
        engine.setSettings (s);

        const auto out = render (engine, 512, 512, playingTransport (ppq), sr);
        SF_CHECK_NEAR (out[0], expected, 0.01);
    }
}

SF_TEST (curveEngine_tempoChangeTakesEffectImmediately)
{
    constexpr double sr = 48000.0;
    PreparedCurve curve { 3 };

    sf::CurveEngine engine;
    engine.prepare (sr, 512);
    engine.setTable (&curve.table);

    sf::CurveEngine::Settings s;
    s.syncMode = sf::SyncMode::Tempo;
    s.divisionIndex = 13;
    s.trigMode = sf::TrigMode::Loop;
    s.smoothMs = 0.0f;
    engine.setSettings (s);

    std::vector<float> out (512, 0.0f);

    // 120 bpm at PPQ 0.5 -> halfway through the cycle.
    auto t = playingTransport (0.5, 120.0);
    engine.process (512, t, t.ppqPerSample (sr) * 512.0, {}, out.data());
    SF_CHECK_NEAR (out[0], 0.5, 0.01);

    // Double the tempo. The phase is still a function of PPQ, so the same position
    // still maps to the same point on the curve.
    t = playingTransport (0.5, 240.0);
    engine.process (512, t, t.ppqPerSample (sr) * 512.0, {}, out.data());
    SF_CHECK_NEAR (out[0], 0.5, 0.01);

    // ...but it now advances through the curve twice as fast.
    const auto advanced = out[511];
    t = playingTransport (0.5, 120.0);
    engine.process (512, t, t.ppqPerSample (sr) * 512.0, {}, out.data());
    SF_CHECK_MSG (advanced < out[511], "a higher tempo must traverse the curve faster");
}

SF_TEST (curveEngine_transportJumpDoesNotProduceGarbage)
{
    constexpr double sr = 48000.0;
    PreparedCurve curve { 2 };

    sf::CurveEngine engine;
    engine.prepare (sr, 512);
    engine.setTable (&curve.table);

    sf::CurveEngine::Settings s;
    s.syncMode = sf::SyncMode::Tempo;
    s.divisionIndex = 13;
    s.trigMode = sf::TrigMode::Loop;
    engine.setSettings (s);

    std::vector<float> out (512, 0.0f);

    for (const double ppq : { 0.0, 4.0, 0.0, 128.5, -3.25, 1000000.0, 0.125 })
    {
        auto t = playingTransport (ppq);
        engine.process (512, t, t.ppqPerSample (sr) * 512.0, {}, out.data());

        for (auto v : out)
        {
            SF_CHECK_FINITE (v);
            SF_CHECK (v >= 0.0f && v <= 1.0f);
        }
    }
}

SF_TEST (curveEngine_depthScalesTheDuckAndZeroIsUnity)
{
    constexpr double sr = 48000.0;
    PreparedCurve curve { 3 };   // Saw Down, so the deepest point is 0

    for (const float depth : { 0.0f, 0.25f, 0.5f, 1.0f })
    {
        sf::CurveEngine engine;
        engine.prepare (sr, 512);
        engine.setTable (&curve.table);

        sf::CurveEngine::Settings s;
        s.syncMode = sf::SyncMode::Free;
        s.rateHz = 2.0f;
        s.trigMode = sf::TrigMode::Loop;
        s.smoothMs = 0.0f;
        s.depth = depth;
        engine.setSettings (s);

        const auto out = render (engine, 24000, 512, stoppedTransport(), sr);
        const auto deepest = *std::min_element (out.begin(), out.end());

        SF_CHECK_NEAR (deepest, 1.0 - (double) depth, 0.02);
    }
}

SF_TEST (curveEngine_oneShotRunsOnceThenReturnsToUnity)
{
    constexpr double sr = 48000.0;
    PreparedCurve curve { 3 };   // Saw Down: ends at 0

    sf::CurveEngine engine;
    engine.prepare (sr, 512);
    engine.setTable (&curve.table);

    sf::CurveEngine::Settings s;
    s.syncMode = sf::SyncMode::Free;
    s.rateHz = 10.0f;            // 4800-sample cycle
    s.trigMode = sf::TrigMode::OneShot;
    s.smoothMs = 0.0f;
    engine.setSettings (s);

    // Idle before any trigger: unity.
    auto out = render (engine, 2400, 256, stoppedTransport(), sr);
    for (auto v : out)
        SF_CHECK_NEAR (v, 1.0, 1.0e-4);

    sf::TriggerEvents triggers;
    triggers.add (0);
    out = render (engine, 9600, 256, stoppedTransport(), sr, triggers);

    // Runs the cycle once...
    SF_CHECK_NEAR (out[0], 1.0, 0.02);
    SF_CHECK_NEAR (out[2400], 0.5, 0.03);
    SF_CHECK_NEAR (out[4700], 0.02, 0.05);
    // ...and then, with no post-delay, releases straight back to unity.
    SF_CHECK_NEAR (out[6000], 1.0, 0.02);
    SF_CHECK_NEAR (out[9599], 1.0, 0.02);

    // A second trigger runs it again.
    sf::TriggerEvents second;
    second.add (0);
    out = render (engine, 4800, 256, stoppedTransport(), sr, second);
    SF_CHECK_NEAR (out[2400], 0.5, 0.03);
}

SF_TEST (curveEngine_postDelayHoldsTheFinalValueBeforeReleasing)
{
    constexpr double sr = 48000.0;
    PreparedCurve curve { 3 };   // Saw Down: ends at 0

    sf::CurveEngine engine;
    engine.prepare (sr, 512);
    engine.setTable (&curve.table);

    sf::CurveEngine::Settings s;
    s.syncMode = sf::SyncMode::Free;
    s.rateHz = 10.0f;            // 4800-sample cycle
    s.trigMode = sf::TrigMode::OneShot;
    s.smoothMs = 0.0f;
    s.postDelayMs = 50.0f;       // 2400 samples held at the final value
    engine.setSettings (s);

    sf::TriggerEvents triggers;
    triggers.add (0);
    const auto out = render (engine, 12000, 256, stoppedTransport(), sr, triggers);

    // Inside the post-delay the curve's last value is still held...
    SF_CHECK_NEAR (out[6000], 0.0, 0.05);
    // ...and once it expires the gain is released back to unity.
    SF_CHECK_NEAR (out[9000], 1.0, 0.02);
}

SF_TEST (curveEngine_audioModeRetriggersFromTheStart)
{
    constexpr double sr = 48000.0;
    PreparedCurve curve { 3 };

    sf::CurveEngine engine;
    engine.prepare (sr, 512);
    engine.setTable (&curve.table);

    sf::CurveEngine::Settings s;
    s.syncMode = sf::SyncMode::Free;
    s.rateHz = 2.0f;             // 24000-sample cycle
    s.trigMode = sf::TrigMode::Audio;
    s.smoothMs = 0.0f;
    engine.setSettings (s);

    sf::TriggerEvents triggers;
    triggers.add (0);
    triggers.add (6000);         // retrigger a quarter of the way in

    const auto out = render (engine, 12000, 256, stoppedTransport(), sr, triggers);

    SF_CHECK_NEAR (out[5999], 0.75, 0.02);   // just before the retrigger
    SF_CHECK_NEAR (out[6000], 1.0, 0.02);    // restarted
    SF_CHECK_NEAR (out[9000], 0.875, 0.02);  // running again from the top
}

SF_TEST (curveEngine_preDelayHoldsUnityBeforeTheCurveStarts)
{
    constexpr double sr = 48000.0;
    PreparedCurve curve { 3 };

    sf::CurveEngine engine;
    engine.prepare (sr, 512);
    engine.setTable (&curve.table);

    sf::CurveEngine::Settings s;
    s.syncMode = sf::SyncMode::Free;
    s.rateHz = 10.0f;
    s.trigMode = sf::TrigMode::OneShot;
    s.smoothMs = 0.0f;
    s.preDelayMs = 20.0f;        // 960 samples
    engine.setSettings (s);

    sf::TriggerEvents triggers;
    triggers.add (0);
    const auto out = render (engine, 4800, 256, stoppedTransport(), sr, triggers);

    SF_CHECK_NEAR (out[500], 1.0, 1.0e-3);    // still inside the pre-delay
    SF_CHECK_MSG (out[2000] < 0.9f, "the curve must start once the pre-delay expires");
}

SF_TEST (curveEngine_attackRateLimitSlowsTheDuck)
{
    constexpr double sr = 48000.0;
    PreparedCurve curve { 5 };   // Gate: an instantaneous step down at the halfway point

    sf::CurveEngine engine;
    engine.prepare (sr, 512);
    engine.setTable (&curve.table);

    sf::CurveEngine::Settings s;
    s.syncMode = sf::SyncMode::Free;
    s.rateHz = 1.0f;             // 48000-sample cycle, step at 24000
    s.trigMode = sf::TrigMode::Loop;
    s.smoothMs = 0.0f;
    s.attackMs = 0.0f;
    engine.setSettings (s);

    const auto instant = render (engine, 48000, 512, stoppedTransport(), sr);
    SF_CHECK_NEAR (instant[24100], 0.0, 0.02);   // 0 ms attack follows the step exactly

    s.attackMs = 50.0f;          // 2400 samples to traverse the full range
    sf::CurveEngine slow;
    slow.prepare (sr, 512);
    slow.setTable (&curve.table);
    slow.setSettings (s);

    const auto limited = render (slow, 48000, 512, stoppedTransport(), sr);
    SF_CHECK_MSG (limited[24100] > 0.8f, "the attack rate limit must slow the fall");
    SF_CHECK_NEAR (limited[26500], 0.0, 0.05);   // ...but it still gets there
}

SF_TEST (curveEngine_swingShiftsTheBoundaryBetweenCyclePairs)
{
    constexpr double sr = 48000.0;
    PreparedCurve curve { 3 };

    sf::CurveEngine engine;
    engine.prepare (sr, 512);
    engine.setTable (&curve.table);

    sf::CurveEngine::Settings s;
    s.syncMode = sf::SyncMode::Free;
    s.rateHz = 2.0f;
    s.trigMode = sf::TrigMode::Loop;
    s.smoothMs = 0.0f;
    s.swing = 0.25f;
    engine.setSettings (s);

    const auto out = render (engine, 48000, 512, stoppedTransport(), sr);

    for (auto v : out)
    {
        SF_CHECK_FINITE (v);
        SF_CHECK (v >= 0.0f && v <= 1.0f);
    }

    // With positive swing the first cycle of each pair is stretched, so it has not
    // finished by the point an unswung curve would have.
    s.swing = 0.0f;
    sf::CurveEngine straight;
    straight.prepare (sr, 512);
    straight.setTable (&curve.table);
    straight.setSettings (s);

    const auto reference = render (straight, 48000, 512, stoppedTransport(), sr);
    SF_CHECK_MSG (out[20000] > reference[20000],
                  "positive swing must lengthen the first cycle of the pair");
}

SF_TEST (curveEngine_outputIsAlwaysBoundedAndFinite)
{
    constexpr double sr = 44100.0;
    std::mt19937 rng { 99 };
    std::uniform_int_distribution<int> shapeDist { 0, sf::CurveModel::getShapeNames().size() - 1 };
    std::uniform_real_distribution<float> unit { 0.0f, 1.0f };

    for (int iteration = 0; iteration < 40; ++iteration)
    {
        PreparedCurve curve { shapeDist (rng) };

        sf::CurveEngine engine;
        engine.prepare (sr, 512);
        engine.setTable (&curve.table);

        sf::CurveEngine::Settings s;
        s.syncMode = (iteration % 2 == 0) ? sf::SyncMode::Tempo : sf::SyncMode::Free;
        s.divisionIndex = iteration % sf::kNumDivisions;
        s.rateHz = 0.01f + unit (rng) * 50.0f;
        s.trigMode = (sf::TrigMode) (iteration % 4);
        s.depth = unit (rng);
        s.phaseOffset = unit (rng);
        s.swing = unit (rng) - 0.5f;
        s.smoothMs = unit (rng) * 50.0f;
        s.preDelayMs = unit (rng) * 200.0f;
        s.postDelayMs = unit (rng) * 200.0f;
        s.attackMs = unit (rng) * 200.0f;
        s.holdMs = unit (rng) * 500.0f;
        s.recoverMs = unit (rng) * 2000.0f;
        engine.setSettings (s);

        sf::TriggerEvents triggers;
        triggers.add (0);
        triggers.add (1000);

        const auto out = render (engine, 20000, 128, playingTransport (unit (rng) * 64.0), sr, triggers);

        for (auto v : out)
        {
            SF_CHECK_FINITE (v);
            SF_CHECK (v >= 0.0f && v <= 1.0f);
        }
    }
}
