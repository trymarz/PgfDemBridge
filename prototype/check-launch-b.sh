#!/bin/bash
# Foam sides run with -mpi-threads: the handshake needs equal thread levels (mpi4py asks MULTIPLE).
# Launch option B on three MPI stacks: Open MPI 5.0.7 (host), Open MPI 4.1 (ubuntu:24.04), MPICH (fedora:42).
set -u
cd "$(dirname "$0")"; mkdir -p bin
R=${RUNS:-$HOME/OpenFOAM/pgf-v2606-port/v2-proto-b-runs}; mkdir -p "$R"
echo "== Open MPI 5.0.7 host"
set +u; source "${FOAM_BASHRC:-$HOME/OpenFOAM/OpenFOAM-v2606/etc/bashrc}"; set -u
(cd foamSideProto && wmake >/dev/null) && mpicxx -O2 -o bin/foamMockProto-ompi5 mock/foamMockProto.C
mpirun -np 2 env | grep -E '^(OMPI_COMM_WORLD_RANK|PMIX_RANK|PMI_RANK|SLURM_PROCID)=' | sort -u
RUNS=$R LAUNCH=B REPS=10 ./matrix.sh b-ompi5-of2606 "$FOAM_USER_APPBIN/foamSideProto" -parallel -mpi-split-by-appnum -case case -mpi-threads -steps 4
RUNS=$R LAUNCH=B REPS=10 ./matrix.sh b-ompi5-mock bin/foamMockProto-ompi5 -mpi-threads -steps 4
D="docker run --rm -v $PWD:/p -v $R:/r -w /p -e RUNS=/r -e LAUNCH=B -e REPS=10 -e HUID=$(id -u) -e HGID=$(id -g)"
echo "== Open MPI 4.1 in ubuntu:24.04"
$D -e OMPI_ALLOW_RUN_AS_ROOT=1 -e OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 ubuntu:24.04 bash -c '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq && apt-get install -y -qq --no-install-recommends openmpi-bin libopenmpi-dev python3-mpi4py python3-numpy g++ >/dev/null
  mpirun --version | head -1
  mpirun --oversubscribe -np 2 env | grep -E "^(OMPI_COMM_WORLD_RANK|PMIX_RANK|PMI_RANK|SLURM_PROCID)=" | sort -u
  mpicxx -O2 -o bin/foamMockProto-ompi41 mock/foamMockProto.C 2>/dev/null
  ./matrix.sh b-ompi41-mock bin/foamMockProto-ompi41 -mpi-threads -steps 4
  chown -R $HUID:$HGID /r bin'
echo "== MPICH in fedora:42"
$D -e OVERSUB= fedora:42 bash -c '
  dnf install -y -q mpich mpich-devel python3-mpi4py-mpich python3-numpy gcc-c++ >/dev/null
  export PATH=/usr/lib64/mpich/bin:$PATH LD_LIBRARY_PATH=/usr/lib64/mpich/lib
  export PYTHONPATH=$(echo /usr/lib64/python3*/site-packages/mpich)
  mpichversion | head -1
  mpirun -np 2 env | grep -E "^(OMPI_COMM_WORLD_RANK|PMIX_RANK|PMI_RANK|SLURM_PROCID)=" | sort -u
  mpicxx -O2 -o bin/foamMockProto-mpich mock/foamMockProto.C
  ./matrix.sh b-mpich-mock bin/foamMockProto-mpich -mpi-threads -steps 4
  echo "-- unequal thread levels (no -mpi-threads) must abort in the handshake, not hang:"
  timeout 30 mpirun -np 2 env NDEM=1 ./role-single-app.sh bin/foamMockProto-mpich -steps 4 2>&1 | grep -m2 -E "mismatch|EXIT CODE"; echo "rc=${PIPESTATUS[0]}"
  chown -R $HUID:$HGID /r bin'
echo "== summaries"; cat "$R"/b-*-summary.txt
