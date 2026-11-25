// Toy FoamSide: OpenFOAM v2606 on its own world (-mpi-split-by-appnum),
// receives "positions" from DEM over a neighbourhood graph, replies 2*x.
#include "argList.H"
#include "Pstream.H"
#include "bridgeProto.H"

using namespace Foam;

int main(int argc, char *argv[])
{
    argList::noCheckProcessorDirectories();
    argList::addOption("steps", "label", "number of exchanges");
    #include "setRootCase.H"

    const label nSteps = args.getOrDefault<label>("steps", 5);

    int worldRank = -1, worldSize = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &worldRank);
    MPI_Comm_size(MPI_COMM_WORLD, &worldSize);
    Pout<< "foam: world rank " << worldRank << "/" << worldSize
        << ", UPstream rank " << UPstream::myProcNo() << "/" << UPstream::nProcs()
        << endl;

    // OpenFOAM collectives must see only Foam ranks
    const label nFoam = returnReduce(label(1), sumOp<label>());
    Info<< "foam: returnReduce over worldComm = " << nFoam << endl;

    bridgeProto::Bridge b;
    b.open(true);
    const int nPeers = int(b.peers.size());
    Info<< "foam: bridge open, DEM peers = " << nPeers << endl;

    std::vector<int> cnt(nPeers), disp(nPeers);
    std::vector<double> buf;
    for (label step = 0; step < nSteps; ++step)
    {
        MPI_Request rq;
        std::vector<int> zero(nPeers, 0);
        bridgeProto::check(MPI_Ineighbor_alltoall(zero.data(), 1, MPI_INT, cnt.data(), 1, MPI_INT, b.graph, &rq), "counts", b.graph);
        MPI_Wait(&rq, MPI_STATUS_IGNORE);
        int tot = 0;
        for (int i = 0; i < nPeers; ++i) { disp[i] = tot; tot += cnt[i]; }
        buf.resize(tot);
        bridgeProto::check(MPI_Neighbor_alltoallv(nullptr, zero.data(), zero.data(), MPI_DOUBLE, buf.data(), cnt.data(), disp.data(), MPI_DOUBLE, b.graph), "up", b.graph);
        for (double& x : buf) x *= 2.0;      // "lambdaDot" in the same order, no ids
        bridgeProto::check(MPI_Neighbor_alltoallv(buf.data(), cnt.data(), disp.data(), MPI_DOUBLE, nullptr, zero.data(), zero.data(), MPI_DOUBLE, b.graph), "down", b.graph);

        // dt + DONE flag from Foam root to everyone on the bridge
        double ctl[2] = {1e-3*(step+1), step == nSteps-1 ? 1.0 : 0.0};
        bridgeProto::check(MPI_Bcast(ctl, 2, MPI_DOUBLE, b.foamRoot, b.bridge), "dt", b.bridge);
        const label got = returnReduce(label(tot), sumOp<label>());
        Info<< "foam: step " << step << " particles " << got << endl;
    }
    b.close();
    Info<< "End" << endl;
    return 0;   // argList dtor -> UPstream::exit -> MPI_Finalize
}
