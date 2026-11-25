// Mock FoamSide without OpenFOAM: repeats UPstream::init's -mpi-split-by-appnum
// sequence (src/Pstream/mpi/UPstream.C:244,544) for stacks without v2606.
#include "../common/bridgeProto.H"
#include <cstring>

int main(int argc, char* argv[])
{
    bool thr = false;  // -mpi-threads, as in OpenFOAM's argList
    for (int i = 1; i < argc; ++i) if (!std::strcmp(argv[i], "-mpi-threads")) thr = true;
    int prov; MPI_Init_thread(&argc, &argv, thr ? MPI_THREAD_MULTIPLE : MPI_THREAD_SINGLE, &prov);
    int* app = nullptr; int flag = 0, appNum = 0;
    MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_APPNUM, &app, &flag);
    if (flag) appNum = *app;
    MPI_Comm world; MPI_Comm_split(MPI_COMM_WORLD, appNum, 0, &world);
    int steps = 5;
    for (int i = 1; i+1 < argc; ++i) if (!std::strcmp(argv[i], "-steps")) steps = std::atoi(argv[i+1]);
    int wr; MPI_Comm_rank(world, &wr);

    bridgeProto::Bridge b; b.open(true);
    const int n = int(b.peers.size());
    std::vector<int> cnt(n), disp(n), zero(n, 0); std::vector<double> buf;
    for (int s = 0; s < steps; ++s)
    {
        MPI_Request rq;
        MPI_Ineighbor_alltoall(zero.data(), 1, MPI_INT, cnt.data(), 1, MPI_INT, b.graph, &rq);
        MPI_Wait(&rq, MPI_STATUS_IGNORE);
        int tot = 0; for (int i = 0; i < n; ++i) { disp[i] = tot; tot += cnt[i]; }
        buf.resize(tot);
        MPI_Neighbor_alltoallv(nullptr, zero.data(), zero.data(), MPI_DOUBLE, buf.data(), cnt.data(), disp.data(), MPI_DOUBLE, b.graph);
        for (double& x : buf) x *= 2.0;
        MPI_Neighbor_alltoallv(buf.data(), cnt.data(), disp.data(), MPI_DOUBLE, nullptr, zero.data(), zero.data(), MPI_DOUBLE, b.graph);
        double ctl[2] = {1e-3*(s+1), s == steps-1 ? 1.0 : 0.0};
        MPI_Bcast(ctl, 2, MPI_DOUBLE, b.foamRoot, b.bridge);
        int g = 0; MPI_Allreduce(&tot, &g, 1, MPI_INT, MPI_SUM, world);
        if (wr == 0) std::printf("mock-foam: step %d particles %d\n", s, g);
    }
    b.close();
    MPI_Comm_free(&world);
    if (wr == 0) std::printf("End\n");
    MPI_Finalize();
    return 0;
}
