#!/bin/bash
# Launch option B: mpirun -np N role-single-app.sh <foam-cmd...>; ranks below NDEM become the DEM side.
cd "$(dirname "$0")"
rank=${OMPI_COMM_WORLD_RANK:-${PMIX_RANK:-${PMI_RANK:-${SLURM_PROCID:?cannot find the MPI rank}}}}
if [ "$rank" -lt "$NDEM" ]; then exec python3 demSideProto.py; else exec "$@"; fi
