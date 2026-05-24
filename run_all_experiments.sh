#!/bin/bash
# ============================================================
# run_all_experiments.sh — STMO Batch Experiment Runner
# Runs all 45 instances and saves results to results.csv
#
# USAGE (from root of STMO-CODE-V1 folder):
#   bash run_all_experiments.sh
#
# ASSUMES:
#   - Source files (STMO.cpp, config.h, etc.) in current directory
#   - ./params/ folder contains all 45 param_N*_M*_S*.txt files
#   - results/ folder will be created automatically
#   - g++ (MinGW or Linux) available on PATH
# ============================================================

set -e

EXEC="./STMO.exe"
PARAMS_DIR="./params"
RESULTS_DIR="./results"
RESULTS_CSV="./results/results.csv"
LOG_DIR="./results/logs"

# ------------------------------------------------------------
# STEP 0: Set END_TIME to 120s and recompile
# ------------------------------------------------------------
echo "Setting END_TIME=120.0 in config.h ..."
sed -i 's/#define END_TIME.*/#define END_TIME    120.0/' config.h
grep "END_TIME" config.h

echo "Compiling STMO ..."
g++ -O2 -std=c++11 -o STMO.exe STMO.cpp -lm
echo "Compile OK."
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

mkdir -p "$RESULTS_DIR" "$LOG_DIR"

# CSV header
echo "N,M,Seed,BestZ,Iterations,TimeUsed,BestAtIter,S2Repairs,S4Phase1Hits,StagnationResets" > "$RESULTS_CSV"

Ns=(30 50 100 150 200)
Ms=(3 6 9)
Seeds=(10 20 30)

TOTAL=45
DONE=0
FAILED=0

echo "============================================================"
echo "  STMO Batch Experiment Runner"
echo "  Total instances: $TOTAL"
echo "  Time limit: 120s per instance (~90 min total)"
echo "  Results: $RESULTS_CSV"
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

      # Save a copy of out.txt for this instance
      if [ -f "./out.txt" ]; then
          cp ./out.txt "$LOG_DIR/out_N${N}_M${M}_S${Seed}.txt"
          OUT="$LOG_DIR/out_N${N}_M${M}_S${Seed}.txt"
      else
          echo "WARN: out.txt not found after run"
          OUT="$LOG_DIR/stdout_N${N}_M${M}_S${Seed}.txt"
      fi

      # Parse out.txt with exact grep patterns
      BEST_Z=$(grep "Best Z"                "$OUT" | awk '{print $NF}')
      ITERS=$(grep  "Total iters"           "$OUT" | awk '{print $3}')
      TIME_S=$(grep "Time used"             "$OUT" | awk '{print $4}')
      BEST_AT=$(grep "Best at iter"         "$OUT" | awk '{print $5}')
      S2REP=$(grep  "Stage 2 total repairs" "$OUT" | awk '{print $NF}')
      S4HIT=$(grep  "Stage 4 Phase1 hits"   "$OUT" | awk '{print $NF}')
      STAG=$(grep   "Stagnation resets"     "$OUT" | awk '{print $NF}')

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
TOTAL_MIN=$(( (END_TOTAL - START_TOTAL) / 60 ))

echo ""
echo "============================================================"
echo "  DONE. $((DONE - FAILED))/$TOTAL instances succeeded."
[ $FAILED -gt 0 ] && echo "  $FAILED FAILED — check $LOG_DIR"
echo "  Total wall time: ~${TOTAL_MIN} minutes"
echo "  Results: $RESULTS_CSV"
echo "============================================================"
