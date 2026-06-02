// ============================================================
// STMO.cpp — Sea Turtle Migration Optimization
// ============================================================
// Build:
//   g++ -O2 -std=c++11 -static -static-libgcc -static-libstdc++ \
//       -Wl,--stack,8388608 -o STMO.exe STMO.cpp -lm   (Windows/MSYS2)
//   g++ -O2 -std=c++11 -o STMO STMO.cpp -lm            (Linux)
//
// Author : Vimal De Souza | Student 1135446 | Yuan Ze University
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
// Run 003: Global adaptive config variables (set in main after genData)
// ============================================================
int   g_maxIter       = MAX_ITER;            // 50000 ceiling
float g_endTime       = END_TIME;            // 120.0 default
int   g_kStag         = K_STAG_LARGE;        // 50 default
int   g_minCount      = MIN_COUNT_SMALL;     // 3 default
int   g_nc            = NC_BASE;             // 3 default
int   g_machineBudget = MACHINE_BUDGET_BASE; // 3 default
int   g_eSize         = E_SIZE_BASE;         // 5 default

// ============================================================
// DIAGNOSTIC COUNTERS
// ============================================================
struct DiagCounters {
    int   totalIterations;
    int   iterBestImproved;
    int   stage2TotalRepairs;
    int   stage4Phase1Success;
    int   stagnationResets;
    float bestObjFinal;
    float timeUsed;
    DiagCounters() : totalIterations(0), iterBestImproved(0),
                     stage2TotalRepairs(0), stage4Phase1Success(0),
                     stagnationResets(0), bestObjFinal(0.0f), timeUsed(0.0f) {}
};

void printProgress(int iter, float bestObj, float elapsed,
                   int pmSize, int s2repairs, int s4phase1) {
    printf("[Iter %4d | Time %6.1fs] Best Z = %8.2f | PM=%d S2=%d S4p1=%d\n",
           iter, elapsed, bestObj, pmSize, s2repairs, s4phase1);
}

void printDiagnosticReport(const DiagCounters& diag, const PairMemory& pm,
                           const TripletMemory& tm, const EliteArchive& ea) {
    printf("\n");
    printf("==================================================\n");
    printf("            STMO DIAGNOSTIC REPORT\n");
    printf("==================================================\n");
    printf(" Best Z value          : %.2f\n", diag.bestObjFinal);
    printf(" Total iterations      : %d / %d\n", diag.totalIterations, g_maxIter);
    printf(" Time used             : %.1fs / %.1fs\n", diag.timeUsed, (float)g_endTime);
    printf(" Best improved at iter : %d\n", diag.iterBestImproved);
    printf("--------------------------------------------------\n");
    printf(" Stage 2 total repairs : %d\n", diag.stage2TotalRepairs);
    printf(" Stage 4 Phase1 hits   : %d\n", diag.stage4Phase1Success);
    printf(" Stagnation resets     : %d\n", diag.stagnationResets);
    printf("--------------------------------------------------\n");
    printf(" PairMemory entries    : %d\n", (int)pm.size());
    printf(" TripletMemory entries : %d\n", (int)tm.size());
    printf(" Elite archive size    : %d / %d\n", ea.count, g_eSize);
    printf("--------------------------------------------------\n");
    printf(" DIAGNOSIS:\n");
    if (diag.iterBestImproved == diag.totalIterations)
        printf("   [!] Best improved at last iter -> need more time\n");
    else if (diag.iterBestImproved < diag.totalIterations / 5)
        printf("   [!] Converged early -> may be stuck (lower DF)\n");
    else
        printf("   [ok] Convergence pattern looks healthy\n");
    if (diag.stage2TotalRepairs == 0) printf("   [!] Stage 2 never triggered (lower g_minCount)\n");
    else                              printf("   [ok] Stage 2 active\n");
    if (diag.stage4Phase1Success == 0) printf("   [!] Stage 4 Phase 1 never succeeded\n");
    else                               printf("   [ok] Stage 4 Phase 1 active\n");
    if ((int)pm.size() < 5) printf("   [!] PairMemory very small (< 5 entries)\n");
    printf("==================================================\n\n");
}

// ============================================================
// MAIN
// ============================================================
int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    srand((unsigned int)time(NULL));

    printf("==================================================\n");
    printf("  Sea Turtle Migration Optimization (STMO)\n");
    printf("  OAS-SDST Problem - Wu et al. (2018)\n");
    printf("==================================================\n\n");

    genData();

    // ── Run 003 adaptive config (A1-A5) ──────────────────────────
    // A1: MAX_ITER = 50000 for ALL N (time is the sole real stop).
    g_maxIter = 50000;

    if (N_Order <= 50) {
        g_endTime       = 120.0f;
        g_kStag         = K_STAG_SMALL;        // 100
        g_minCount      = MIN_COUNT_SMALL;     // 3
        g_nc            = NC_BASE;             // 3
        g_machineBudget = MACHINE_BUDGET_BASE; // 3
        g_eSize         = E_SIZE_BASE;         // 5
    } else if (N_Order >= 150) {
        g_endTime       = END_TIME_LARGE;      // A2: 240
        g_kStag         = K_STAG_LARGE;        // 50
        g_minCount      = MIN_COUNT_LARGE;     // 1
        g_nc            = NC_LARGE;            // A3: 5
        g_machineBudget = MACHINE_BUDGET_LARGE;// A4: 5
        g_eSize         = E_SIZE_LARGE;        // A5: 10
    } else { // 50 < N < 150
        g_endTime       = 120.0f;
        g_kStag         = K_STAG_LARGE;        // 50
        g_minCount      = MIN_COUNT_SMALL;     // 3
        g_nc            = NC_BASE;             // 3
        g_machineBudget = MACHINE_BUDGET_BASE; // 3
        g_eSize         = E_SIZE_BASE;         // 5
    }

    printf("[Adaptive] N=%d -> MaxIter=%d EndTime=%.0fs KStag=%d MinCount=%d\n",
           N_Order, g_maxIter, g_endTime, g_kStag, g_minCount);
    printf("[Adaptive] NC=%d MachineBudget=%d ESize=%d\n",
           g_nc, g_machineBudget, g_eSize);
    printf("[Config]   P=%d DF=%.2f\n\n", P, DF);
    // ─────────────────────────────────────────────────────────────

    PairMemory    pairMem;
    TripletMemory tripletMem;
    EliteArchive  eliteArchive;
    M0Pool        m0Pool;
    StructuralMap structMaps[MAX_P];

    Population pop;
    printf("[Init] Initializing %d turtles randomly...\n", P);
    for (int p = 0; p < P; p++) initializeTurtleRandom(pop[p]);
    printf("[Init] Done.\n\n");

    printf("[BatchEval 1] Evaluating initial population...\n");
    EvalSnapshot snap1 = batchEvaluate(pop, eliteArchive, m0Pool, 0, true, true);
    updatePairMemory(pairMem, snap1);
    labelAllPairs(pairMem);

    float bestObj = eliteArchive.count > 0 ? eliteArchive.turtles[0].obj : 0.0f;
    printf("[BatchEval 1] Initial best Z = %.2f | PM entries = %d\n\n",
           bestObj, (int)pairMem.size());

    DiagCounters diag;
    int   stagnationCounter = 0;
    clock_t startClock = clock();
    float   elapsed = 0.0f;

    printf("[Loop] Starting main loop...\n");
    printf("[Loop] Stopping when time >= %.0fs OR iter >= %d\n\n",
           (float)g_endTime, g_maxIter);

    int iter = 0;
    while (true) {
        iter++;
        elapsed = (float)(clock() - startClock) / CLOCKS_PER_SEC;
        if (elapsed >= (float)g_endTime) { printf("[Stop] Time limit reached (%.1fs)\n", elapsed); break; }
        if (iter > g_maxIter)            { printf("[Stop] Iteration limit reached (%d)\n", g_maxIter); break; }

        stage1_OceanCurrentDrift(pop);
        stage2_MemoryAwareDrift(pop, pairMem, m0Pool);

        EvalSnapshot snap2 = batchEvaluate(pop, eliteArchive, m0Pool, iter, false, false);
        stage3_ACMM(pop, pairMem, tripletMem, snap2, structMaps);
        stage4_MFBO(pop, structMaps, pairMem);
        stage5_CMA(pop, eliteArchive, pairMem, tripletMem, m0Pool);

        EvalSnapshot snap3 = batchEvaluate(pop, eliteArchive, m0Pool, iter, true, true);

        float currentBest = eliteArchive.count > 0 ? eliteArchive.turtles[0].obj : 0.0f;
        if (currentBest > bestObj) {
            bestObj = currentBest;
            diag.iterBestImproved = iter;
            stagnationCounter = 0;
        } else stagnationCounter++;

        for (int p = 0; p < P; p++) {
            diag.stage2TotalRepairs  += pop[p].stage2_repairs;
            diag.stage4Phase1Success += pop[p].stage4_phase1;
            pop[p].stage2_repairs = 0;
            pop[p].stage4_phase1  = 0;
        }

        if (stagnationCounter >= g_kStag) {
            stagnationReset(pairMem);
            stagnationCounter = 0;
            diag.stagnationResets++;
        }

        if (iter % PRINT_EVERY == 0)
            printProgress(iter, bestObj, elapsed, (int)pairMem.size(),
                          diag.stage2TotalRepairs, diag.stage4Phase1Success);
    }

    elapsed = (float)(clock() - startClock) / CLOCKS_PER_SEC;
    diag.totalIterations = iter - 1;
    diag.bestObjFinal    = bestObj;
    diag.timeUsed        = elapsed;

    printf("\n==================================================\n");
    printf("  FINAL RESULT\n");
    printf("==================================================\n");
    printf("  Best Z = %.4f\n", bestObj);
    printf("  Total iterations = %d\n", diag.totalIterations);
    printf("  Time = %.2f seconds\n", elapsed);
    printf("==================================================\n");

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
        for (int pos = 0; pos < N_Order; pos++)
            if (best.M_select[pos] == 0) printf("J%d ", best.Order_seq[pos]);
        printf("\n");
    }

    if (PRINT_DIAG) printDiagnosticReport(diag, pairMem, tripletMem, eliteArchive);

    delete[] settime_0;
    delete[] proctime;
    delete[] revenue;
    delete[] penaweight;
    delete[] duedate;
    for (int i = 0; i < N_Order; i++) delete[] settime[i];
    delete[] settime;

    return 0;
}
