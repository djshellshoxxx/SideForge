#include "TestFramework.h"
#include <juce_events/juce_events.h>
#include <algorithm>
#include <cstring>

namespace sft
{
std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}
} // namespace sft

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const char* filter = nullptr;

    for (int i = 1; i < argc; ++i)
        if (std::strncmp (argv[i], "--filter=", 9) == 0)
            filter = argv[i] + 9;

    auto& tests = sft::registry();
    std::stable_sort (tests.begin(), tests.end(),
                      [] (const sft::TestCase& a, const sft::TestCase& b)
                      { return std::strcmp (a.name, b.name) < 0; });

    int passed = 0, failed = 0, skipped = 0;

    for (const auto& t : tests)
    {
        if (filter != nullptr && std::strstr (t.name, filter) == nullptr)
        {
            ++skipped;
            continue;
        }

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
