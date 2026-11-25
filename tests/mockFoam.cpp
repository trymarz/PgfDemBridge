// Mock FoamSide: repeats OpenFOAM v2606's -mpi-split-by-appnum (src/Pstream/mpi/UPstream.C:244,544),
// then answers every body it finds in its slab. Options: -steps N, -mode 0|1, -mpi-threads,
// -closed (closed slabs: bodies on a face are found twice), -stop-at K (close before exchange K).
#include "mockCommon.hpp"
#include <cstdio>

int main(int argc, char* argv[])
{
    using namespace mock;
    const bool threads = flag(argc, argv, "-mpi-threads");
    int provided = 0;
    MPI_Init_thread(&argc, &argv, threads ? MPI_THREAD_MULTIPLE : MPI_THREAD_SINGLE, &provided);
    int* app = nullptr; int has = 0, appNum = 0;
    MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_APPNUM, &app, &has);
    if (has) appNum = *app;
    MPI_Comm foam;
    MPI_Comm_split(MPI_COMM_WORLD, appNum, 0, &foam);
    int f = 0, nF = 1;
    MPI_Comm_rank(foam, &f);
    MPI_Comm_size(foam, &nF);

    const int steps = std::atoi(opt(argc, argv, "-steps", "6"));
    const int stopAt = std::atoi(opt(argc, argv, "-stop-at", "-1"));
    const LambdaMode mode = LambdaMode(std::atoi(opt(argc, argv, "-mode", "0")));
    const bool closed = flag(argc, argv, "-closed");

    PgfDemBridge::FoamSide side(mode, slab(f), MPI_COMM_WORLD);
    long long found = 0;
    int s = 0;
    for (; s < steps && s != stopAt; ++s)
    {
        const std::vector<double>& up = side.receive();
        if (side.stopped()) break;
        std::vector<double> v(side.nBodies(), std::numeric_limits<double>::quiet_NaN());
        for (std::size_t i = 0; i < v.size(); ++i)
        {
            const double* r = &up[PgfDemBridge::strideUp*i];
            if (!slab(f).contains(r) || r[3] != 2.0*r[0] || r[5] != 2.0*r[2] || r[6] != r[0] + r[1])
            {
                std::fprintf(stderr, "mock-foam %d: bad record %zu at step %d\n", f, i, s);
                MPI_Abort(MPI_COMM_WORLD, 4);
            }
            if (finds(f, nF, r, closed)) { v[i] = value(f, r, mode); ++found; }
        }
        side.reply(v.data(), 1e-3*(s + 1), s == steps - 1);
    }
    side.close();

    // Outside the bridge: Foam's finds must equal DEM's applied values (half-open slabs).
    long long diff = closed ? 0 : found, tot = 0;
    MPI_Allreduce(MPI_IN_PLACE, &found, 1, MPI_LONG_LONG, MPI_SUM, foam);
    MPI_Allreduce(&diff, &tot, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
    if (f == 0)
    {
        std::printf("mock-foam: %d exchanges, %lld bodies found%s\n", s, found, side.stopped() ? ", DEM stopped" : "");
        std::printf(tot == 0 ? "End\n" : "mock-foam: found/applied mismatch %lld\n", tot);
    }
    MPI_Comm_free(&foam);
    MPI_Finalize();
    return tot == 0 ? 0 : 5;
}
