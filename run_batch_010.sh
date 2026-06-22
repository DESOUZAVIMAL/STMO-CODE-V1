#!/bin/bash
# Run10-final full 45-instance batch (C2 disabled, GHOST_MAX_AGE=1000,
# N<=100 -> 120s, N>=150 -> 240s, DIAG_MODE=1).
BINARY=./STMO_run10_final
RESULTS_DIR=results/run_010_final
mkdir -p "$RESULTS_DIR"

NS=(30 50 100 150 200)
MS=(3 6 9)
SEEDS=(10 20 30)

TOTAL=45
DONE=0
START=$(date +%s)

for N in "${NS[@]}"; do
  for M in "${MS[@]}"; do
    for SEED in "${SEEDS[@]}"; do
      INST="N${N}_M${M}_S${SEED}"
      PARAMFILE="params/param_N${N}_M${M}_S${SEED}.txt"

      if [ ! -f "$PARAMFILE" ]; then
        echo "MISSING: $PARAMFILE — skipping"
        continue
      fi

      cp "$PARAMFILE" param.txt
      DONE=$((DONE+1))
      echo "[${DONE}/${TOTAL}] Running $INST ... ($(date +%H:%M:%S))"

      "$BINARY" > "$RESULTS_DIR/stdout_${INST}.txt" 2>&1

      BESTZ=$(grep -E '^  Best Z =' "$RESULTS_DIR/stdout_${INST}.txt" | tail -1)
      ITERS=$(grep -E '^  Total iterations =' "$RESULTS_DIR/stdout_${INST}.txt" | tail -1)
      echo "    $INST -> $BESTZ | $ITERS"
    done
  done
done

END=$(date +%s)
echo ""
echo "Batch complete. $DONE/$TOTAL instances. Elapsed: $(( (END-START)/60 )) min."
echo "Results in $RESULTS_DIR/"
