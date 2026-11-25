/*---------------------------------------------------------------------------*\
PgfDemBridge: coupling protocol v2 between porousGasificationFoam (OpenFOAM)
and Yade DEM (yade-pgf). Plain C++17 and MPI-3.1; no OpenFOAM or Yade headers.

Launch. One mpirun starts every rank of both programs. Each program first
splits the world communicator into its own group (OpenFOAM with
-mpi-split-by-appnum, Yade when yade.mpy is imported), then constructs its
side here. Both sides must initialise MPI at the same thread level: MPICH
picks collective algorithms by thread level, so unequal levels deadlock.

Collectives on the bridge (a dup of world), in this order:
  open     Allgather int[8] {magic, version, side, thread level, lambdaMode,
           strideUp, strideDown, 0}; the layout never changes, so a version
           mismatch is reported, not misread. Then Allgather of each Foam
           rank's mesh box (double[6]).
  step     Allreduce(MAX) int[2] {rebuild, stop}. On rebuild, a new
           neighbour graph from the DEM ranks' edges (MPI_Dist_graph_create).
           Then on the graph: counts (Neighbor_alltoall), bodies up and
           values down (Neighbor_alltoallv, blocking), and on the bridge a
           Bcast of {dt, done} from Foam's first rank.
  close    if not done: the step header with stop = 1; then Barrier, free.

Layout. Up, per body: pos[3], vel[3], lambda (PGF's lambda field and the
apparentVolume regression read it in both modes). Down, one
double per body in the same order (lambdaDot, or the apparentVolume factor
r); NaN where the Foam rank did not find the body. No ids cross: a DEM rank
sends each body to every Foam rank whose mesh box holds its centre, and takes
the value of the first such Foam rank (lowest rank) that found it.

Errors. Every MPI call returns its code (MPI_ERRORS_RETURN); a failure is
reported with the world rank and stage, then the run is aborted on world, so
a fatal error on either side ends both programs instead of hanging.
\*---------------------------------------------------------------------------*/
#pragma once

#include <mpi.h>
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace PgfDemBridge
{

constexpr int magic = 0x50474644;   // "PGFD"
constexpr int protocolVersion = 2;

enum class LambdaMode : int { rate = 0, apparentVolume = 1 };

//- Doubles per body sent up: pos[3], vel[3], lambda.
constexpr int strideUp = 7;

//- Doubles per body sent down: one value, NaN if not found.
constexpr int strideDown = 1;

//- Axis-aligned box; contains() uses v1's relative tolerance (PgfCoupling::inBox).
struct Box
{
    std::array<double, 3> lo, hi;
    bool contains(const double* p) const;
};

//- What a step returns: Foam's dt, and whether this was the last exchange.
struct Control
{
    double dt = 0;
    bool done = false;      // Foam ended the run, or a side stopped early
    bool stopped = false;   // a side called close() before done
};

//- Called with a message on a fatal error; the run is aborted when it returns or throws.
//  It must not exit or call MPI_Finalize: other ranks may be waiting in a collective.
using ErrorHandler = std::function<void(const std::string&)>;


//- Shared part of both sides: handshake, graph, step header, teardown.
class Bridge
{
public:
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;
    virtual ~Bridge();

    LambdaMode lambdaMode() const { return mode_; }
    int nFoam() const { return int(foamRanks_.size()); }
    int nDem() const { return nRanks_ - nFoam(); }
    const std::vector<Box>& foamBoxes() const { return foamBoxes_; }
    bool done() const { return done_; }

    //- Collective on world: stops the peer if not done, then frees the bridge.
    void close();

protected:
    Bridge(MPI_Comm world, bool isFoam, int lambdaMode, const Box* box, ErrorHandler onError);

    [[noreturn]] void fail(const std::string& msg) const;
    void check(int rc, const char* stage) const;

    //- Step header; returns true if a side stopped (then done_ is set).
    bool stepHeader(bool wantRebuild, bool stop);
    void rebuildGraph(const std::vector<int>& foamPeers);
    Control control(double dt, bool done);

    MPI_Comm bridge_ = MPI_COMM_NULL;
    MPI_Comm graph_ = MPI_COMM_NULL;
    std::vector<int> in_, out_;          // graph neighbours (bridge ranks)
    std::vector<int> foamRanks_;         // bridge ranks of the Foam side
    std::vector<Box> foamBoxes_;         // indexed like foamRanks_
    int nRanks_ = 0, me_ = 0;
    LambdaMode mode_ = LambdaMode::rate;
    bool done_ = false;
    bool rebuild_ = false;               // header saw a rebuild request
    bool midStep_ = false;               // Foam: between receive() and reply()

private:
    void handshake(bool isFoam, int lambdaMode, const Box* box);
    ErrorHandler onError_;
    MPI_Comm world_;
};


//- The OpenFOAM side: receive bodies, answer with one value each.
class FoamSide : public Bridge
{
public:
    //- Collective on world. box: this rank's mesh points' bounds.
    FoamSide(LambdaMode mode, const Box& box, MPI_Comm world = MPI_COMM_WORLD, ErrorHandler onError = {});

    //- Step, part 1 (collective): strideUp doubles per body. Empty and
    //  stopped() true if the DEM side stopped; then skip reply().
    const std::vector<double>& receive();
    std::size_t nBodies() const { return up_.size()/strideUp; }
    bool stopped() const { return stopped_; }

    //- Step, part 2 (collective): one value per received body, then dt and done.
    void reply(const double* values, double dt, bool done);

private:
    std::vector<double> up_;
    std::vector<int> inCount_, inDispl_;   // bodies per in-neighbour
    bool stopped_ = false;
};


//- The DEM side: send bodies, get one value each.
class DemSide : public Bridge
{
public:
    //- Collective on world. expectLambdaMode: 0 or 1 to insist on a mode, -1 for any.
    explicit DemSide(MPI_Comm world = MPI_COMM_WORLD, int expectLambdaMode = -1, ErrorHandler onError = {});

    //- One exchange (collective). up: n bodies, strideUp doubles each;
    //  values: n doubles out, NaN where no Foam rank found the body.
    Control exchange(const double* up, std::size_t n, double* values);

    //- Bodies of the last exchange that no Foam rank found (this rank).
    int nLost() const { return nLost_; }

private:
    int nLost_ = 0;
};

} // namespace PgfDemBridge
