// Geometry and values shared by the mock sides, so each side can check the other.
// Foam rank f owns the slab x in [f, f+1], y and z in [0, 1].
#pragma once

#include "PgfDemBridge.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace mock
{

using PgfDemBridge::LambdaMode;

inline PgfDemBridge::Box slab(int f) { return {{double(f), 0, 0}, {double(f + 1), 1, 1}}; }

//- Does Foam rank f (of nF) find a body at p? Half-open slabs, or closed ones (-closed).
inline bool finds(int f, int nF, const double* p, bool closed)
{
    if (p[1] < 0 || p[1] > 1 || p[2] < 0 || p[2] > 1) return false;
    if (closed || f == nF - 1) return p[0] >= f && p[0] <= f + 1;
    return p[0] >= f && p[0] < f + 1;
}

//- Foam's answer for a record; it encodes the finding rank and the record's fields.
inline double value(int f, const double* rec, LambdaMode m)
{
    return 1000.0*(f + 1) + rec[0] + 10.0*rec[3] + 100.0*rec[6] + (m == LambdaMode::rate ? 0.0 : 0.5);
}

//- Body i of DEM rank d at step s: a window that drifts in x, so the graph must grow.
inline void body(int d, int s, int i, int nF, double* rec)
{
    unsigned u = 2654435761u*unsigned(1 + d*7919 + s*104729 + i*31);
    auto rnd = [&u]() { u = u*1664525u + 1013904223u; return (u >> 8)*(1.0/16777216.0); };
    const double centre = std::fmod(d + 0.7*s, nF + 1.0) - 0.5;
    rec[0] = i == 0 ? std::round(centre) : centre + 1.2*(rnd() - 0.5);   // i == 0 on a slab face
    rec[1] = -0.1 + 1.2*rnd();
    rec[2] = -0.1 + 1.2*rnd();
    for (int k = 0; k < 3; ++k) rec[3 + k] = 2.0*rec[k];
    rec[6] = rec[0] + rec[1];
}

//- Bodies on DEM rank d at step s; it changes every step (erased and inserted bodies).
inline int nBodies(int d, int s) { return d == 0 && s == 1 ? 0 : 20 + 3*d - (s % 4); }

inline const char* opt(int argc, char** argv, const char* name, const char* dflt)
{
    for (int i = 1; i + 1 < argc; ++i) if (!std::strcmp(argv[i], name)) return argv[i + 1];
    return dflt;
}

inline bool flag(int argc, char** argv, const char* name)
{
    for (int i = 1; i < argc; ++i) if (!std::strcmp(argv[i], name)) return true;
    return false;
}

} // namespace mock
