#!/usr/bin/env python3
"""Toy DemSide: does what yade.mpy.configure() does at import, then the bridge."""
import sys
import numpy as np
from mpi4py import MPI

MAGIC, VERSION = 0x50474644, 2
world = MPI.COMM_WORLD
comm = world.Split(3, 0)          # verbatim from yade-pgf py/mpy.py:211
assert comm.Get_parent() == MPI.COMM_NULL
r, n = comm.Get_rank(), comm.Get_size()
print(f"dem: world rank {world.Get_rank()}/{world.Get_size()}, sub rank {r}/{n}, appnum {world.Get_attr(MPI.APPNUM)}", flush=True)

import os
D=os.environ.get("BDEBUG")
def dbg(m):
    if D: print("dem dbg", world.Get_rank(), m, flush=True)
dbg("pre-dup")
bridge = world.Dup()
dbg("dup")
bridge.Set_errhandler(MPI.ERRORS_RETURN)
hs = np.array([MAGIC, VERSION, 0], dtype=np.intc)
allhs = np.empty(3 * bridge.Get_size(), dtype=np.intc)
bridge.Allgather(hs, allhs)
allhs = allhs.reshape(-1, 3)
dbg("handshake")
assert (allhs[:, 0] == MAGIC).all() and (allhs[:, 1] == VERSION).all()
isFoam = allhs[:, 2] == 1
foamRoot = int(np.argmax(isFoam))
peers = [int(i) for i in np.nonzero(isFoam)[0]]
graph = bridge.Create_dist_graph_adjacent(peers, peers)
graph.Set_errhandler(MPI.ERRORS_RETURN)
dbg("graph")

step, erased, done = 0, 0, False
me = world.Get_rank()
while not done:
    # particle count per Foam peer; drop one particle at step 2 (erased body)
    cnt = np.array([3 + me + i - (1 if step >= 2 else 0) for i in range(len(peers))], dtype=np.intc)
    recv0 = np.zeros(len(peers), dtype=np.intc)
    graph.Ineighbor_alltoall(cnt, recv0).Wait()  # must match the C side: blocking never matches non-blocking
    dbg("counts")
    disp = np.concatenate(([0], np.cumsum(cnt)[:-1])).astype(np.intc)
    pos = np.arange(cnt.sum(), dtype=np.float64) + 100.0 * me + step
    z = np.zeros(len(peers), dtype=np.intc)
    graph.Neighbor_alltoallv([pos, (cnt, disp), MPI.DOUBLE], [np.empty(0), (z, z), MPI.DOUBLE])
    back = np.empty_like(pos)
    graph.Neighbor_alltoallv([np.empty(0), (z, z), MPI.DOUBLE], [back, (cnt, disp), MPI.DOUBLE])
    assert np.array_equal(back, 2.0 * pos), "reply order mismatch"
    ctl = np.empty(2)
    bridge.Bcast(ctl, root=foamRoot)
    tot = comm.allreduce(int(cnt.sum()))
    if r == 0:
        print(f"dem: step {step} dt {ctl[0]:g} particles {tot} ok", flush=True)
    done = ctl[1] > 0.5
    step += 1

bridge.Barrier()
graph.Free()
bridge.Free()
if r == 0:
    print("dem: RUN FINISH", flush=True)
# mpi4py finalizes at exit
