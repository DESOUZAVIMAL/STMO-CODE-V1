#!/bin/bash
# ============================================================
# run_all_experiments.sh — STMO Batch Experiment Runner
#
# USAGE:
#   bash run_all_experiments.sh
#   bash run_all_experiments.sh "increased DF to 0.98"
#
# Each run saves to its own versioned folder so old results
# are NEVER overwritten:
#
#   results/
#     run_001_2026-05-24_baseline/
#       config_snapshot.h    ← exact config.h used
#       run_info.txt         ← note + all key settings
#       results.csv          ← this run's 45 results
#       logs/                ← all individual out_*.txt files
#     run_002_2026-05-25_increased_DF/
#       ...
#     all_runs_summary.csv   ← master comparison table (one row per run)
#
# ASSUMES:
#   - Source files (STMO.cpp, config.h, etc.) in current directory
#   - ./params/ folder contains all 45 param_N*_M*_S*.txt files
#   - results/ folder will be created automatically
#   - g++ (MinGW or Linux) or pre-compiled STMO.exe available
# ============================================================

set -e

EXEC="./STMO.exe"
PARAMS_DIR="./params"
RESULTS_DIR="./results"
MASTER_CSV="$RESULTS_DIR/all_runs_summary.csv"

# ------------------------------------------------------------
# Run note — passed as first argument, or defaults to "baseline"
# ------------------------------------------------------------
RUN_NOTE="${1:-baseline}"

# ------------------------------------------------------------
# STEP 0: Set END_TIME to 120s and recompile
# ------------------------------------------------------------
echo "Setting END_TIME=120.0 in config.h ..."
sed -i 's/#define END_TIME  *[0-9][0-9]*\.[0-9]*$/#define END_TIME        120.0/' config.h
grep "END_TIME" config.h

echo "Compiling STMO ..."
# On Windows/MSYS2 the system g++ may not be in PATH; fall back to full path.
GXX="g++"
if ! command -v g++ &>/dev/null; then
    if [ -f "/c/msys64/mingw64/bin/g++.exe" ]; then
        GXX="/c/msys64/mingw64/bin/g++.exe"
    fi
fi
if command -v "$GXX" &>/dev/null || [ -f "$GXX" ]; then
    # --stack flag sets 8 MB stack on Windows (needed: Population+EliteArchive ~2.4 MB)
    "$GXX" -O2 -std=c++11 -static -static-libgcc -static-libstdc++ \
           -Wl,--stack,8388608 -o STMO.exe STMO.cpp -lm
    echo "Compile OK."
elif [ -f "./STMO.exe" ]; then
    echo "g++ not found — using existing STMO.exe."
else
    echo "ERROR: g++ not found and no STMO.exe present. Cannot run."
    exit 1
fi
echo ""

# Sanity checks
if [ ! -f "$EXEC" ]; then
    echo "ERROR: $EXEC not found after compile. Check build output above."
    exit 1
fi
if [ ! -d "$PARAMS_DIR" ]; then
    echo "ERROR: $PARAMS_DIR not found. Copy the params/ folder here."
    exit 1
fi

# ------------------------------------------------------------
# Create versioned run folder
# ------------------------------------------------------------
mkdir -p "$RESULTS_DIR"
EXISTING=$(ls -d "$RESULTS_DIR"/run_* 2>/dev/null | wc -l)
RUN_NUM=$(printf "%03d" $((EXISTING + 1)))
RUN_DATE=$(date +"%Y-%m-%d_%H-%M")
RUN_SLUG=$(echo "$RUN_NOTE" | sed 's/[^a-zA-Z0-9_-]/_/g' | cut -c1-40)
RUN_DIR="$RESULTS_DIR/run_${RUN_NUM}_${RUN_DATE}_${RUN_SLUG}"
LOG_DIR="$RUN_DIR/logs"
RESULTS_CSV="$RUN_DIR/results.csv"

mkdir -p "$RUN_DIR" "$LOG_DIR"

# ------------------------------------------------------------
# Snapshot config.h and write run_info.txt
# ------------------------------------------------------------
cp config.h "$RUN_DIR/config_snapshot.h"

END_TIME_VAL=$(grep  -m1 "END_TIME"    config.h | grep "define" | awk '{print $3}')
MAX_ITER_VAL=$(grep  -m1 "MAX_ITER"    config.h | grep "define" | awk '{print $3}')
DF_VAL=$(      grep  -m1 "^#define DF" config.h | awk '{print $3}')
MIN_COUNT_VAL=$(grep -m1 "MIN_COUNT"   config.h | grep "define" | awk '{print $3}')
K_STAG_VAL=$(  grep  -m1 "K_STAG"     config.h | grep "define" | awk '{print $3}')
P_VAL=$(       grep  -m1 "^#define P " config.h | awk '{print $3}')
NC_VAL=$(      grep  -m1 "^#define NC" config.h | awk '{print $3}')

cat > "$RUN_DIR/run_info.txt" <<EOF
============================================================
  STMO Run #${RUN_NUM}
  Date:     $(date +"%Y-%m-%d %H:%M")
  Note:     ${RUN_NOTE}
============================================================

--- Key Settings (from config.h) ---
  END_TIME      = ${END_TIME_VAL}  (seconds per instance)
  MAX_ITER      = ${MAX_ITER_VAL}  (iteration cap)
  P (pop size)  = ${P_VAL}
  NC (candid.)  = ${NC_VAL}
  DF (decay)    = ${DF_VAL}
  MIN_COUNT     = ${MIN_COUNT_VAL}
  K_STAG        = ${K_STAG_VAL}

--- Files ---
  Config snapshot : run_${RUN_NUM}_${RUN_DATE}_${RUN_SLUG}/config_snapshot.h
  Results CSV     : run_${RUN_NUM}_${RUN_DATE}_${RUN_SLUG}/results.csv
  Log files       : run_${RUN_NUM}_${RUN_DATE}_${RUN_SLUG}/logs/

EOF

# ------------------------------------------------------------
# CSV header
# ------------------------------------------------------------
echo "N,M,Seed,BestZ,Iterations,TimeUsed,BestAtIter,S2Repairs,S4Phase1Hits,StagnationResets" > "$RESULTS_CSV"

Ns=(30 50 100 150 200)
Ms=(3 6 9)
Seeds=(10 20 30)

TOTAL=45
DONE=0
FAILED=0

echo "============================================================"
echo "  STMO Batch Experiment Runner"
echo "  Run #${RUN_NUM} — ${RUN_NOTE}"
echo "  Total instances: $TOTAL"
echo "  Time limit: 120s per instance (~90 min total)"
echo "  Saving to: $RUN_DIR"
echo "============================================================"
echo ""

START_TOTAL=$(date +%s)

for N in "${Ns[@]}"; do
  for M in "${Ms[@]}"; do
    for Seed in "${Seeds[@]}"; do
      DONE=$((DONE + 1))
      PARAM_FILE="$PARAMS_DIR/param_N${N}_M${M}_S${Seed}.txt"

      if [ ! -f "$PARAM_FILE" ]; then
          echo "[$DONE/$TOTAL] SKIP — missing: $PARAM_FILE"
          FAILED=$((FAILED + 1))
          continue
      fi

      echo -n "[$DONE/$TOTAL] N=$N M=$M Seed=$Seed ... "

      cp "$PARAM_FILE" ./param.txt

      START=$(date +%s)
      "$EXEC" > "$LOG_DIR/stdout_N${N}_M${M}_S${Seed}.txt" 2>&1
      EXIT_CODE=$?
      END=$(date +%s)
      ELAPSED=$((END - START))

      if [ $EXIT_CODE -ne 0 ]; then
          echo "FAILED (exit $EXIT_CODE)"
          FAILED=$((FAILED + 1))
          echo "$N,$M,$Seed,ERROR,ERROR,ERROR,ERROR,ERROR,ERROR,ERROR" >> "$RESULTS_CSV"
          continue
      fi

      if [ -f "./out.txt" ]; then
          cp ./out.txt "$LOG_DIR/out_N${N}_M${M}_S${Seed}.txt"
          OUT="$LOG_DIR/out_N${N}_M${M}_S${Seed}.txt"
      else
          echo "WARN: out.txt not found after run"
          OUT="$LOG_DIR/stdout_N${N}_M${M}_S${Seed}.txt"
      fi

      BEST_Z=$(grep -m1 "Best Z"              "$OUT" | awk '{print $NF}')
      ITERS=$(  grep -m1 "Total iters"        "$OUT" | awk '{print $4}')
      TIME_S=$( grep -m1 "Time used"          "$OUT" | awk '{print $4}')
      BEST_AT=$(grep -m1 "Best at iter"       "$OUT" | awk '{print $5}')
      S2REP=$(  grep -m1 "Stage 2 total"      "$OUT" | awk '{print $NF}')
      S4HIT=$(  grep -m1 "Stage 4 Phase1 hits" "$OUT" | awk '{print $NF}')
      STAG=$(   grep -m1 "Stagnation resets"  "$OUT" | awk '{print $NF}')

      BEST_Z=${BEST_Z:-"PARSE_ERR"}
      ITERS=${ITERS:-"?"}
      TIME_S=${TIME_S:-$ELAPSED}
      BEST_AT=${BEST_AT:-"?"}
      S2REP=${S2REP:-"?"}
      S4HIT=${S4HIT:-"?"}
      STAG=${STAG:-"?"}

      echo "Z=$BEST_Z  iters=$ITERS  t=${TIME_S}s  best@$BEST_AT"
      echo "$N,$M,$Seed,$BEST_Z,$ITERS,$TIME_S,$BEST_AT,$S2REP,$S4HIT,$STAG" >> "$RESULTS_CSV"
    done
  done
done

END_TOTAL=$(date +%s)
TOTAL_SEC=$((END_TOTAL - START_TOTAL))
TOTAL_MIN=$(( TOTAL_SEC / 60 ))

SUCCEEDED=$((DONE - FAILED))

# ------------------------------------------------------------
# Append summary to run_info.txt
# ------------------------------------------------------------
cat >> "$RUN_DIR/run_info.txt" <<EOF
--- Run Summary ---
  Instances succeeded : ${SUCCEEDED} / ${TOTAL}
  Instances failed    : ${FAILED}
  Total wall time     : ~${TOTAL_MIN} minutes
EOF

# ------------------------------------------------------------
# Compute stats from results.csv and append to master summary
# ------------------------------------------------------------
STATS=$(awk -F',' '
NR>1 && $4 ~ /^[0-9]/ {
    sum+=$4; n++
    if (n==1 || $4>mx) mx=$4
    if (n==1 || $4<mn) mn=$4
    si+=$5; ti+=$6
}
END {
    if (n>0) printf "%.4f,%.4f,%.4f,%.1f,%.2f", sum/n, mx, mn, si/n, ti/n
    else     printf "NA,NA,NA,NA,NA"
}' "$RESULTS_CSV")

# Create master CSV header if it does not exist yet
if [ ! -f "$MASTER_CSV" ]; then
    echo "RunID,Date,Note,END_TIME,MAX_ITER,DF,MIN_COUNT,K_STAG,Instances,AvgBestZ,MaxBestZ,MinBestZ,AvgIters,AvgTimeSec" > "$MASTER_CSV"
fi

echo "${RUN_NUM},$(date +"%Y-%m-%d %H:%M"),${RUN_NOTE},${END_TIME_VAL},${MAX_ITER_VAL},${DF_VAL},${MIN_COUNT_VAL},${K_STAG_VAL},${SUCCEEDED},${STATS}" >> "$MASTER_CSV"

# ------------------------------------------------------------
# Final summary
# ------------------------------------------------------------
echo ""
echo "============================================================"
echo "  DONE — Run #${RUN_NUM}: ${RUN_NOTE}"
echo "  ${SUCCEEDED}/${TOTAL} instances succeeded."
[ $FAILED -gt 0 ] && echo "  ${FAILED} FAILED — check $LOG_DIR"
echo "  Total wall time     : ~${TOTAL_MIN} minutes"
echo "  This run saved to   : $RUN_DIR"
echo "  Master comparison   : $MASTER_CSV"
echo "============================================================"
echo ""
echo "To compare all runs so far:"
echo "  cat $MASTER_CSV"
echo ""
echo "Preview of this run's results (first 6 rows):"
head -6 "$RESULTS_CSV"
