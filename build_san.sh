#!/bin/bash
# ============================================================
# build_san.sh — STMO Run 009 Sanitizer Pass (spec §1c-i)
#
# USAGE:
#   bash build_san.sh
#
# Builds an ASan+UBSan instrumented binary and runs it on 3
# representative instances. Captures each sanitizer report to
#   results/run_009_diag/sanitizer/sanitizer_N{N}_M{M}_S{Seed}.log
#
# A CLEAN pass = no "runtime error", "ERROR:", "AddressSanitizer",
# or "UndefinedBehaviorSanitizer" lines in any log.
#
# NOTE: This is a code-health audit, not a data run. We use a short
# iteration cap so the sanitizer (10-30x slower) finishes quickly;
# memory/UB bugs surface in the first few thousand iterations, not
# only at 30000. The diagnostic CSVs from this build are throwaway.
#
# PLATFORM: ASan/UBSan need a non-static build. On MSYS2 use the
# UCRT64 or CLANG64 toolchain; plain MinGW static builds can't link
# the sanitizer runtimes. On Linux/macOS this works out of the box.
# ============================================================

set -e

PARAMS_DIR="./params"
SAN_DIR="./results/run_009_diag/sanitizer"
SAN_ITERS=4000   # short cap — bugs surface early; sanitizer is slow

GXX="g++"
if ! command -v g++ &>/dev/null; then
    if [ -f "/c/msys64/ucrt64/bin/g++.exe" ]; then
        GXX="/c/msys64/ucrt64/bin/g++.exe"
    elif [ -f "/c/msys64/mingw64/bin/g++.exe" ]; then
        GXX="/c/msys64/mingw64/bin/g++.exe"
    fi
fi

echo "Building sanitizer binary (ASan + UBSan) ..."
if ! "$GXX" -O1 -g -std=c++11 \
        -DDIAG_MODE=1 -DMAX_ITERATIONS=$SAN_ITERS \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -o STMO_san STMO.cpp -lm; then
    echo "ERROR: sanitizer build failed."
    echo "  On MSYS2 use UCRT64/CLANG64 — MinGW static toolchains can't"
    echo "  link libasan/libubsan. On Linux/macOS this should just work."
    exit 1
fi
echo "Sanitizer build OK."
echo ""

mkdir -p "$SAN_DIR"

# Representative spread: small, medium, large N (spec §1c-i).
INSTANCES=("30 3 10" "100 6 20" "200 9 30")

# Surface leaks and keep going past the first UB so we see all reports.
export ASAN_OPTIONS="detect_leaks=1:halt_on_error=0:abort_on_error=0"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0"

CLEAN=1
for inst in "${INSTANCES[@]}"; do
    read -r N M Seed <<< "$inst"
    PARAM_FILE="$PARAMS_DIR/param_N${N}_M${M}_S${Seed}.txt"
    LOG="$SAN_DIR/sanitizer_N${N}_M${M}_S${Seed}.log"

    if [ ! -f "$PARAM_FILE" ]; then
        echo "SKIP — missing param: $PARAM_FILE"
        continue
    fi

    echo -n "Sanitizing N=$N M=$M Seed=$Seed (cap $SAN_ITERS iters) ... "
    cp "$PARAM_FILE" ./param.txt

    set +e
    ./STMO_san 1 > "$LOG" 2>&1
    set -e

    if grep -qE "runtime error|ERROR: AddressSanitizer|ERROR: LeakSanitizer|UndefinedBehaviorSanitizer|SUMMARY: " "$LOG"; then
        echo "ISSUES FOUND — see $LOG"
        CLEAN=0
    else
        echo "clean."
    fi
done

echo ""
echo "============================================================"
if [ "$CLEAN" -eq 1 ]; then
    echo "  SANITIZER PASS: CLEAN — no ASan/UBSan reports."
else
    echo "  SANITIZER PASS: ISSUES FOUND — inspect logs in $SAN_DIR"
    echo "  (Per spec §0: LOG and report bugs in this run — do NOT fix them here.)"
fi
echo "  Logs: $SAN_DIR/sanitizer_N*_M*_S*.log"
echo "============================================================"
