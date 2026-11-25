// Toy bridge core shared by both sides (C side); the Python side mirrors it.
#pragma once
#include <mpi.h>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace bridgeProto
{

constexpr int MAGIC = 0x50474644;  // "PGFD"
constexpr int VERSION = 2;

inline void check(int rc, const char* stage, MPI_Comm c)
{
    if (rc != MPI_SUCCESS)
    {
        char msg[MPI_MAX_ERROR_STRING]; int len = 0; int r = -1;
        MPI_Error_string(rc, msg, &len);
        MPI_Comm_rank(MPI_COMM_WORLD, &r);
        std::fprintf(stderr, "[bridge] world rank %d stage %s: %s\n", r, stage, msg);
        MPI_Abort(MPI_COMM_WORLD, 2);
    }
}

// Bridge = dup of MPI_COMM_WORLD; graph = bipartite, all Foam x all DEM ranks.
struct Bridge
{
    MPI_Comm bridge = MPI_COMM_NULL;
    MPI_Comm graph = MPI_COMM_NULL;
    std::vector<int> peers;   // bridge ranks of the other side
    int foamRoot = -1;        // bridge rank of Foam's rank 0

    void open(bool isFoam)
    {
        MPI_Comm_dup(MPI_COMM_WORLD, &bridge);
        MPI_Comm_set_errhandler(bridge, MPI_ERRORS_RETURN);
        int n = 0, me = 0;
        MPI_Comm_size(bridge, &n);
        MPI_Comm_rank(bridge, &me);

        // MPICH picks collective algorithms by thread level, so unequal levels deadlock.
        int thr = 0; MPI_Query_thread(&thr);
        int hs[4] = {MAGIC, VERSION, isFoam ? 1 : 0, thr};
        std::vector<int> all(4*n);
        check(MPI_Allgather(hs, 4, MPI_INT, all.data(), 4, MPI_INT, bridge), "handshake", bridge);
        for (int r = 0; r < n; ++r)
        {
            if (all[4*r] != MAGIC || all[4*r+1] != VERSION || all[4*r+3] != thr)
            {
                std::fprintf(stderr, "[bridge] handshake mismatch from rank %d (thread level %d vs %d)\n", r, all[4*r+3], thr);
                MPI_Abort(MPI_COMM_WORLD, 3);
            }
            const bool rFoam = all[4*r+2];
            if (rFoam && foamRoot < 0) foamRoot = r;
            if (rFoam != isFoam) peers.push_back(r);
        }
        check(MPI_Dist_graph_create_adjacent
        (
            bridge, int(peers.size()), peers.data(), MPI_UNWEIGHTED,
            int(peers.size()), peers.data(), MPI_UNWEIGHTED,
            MPI_INFO_NULL, 0, &graph
        ), "graph", bridge);
        MPI_Comm_set_errhandler(graph, MPI_ERRORS_RETURN);
    }

    // DONE handshake then free; MPI_Finalize stays with the owner.
    void close()
    {
        check(MPI_Barrier(bridge), "teardown-barrier", bridge);
        MPI_Comm_free(&graph);
        MPI_Comm_free(&bridge);
    }
};

} // namespace bridgeProto
