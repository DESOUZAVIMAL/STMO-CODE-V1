// ============================================================
// STMO.cpp — Sea Turtle Migration Optimization
// ============================================================
// Main entry point for the STMO algorithm.
//
// Problem: Order Acceptance and Scheduling on Identical
//          Parallel Machines with Sequence-Dependent Setup Times
//          (Wu et al. 2018 — Applied Soft Computing)
//
// Algorithm: STMO — Sea Turtle Migration Optimization
//            5 stages mapping turtle life-cycle phases:
//   Stage 1: OceanCurrentDrift  (juvenile random exploration)
//   Stage 2: MemoryAwareDrift   (young adult — avoid weak zones)
//   Stage 3: ACMM               (adult — build internal map)
//   Stage 4: MFBO               (foraging — targeted repair)
//   Stage 5: CMA                (nesting return — elite transfer)
//
// Build:
//   g++ -O2 -std=c++11 -o STMO STMO.cpp -lm
//
// Run:
//   ./STMO
//   (param.txt must be in the same directory)
//
// Author : Vimal De Souza
// Student: 1135446 — Yuan Ze University
// Advisor: Dr. Gen-Han Wu
// ============================================================

#include "config.h"
#include "stmo_types.h"
#include "problem.h"
#include "memory_ops.h"
#include "stages.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <algorithm>

// ============================================================
// DIAGNOSTIC COUNTERS — accumulated across all iterations
// ============================================================
struct DiagCounters {
    int   totalIterations;
    int   iterBestImproved;     // last iteration where best improved
    int   stage2TotalRepairs;   // total Stage 2 repairs across all iters
    int   stage4Phase1Success;  // total Stage 4 Phase 1 successes
    int   stagnationResets;     // how many times stagnation reset fired
    float bestObjFinal;
    float timeUsed;

    DiagCounters() : totalIterations(0), iterBestImproved(0),
                     stage2TotalRepairs(0), stage4Phase1Success(0),
                     stagnationResets(0), bestObjFinal(0.0f),
                     timeUsed(0.0f) {}
};

// ============================================================
// PRINT PROGRESS — called every PRINT_EVERY iterations
// ============================================================
void printProgress(int iter, float bestObj, float elapsed,
                   int pmSize, int s2repairs, int s4phase1) {
    printf("[Iter %4d | Time %6.1fs] Best Z = %8.2f | "
           "PM=%d S2=%d S4p1=%d\n",
           iter, elapsed, bestObj, pmSize, s2repairs, s4phase1);
}

// ============================================================
// PRINT DIAGNOSTIC REPORT — called once at end of run
// ============================================================
void printDiagnosticReport(const DiagCounters& diag,
                           const PairMemory& pm,
                           const TripletMemory& tm,
                           const EliteArchive& ea) {
    printf("\n");
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║         STMO DIAGNOSTIC REPORT               ║\n");
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║ Best Z value         : %8.2f               ║\n", diag.bestObjFinal);
    printf("║ Total iterations     : %4d / %-4d           ║\n",
           diag.totalIterations, MAX_ITER);
    printf("║ Time used            : %6.1fs / %.1fs        ║\n",
           diag.timeUsed, (float)END_TIME);
    printf("║ Best improved at iter: %4d                   ║\n",
           diag.iterBestImproved);
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║ Stage 2 total repairs: %4d                   ║\n",
           diag.stage2TotalRepairs);
    printf("║ Stage 4 Phase1 hits  : %4d                   ║\n",
           diag.stage4Phase1Success);
    printf("║ Stagnation resets    : %4d                   ║\n",
           diag.stagnationResets);
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║ PairMemory entries   : %4d                   ║\n",
           (int)pm.size());
    printf("║ TripletMemory entries: %4d                   ║\n",
           (int)tm.size());
    printf("║ Elite archive size   : %4d / %-4d            ║\n",
           ea.count, E_SIZE);
    printf("╠══════════════════════════════════════════════╣\n");

    // Diagnosis and suggestions
    printf("║ DIAGNOSIS:                                    ║\n");
    if (diag.iterBestImproved == diag.totalIterations) {
        printf("║ ⚠ Best improved at last iter → need more time║\n");
        printf("║   Suggestion: increase END_TIME in config.h  ║\n");
    } else if (diag.iterBestImproved < diag.totalIterations / 5) {
        printf("║ ⚠ Converged early → may be stuck             ║\n");
        printf("║   Suggestion: decrease DF in config.h        ║\n");
    } else {
        printf("║ ✓ Convergence pattern looks healthy          ║\n");
    }

    if (diag.stage2TotalRepairs == 0) {
        printf("║ ⚠ Stage 2 never triggered                    ║\n");
        printf("║   Suggestion: decrease MIN_COUNT in config.h ║\n");
    } else {
        printf("║ ✓ Stage 2 active                             ║\n");
    }

    if (diag.stage4Phase1Success == 0) {
        printf("║ ⚠ Stage 4 Phase 1 never succeeded            ║\n");
        printf("║   Suggestion: check weak pool / pair memory  ║\n");
    } else {
        printf("║ ✓ Stage 4 Phase 1 active                     ║\n");
    }

    if ((int)pm.size() < 5) {
        printf("║ ⚠ PairMemory very small (< 5 entries)        ║\n");
        printf("║   Suggestion: decrease MIN_COUNT in config.h ║\n");
    }

    printf("╚══════════════════════════════════════════════╝\n");
    printf("\n");
}

// ============================================================
// MAIN — STMO Algorithm Entry Point
// ============================================================
int main() {
    // Seed random number generator
    srand((unsigned int)time(NULL));

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║  Sea Turtle Migration Optimization (STMO)    ║\n");
    printf("║  OAS-SDST Problem — Wu et al. (2018)         ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");

    // ── Step 1: Load problem data ─────────────────────────────
    genData();
    printf("[Config] P=%d NC=%d DF=%.2f K_STAG=%d E=%d\n\n",
           P, NC, DF, K_STAG, E_SIZE);

    // ── Step 2: Declare persistent memory structures ──────────
    PairMemory    pairMem;
    TripletMemory tripletMem;
    EliteArchive  eliteArchive;
    M0Pool        m0Pool;
    StructuralMap structMaps[MAX_P]; // ephemeral — rebuilt each iter

    // ── Step 3: Initialize population (all random) ────────────
    Population pop;
    printf("[Init] Initializing %d turtles randomly...\n", P);
    for (int p = 0; p < P; p++) {
        initializeTurtleRandom(pop[p]);
    }
    printf("[Init] Done.\n\n");

    // ── Step 4: Batch Evaluation 1 (startup) ──────────────────
    printf("[BatchEval 1] Evaluating initial population...\n");
    EvalSnapshot snap1 = batchEvaluate(pop, eliteArchive, m0Pool,
                                       0, true, true);

    // Seed PairMemory from startup snapshot
    updatePairMemory(pairMem, snap1);
    labelAllPairs(pairMem);

    // Find initial best
    float bestObj = eliteArchive.count > 0 ? eliteArchive.turtles[0].obj : 0.0f;
    printf("[BatchEval 1] Initial best Z = %.2f | PM entries = %d\n\n",
           bestObj, (int)pairMem.size());

    // ── Step 5: Main iteration loop ───────────────────────────
    DiagCounters diag;
    int  stagnationCounter = 0;
    clock_t startClock     = clock();
    float   elapsed        = 0.0f;

    printf("[Loop] Starting main loop...\n");
    printf("[Loop] Stopping when time >= %.0fs OR iter >= %d\n\n",
           (float)END_TIME, MAX_ITER);

    int iter = 0;
    while (true) {
        iter++;
        elapsed = (float)(clock() - startClock) / CLOCKS_PER_SEC;

        // ── Termination check ─────────────────────────────────
        if (elapsed >= (float)END_TIME) {
            printf("[Stop] Time limit reached (%.1fs)\n", elapsed);
            break;
        }
        if (iter > MAX_ITER) {
            printf("[Stop] Iteration limit reached (%d)\n", MAX_ITER);
            break;
        }

        // ── Stage 1: OceanCurrentDrift ────────────────────────
        stage1_OceanCurrentDrift(pop);

        // ── Stage 2: MemoryAwareDrift ─────────────────────────
        stage2_MemoryAwareDrift(pop, pairMem, m0Pool);

        // ── Batch Evaluation 2 ────────────────────────────────
        EvalSnapshot snap2 = batchEvaluate(pop, eliteArchive, m0Pool,
                                           iter, false, false);

        // ── Stage 3: ACMM ─────────────────────────────────────
        stage3_ACMM(pop, pairMem, tripletMem, snap2, structMaps);

        // ── Stage 4: MFBO (Phase 1 only) ─────────────────────
        stage4_MFBO(pop, structMaps, pairMem);

        // ── Stage 5: CMA ──────────────────────────────────────
        stage5_CMA(pop, eliteArchive, pairMem, tripletMem, m0Pool);

        // ── Batch Evaluation 3 ────────────────────────────────
        EvalSnapshot snap3 = batchEvaluate(pop, eliteArchive, m0Pool,
                                           iter, true, true);

        // ── Track best objective ──────────────────────────────
        float currentBest = eliteArchive.count > 0
                          ? eliteArchive.turtles[0].obj : 0.0f;

        if (currentBest > bestObj) {
            bestObj = currentBest;
            diag.iterBestImproved = iter;
            stagnationCounter = 0;
        } else {
            stagnationCounter++;
        }

        // ── Accumulate diagnostic counters ────────────────────
        for (int p = 0; p < P; p++) {
            diag.stage2TotalRepairs += pop[p].stage2_repairs;
            diag.stage4Phase1Success += pop[p].stage4_phase1;
            // Reset per-turtle counters for next iteration
            pop[p].stage2_repairs = 0;
            pop[p].stage4_phase1  = 0;
        }

        // ── Stagnation check ──────────────────────────────────
        if (stagnationCounter >= K_STAG) {
            stagnationReset(pairMem);
            stagnationCounter = 0;
            diag.stagnationResets++;
        }

        // ── Progress print ────────────────────────────────────
        if (iter % PRINT_EVERY == 0) {
            int s2r  = diag.stage2TotalRepairs;
            int s4p1 = diag.stage4Phase1Success;
            printProgress(iter, bestObj, elapsed,
                         (int)pairMem.size(), s2r, s4p1);
        }
    }

    // ── Step 6: Final result output ───────────────────────────
    elapsed = (float)(clock() - startClock) / CLOCKS_PER_SEC;
    diag.totalIterations = iter - 1;
    diag.bestObjFinal    = bestObj;
    diag.timeUsed        = elapsed;

    printf("\n");
    printf("══════════════════════════════════════════════\n");
    printf("  FINAL RESULT\n");
    printf("══════════════════════════════════════════════\n");
    printf("  Best Z = %.4f\n", bestObj);
    printf("  Total iterations = %d\n", diag.totalIterations);
    printf("  Time = %.2f seconds\n", elapsed);
    printf("══════════════════════════════════════════════\n");

    // Print best turtle's schedule
    if (eliteArchive.count > 0) {
        const Turtle& best = eliteArchive.turtles[0];
        printf("\nBest Schedule:\n");
        for (int m = 0; m < M_Machine; m++) {
            printf("  Machine %d: ", m+1);
            for (int k = 0; k < best.machineCount[m]; k++) {
                int job = best.machineSeq[m][k];
                printf("J%d(C=%.1f) ", job, best.ctimes[job-1]);
            }
            printf("\n");
        }
        printf("  Rejected: ");
        for (int pos = 0; pos < N_Order; pos++) {
            if (best.M_select[pos] == 0)
                printf("J%d ", best.Order_seq[pos]);
        }
        printf("\n");
    }

    // ── Step 7: Diagnostic report ─────────────────────────────
    if (PRINT_DIAG) {
        printDiagnosticReport(diag, pairMem, tripletMem, eliteArchive);
    }

    // ── Step 8: Cleanup ───────────────────────────────────────
    // Free problem data arrays allocated in genData()
    delete[] settime_0;
    delete[] proctime;
    delete[] revenue;
    delete[] penaweight;
    delete[] duedate;
    for (int i = 0; i < N_Order; i++)
        delete[] settime[i];
    delete[] settime;

    return 0;
}
