#!/bin/bash
# Checks 2 and 4 of #328: the real v2606 side on Open MPI 4.1 (OpenFOAM's ubuntu:24.04 packages),
# and real Yade (yade-pgf, YADE_BIN) as the DEM side on the host. Both use launch option B.
set -u
cd "$(dirname "$0")"; mkdir -p bin
R=${RUNS:-$HOME/OpenFOAM/pgf-v2606-port/v2-proto-b-runs}; mkdir -p "$R"
FOAMCMD=(-parallel -mpi-split-by-appnum -case case -mpi-threads -steps 4)

echo "== check 4: real Yade ($YADE_BIN) on the host, Open MPI 5"
set +u; source "${FOAM_BASHRC:-$HOME/OpenFOAM/OpenFOAM-v2606/etc/bashrc}"; set -u
(cd foamSideProto && wmake >/dev/null 2>&1) && mpicxx -O2 -o bin/foamMockProto-ompi5 mock/foamMockProto.C
export DEMCMD="$YADE_BIN -n -x -j1 demSideProto.py" DEM_YADE=1
RUNS=$R LAUNCH=B REPS=10 ./matrix.sh b-ompi5-yade-of2606 "$FOAM_USER_APPBIN/foamSideProto" "${FOAMCMD[@]}"
unset DEMCMD DEM_YADE

echo "== check 2: real v2606 side, Open MPI 4.1 in ubuntu:24.04"
docker run --rm -v "$PWD":/p -v "$R":/r -w /p -e RUNS=/r -e LAUNCH=B -e REPS=10 \
  -e OMPI_ALLOW_RUN_AS_ROOT=1 -e OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 \
  -e HUID=$(id -u) -e HGID=$(id -g) ubuntu:24.04 bash -c '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq && apt-get install -y -qq --no-install-recommends curl ca-certificates >/dev/null
  curl -s https://dl.openfoam.com/add-debian-repo.sh | bash >/dev/null
  apt-get install -y -qq --no-install-recommends openfoam2606-dev python3-mpi4py python3-numpy >/dev/null
  set +u; source /usr/lib/openfoam/openfoam2606/etc/bashrc; set -u
  mpirun --version | head -1; foamVersion 2>/dev/null || echo "WM_PROJECT_VERSION=$WM_PROJECT_VERSION"
  (cd foamSideProto && wclean >/dev/null && wmake >/dev/null 2>&1) || { echo "wmake failed"; exit 1; }
  ./matrix.sh b-ompi41-of2606 "$FOAM_USER_APPBIN/foamSideProto" -parallel -mpi-split-by-appnum -case case -mpi-threads -steps 4
  cd foamSideProto && wclean >/dev/null; chown -R $HUID:$HGID /r /p'
(cd foamSideProto && wmake >/dev/null 2>&1)   # rebuild the host binary's objects
echo "== summaries"; cat "$R"/b-ompi5-yade-of2606-summary.txt "$R"/b-ompi41-of2606-summary.txt
