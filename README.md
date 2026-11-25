# PgfDemBridge

Coupling bridge between OpenFOAM ([porousGasificationFoam](https://github.com/trymarz/porousGasificationFoam))
and Yade DEM ([yade-pgf](https://github.com/trymarz/yade-pgf)) over MPI: coupling protocol v2.

The plan and the design decisions are tracked in
[porousGasificationFoam#328](https://github.com/trymarz/porousGasificationFoam/issues/328).
The core will be plain C++17 and MPI, used by both sides:
`PgfDemBridge::FoamSide` (an OpenFOAM library in PGF) and `PgfDemBridge::DemSide` (a Yade engine in yade-pgf).

**Status:** only the prototype exists so far. There is no core yet.

## How v2 works

![Groups](prototype/doc/1-groups.png)

- **Launch.** One `mpirun` starts every Yade process and every OpenFOAM process at the same time. Nothing is spawned later.
- **Private groups.** Each program splits `MPI_COMM_WORLD` into its own private group:
  - OpenFOAM v2606 does this with `-mpi-split-by-appnum`;
  - Yade does it when `yade.mpy` is imported.
- **Bridge.** A copy of `MPI_COMM_WORLD`, used only for coupling data.
- **Handshake.** The first exchange on the bridge checks a magic number, the protocol version and each process's side.
- **Neighbour graph.** Says which DEM process exchanges data with which OpenFOAM process.

![One step](prototype/doc/2-step.png)

- **Each coupling step** is a fixed sequence of collectives, operations that every process joins:
  1. particle counts;
  2. particle data, sent to the OpenFOAM side;
  3. results, sent back in the same order;
  4. `dt` and a DONE flag.
- **Shutdown:** barrier, free the bridge, then each program closes MPI normally. There is no disconnect.
- **Failure:** a fatal error on either side calls `MPI_Abort(MPI_COMM_WORLD)`, which ends both programs.
- **Same MPI thread level on both sides.** MPICH picks its collective algorithms by thread level, so a Foam rank in `MPI_THREAD_SINGLE` and a DEM rank in `MPI_THREAD_MULTIPLE` deadlock in the first barrier. mpi4py (and so Yade) asks for `MPI_THREAD_MULTIPLE`, so OpenFOAM runs with `-mpi-threads`. The handshake compares the levels and aborts on a mismatch instead of hanging.

![Launch](prototype/doc/3-launch.png)

**Launch option B was chosen.** `mpirun -np N` runs one small start-up script. Each copy reads its rank from the launcher's environment and `exec`s Yade or OpenFOAM.

This avoids a crash on Debian 13: there, Open MPI 5.0.7 segfaults (in PMIx `PMIx_Setenv`) on any `mpirun A : B` command line, i.e. launch option A. The script finds the rank in the first of these variables that is set:

| Launcher | Variable |
|---|---|
| Open MPI 4.x, 5.x | `OMPI_COMM_WORLD_RANK` |
| PMIx-based launchers | `PMIX_RANK` |
| MPICH, Intel MPI (Hydra) | `PMI_RANK` |
| Slurm `srun` | `SLURM_PROCID` |

## Decisions (porousGasificationFoam#328)

| # | Question | Decision |
|---|---|---|
| D1 | Launch, given the Open MPI 5.0.7 MPMD crash | Option B: one `mpirun -np N role-single-app.sh <foam-cmd>`; each copy reads its rank and `exec`s Yade or OpenFOAM |
| D2 | Where the code lives | This repository |
| D3 | Collective variant | Blocking neighbourhood collectives only, MPI-3.1. No non-blocking, no MPI-4 persistent, no build-time switch. Changing the variant later bumps the protocol version |
| D4 | One Foam rank | A one-piece decomposition (`processor0/`); no serial path |
| D5 | Remaining prototype checks | All four run (below); no upstream Open MPI/PMIx report for now |

## Prototype (`prototype/`)

Throw-away code that answered the first box of #328. It will seed the core's mock sides and stress tests.

| Path | Role |
|---|---|
| `common/bridgeProto.H` | Toy core: handshake, graph, teardown |
| `foamSideProto/` | Toy OpenFOAM v2606 app (`wmake`; built to `$FOAM_USER_APPBIN`) |
| `mock/foamMockProto.C` | The same OpenFOAM side without OpenFOAM (`mpicxx`) |
| `demSideProto.py` | mpi4py DEM side; it does the split that `yade.mpy` does |
| `matrix.sh` | Runs layouts 1x1, 2x2, 1x3, 3x1 (DEM x Foam), `REPS` times each, with a 30 s timeout |
| `host-run.sh` | Launch option A runs; Open MPI 4.1 runs in an `ubuntu:24.04` container |
| `role-single-app.sh` | Launch option B; `DEMCMD` replaces the DEM command (e.g. real Yade) |
| `check-launch-b.sh` | Option B on Open MPI 5.0.7 (host), Open MPI 4.1 (`ubuntu:24.04`) and MPICH 4.2.2 (`fedora:42`) |
| `check-real-sides.sh` | Option B with real Yade (`YADE_BIN`), and the real v2606 side on Open MPI 4.1 (OpenFOAM's `ubuntu:24.04` packages) |
| `results/` | Run summaries |

Each layout (1x1, 2x2, 1x3, 3x1) runs 10 times; a run passes only if both sides print their end line and `mpirun` returns 0 within 30 s. Summaries are in `results/`.

**Launch option B** (with the thread-level check; Foam runs with `-mpi-threads`):

| MPI | DEM side | Foam side | Result |
|---|---|---|---|
| Open MPI 5.0.7, Debian 13 | mpi4py | v2606 | 40/40 clean |
| Open MPI 5.0.7, Debian 13 | mpi4py | mock | 40/40 clean |
| Open MPI 5.0.7, Debian 13 | **Yade** (yade-pgf 475bb35, `import yade.mpy`, `O.step()` per exchange) | v2606 | 40/40 clean |
| Open MPI 4.1.6, `ubuntu:24.04` | mpi4py | mock | 40/40 clean |
| Open MPI 4.1.6, `ubuntu:24.04` | mpi4py | **v2606** (OpenFOAM packages) | 40/40 clean |
| MPICH 4.2.2, `fedora:42` | mpi4py | mock | 40/40 clean |
| MPICH 4.2.2, `fedora:42` | mpi4py | mock without `-mpi-threads` | handshake abort, rc 3 (no hang) |

**Launch option A** (`mpirun A : B`, before the thread-level check):

| MPI | Foam side | Result |
|---|---|---|
| Open MPI 5.0.7, Debian 13 | v2606 or mock | `mpirun` segfaults before any process starts |
| Open MPI 5.0.7 + `PMIX_MCA_pmdl=^ompi` | v2606 | 40/40 clean |
| Open MPI 5.0.7 + `PMIX_MCA_pmdl=^ompi` | mock | 40/40 clean |
| Open MPI 4.1.6, `ubuntu:24.04` | mock | 40/40 clean |

Not run yet:
- more than one node;
- the `DemSide` engine in C++ (the Yade check runs the Python toy inside Yade);
- the `pgf pack` image.
