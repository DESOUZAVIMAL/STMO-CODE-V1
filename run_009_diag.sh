#!/bin/bash
# ============================================================
# run_009_diag.sh — STMO Run 009 Diagnostic Batch Runner
#
# USAGE:
#   bash run_009_diag.sh
#
# WHAT THIS DOES (spec §1, §7):
#   - Compiles the DIAG_MODE=1 binary (full passive instrumentation).
#   - Runs all 45 instances (N x M x Seed), each repeated 3x for the
#     reproducibility probe (§1b).
#   - Each repeat writes its full diagnostic set to:
#         results/run_009_diag/N{N}_M{M}_S{Seed}/repeat{r}/
#     (the C++ diag module creates these dirs and files itself).
#   - STMO appends one row per repeat to the shared:
#         results/run_009_diag/reproducibility.csv
#   - After all runs, this script generates:
#         results/run_009_diag/reproducibility_report.csv  (§1b verdict)
#
# IMPORTANT — this is a DATA-GATHERING run, NOT an optimization run.
#   The deliverable is EVIDENCE. Do not change the algorithm.
#   MAX_ITERATIONS=30000 is the PRIMARY stop (equal iters for all N);
#   END_TIME=600s is only a safety ceiling.
#
# ASSUMES:
#   - Source files (STMO.cpp, config.h, diag.h, ...) in current directory
#   - ./params/ folder contains all 45 param_N*_M*_S*.txt files
#   - g++ (MinGW/MSYS2 on Windows, or Linux) available
# ============================================================

set -e

PARAMS_DIR="./params"
ROOT_DIR="./results/run_009_diag"
REPRO_CSV="$ROOT_DIR/reproducibility.csv"
REPRO_REPORT="$ROOT_DIR/reproducibility_report.csv"

# Run config (spec §1). Forced via -D so the binary is unambiguous
# regardless of the current config.h editing state.
DIAG_MODE_VAL=1
MAX_ITERATIONS_VAL=30000
REPEATS=3

# ------------------------------------------------------------
# STEP 0: Compile the DIAG_MODE=1 binary (spec build).
# ------------------------------------------------------------
echo "Compiling STMO (DIAG_MODE=$DIAG_MODE_VAL, MAX_ITERATIONS=$MAX_ITERATIONS_VAL) ..."
GXX="g++"
if ! command -v g++ &>/dev/null; then
    if [ -f "/c/msys64/mingw64/bin/g++.exe" ]; then
        GXX="/c/msys64/mingw64/bin/g++.exe"
    fi
fi

# Windows/MSYS2 needs the static + large-stack flags; Linux/macOS ignore
# the --stack linker arg harmlessly only on MinGW, so detect and branch.
COMMON_FLAGS="-O2 -std=c++11 -DDIAG_MODE=$DIAG_MODE_VAL -DMAX_ITERATIONS=$MAX_ITERATIONS_VAL"
if command -v "$GXX" &>/dev/null || [ -f "$GXX" ]; then
    if "$GXX" $COMMON_FLAGS -static -static-libgcc -static-libstdc++ \
           -Wl,--stack,8388608 -o STMO.exe STMO.cpp -lm 2>/tmp/stmo_build_win.log; then
        EXEC="./STMO.exe"
        echo "Compile OK (Windows/static)."
    elif "$GXX" $COMMON_FLAGS -o STMO STMO.cpp -lm 2>/tmp/stmo_build_nix.log; then
        EXEC="./STMO"
        echo "Compile OK (Linux/macOS)."
    elif [ -f "./STMO.exe" ]; then
        EXEC="./STMO.exe"
        echo "WARNING: compile failed; using existing STMO.exe."
        cat /tmp/stmo_build_win.log /tmp/stmo_build_nix.log 2>/dev/null || true
    else
        echo "ERROR: compile failed and no binary present."
        cat /tmp/stmo_build_win.log /tmp/stmo_build_nix.log 2>/dev/null || true
        exit 1
    fi
else
    echo "ERROR: g++ not found."
    exit 1
fi
echo ""

if [ ! -d "$PARAMS_DIR" ]; then
    echo "ERROR: $PARAMS_DIR not found. Copy the params/ folder here."
    exit 1
fi

mkdir -p "$ROOT_DIR"

# Fresh reproducibility.csv each batch so verdicts reflect THIS run only.
rm -f "$REPRO_CSV" "$REPRO_REPORT"

Ns=(30 50 100 150 200)
Ms=(3 6 9)
Seeds=(10 20 30)

TOTAL=$(( ${#Ns[@]} * ${#Ms[@]} * ${#Seeds[@]} * REPEATS ))
DONE=0
FAILED=0

echo "============================================================"
echo "  STMO Run 009 — Diagnostic / Data-Gathering Run"
echo "  Instances: 45   Repeats each: $REPEATS   Total runs: $TOTAL"
echo "  MAX_ITERATIONS=$MAX_ITERATIONS_VAL (primary)  END_TIME=600s (ceiling)"
echo "  Output root: $ROOT_DIR"
echo "============================================================"
echo ""

START_TOTAL=$(date +%s)

for N in "${Ns[@]}"; do
  for M in "${Ms[@]}"; do
    for Seed in "${Seeds[@]}"; do
      PARAM_FILE="$PARAMS_DIR/param_N${N}_M${M}_S${Seed}.txt"
      if [ ! -f "$PARAM_FILE" ]; then
          echo "SKIP — missing param: $PARAM_FILE"
          FAILED=$(( FAILED + REPEATS ))
          DONE=$(( DONE + REPEATS ))
          continue
      fi

      cp "$PARAM_FILE" ./param.txt

      for r in $(seq 1 $REPEATS); do
          DONE=$((DONE + 1))
          echo -n "[$DONE/$TOTAL] N=$N M=$M Seed=$Seed repeat=$r ... "

          LOGDIR="$ROOT_DIR/N${N}_M${M}_S${Seed}/repeat${r}"
          mkdir -p "$LOGDIR"

          set +e
          START=$(date +%s)
          "$EXEC" "$r" > "$LOGDIR/stdout.txt" 2>&1
          EXIT_CODE=$?
          END=$(date +%s)
          ELAPSED=$((END - START))
          set -e

          if [ $EXIT_CODE -ne 0 ]; then
              echo "FAILED (exit $EXIT_CODE)"
              FAILED=$((FAILED + 1))
              continue
          fi

          set +e
          BEST_Z=$(grep -m1 "^  Best Z ="           "$LOGDIR/stdout.txt" | awk '{print $4}')
          ITERS=$( grep -m1 "^  Total iterations =" "$LOGDIR/stdout.txt" | awk '{print $4}')
          set -e
          echo "Z=${BEST_Z:-?}  iters=${ITERS:-?}  t=${ELAPSED}s"
      done
    done
  done
done

END_TOTAL=$(date +%s)
TOTAL_MIN=$(( (END_TOTAL - START_TOTAL) / 60 ))
SUCCEEDED=$((DONE - FAILED))

# ------------------------------------------------------------
# Generate reproducibility_report.csv (spec §1b).
# For each (N,M,Seed): DETERMINISTIC iff all repeats share the SAME
# final_bestZ AND the SAME traj_hash. Else NONDETERMINISTIC.
# reproducibility.csv columns:
#   N,M,Seed,repeat_idx,final_bestZ,traj_hash,final_iter,wall_time_s
# ------------------------------------------------------------
if [ -f "$REPRO_CSV" ]; then
    awk -F',' '
    NR==1 { next }
    {
        key = $1","$2","$3
        nrep[key]++
        if (!(key in zfirst)) { zfirst[key]=$5; hfirst[key]=$6 }
        if ($5 != zfirst[key]) zbad[key]=1
        if ($6 != hfirst[key]) hbad[key]=1
        # remember insertion order
        if (!(key in seen)) { order[++nk]=key; seen[key]=1 }
    }
    END {
        print "N,M,Seed,n_repeats,final_bestZ,traj_hash,verdict"
        for (i=1;i<=nk;i++) {
            key=order[i]
            v = (zbad[key] || hbad[key]) ? "NONDETERMINISTIC" : "DETERMINISTIC"
            printf "%s,%d,%s,%s,%s\n", key, nrep[key], zfirst[key], hfirst[key], v
        }
    }' "$REPRO_CSV" > "$REPRO_REPORT"
    echo ""
    echo "Reproducibility report written: $REPRO_REPORT"
    NONDET=$(awk -F',' 'NR>1 && $7=="NONDETERMINISTIC"' "$REPRO_REPORT" | wc -l | tr -d ' ')
    if [ "$NONDET" -gt 0 ]; then
        echo "  *** WARNING: $NONDET instance(s) NONDETERMINISTIC — see report. ***"
    else
        echo "  All instances DETERMINISTIC (same seed -> same Z + same trajectory)."
    fi
else
    echo "WARNING: $REPRO_CSV not found — no reproducibility report generated."
fi

echo ""
echo "============================================================"
echo "  DONE — Run 009 diagnostic batch"
echo "  ${SUCCEEDED}/${TOTAL} runs succeeded."
[ $FAILED -gt 0 ] && echo "  ${FAILED} FAILED — check per-repeat stdout.txt files."
echo "  Total wall time: ~${TOTAL_MIN} minutes"
echo "  Data root: $ROOT_DIR"
echo "============================================================"
