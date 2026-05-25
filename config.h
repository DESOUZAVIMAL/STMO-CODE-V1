// ============================================================
// config.h — STMO Algorithm Configuration
// ============================================================
// ALL tunable parameters live here and ONLY here.
// No magic numbers anywhere else in the codebase.
// To change any setting: edit this file only.
//
// DIAGNOSTIC GUIDE (what to change if results are poor):
//   Result: best Z still improving at end of run
//   → Increase END_TIME or MAX_ITER (not enough time)
//
//   Result: best Z flat after iteration 20-30
//   → Decrease DF (faster decay), increase NC (more exploration)
//
//   Result: best Z flat from start (no improvement ever)
//   → Check Stage 1 logic, increase NC
//
//   Result: Stage 2 never triggers (shown in diagnostic)
//   → Decrease MIN_COUNT from 3 to 1 or 2
//
//   Result: Stage 4 Phase 1 never triggers
//   → Lower WEAK threshold (but this is computed from mu/sigma)
//   → Check PairMemory has enough entries
//
//   Result: stagnation counter hits K_STAG every time
//   → Increase K_STAG or decrease DF
// ============================================================

#ifndef CONFIG_H
#define CONFIG_H

// ------------------------------------------------------------
// PROBLEM SIZE LIMITS
// These are upper bounds for static array allocation.
// Actual N and M are read from param.txt at runtime.
// ------------------------------------------------------------
#define MAX_N   300     // maximum number of orders supported
#define MAX_M   15      // maximum number of machines supported
#define MAX_P   50      // maximum population size supported

// ------------------------------------------------------------
// POPULATION
// ------------------------------------------------------------
#define P           30      // number of turtles in population

// Run 002: Adaptive config by N.
// STMO.cpp sets g_maxIter, g_endTime, g_kStag, g_minCount after reading N.
// Small N (<=50): needs more iterations (wastes 90% of 120s budget otherwise)
// Large N (>=150): needs more time + faster PairMemory labelling (MIN_COUNT=1)
#define MAX_ITER         2500    // default (N >= 100)
#define MAX_ITER_SMALL  10000   // N <= 50
#define END_TIME        120.0   // default (N <= 100)
#define END_TIME_LARGE  180.0   // N >= 150: still improving at 94-97% of run
#define K_STAG_SMALL    100     // N <= 50: prevent PairMemory from resetting too fast
#define K_STAG_LARGE     50     // N >= 100: current value
#define MIN_COUNT_SMALL   3     // N <= 100: current value
#define MIN_COUNT_LARGE   1     // N >= 150: Stage 4 silent for 510 iters otherwise

// Global runtime values set adaptively in STMO.cpp after genData():
extern int   g_maxIter;   // replaces MAX_ITER in main loop
extern float g_endTime;   // replaces END_TIME in main loop
extern int   g_kStag;     // replaces K_STAG in stagnation check
extern int   g_minCount;  // replaces MIN_COUNT in labelAllPairs()

// ------------------------------------------------------------
// STAGE 1 — OceanCurrentDrift
// ------------------------------------------------------------
#define NC          3       // number of random candidates per turtle per iteration
// nc=3 balances exploration breadth vs computation cost.
// Increase to 5 if results show poor initial diversity.
// Decrease to 1 if Stage 1 is too slow on large instances.

// Move type weights for random selection in Stage 1
// Must sum to 1.0. Adjust if one operator dominates too much.
#define W_INSERT    0.20f   // Order Insert probability
#define W_REVISED   0.15f   // Machine Revised probability
#define W_SWAP      0.15f   // Order Swap probability
#define W_MSWAP     0.10f   // Machine Swap probability
#define W_COMBO     0.40f   // Combo (Insert + Revised) probability

// ------------------------------------------------------------
// STAGE 2 — MemoryAwareDrift
// ------------------------------------------------------------
#define MAX_ATTEMPTS    5   // max repair attempts per weak pair
// If no repair improves in MAX_ATTEMPTS tries, give up on that pair.

#define MACHINE_BUDGET  3   // max weak pairs repaired per machine per iteration
// k = min(MACHINE_BUDGET, weak pairs found on that machine)
// Controls scalability: total Stage 2 repairs <= M * MACHINE_BUDGET per turtle
// Increase if large instances (N=200) show many weak pairs left unrepaired.
// Decrease if Stage 2 is too slow.

// ------------------------------------------------------------
// STAGE 3 — ACMM (Pair Memory)
// ------------------------------------------------------------
#define DF              0.95f   // decay factor applied every iteration
// Each iteration: avgScore *= DF for non-observed pairs
// 0.95 = pair needs ~20 iterations of observation to stay relevant
// Decrease (e.g. 0.90) for faster forgetting (more adaptive)
// Increase (e.g. 0.99) for longer memory (more persistent)

#define MIN_COUNT       3       // min observations before pair is labelled
// Prevents labelling pairs as STRONG/WEAK from single lucky/unlucky observation
// Decrease to 1 if PairMemory is too empty in early iterations
// Increase to 5 for more conservative, stable labels

#define GHOST_THRESHOLD 0.01f   // remove pair entry if avgScore drops below this
// After decay, old entries fade away automatically.
// Keeps PairMemory lean and relevant.

// Score labelling thresholds (relative to population statistics)
// These are computed each iteration from mu and sigma — DO NOT hardcode.
// STRONG  : score >= mu + STRONG_SIGMA_MULT * sigma
// WEAK    : score <  mu - WEAK_SIGMA_MULT   * sigma
// CRITICAL: score <  0  (always, regardless of mu/sigma)
// NEUTRAL : everything else
#define STRONG_SIGMA_MULT   0.5f
#define WEAK_SIGMA_MULT     0.5f

// ODTP (Overlap-Driven Triplet Promotion)
#define TRIPLET_MIN_COUNT   2   // min co-occurrences before triplet is promoted
// Lower than MIN_COUNT because triplets are rarer than pairs

// ------------------------------------------------------------
// STAGE 4 — MFBO (Phase 1 only — Phase 2 VNS deferred)
// ------------------------------------------------------------
#define KN          3       // VNS candidates per neighborhood (future use)
// Currently deferred per professor instruction.
// Kept here so value is ready when Stage 4 Phase 2 is added.

// ------------------------------------------------------------
// STAGE 5 — CMA (Elite Archive)
// ------------------------------------------------------------
#define E_SIZE      5       // number of elite solutions in archive
// Top-E turtles by objective are kept.
// Increase to 10 for more diverse elite patterns.
// Decrease to 3 if CMA transfers are too similar.

// ------------------------------------------------------------
// STAGNATION HANDLING
// ------------------------------------------------------------
#define K_STAG      50      // iterations without improvement before reset
// When best objective unchanged for K_STAG iterations:
//   → Remove oldest 25% of WEAK records from PairMemory
//   → Reset stagnation counter
// Increase if algorithm is resetting too frequently (< 50 iters)
// Decrease if algorithm is stuck for too long before resetting

// Fraction of WEAK records removed during stagnation reset
#define STAG_RESET_FRACTION 0.25f   // remove oldest 25% of WEAK records

// ------------------------------------------------------------
// OUTPUT AND DIAGNOSTICS
// ------------------------------------------------------------
#define PRINT_EVERY     10      // print progress every N iterations
#define PRINT_DIAG      1       // 1 = print diagnostic report at end, 0 = skip

#endif // CONFIG_H
