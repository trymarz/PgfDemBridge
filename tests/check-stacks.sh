#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Core tests x10 on the stacks the host lacks: Open MPI 4.1.6 (ubuntu:24.04) and MPICH 4.2.2 (fedora:42).
# Usage: [STACKS="ompi41 mpich"] tests/check-stacks.sh > log 2>&1 (needs docker); build trees go to build-<stack>/.
# --shm-size: MPICH keeps its shared memory in /dev/shm, and Docker's 64 MB default gives SIGBUS.
set -u
cd "$(dirname "$0")/.."
T='cmake -S . -B build-$S -DPGFDB_WERROR=ON $F >/dev/null && cmake --build build-$S -j4 >/dev/null &&
   ctest --test-dir build-$S --repeat until-fail:10 --timeout 60 --output-on-failure > build-$S/ctest.log 2>&1;
   grep -E "tests passed|\(Failed|\(Timeout|\*\*\*" build-$S/ctest.log; chown -R $HUID:$HGID build-$S'
D="docker run --rm --shm-size=1g -v $PWD:/p -w /p -e HUID=$(id -u) -e HGID=$(id -g)"
case " ${STACKS:-ompi41 mpich} " in *" ompi41 "*)
echo "== Open MPI 4.1 in ubuntu:24.04"
$D -e S=ompi41 -e F=-DPGFDB_MPIEXEC_PREFLAGS=--oversubscribe \
   -e OMPI_ALLOW_RUN_AS_ROOT=1 -e OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 ubuntu:24.04 bash -c '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq && apt-get install -y -qq --no-install-recommends openmpi-bin libopenmpi-dev cmake make g++ >/dev/null
  mpirun --version | head -1; '"$T" ;; esac
case " ${STACKS:-ompi41 mpich} " in *" mpich "*)
echo "== MPICH in fedora:42"
$D -e S=mpich -e F= fedora:42 bash -c '
  dnf install -y -q mpich mpich-devel cmake make gcc-c++ >/dev/null
  export PATH=/usr/lib64/mpich/bin:$PATH LD_LIBRARY_PATH=/usr/lib64/mpich/lib
  mpichversion | head -1; '"$T" ;; esac
