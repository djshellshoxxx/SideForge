#include "TestHelpers.h"
#include "Model/CurveModel.h"

using namespace sft;

SF_TEST (curve_defaultIsFlatUnity)
{
    sf::CurveModel m;

    for (float x = 0.0f; x <= 1.0f; x += 0.05f)
        SF_CHECK_NEAR (m.evaluate (x), 1.0f, 1.0e-5f);
}

SF_TEST (curve_endpointsArePinnedAndOrdered)
{
    sf::CurveModel m;
    m.addNode (0.5f, 0.2f);
    m.addNode (0.25f, 0.8f);
    m.addNode (0.75f, 0.4f);

    SF_CHECK_NEAR (m.getNode (0).x, 0.0f, 1.0e-6f);
    SF_CHECK_NEAR (m.getNode (m.getNumNodes() - 1).x, 1.0f, 1.0e-6f);

    for (int i = 1; i < m.getNumNodes(); ++i)
        SF_CHECK (m.getNode (i).x >= m.getNode (i - 1).x);
}

SF_TEST (curve_endpointsCannotBeRemoved)
{
    sf::CurveModel m;
    m.addNode (0.5f, 0.5f);
    const auto n = m.getNumNodes();

    m.removeNode (0);
    m.removeNode (m.getNumNodes() - 1);
    SF_CHECK (m.getNumNodes() == n);

    m.removeNode (1);
    SF_CHECK (m.getNumNodes() == n - 1);
}

SF_TEST (curve_linearSegmentIsActuallyLinear)
{
    sf::CurveModel m;
    m.setNodes ({ { 0.0f, 0.0f, 0.0f, sf::SegmentType::Curve },
                  { 1.0f, 1.0f, 0.0f, sf::SegmentType::Curve } });

    for (float x = 0.0f; x <= 1.0f; x += 0.05f)
        SF_CHECK_NEAR (m.evaluate (x), x, 1.0e-5f);
}

SF_TEST (curve_tensionStaysMonotonicAndInRange)
{
    for (float tension : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
    {
        sf::CurveModel m;
        m.setNodes ({ { 0.0f, 0.0f, tension, sf::SegmentType::Curve },
                      { 1.0f, 1.0f, 0.0f, sf::SegmentType::Curve } });

        float prev = -1.0f;

        for (int i = 0; i <= 200; ++i)
        {
            const auto y = m.evaluate ((float) i / 200.0f);
            SF_CHECK_FINITE (y);
            SF_CHECK (y >= -1.0e-5f && y <= 1.0f + 1.0e-5f);
            SF_CHECK_MSG (y >= prev - 1.0e-5f, "tension warp must stay monotonic");
            prev = y;
        }

        SF_CHECK_NEAR (m.evaluate (0.0f), 0.0f, 1.0e-5f);
        SF_CHECK_NEAR (m.evaluate (1.0f), 1.0f, 1.0e-5f);
    }
}

SF_TEST (curve_stepSegmentHoldsLeftValue)
{
    sf::CurveModel m;
    m.setNodes ({ { 0.0f, 1.0f, 0.0f, sf::SegmentType::Step },
                  { 0.5f, 0.0f, 0.0f, sf::SegmentType::Step },
                  { 1.0f, 0.0f, 0.0f, sf::SegmentType::Step } });

    SF_CHECK_NEAR (m.evaluate (0.1f), 1.0f, 1.0e-5f);
    SF_CHECK_NEAR (m.evaluate (0.49f), 1.0f, 1.0e-5f);
    SF_CHECK_NEAR (m.evaluate (0.51f), 0.0f, 1.0e-5f);
}

SF_TEST (curve_sCurveIsSymmetric)
{
    sf::CurveModel m;
    m.setNodes ({ { 0.0f, 0.0f, 0.0f, sf::SegmentType::SCurve },
                  { 1.0f, 1.0f, 0.0f, sf::SegmentType::SCurve } });

    for (float x = 0.0f; x <= 0.5f; x += 0.05f)
        SF_CHECK_NEAR (m.evaluate (x) + m.evaluate (1.0f - x), 1.0f, 1.0e-4f);
}

SF_TEST (curve_tableMatchesDirectEvaluation)
{
    sf::CurveModel m;
    m.setPresetShape (1);
    m.addNode (0.3f, 0.25f);

    sf::CurveTable table;
    m.renderTo (table);

    // Phase 1.0 wraps to 0.0 by design, so compare over [0, 1).
    for (int i = 0; i < 1000; ++i)
    {
        const auto x = (double) i / 1000.0;
        SF_CHECK_NEAR (table.lookup (x), m.evaluate ((float) x), 2.0e-3f);
    }

    SF_CHECK_NEAR (table.lookup (1.0), table.lookup (0.0), 1.0e-6f);

    // The guard sample carries evaluate(1.0) so the last inter-sample interval
    // interpolates to the true end of the curve instead of folding back to 0.
    SF_CHECK_NEAR (table.samples[sf::kCurveTableSize], m.evaluate (1.0f), 1.0e-5f);
}

SF_TEST (curve_serialisationRoundTrip)
{
    sf::CurveModel a;
    a.setPresetShape (7);
    a.addNode (0.11f, 0.33f);
    a.setTension (1, -0.7f);
    a.setSegmentType (1, sf::SegmentType::SCurve);

    sf::CurveModel b;
    SF_CHECK (b.fromValueTree (a.toValueTree()));
    SF_CHECK (b.getNumNodes() == a.getNumNodes());

    for (int i = 0; i < a.getNumNodes(); ++i)
    {
        SF_CHECK_NEAR (b.getNode (i).x, a.getNode (i).x, 1.0e-6f);
        SF_CHECK_NEAR (b.getNode (i).y, a.getNode (i).y, 1.0e-6f);
        SF_CHECK_NEAR (b.getNode (i).tension, a.getNode (i).tension, 1.0e-6f);
        SF_CHECK (b.getNode (i).type == a.getNode (i).type);
    }
}

SF_TEST (curve_rejectsMalformedTrees)
{
    sf::CurveModel m;
    m.setPresetShape (2);
    const auto before = m.evaluate (0.5f);

    SF_CHECK (! m.fromValueTree (juce::ValueTree { "NotACurve" }));
    SF_CHECK (! m.fromValueTree (juce::ValueTree { "Curve" }));   // no children

    juce::ValueTree oneNode { "Curve" };
    juce::ValueTree n { "Node" };
    n.setProperty ("x", 0.0, nullptr);
    oneNode.appendChild (n, nullptr);
    SF_CHECK (! m.fromValueTree (oneNode));

    SF_CHECK_NEAR (m.evaluate (0.5f), before, 1.0e-6f);
}

SF_TEST (curve_nodeCountIsBounded)
{
    sf::CurveModel m;

    for (int i = 0; i < 200; ++i)
        m.addNode ((float) i / 200.0f, 0.5f);

    SF_CHECK (m.getNumNodes() <= sf::CurveModel::kMaxNodes);
}

SF_TEST (curve_allPresetShapesAreFinite)
{
    const auto names = sf::CurveModel::getShapeNames();

    for (int s = 0; s < names.size(); ++s)
    {
        sf::CurveModel m;
        m.setPresetShape (s);
        SF_CHECK (m.getNumNodes() >= 2);

        for (int i = 0; i <= 100; ++i)
        {
            const auto y = m.evaluate ((float) i / 100.0f);
            SF_CHECK_FINITE (y);
            SF_CHECK (y >= 0.0f && y <= 1.0f);
        }
    }
}

SF_TEST (curve_exchangePublishesLatestTable)
{
    sf::CurveTableExchange ex;
    sf::CurveModel m;

    m.setPresetShape (3); // saw down
    ex.publish (m);
    SF_CHECK_NEAR (ex.acquire().lookup (0.0), 1.0, 1.0e-4);

    m.setPresetShape (4); // saw up
    ex.publish (m);
    SF_CHECK_NEAR (ex.acquire().lookup (0.0), 0.0, 1.0e-4);

    // Repeated publishes must keep cycling through distinct slots.
    for (int i = 0; i < 10; ++i)
    {
        m.setPresetShape (i % 2 == 0 ? 3 : 4);
        ex.publish (m);
        SF_CHECK_NEAR (ex.acquire().lookup (0.0), i % 2 == 0 ? 1.0 : 0.0, 1.0e-4);
    }
}
