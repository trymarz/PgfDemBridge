// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests of the core's MPI-free parts: layout and box containment.
#include "PgfDemBridge.hpp"
#include <cstdio>
#include <limits>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main()
{
    using namespace PgfDemBridge;
    CHECK(strideUp == 7);
    CHECK(strideDown == 1);

    const Box b{{0, 0, 0}, {1, 2, 3}};
    const double in[3] = {0.5, 1, 1.5}, face[3] = {1, 2, 3}, near[3] = {1 + 1e-12, 0, 0};
    const double out[3] = {1.01, 0, 0}, below[3] = {0, -1e-6, 0};
    const double nan3[3] = {std::numeric_limits<double>::quiet_NaN(), 0, 0};
    CHECK(b.contains(in));
    CHECK(b.contains(face));
    CHECK(b.contains(near));      // within v1's relative tolerance
    CHECK(!b.contains(out));
    CHECK(!b.contains(below));
    CHECK(!b.contains(nan3));     // NaN positions are in no box

    const Box big{{-1e6, 0, 0}, {1e6, 1, 1}};
    const double edge[3] = {1e6 + 1e-4, 0.5, 0.5};
    CHECK(big.contains(edge));    // tolerance scales with the box

    std::printf(failures ? "%d failures\n" : "all unit tests passed\n", failures);
    return failures ? 1 : 0;
}
