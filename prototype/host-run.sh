#!/bin/bash
# Host runs for the prototype: Open MPI 5.0.7 (Debian 13 host) and 4.1 (ubuntu:24.04 docker).
set -u
cd "$(dirname "$0")"; mkdir -p bin
echo "== smoke: plain MPMD"; timeout 30 mpirun -np 1 hostname : -np 1 hostname; echo "rc=$?"
set +u; source "${FOAM_BASHRC:-$HOME/OpenFOAM/OpenFOAM-v2606/etc/bashrc}"; set -u
(cd foamSideProto && wmake)
mpicxx -O2 -o bin/foamMockProto-ompi5 mock/foamMockProto.C
REPS=10 ./matrix.sh ompi5-of2606 "$FOAM_USER_APPBIN/foamSideProto" -parallel -mpi-split-by-appnum -case case -mpi-threads -steps 4
REPS=10 ./matrix.sh ompi5-mock bin/foamMockProto-ompi5 -mpi-threads -steps 4
echo "== Open MPI 5.0.7 with the PMIx workaround (launch option A)"
PMIX_MCA_pmdl=^ompi REPS=10 ./matrix.sh ompi5w-of2606 "$FOAM_USER_APPBIN/foamSideProto" -parallel -mpi-split-by-appnum -case case -mpi-threads -steps 4
PMIX_MCA_pmdl=^ompi REPS=10 ./matrix.sh ompi5w-mock bin/foamMockProto-ompi5 -mpi-threads -steps 4
echo "== Open MPI 4.1 in ubuntu:24.04"
docker run --rm -v "$PWD":/p -w /p -e HUID=$(id -u) -e HGID=$(id -g) \
  -e OMPI_ALLOW_RUN_AS_ROOT=1 -e OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 ubuntu:24.04 bash -c '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq && apt-get install -y -qq --no-install-recommends openmpi-bin libopenmpi-dev python3-mpi4py python3-numpy g++ >/dev/null
  mpirun --version | head -1
  mpicxx -O2 -o bin/foamMockProto-ompi41 mock/foamMockProto.C 2>/dev/null
  REPS=10 ./matrix.sh ompi41-mock bin/foamMockProto-ompi41 -mpi-threads -steps 4
  chown -R $HUID:$HGID runs bin'
echo "== summaries"; cat runs/*-summary.txt
