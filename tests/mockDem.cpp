// SPDX-License-Identifier: GPL-3.0-or-later
// Mock DemSide: repeats yade.mpy's split at import (yade-pgf py/mpy.py:211), then sends its
// bodies and checks every value. Options: -thread single|multiple (mpi4py: multiple),
// -expect-mode M, -closed (as mockFoam), -stop-at K (close before exchange K),
// -stop-rank R (only DEM rank R stops early; the others keep stepping).
#include "mockCommon.hpp"
#include <algorithm>
#include <cstdio>

int main(int argc, char* argv[])
{
    using namespace mock;
    const bool single = !std::strcmp(opt(argc, argv, "-thread", "multiple"), "single");
    int provided = 0;
    MPI_Init_thread(&argc, &argv, single ? MPI_THREAD_SINGLE : MPI_THREAD_MULTIPLE, &provided);
    MPI_Comm dem;
    MPI_Comm_split(MPI_COMM_WORLD, 3, 0, &dem);
    int d = 0;
    MPI_Comm_rank(dem, &d);

    const int stopRank = std::atoi(opt(argc, argv, "-stop-rank", "-1"));
    int stopAt = std::atoi(opt(argc, argv, "-stop-at", "-1"));
    if (stopRank >= 0 && d != stopRank) stopAt = -1;
    const bool closed = flag(argc, argv, "-closed");
    PgfDemBridge::DemSide side(MPI_COMM_WORLD, std::atoi(opt(argc, argv, "-expect-mode", "-1")));
    const LambdaMode mode = side.lambdaMode();
    const int su = PgfDemBridge::strideUp, nF = side.nFoam();

    long long applied = 0, lost = 0;
    int s = 0;
    bool stopped = false;
    for (; s != stopAt; ++s)
    {
        const int n = nBodies(d, s);
        std::vector<double> up(su*n), v(n);
        for (int i = 0; i < n; ++i) body(d, s, i, nF, &up[PgfDemBridge::strideUp*i]);
        const PgfDemBridge::Control c = side.exchange(up.data(), n, v.data());
        if (c.stopped) { stopped = true; break; }
        for (int i = 0; i < n; ++i)
        {
            const double* r = &up[PgfDemBridge::strideUp*i];
            int owner = -1;
            for (int f = 0; f < nF && owner < 0; ++f) if (finds(f, nF, r, closed)) owner = f;
            const double want = owner < 0 ? std::numeric_limits<double>::quiet_NaN() : value(owner, r, mode);
            const bool ok = owner < 0 ? std::isnan(v[i]) : std::abs(v[i] - want) < 1e-9*std::abs(want);
            if (!ok)
            {
                std::fprintf(stderr, "mock-dem %d: body %d step %d got %.17g, want %.17g\n", d, i, s, v[i], want);
                MPI_Abort(MPI_COMM_WORLD, 4);
            }
            owner < 0 ? ++lost : ++applied;
        }
        if (side.nLost() != int(n - std::count_if(v.begin(), v.end(), [](double x) { return !std::isnan(x); })) ||
            std::abs(c.dt - 1e-3*(s + 1)) > 1e-15)
        {
            std::fprintf(stderr, "mock-dem %d: nLost or dt wrong at step %d\n", d, s);
            MPI_Abort(MPI_COMM_WORLD, 4);
        }
        if (c.done) { ++s; break; }
    }
    side.close();

    long long neg = closed ? 0 : -applied, tot = 0;
    MPI_Allreduce(MPI_IN_PLACE, &applied, 1, MPI_LONG_LONG, MPI_SUM, dem);
    MPI_Allreduce(MPI_IN_PLACE, &lost, 1, MPI_LONG_LONG, MPI_SUM, dem);
    MPI_Allreduce(&neg, &tot, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
    if (d == 0)
        std::printf("mock-dem: %d exchanges, %lld applied, %lld lost%s\nRUN FINISH\n",
                    s, applied, lost, stopped ? ", Foam stopped" : "");
    MPI_Comm_free(&dem);
    MPI_Finalize();
    return tot == 0 ? 0 : 5;
}
