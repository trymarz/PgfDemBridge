#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Launch option B for the tests: mpirun -np N env NDEM=k role.sh <mockFoam> <foam args...>.
# Ranks [NFOAMFIRST, NFOAMFIRST+NDEM) run "$MOCKDEM" $DEMARGS; the others run the Foam command.
rank=${OMPI_COMM_WORLD_RANK:-${PMIX_RANK:-${PMI_RANK:-${SLURM_PROCID:?cannot find the MPI rank}}}}
f=${NFOAMFIRST:-0}
if [ "$rank" -ge "$f" ] && [ "$rank" -lt $((f + NDEM)) ]; then exec "$MOCKDEM" $DEMARGS; else exec "$@"; fi
