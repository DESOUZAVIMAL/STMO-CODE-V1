#!/bin/bash
# fix_csv.sh — Regenerate clean results.csv from individual out_*.txt log files
#
# USAGE:
#   bash fix_csv.sh                                   ← fixes the latest run
#   bash fix_csv.sh results/run_002_2026-05-25_test   ← fixes a specific run
#
# Run this if a previous run_all_experiments.sh produced a broken results.csv.

if [ -n "$1" ]; then
    RUN_DIR="$1"
else
    # Find the most recently modified run folder
    RUN_DIR=$(ls -dt ./results/run_* 2>/dev/null | head -1)
    if [ -z "$RUN_DIR" ]; then
        echo "ERROR: No run_* folders found in ./results/. Run experiments first."
        exit 1
    fi
fi

LOGS="$RUN_DIR/logs"
OUT_CSV="$RUN_DIR/results.csv"

echo "Fixing: $OUT_CSV"
echo "Logs  : $LOGS"

if [ ! -d "$LOGS" ]; then
    echo "ERROR: $LOGS not found."
    exit 1
fi

echo "N,M,Seed,BestZ,Iterations,TimeUsed,BestAtIter,S2Repairs,S4Phase1Hits,StagnationResets" > "$OUT_CSV"

COUNT=0
FAILED=0

for f in "$LOGS"/out_N*_M*_S*.txt; do
    [ -f "$f" ] || continue

    BASE=$(basename "$f" .txt)
    N=$(echo "$BASE" | sed 's/out_N\([0-9]*\)_M.*/\1/')
    M=$(echo "$BASE" | sed 's/.*_M\([0-9]*\)_S.*/\1/')
    S=$(echo "$BASE" | sed 's/.*_S\([0-9]*\)/\1/')

    BEST_Z=$(grep -m1 "Best Z"               "$f" | awk '{print $NF}')
    ITERS=$(  grep -m1 "Total iters"         "$f" | awk '{print $4}')
    TIME_S=$( grep -m1 "Time used"           "$f" | awk '{print $4}')
    BEST_AT=$(grep -m1 "Best at iter"        "$f" | awk '{print $5}')
    S2REP=$(  grep -m1 "Stage 2 total"       "$f" | awk '{print $NF}')
    S4HIT=$(  grep -m1 "Stage 4 Phase1 hits" "$f" | awk '{print $NF}')
    STAG=$(   grep -m1 "Stagnation resets"   "$f" | awk '{print $NF}')

    if [ -z "$BEST_Z" ] || [ -z "$ITERS" ]; then
        echo "WARN: could not parse $f — skipping"
        FAILED=$((FAILED + 1))
        continue
    fi

    echo "$N,$M,$S,$BEST_Z,$ITERS,$TIME_S,$BEST_AT,$S2REP,$S4HIT,$STAG" >> "$OUT_CSV"
    COUNT=$((COUNT + 1))
done

SORTED=$(tail -n +2 "$OUT_CSV" | sort -t',' -k1,1n -k2,2n -k3,3n)
{ head -1 "$OUT_CSV"; echo "$SORTED"; } > "${OUT_CSV}.tmp" && mv "${OUT_CSV}.tmp" "$OUT_CSV"

echo ""
echo "Done. $COUNT rows written to $OUT_CSV"
[ $FAILED -gt 0 ] && echo "WARNING: $FAILED files could not be parsed."
echo ""
echo "Preview (first 5 rows):"
head -6 "$OUT_CSV"
