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
