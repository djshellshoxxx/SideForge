#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace sft
{
struct TestCase
{
    const char* name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry();

struct Registrar
{
    Registrar (const char* n, std::function<void()> f) { registry().push_back ({ n, std::move (f) }); }
};

struct Failure : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

[[noreturn]] inline void failHere (const char* file, int line, const std::string& what)
{
    throw Failure (std::string (file) + ":" + std::to_string (line) + "  " + what);
}

inline std::string num (double v)
{
    char buf[64];
    std::snprintf (buf, sizeof (buf), "%.9g", v);
    return buf;
}
} // namespace sft

#define SF_TEST(name)                                                        \
    static void name();                                                      \
    static ::sft::Registrar sf_reg_##name { #name, name };                   \
    static void name()

#define SF_CHECK(cond)                                                       \
    do {                                                                     \
        if (! (cond))                                                        \
            ::sft::failHere (__FILE__, __LINE__, "expected: " #cond);        \
    } while (false)

#define SF_CHECK_MSG(cond, msg)                                              \
    do {                                                                     \
        if (! (cond))                                                        \
            ::sft::failHere (__FILE__, __LINE__,                             \
                             std::string ("expected: " #cond " | ") + (msg));\
    } while (false)

#define SF_CHECK_NEAR(a, b, tol)                                             \
    do {                                                                     \
        const double sf_a = (double) (a), sf_b = (double) (b);               \
        if (! (std::abs (sf_a - sf_b) <= (double) (tol)))                    \
            ::sft::failHere (__FILE__, __LINE__,                             \
                             #a " = " + ::sft::num (sf_a) + ", expected "    \
                             + ::sft::num (sf_b) + " +/- " + ::sft::num ((double) (tol))); \
    } while (false)

#define SF_CHECK_FINITE(v)                                                   \
    do {                                                                     \
        const double sf_v = (double) (v);                                    \
        if (! std::isfinite (sf_v))                                          \
            ::sft::failHere (__FILE__, __LINE__, #v " is not finite");       \
    } while (false)
