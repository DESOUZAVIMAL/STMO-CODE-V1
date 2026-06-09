// ============================================================
// config.h — STMO Algorithm Configuration
// ============================================================
// ALL tunable parameters live here and ONLY here.
// ============================================================

#ifndef CONFIG_H
#define CONFIG_H

// ------------------------------------------------------------
// PROBLEM SIZE LIMITS
// ------------------------------------------------------------
#define MAX_N   300
#define MAX_M   15
#define MAX_P   50

// ------------------------------------------------------------
// POPULATION
// ------------------------------------------------------------
#define P           30

// ------------------------------------------------------------
// Run 003 — Adaptive config by N (set at runtime in STMO.cpp).
// MAX_ITER is now a high safety ceiling for ALL N so that
// END_TIME is the sole real stopping condition.
// ------------------------------------------------------------
#define MAX_ITER         50000   // Run003 A1: high ceiling, time controls stop
#define MAX_ITER_SMALL   50000   // kept for compatibility
#define END_TIME         120.0   // default (N <= 100)
#define END_TIME_LARGE   300.0   // Run008 subset: N >= 150 time-extension test
#define K_STAG_SMALL     100     // N <= 50
#define K_STAG_LARGE      50     // N >= 100
#define MIN_COUNT_SMALL    3     // N <= 100
#define MIN_COUNT_LARGE    1     // N >= 150

// Baseline values for adaptive parameters (large-N overrides in STMO.cpp)
#define NC_BASE            3     // Stage 1 candidates (small/medium N)
#define NC_LARGE           5     // Run003 A3: N >= 150
#define MACHINE_BUDGET_BASE  3   // Stage 2 repairs per machine (small/medium N)
#define MACHINE_BUDGET_LARGE 5   // Run003 A4: N >= 150
#define E_SIZE_BASE        5     // elite archive size (small/medium N)
#define E_SIZE_LARGE      10     // Run003 A5: N >= 150 (must be <= MAX_P)

// Global runtime values — STMO.cpp sets these after reading N.
extern int   g_maxIter;        // replaces MAX_ITER in main loop
extern float g_endTime;        // replaces END_TIME in main loop
extern int   g_kStag;          // replaces K_STAG in stagnation check
extern int   g_minCount;       // replaces MIN_COUNT in labelling/lookup
extern int   g_nc;             // replaces NC in Stage 1
extern int   g_machineBudget;  // replaces MACHINE_BUDGET in Stage 2
extern int   g_eSize;          // replaces E_SIZE in elite archive

// Run 7 global counters and state (defined in STMO.cpp)
extern long  g_s1_acc,  g_s1_gbest;
extern long  g_s2_acc,  g_s2_strict,  g_s2_gbest;
extern long  g_s4_acc,  g_s4_gbest;
extern long  g_s5_acc,  g_s5_gbest;
extern int   g_restartCount;
extern int   g_ccrit;
extern int   g_cweak;
extern float g_globalBest;

// ------------------------------------------------------------
// STAGE 1 — OceanCurrentDrift
// ------------------------------------------------------------
// Stage 1 move-type weights (must sum to 1.0)
#define W_INSERT    0.20f
#define W_REVISED   0.15f
#define W_SWAP      0.15f
#define W_MSWAP     0.10f
#define W_COMBO     0.40f

// ------------------------------------------------------------
// STAGE 2 — MemoryAwareDrift
// ------------------------------------------------------------
#define MAX_ATTEMPTS    5

// ------------------------------------------------------------
// STAGE 3 — ACMM (Pair Memory)
// ------------------------------------------------------------
#define DF              0.95f
#define GHOST_THRESHOLD 0.01f
#define GHOST_MAX_AGE   100     // Run006: iters w/o observation before a pair is pruned (conservative)
#define PM_WARN_SIZE    5000    // Run006: log-only PairMemory-size warning threshold (no eviction)
#define STRONG_SIGMA_MULT   0.5f
#define WEAK_SIGMA_MULT     0.5f
#define TRIPLET_MIN_COUNT   2

// ------------------------------------------------------------
// STAGE 4 — MFBO
// ------------------------------------------------------------
#define KN          3   // VNS candidates (Phase 2, future use)

// ------------------------------------------------------------
// STAGNATION HANDLING
// ------------------------------------------------------------
#define STAG_RESET_FRACTION 0.25f

// ------------------------------------------------------------
// RUN 7 — Convergence-triggered restart + speed changes
// ------------------------------------------------------------
#define ENABLE_TRIPLETS 0   // set to 1 to re-enable triplet subsystem for ablation
#define RESTART_STAG    2000   // restart after this many no-improvement iters

// ------------------------------------------------------------
// OUTPUT
// ------------------------------------------------------------
#define PRINT_EVERY     10
#define PRINT_DIAG      1

#endif // CONFIG_H
