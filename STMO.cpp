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
#include "diag.h"
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
// Run 7: Productivity counters + global-best tracker
// ============================================================
long  g_s1_acc=0,  g_s1_gbest=0;
long  g_s2_acc=0,  g_s2_strict=0,  g_s2_gbest=0;
long  g_s4_acc=0,  g_s4_gbest=0;
long  g_s5_acc=0,  g_s5_gbest=0;
int   g_restartCount=0;
int   g_ccrit=0;
int   g_cweak=0;
float g_globalBest=0.0f;

// Run009 passive diagnostic accumulators (fed by DIAG_PROPOSE/DIAG_ACCEPT).
#if DIAG_MODE
double g_diag_acc_dz[6]   = {0,0,0,0,0,0};
long   g_diag_acc_n[6]    = {0,0,0,0,0,0};
long   g_diag_prop_n[6]   = {0,0,0,0,0,0};
long   g_diag_ch_prop[6]  = {0,0,0,0,0,0};
#endif

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
    printf("[Iter %4d | Time %6.1fs] Best Z = %8.2f | PM=%d%s Ccrit=%d Cweak=%d restarts=%d S2=%d S4p1=%d"
           " | S1acc=%ld S1g=%ld S2acc=%ld S2s=%ld S2g=%ld S4acc=%ld S4g=%ld S5acc=%ld S5g=%ld\n",
           iter, elapsed, bestObj, pmSize,
           (pmSize > PM_WARN_SIZE ? " [PM_WARN]" : ""),
           g_ccrit, g_cweak, g_restartCount, s2repairs, s4phase1,
           g_s1_acc, g_s1_gbest, g_s2_acc, g_s2_strict, g_s2_gbest,
           g_s4_acc, g_s4_gbest, g_s5_acc, g_s5_gbest);
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
    printf(" Convergence restarts  : %d\n", g_restartCount);
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
    printf("--------------------------------------------------\n");
    printf(" [STAGE PRODUCTIVITY]\n");
    printf(" S1: accepted=%-8ld  globalBest=%ld\n",                 g_s1_acc, g_s1_gbest);
    printf(" S2: accepted=%-8ld  strict=%-8ld  globalBest=%ld\n",  g_s2_acc, g_s2_strict, g_s2_gbest);
    printf(" S4: accepted=%-8ld  globalBest=%ld\n",                 g_s4_acc, g_s4_gbest);
    printf(" S5: accepted=%-8ld  globalBest=%ld\n",                 g_s5_acc, g_s5_gbest);
    printf(" Restarts: %d\n", g_restartCount);
    printf("==================================================\n\n");
}

// ============================================================
// MAIN
// ============================================================
int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    srand((unsigned int)time(NULL));   // NOTE: overwritten by srand(Seed) in genData() — kept unchanged

    // Run009 repeat index (1-based) for the reproducibility probe. From argv[1]
    // if given, else the DIAG_REPEAT env var, else 1. Purely a logging tag — it
    // does NOT touch the RNG or any algorithm state.
    int g_diagRepeat = 1;
    if (argc > 1) g_diagRepeat = atoi(argv[1]);
    else { const char* e = getenv("DIAG_REPEAT"); if (e) g_diagRepeat = atoi(e); }
    (void)g_diagRepeat;

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

    // ── Run009 run config (applies in BOTH DIAG modes so the §7 gate
    // compares like-for-like): iteration cap is the primary stop, 600 s
    // is the safety ceiling, uniform across all N. Algorithm logic, RNG,
    // and operators are untouched — only the stopping bounds change. ──
    g_maxIter = MAX_ITERATIONS;     // 30000
    g_endTime = DIAG_END_TIME;      // 600.0
    printf("[Run009]   MaxIter=%d EndTime=%.0fs DIAG_MODE=%d\n\n",
           g_maxIter, g_endTime, DIAG_MODE);

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
    g_globalBest = bestObj;
    printf("[BatchEval 1] Initial best Z = %.2f | PM entries = %d\n\n",
           bestObj, (int)pairMem.size());

#if DIAG_MODE
    diag::begin(N_Order, M_Machine, Seed, g_diagRepeat);
    Turtle diagPrevBest;                       // previous global-best, for best_events deltas
    if (eliteArchive.count > 0) diagPrevBest = eliteArchive.turtles[0];
    // iter-0 checkpoint: dump the initial population + first scalar rows.
    diag::iterEnd(0, 0.0f, pop, eliteArchive, pairMem, m0Pool, bestObj);
#endif

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
        bool bestImproved = (currentBest > bestObj);
        if (bestImproved) {
#if DIAG_MODE
            if (eliteArchive.count > 0)
                diag::bestEvent(iter, eliteArchive.turtles[0], diagPrevBest,
                                bestObj, currentBest, pairMem);
#endif
            bestObj = currentBest;
            g_globalBest = bestObj;
            diag.iterBestImproved = iter;
            stagnationCounter = 0;
#if DIAG_MODE
            if (eliteArchive.count > 0) diagPrevBest = eliteArchive.turtles[0];
#endif
        } else {
            stagnationCounter++;
        }

        for (int p = 0; p < P; p++) {
            diag.stage2TotalRepairs  += pop[p].stage2_repairs;
            diag.stage4Phase1Success += pop[p].stage4_phase1;
            pop[p].stage2_repairs = 0;
            pop[p].stage4_phase1  = 0;
        }

        // Run 7: convergence-triggered restart (replaces stagnationReset)
        if (!bestImproved
                && stagnationCounter >= RESTART_STAG
                && g_ccrit == 0
                && elapsed < 0.80f * (float)g_endTime) {
            convergenceRestart(pop, P, pairMem);
            stagnationCounter = 0;
            g_restartCount++;
            printf("[Restart] iter=%d restart=#%d perturbed %d/%d turtles\n",
                   iter, g_restartCount, P/2, P);
        }

        if (iter % PRINT_EVERY == 0) {
            printProgress(iter, bestObj, elapsed, (int)pairMem.size(),
                          diag.stage2TotalRepairs, diag.stage4Phase1Success);
        }

#if DIAG_MODE
        // Passive end-of-iteration logging. Reads existing state only.
        diag::iterEnd(iter, elapsed, pop, eliteArchive, pairMem, m0Pool, bestObj);
#endif
    }

    elapsed = (float)(clock() - startClock) / CLOCKS_PER_SEC;
    diag.totalIterations = iter - 1;
    diag.timeUsed        = elapsed;

    // ── Final-best feasibility gate (compiled into the official build too) ──
    // Re-decode the reported best from its encoding and HARD-STOP if it is
    // infeasible: never emit a BestZ from an invalid solution. Report
    // best.obj recomputed here — not any stale cached bestObj.
    Turtle best;
    bool haveBest = (eliteArchive.count > 0);
    if (haveBest) {
        best = eliteArchive.turtles[0];
        decodeAndEval(best);                 // recompute objective from the encoding
        if (!validateTurtle(best)) {         // REAL check — runs in the official build too
            fprintf(stderr, "ERROR: final best failed feasibility validation; aborting run.\n");
            return 2;                        // never report a BestZ from an infeasible solution
        }
        ASSERT_VALID(best);                  // also hard-stops in the debug build
        bestObj = best.obj;                  // report THIS value, not a stale bestObj
    }
    diag.bestObjFinal = bestObj;

#if DIAG_MODE
    // Final population dump + runtime/summary/code-health + reproducibility row.
    diag::end(diag.totalIterations, elapsed, pop, eliteArchive,
              pairMem, tripletMem, m0Pool, bestObj);
#endif

    printf("\n==================================================\n");
    printf("  FINAL RESULT\n");
    printf("==================================================\n");
    printf("  Best Z = %.4f\n", bestObj);
    printf("  Total iterations = %d\n", diag.totalIterations);
    printf("  Time = %.2f seconds\n", elapsed);
    printf("==================================================\n");

    if (haveBest) {
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
