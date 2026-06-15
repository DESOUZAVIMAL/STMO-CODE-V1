#!/bin/bash
# ============================================================
# run_009_diag.sh — STMO Run 009 Diagnostic Batch Runner
#
# USAGE:
#   bash run_009_diag.sh
#
# DESIGN (two passes):
#
#   PASS A — DATA (Job 1: find the bottleneck)
#     All 45 instances (N x M x Seed) run ONCE, per-N stop budget:
#         N <= 50  : 120 s ceiling / 50000-iter cap
#         N >= 100 : 300 s ceiling / 20000-iter cap   (time is the real
#                    stop for N>=150 — that IS the bottleneck evidence)
#     Output: results/run_009_diag/N{N}_M{M}_S{Seed}/repeat1/
#
#   PASS B — REPRODUCIBILITY (Job 2: same seed -> same answer)
#     5 representative instances (one per N, at M=6 Seed=20) run 3x,
#     PURELY ITERATION-BOUND at DIAG_FORCE_ITERS=5000 (clock disabled),
#     so every repeat stops at the IDENTICAL iteration and the
#     determinism comparison is valid (time-bound runs cannot be compared
#     because machine jitter changes the stop iteration).
#     Output: results/run_009_diag/N{N}_M{M}_S{Seed}_repro/repeat{1,2,3}/
#     Rows  : results/run_009_diag/reproducibility_repro.csv
#     Report: results/run_009_diag/reproducibility_report.csv  (§1b verdict)
#
# This is a DATA-GATHERING run, NOT an optimization run. Do not change
# the algorithm. The diag module (DIAG_MODE=1) is passive instrumentation.
#
# ASSUMES: source files + ./params/ (45 param_N*_M*_S*.txt) in CWD.
# ============================================================

set -e

PARAMS_DIR="./params"
ROOT_DIR="./results/run_009_diag"
REPRO_CSV="$ROOT_DIR/reproducibility_repro.csv"
REPRO_REPORT="$ROOT_DIR/reproducibility_report.csv"

REPRO_ITERS=5000          # iteration-bound cap for Pass B (matches DIAG_REPRO_ITERS)
REPRO_REPEATS=3           # repeats per subset instance in Pass B

# ------------------------------------------------------------
# Compile the DIAG_MODE=1 binary. Per-N stop budgets live in the
# binary (config.h + STMO.cpp) — no -DMAX_ITERATIONS needed here.
# ------------------------------------------------------------
echo "Compiling STMO (DIAG_MODE=1, per-N stop config) ..."
GXX="g++"
if ! command -v g++ &>/dev/null; then
    [ -f "/c/msys64/mingw64/bin/g++.exe" ] && GXX="/c/msys64/mingw64/bin/g++.exe"
fi
if "$GXX" -O2 -std=c++11 -DDIAG_MODE=1 -static -static-libgcc -static-libstdc++ \
       -Wl,--stack,8388608 -o STMO.exe STMO.cpp -lm 2>/tmp/stmo_build_win.log; then
    EXEC="./STMO.exe"; echo "Compile OK (Windows/static)."
elif "$GXX" -O2 -std=c++11 -DDIAG_MODE=1 -o STMO STMO.cpp -lm 2>/tmp/stmo_build_nix.log; then
    EXEC="./STMO"; echo "Compile OK (Linux/macOS)."
elif [ -f "./STMO.exe" ]; then
    EXEC="./STMO.exe"; echo "WARNING: compile failed; using existing STMO.exe."
else
    echo "ERROR: compile failed and no binary present."
    cat /tmp/stmo_build_win.log /tmp/stmo_build_nix.log 2>/dev/null || true
    exit 1
fi
echo ""

[ -d "$PARAMS_DIR" ] || { echo "ERROR: $PARAMS_DIR not found."; exit 1; }
mkdir -p "$ROOT_DIR"
rm -f "$REPRO_CSV" "$REPRO_REPORT"

Ns=(30 50 100 150 200)
Ms=(3 6 9)
Seeds=(10 20 30)

START_TOTAL=$(date +%s)

# ============================================================
# PASS A — full diagnostic data, all 45 instances, per-N budget
# ============================================================
echo "============================================================"
echo "  PASS A — DATA  (45 instances x 1, per-N stop budget)"
echo "============================================================"
DONE=0; FAILED=0; TOTAL_A=45
for N in "${Ns[@]}"; do
  for M in "${Ms[@]}"; do
    for Seed in "${Seeds[@]}"; do
      DONE=$((DONE + 1))
      PARAM_FILE="$PARAMS_DIR/param_N${N}_M${M}_S${Seed}.txt"
      if [ ! -f "$PARAM_FILE" ]; then
          echo "[$DONE/$TOTAL_A] SKIP — missing $PARAM_FILE"; FAILED=$((FAILED+1)); continue
      fi
      echo -n "[$DONE/$TOTAL_A] N=$N M=$M Seed=$Seed ... "
      cp "$PARAM_FILE" ./param.txt
      LOGDIR="$ROOT_DIR/N${N}_M${M}_S${Seed}/repeat1"; mkdir -p "$LOGDIR"
      set +e
      START=$(date +%s)
      "$EXEC" 1 > "$LOGDIR/stdout.txt" 2>&1      # repeat index 1
      EXIT_CODE=$?
      ELAPSED=$(( $(date +%s) - START ))
      set -e
      if [ $EXIT_CODE -ne 0 ]; then echo "FAILED (exit $EXIT_CODE)"; FAILED=$((FAILED+1)); continue; fi
      set +e
      BEST_Z=$(grep -m1 "^  Best Z ="           "$LOGDIR/stdout.txt" | awk '{print $4}')
      ITERS=$( grep -m1 "^  Total iterations =" "$LOGDIR/stdout.txt" | awk '{print $4}')
      set -e
      echo "Z=${BEST_Z:-?}  iters=${ITERS:-?}  t=${ELAPSED}s"
    done
  done
done
echo ""

# ============================================================
# PASS B — reproducibility probe, iteration-bound, subset x3
# ============================================================
echo "============================================================"
echo "  PASS B — REPRODUCIBILITY  (subset x${REPRO_REPEATS}, iter-bound @ ${REPRO_ITERS})"
echo "============================================================"
# One representative instance per N (mid M, mid Seed).
SUBSET_M=6
SUBSET_S=20
for N in "${Ns[@]}"; do
    PARAM_FILE="$PARAMS_DIR/param_N${N}_M${SUBSET_M}_S${SUBSET_S}.txt"
    if [ ! -f "$PARAM_FILE" ]; then echo "SKIP — missing $PARAM_FILE"; continue; fi
    cp "$PARAM_FILE" ./param.txt
    for r in $(seq 1 $REPRO_REPEATS); do
        echo -n "  N=$N M=$SUBSET_M Seed=$SUBSET_S repeat=$r (iter-bound) ... "
        LOGDIR="$ROOT_DIR/N${N}_M${SUBSET_M}_S${SUBSET_S}_repro/repeat${r}"; mkdir -p "$LOGDIR"
        set +e
        DIAG_TAG=_repro DIAG_FORCE_ITERS=$REPRO_ITERS "$EXEC" "$r" > "$LOGDIR/stdout.txt" 2>&1
        EXIT_CODE=$?
        set -e
        if [ $EXIT_CODE -ne 0 ]; then echo "FAILED (exit $EXIT_CODE)"; continue; fi
        Z=$(grep -m1 "^  Best Z =" "$LOGDIR/stdout.txt" | awk '{print $4}')
        echo "Z=${Z:-?}"
    done
done
echo ""

# ============================================================
# Reproducibility report (§1b): for each subset instance,
# DETERMINISTIC iff all repeats share final_bestZ AND traj_hash.
# ============================================================
if [ -f "$REPRO_CSV" ]; then
    awk -F',' '
    NR==1 { next }
    {
        key=$1","$2","$3; nrep[key]++
        if (!(key in zf)) { zf[key]=$5; hf[key]=$6 }
        if ($5!=zf[key]) zbad[key]=1
        if ($6!=hf[key]) hbad[key]=1
        if (!(key in seen)) { order[++nk]=key; seen[key]=1 }
    }
    END {
        print "N,M,Seed,n_repeats,final_bestZ,traj_hash,verdict"
        for (i=1;i<=nk;i++){ k=order[i]
            v=(zbad[k]||hbad[k])?"NONDETERMINISTIC":"DETERMINISTIC"
            printf "%s,%d,%s,%s,%s\n", k, nrep[k], zf[k], hf[k], v }
    }' "$REPRO_CSV" > "$REPRO_REPORT"
    echo "Reproducibility report: $REPRO_REPORT"
    NONDET=$(awk -F',' 'NR>1 && $7=="NONDETERMINISTIC"' "$REPRO_REPORT" | wc -l | tr -d ' ')
    if [ "$NONDET" -gt 0 ]; then
        echo "  *** WARNING: $NONDET instance(s) NONDETERMINISTIC — investigate. ***"
    else
        echo "  All subset instances DETERMINISTIC (same seed -> same Z + trajectory)."
    fi
else
    echo "WARNING: $REPRO_CSV not found — Pass B may not have run."
fi

TOTAL_MIN=$(( ($(date +%s) - START_TOTAL) / 60 ))
echo ""
echo "============================================================"
echo "  DONE — Run 009 diagnostic batch"
echo "  Pass A: ${DONE} data runs ($FAILED failed)   Pass B: ${#Ns[@]} x ${REPRO_REPEATS} reproducibility runs"
echo "  Total wall time: ~${TOTAL_MIN} minutes"
echo "  Data root: $ROOT_DIR"
echo "============================================================"
