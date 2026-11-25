#!/bin/bash
# Usage: matrix.sh <label> <foam-cmd...>; DEMxFOAM layouts, REPS each, 30 s timeout, stop a layout after 2 failures.
# LAUNCH=B uses role-single-app.sh (one app); OVERSUB="" for MPICH; RUNS sets the log dir.
label=$1; shift; FOAMCMD=("$@")
cd "$(dirname "$0")"; RUNS=${RUNS:-runs}; mkdir -p "$RUNS"
REPS=${REPS:-10}; X=${MPIRUN_EXTRA:-}; OVERSUB=${OVERSUB---oversubscribe}
summary=$RUNS/$label-summary.txt; : > $summary
for layout in "1 1" "2 2" "1 3" "3 1"; do
  read ny nf <<< "$layout"
  ok=0; bad=0
  for i in $(seq 1 $REPS); do
    log=$RUNS/$label-${ny}x${nf}-$i.log
    if [ "${LAUNCH:-A}" = B ]; then
      timeout 30 mpirun $X $OVERSUB -np $((ny+nf)) env NDEM=$ny ./role-single-app.sh "${FOAMCMD[@]}" > $log 2>&1
    else
      timeout 30 mpirun $X $OVERSUB -np $ny python3 demSideProto.py : -np $nf "${FOAMCMD[@]}" > $log 2>&1
    fi
    rc=$?
    if [ $rc -eq 0 ] && grep -q "RUN FINISH" $log && grep -q "^End\|^End$" $log; then ok=$((ok+1)); else bad=$((bad+1)); echo "  FAIL rc=$rc $log" | tee -a $summary; [ $bad -ge 2 ] && break; fi
  done
  echo "$label ${ny}xDEM ${nf}xFOAM: $ok ok, $bad failed" | tee -a $summary
done
