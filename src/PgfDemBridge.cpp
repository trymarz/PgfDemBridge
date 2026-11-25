#include "PgfDemBridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace PgfDemBridge
{

namespace
{

constexpr double nan = std::numeric_limits<double>::quiet_NaN();
enum Field { fMagic, fVersion, fSide, fThread, fMode, fUp, fDown, fSpare, nFields };

const char* threadName(int t)
{
    switch (t)
    {
        case MPI_THREAD_SINGLE: return "SINGLE";
        case MPI_THREAD_FUNNELED: return "FUNNELED";
        case MPI_THREAD_SERIALIZED: return "SERIALIZED";
        case MPI_THREAD_MULTIPLE: return "MULTIPLE";
    }
    return "unknown";
}

int indexOf(const std::vector<int>& v, int x)
{
    const auto it = std::find(v.begin(), v.end(), x);
    return it == v.end() ? -1 : int(it - v.begin());
}

} // namespace


bool Box::contains(const double* p) const
{
    for (int k = 0; k < 3; ++k)
    {
        const double tol = 1e-9*std::max(1.0, std::abs(hi[k] - lo[k]));
        if (!(p[k] >= lo[k] - tol && p[k] <= hi[k] + tol)) return false;
    }
    return true;
}


// * * * * * * * * * * * * * * * * Bridge  * * * * * * * * * * * * * * * * //

Bridge::Bridge(MPI_Comm world, bool isFoam, int lambdaMode, const Box* box, ErrorHandler onError)
:
    onError_(std::move(onError)),
    world_(world)
{
    check(MPI_Comm_dup(world, &bridge_), "open (dup)");
    MPI_Comm_set_errhandler(bridge_, MPI_ERRORS_RETURN);
    MPI_Comm_size(bridge_, &nRanks_);
    MPI_Comm_rank(bridge_, &me_);
    handshake(isFoam, lambdaMode, box);
}


Bridge::~Bridge()
{
    // An unclosed bridge would leave the peer waiting forever, so end the run.
    if (bridge_ != MPI_COMM_NULL) fail("bridge destroyed without close()");
}


void Bridge::fail(const std::string& msg) const
{
    int r = -1;
    MPI_Comm_rank(world_, &r);
    const std::string full = "[PgfDemBridge] world rank " + std::to_string(r) + ": " + msg;
    if (!onError_) std::fprintf(stderr, "%s\n", full.c_str());
    else try { onError_(full); } catch (...) {}   // a throwing handler must still end the run
    MPI_Abort(world_, 3);
    std::abort();
}


void Bridge::check(int rc, const char* stage) const
{
    if (rc == MPI_SUCCESS) return;
    char err[MPI_MAX_ERROR_STRING];
    int len = 0;
    MPI_Error_string(rc, err, &len);
    fail(std::string("MPI error in ") + stage + ": " + std::string(err, len));
}


void Bridge::handshake(bool isFoam, int lambdaMode, const Box* box)
{
    int thread = MPI_THREAD_SINGLE;
    MPI_Query_thread(&thread);
    const int mine[nFields] =
    {
        magic, protocolVersion, isFoam ? 1 : 0, thread, lambdaMode,
        strideUp, strideDown, 0
    };
    std::vector<int> all(nFields*nRanks_);
    check(MPI_Allgather(mine, nFields, MPI_INT, all.data(), nFields, MPI_INT, bridge_), "handshake");

    // Every rank reaches the same verdict; bridge rank 0 reports it.
    std::ostringstream why;
    int foamMode = -1, nDemRanks = 0;
    for (int r = 0; r < nRanks_ && why.str().empty(); ++r)
    {
        const int* h = &all[nFields*r];
        if (h[fMagic] != magic)
        {
            why << "bridge rank " << r << " is not a PgfDemBridge peer (bad magic)";
        }
        else if (h[fVersion] != protocolVersion)
        {
            why << "protocol version mismatch: bridge rank " << r << " has v" << h[fVersion]
                << ", bridge rank " << me_ << " v" << protocolVersion;
        }
        else if (h[fThread] != thread)
        {
            why << "MPI thread level mismatch: bridge rank " << r << " " << threadName(h[fThread])
                << ", bridge rank " << me_ << " " << threadName(thread)
                << ". Both sides must init MPI at the same level (run OpenFOAM with -mpi-threads)";
        }
        else if (h[fUp] != strideUp || h[fDown] != strideDown)
        {
            why << "per-body stride mismatch on bridge rank " << r;
        }
        else if (h[fSide] == 1)
        {
            if (h[fMode] != 0 && h[fMode] != 1)
                why << "unknown lambdaMode " << h[fMode] << " on Foam bridge rank " << r;
            else if (foamMode >= 0 && h[fMode] != foamMode)
                why << "Foam ranks disagree on lambdaMode (" << foamMode << " vs " << h[fMode] << ")";
            foamMode = h[fMode];
            foamRanks_.push_back(r);
        }
        else
        {
            ++nDemRanks;
        }
    }
    if (why.str().empty())
    {
        if (foamRanks_.empty() || nDemRanks == 0)
            why << "the bridge needs at least one Foam and one DEM rank ("
                << foamRanks_.size() << " Foam, " << nDemRanks << " DEM)";
        for (int r = 0; r < nRanks_ && why.str().empty(); ++r)
        {
            const int* h = &all[nFields*r];
            if (h[fSide] == 0 && h[fMode] >= 0 && h[fMode] != foamMode)
                why << "lambdaMode mismatch: OpenFOAM uses " << foamMode
                    << ", DEM bridge rank " << r << " expects " << h[fMode];
        }
    }
    if (!why.str().empty())
    {
        if (me_ != 0) MPI_Barrier(bridge_);   // never returns: rank 0 aborts
        fail(why.str());
    }
    mode_ = LambdaMode(foamMode);

    std::array<double, 6> b{nan, nan, nan, nan, nan, nan};
    if (isFoam) b = {box->lo[0], box->lo[1], box->lo[2], box->hi[0], box->hi[1], box->hi[2]};
    std::vector<double> boxes(6*nRanks_);
    check(MPI_Allgather(b.data(), 6, MPI_DOUBLE, boxes.data(), 6, MPI_DOUBLE, bridge_), "handshake (boxes)");
    for (const int r : foamRanks_)
    {
        const double* p = &boxes[6*r];
        foamBoxes_.push_back(Box{{p[0], p[1], p[2]}, {p[3], p[4], p[5]}});
    }
}


bool Bridge::stepHeader(bool wantRebuild, bool stop)
{
    int h[2] = {wantRebuild ? 1 : 0, stop ? 1 : 0};
    check(MPI_Allreduce(MPI_IN_PLACE, h, 2, MPI_INT, MPI_MAX, bridge_), "step header");
    rebuild_ = h[0] != 0;
    if (h[1]) done_ = true;
    return h[1] != 0;
}


void Bridge::rebuildGraph(const std::vector<int>& foamPeers)
{
    if (graph_ != MPI_COMM_NULL) check(MPI_Comm_free(&graph_), "graph free");

    // Each DEM rank declares both directions of its own edges; Foam ranks declare none.
    const int k = int(foamPeers.size());
    std::vector<int> src{me_}, deg{k}, dst(foamPeers);
    for (const int f : foamPeers)
    {
        src.push_back(f);
        deg.push_back(1);
        dst.push_back(me_);
    }
    const int n = k ? k + 1 : 0;
    check
    (
        MPI_Dist_graph_create
        (
            bridge_, n, src.data(), deg.data(), dst.data(),
            MPI_UNWEIGHTED, MPI_INFO_NULL, 0, &graph_
        ),
        "graph create"
    );
    MPI_Comm_set_errhandler(graph_, MPI_ERRORS_RETURN);

    int nIn = 0, nOut = 0, weighted = 0;
    check(MPI_Dist_graph_neighbors_count(graph_, &nIn, &nOut, &weighted), "graph neighbours");
    in_.assign(nIn, -1);
    out_.assign(nOut, -1);
    check
    (
        MPI_Dist_graph_neighbors
        (
            graph_, nIn, in_.data(), MPI_UNWEIGHTED, nOut, out_.data(), MPI_UNWEIGHTED
        ),
        "graph neighbours"
    );
}


Control Bridge::control(double dt, bool done)
{
    double c[2] = {dt, done ? 1.0 : 0.0};
    check(MPI_Bcast(c, 2, MPI_DOUBLE, foamRanks_.front(), bridge_), "control");
    done_ = c[1] > 0.5;
    return Control{c[0], done_, false};
}


void Bridge::close()
{
    if (bridge_ == MPI_COMM_NULL) return;
    if (midStep_) fail("close() between receive() and reply()");
    if (!done_) stepHeader(false, true);
    check(MPI_Barrier(bridge_), "close");
    if (graph_ != MPI_COMM_NULL) MPI_Comm_free(&graph_);
    MPI_Comm_free(&bridge_);
    bridge_ = MPI_COMM_NULL;
}


// * * * * * * * * * * * * * * * * FoamSide  * * * * * * * * * * * * * * * //

FoamSide::FoamSide(LambdaMode mode, const Box& box, MPI_Comm world, ErrorHandler onError)
:
    Bridge(world, true, int(mode), &box, std::move(onError))
{}


const std::vector<double>& FoamSide::receive()
{
    if (midStep_) fail("receive() called twice without reply()");
    if (done_) fail("receive() after the last exchange");
    if (stepHeader(graph_ == MPI_COMM_NULL, false))
    {
        stopped_ = true;
        up_.clear();
        return up_;
    }
    if (rebuild_) rebuildGraph({});

    const int su = strideUp;
    std::vector<int> zero(std::max(in_.size(), out_.size()), 0);
    inCount_.assign(in_.size(), 0);
    check(MPI_Neighbor_alltoall(zero.data(), 1, MPI_INT, inCount_.data(), 1, MPI_INT, graph_), "counts");

    std::vector<int> rc(in_.size()), rd(in_.size());
    inDispl_.assign(in_.size(), 0);
    int total = 0;
    for (std::size_t j = 0; j < in_.size(); ++j)
    {
        inDispl_[j] = total;
        rc[j] = su*inCount_[j];
        rd[j] = su*total;
        total += inCount_[j];
    }
    up_.resize(std::size_t(su)*total);
    check
    (
        MPI_Neighbor_alltoallv
        (
            nullptr, zero.data(), zero.data(), MPI_DOUBLE,
            up_.data(), rc.data(), rd.data(), MPI_DOUBLE, graph_
        ),
        "bodies up"
    );
    midStep_ = true;
    return up_;
}


void FoamSide::reply(const double* values, double dt, bool done)
{
    if (!midStep_) fail("reply() without receive()");

    // The graph is symmetric: each out-neighbour gets back the block it sent.
    std::vector<int> sc(out_.size(), 0), sd(out_.size(), 0);
    for (std::size_t k = 0; k < out_.size(); ++k)
    {
        const int j = indexOf(in_, out_[k]);
        if (j >= 0) { sc[k] = inCount_[j]; sd[k] = inDispl_[j]; }
    }
    std::vector<int> zero(in_.size(), 0);
    check
    (
        MPI_Neighbor_alltoallv
        (
            values, sc.data(), sd.data(), MPI_DOUBLE,
            nullptr, zero.data(), zero.data(), MPI_DOUBLE, graph_
        ),
        "values down"
    );
    midStep_ = false;
    control(dt, done);
}


// * * * * * * * * * * * * * * * * DemSide * * * * * * * * * * * * * * * * //

DemSide::DemSide(MPI_Comm world, int expectLambdaMode, ErrorHandler onError)
:
    Bridge(world, false, expectLambdaMode, nullptr, std::move(onError))
{}


Control DemSide::exchange(const double* up, std::size_t n, double* values)
{
    if (done_) fail("exchange() after the last exchange");
    const int su = strideUp;
    const int nF = nFoam();

    // Which Foam ranks' boxes hold each body (bodies per Foam index).
    std::vector<std::vector<int>> byFoam(nF);
    for (std::size_t b = 0; b < n; ++b)
        for (int f = 0; f < nF; ++f)
            if (foamBoxes_[f].contains(up + su*b)) byFoam[f].push_back(int(b));

    std::vector<int> peers(out_);
    bool want = graph_ == MPI_COMM_NULL;
    for (int f = 0; f < nF; ++f)
    {
        if (!byFoam[f].empty() && indexOf(out_, foamRanks_[f]) < 0)
        {
            want = true;
            peers.push_back(foamRanks_[f]);
        }
    }
    std::fill(values, values + n, nan);
    if (stepHeader(want, false))
    {
        nLost_ = int(n);
        return Control{0, true, true};
    }
    if (rebuild_)
    {
        std::sort(peers.begin(), peers.end());   // kept peers stay: the graph only grows
        rebuildGraph(peers);
    }

    // Records in out_ order; each Foam rank gets the bodies in its box.
    const std::size_t nOut = out_.size();
    std::vector<int> cnt(nOut, 0), disp(nOut, 0), recBody;
    std::vector<double> buf;
    for (std::size_t k = 0; k < nOut; ++k)
    {
        disp[k] = int(recBody.size());
        const int f = indexOf(foamRanks_, out_[k]);
        for (const int b : byFoam[f])
        {
            recBody.push_back(b);
            buf.insert(buf.end(), up + su*b, up + su*(b + 1));
        }
        cnt[k] = int(recBody.size()) - disp[k];
    }
    std::vector<int> dummy(in_.size(), 0);
    check(MPI_Neighbor_alltoall(cnt.data(), 1, MPI_INT, dummy.data(), 1, MPI_INT, graph_), "counts");

    std::vector<int> sc(nOut), sd(nOut);
    for (std::size_t k = 0; k < nOut; ++k) { sc[k] = su*cnt[k]; sd[k] = su*disp[k]; }
    std::vector<int> zero(std::max(in_.size(), nOut), 0);
    check
    (
        MPI_Neighbor_alltoallv
        (
            buf.data(), sc.data(), sd.data(), MPI_DOUBLE,
            nullptr, zero.data(), zero.data(), MPI_DOUBLE, graph_
        ),
        "bodies up"
    );

    // Values come back into the slots of the records they answer.
    std::vector<double> back(recBody.size(), nan);
    std::vector<int> rc(in_.size(), 0), rd(in_.size(), 0);
    for (std::size_t j = 0; j < in_.size(); ++j)
    {
        const int k = indexOf(out_, in_[j]);
        if (k >= 0) { rc[j] = cnt[k]; rd[j] = disp[k]; }
    }
    check
    (
        MPI_Neighbor_alltoallv
        (
            nullptr, zero.data(), zero.data(), MPI_DOUBLE,
            back.data(), rc.data(), rd.data(), MPI_DOUBLE, graph_
        ),
        "values down"
    );

    // The lowest Foam rank that found a body sets its value (v1 rule, #229).
    for (const int fr : foamRanks_)
    {
        const int k = indexOf(out_, fr);
        if (k < 0) continue;
        for (int i = disp[k]; i < disp[k] + cnt[k]; ++i)
        {
            double& v = values[recBody[i]];
            if (std::isnan(v) && !std::isnan(back[i])) v = back[i];
        }
    }
    nLost_ = int(std::count_if(values, values + n, [](double v) { return std::isnan(v); }));
    return control(0, false);
}

} // namespace PgfDemBridge
