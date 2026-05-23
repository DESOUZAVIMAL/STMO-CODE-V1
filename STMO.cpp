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
//   cl /EHsc /O2 /std:c++14 /Fe"STMO.exe" STMO.cpp
//
// Run:
//   STMO.exe
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
#include <vector>
#include <string>

// ============================================================
// RESEARCH DATA STRUCTURES
// Collected each iteration; written to out.txt at end of run.
// ============================================================

// One row of the convergence table (captured every PRINT_EVERY iters)
struct IterSnapshot {
    int   iter;
    float elapsed;
    float bestZ;
    float deltaZ;       // change vs previous snapshot
    float s12delta;     // Stage1/2 contribution this iteration
    float s345delta;    // Stage3/4/5 contribution this iteration
    int   pmSize;
    int   pmStrong;
    int   pmWeak;
    int   pmNeutral;
    int   pmCritical;
    int   s2total;      // cumulative Stage 2 repairs so far
    int   s4total;      // cumulative Stage 4 Phase1 hits so far
    int   stagCount;    // stagnation counter at this point
    int   tmSize;       // TripletMemory size
};

// One row of the new-best event log
struct BestEvent {
    int   iter;
    float elapsed;
    float bestZ;
    float deltaZ;
    const char* source; // "Stage1/2", "Stage3/4/5", or "Both"
};

// ============================================================
// DIAGNOSTIC COUNTERS — accumulated across all iterations
// ============================================================
struct DiagCounters {
    int   totalIterations;
    int   iterBestImproved;
    int   stage2TotalRepairs;
    int   stage4Phase1Success;
    int   stagnationResets;
    int   s12wins;        // iterations where Stage1/2 produced new best
    int   s345wins;       // iterations where Stage3/4/5 produced new best
    int   bothWins;       // iterations where both contributed
    float bestObjFinal;
    float timeUsed;

    std::vector<IterSnapshot> snapshots;
    std::vector<BestEvent>    bestEvents;

    DiagCounters() : totalIterations(0), iterBestImproved(0),
                     stage2TotalRepairs(0), stage4Phase1Success(0),
                     stagnationResets(0), s12wins(0), s345wins(0),
                     bothWins(0), bestObjFinal(0.0f), timeUsed(0.0f) {}
};

// ============================================================
// PM LABEL COUNTER — helper for reporting
// ============================================================
struct PMLabels { int strong, weak, neutral, critical, unlabelled; };

PMLabels countPMLabels(const PairMemory& pm) {
    PMLabels c = {0, 0, 0, 0, 0};
    for (const auto& e : pm) {
        char l = e.second.label;
        if      (l == LABEL_STRONG)   c.strong++;
        else if (l == LABEL_WEAK)     c.weak++;
        else if (l == LABEL_NEUTRAL)  c.neutral++;
        else if (l == LABEL_CRITICAL) c.critical++;
        else                          c.unlabelled++;
    }
    return c;
}

// ============================================================
// PRINT PROGRESS — console, called every PRINT_EVERY iterations
// ============================================================
void printProgress(int iter, float bestObj, float elapsed,
                   int pmSize, int s2repairs, int s4phase1) {
    printf("[Iter %4d | Time %6.1fs] Best Z = %8.2f | "
           "PM=%d S2=%d S4p1=%d\n",
           iter, elapsed, bestObj, pmSize, s2repairs, s4phase1);
}

// ============================================================
// PRINT DETAIL — console, called every 50 iterations
// Shows PM label breakdown and stagnation counter
// ============================================================
void printDetail(int iter, float elapsed, float bestZ,
                 const PMLabels& lbl, int stagCount, int tmSize) {
    printf("  [Detail @%4d | %.1fs] PM: %dStr %dWk %dNeu %dCrit | "
           "Triplets=%d | Stag=%d\n",
           iter, elapsed,
           lbl.strong, lbl.weak, lbl.neutral, lbl.critical,
           tmSize, stagCount);
}

// ============================================================
// WRITE RESEARCH REPORT — full structured output to out.txt
// Called once at end of run.
// ============================================================
void writeResearchReport(const DiagCounters& diag,
                         const PairMemory& pm,
                         const TripletMemory& tm,
                         const EliteArchive& ea) {
    FILE* f = fopen("out.txt", "w");
    if (!f) {
        printf("[Report] ERROR: cannot open out.txt for writing\n");
        return;
    }

    // ── Header ───────────────────────────────────────────────
    fprintf(f, "========================================================\n");
    fprintf(f, "  STMO Research Report\n");
    fprintf(f, "  Sea Turtle Migration Optimization\n");
    fprintf(f, "  OAS-SDST — Wu et al. (2018) Applied Soft Computing\n");
    fprintf(f, "========================================================\n\n");

    // ── Configuration ─────────────────────────────────────────
    fprintf(f, "[Configuration]\n");
    fprintf(f, "  Problem       : N=%d orders, M=%d machines, Seed=%d\n",
            N_Order, M_Machine, Seed);
    fprintf(f, "  Population    : P=%d turtles, NC=%d candidates/turtle\n",
            P, NC);
    fprintf(f, "  Stopping      : END_TIME=%.0fs, MAX_ITER=%d\n",
            (float)END_TIME, MAX_ITER);
    fprintf(f, "  PairMemory    : DF=%.2f, MIN_COUNT=%d, GHOST=%.3f\n",
            DF, MIN_COUNT, GHOST_THRESHOLD);
    fprintf(f, "  Labels        : STRONG_mult=%.1f, WEAK_mult=%.1f\n",
            STRONG_SIGMA_MULT, WEAK_SIGMA_MULT);
    fprintf(f, "  Stage2        : MACHINE_BUDGET=%d, MAX_ATTEMPTS=%d\n",
            MACHINE_BUDGET, MAX_ATTEMPTS);
    fprintf(f, "  Stage4/5      : K_STAG=%d, E_SIZE=%d\n\n",
            K_STAG, E_SIZE);

    // ── Final Summary ─────────────────────────────────────────
    fprintf(f, "[Final Summary]\n");
    fprintf(f, "  Best Z          : %.4f\n", diag.bestObjFinal);
    fprintf(f, "  Total iters     : %d / %d\n",
            diag.totalIterations, MAX_ITER);
    fprintf(f, "  Time used       : %.2f s / %.0f s\n",
            diag.timeUsed, (float)END_TIME);
    fprintf(f, "  Best at iter    : %d (%.1f%% of run)\n",
            diag.iterBestImproved,
            diag.totalIterations > 0
                ? 100.0f * diag.iterBestImproved / diag.totalIterations
                : 0.0f);
    fprintf(f, "  New-best events : %d\n", (int)diag.bestEvents.size());
    fprintf(f, "  Stagnation resets: %d\n\n", diag.stagnationResets);

    // ── Phase Attribution ─────────────────────────────────────
    int totalWins = diag.s12wins + diag.s345wins + diag.bothWins;
    fprintf(f, "[Phase Attribution — which stages produced new bests]\n");
    fprintf(f, "  Stage 1/2   (Exploration + Repair) : %d events (%.1f%%)\n",
            diag.s12wins,
            totalWins > 0 ? 100.0f * diag.s12wins / totalWins : 0.0f);
    fprintf(f, "  Stage 3/4/5 (Memory + Elite)       : %d events (%.1f%%)\n",
            diag.s345wins,
            totalWins > 0 ? 100.0f * diag.s345wins / totalWins : 0.0f);
    fprintf(f, "  Both contributed same iteration    : %d events (%.1f%%)\n",
            diag.bothWins,
            totalWins > 0 ? 100.0f * diag.bothWins / totalWins : 0.0f);
    fprintf(f, "\n");

    // ── Stage Activity ─────────────────────────────────────────
    fprintf(f, "[Stage Activity Summary]\n");
    fprintf(f, "  Stage 2 total repairs : %d\n", diag.stage2TotalRepairs);
    fprintf(f, "  Stage 4 Phase1 hits   : %d\n", diag.stage4Phase1Success);
    fprintf(f, "\n");

    // ── PairMemory Final Distribution ─────────────────────────
    PMLabels lbl = countPMLabels(pm);
    int total = (int)pm.size();
    float pct = total > 0 ? 100.0f / total : 0.0f;
    fprintf(f, "[PairMemory Final Label Distribution]\n");
    fprintf(f, "  Total entries  : %d\n", total);
    fprintf(f, "  STRONG         : %d (%.1f%%)\n",
            lbl.strong,   lbl.strong   * pct);
    fprintf(f, "  NEUTRAL        : %d (%.1f%%)\n",
            lbl.neutral,  lbl.neutral  * pct);
    fprintf(f, "  WEAK           : %d (%.1f%%)\n",
            lbl.weak,     lbl.weak     * pct);
    fprintf(f, "  CRITICAL       : %d (%.1f%%)\n",
            lbl.critical, lbl.critical * pct);
    fprintf(f, "  Unlabelled     : %d (%.1f%%)\n",
            lbl.unlabelled, lbl.unlabelled * pct);
    fprintf(f, "  TripletMemory  : %d entries\n", (int)tm.size());
    fprintf(f, "  Elite archive  : %d / %d\n\n", ea.count, E_SIZE);

    // ── Diagnosis ─────────────────────────────────────────────
    fprintf(f, "[Diagnosis]\n");
    if (diag.iterBestImproved == diag.totalIterations)
        fprintf(f, "  !! Best improved at LAST iter — still converging, increase MAX_ITER/END_TIME\n");
    else if (diag.iterBestImproved < diag.totalIterations / 5)
        fprintf(f, "  !! Converged early (iter %d of %d) — try decreasing DF for faster exploration\n",
                diag.iterBestImproved, diag.totalIterations);
    else
        fprintf(f, "  OK Convergence looks healthy (best at iter %d of %d)\n",
                diag.iterBestImproved, diag.totalIterations);

    if (diag.stage2TotalRepairs == 0)
        fprintf(f, "  !! Stage 2 never triggered — decrease MIN_COUNT\n");
    else
        fprintf(f, "  OK Stage 2 active (%d repairs)\n", diag.stage2TotalRepairs);

    if (diag.stage4Phase1Success == 0)
        fprintf(f, "  !! Stage 4 Phase1 never triggered — check PairMemory STRONG count\n");
    else
        fprintf(f, "  OK Stage 4 Phase1 active (%d hits)\n", diag.stage4Phase1Success);

    if (lbl.strong < 5)
        fprintf(f, "  !! Very few STRONG pairs (%d) — try decreasing STRONG_SIGMA_MULT\n",
                lbl.strong);
    fprintf(f, "\n");

    // ── New Best Event Log ─────────────────────────────────────
    fprintf(f, "[New Best Events — logged every time Best Z improved]\n");
    fprintf(f, "%-6s %-9s %-13s %-13s %-14s\n",
            "Iter", "Time(s)", "BestZ", "DeltaZ", "Source");
    fprintf(f, "%-6s %-9s %-13s %-13s %-14s\n",
            "------", "---------", "-------------", "-------------", "--------------");
    for (const auto& e : diag.bestEvents) {
        fprintf(f, "%-6d %-9.2f %-13.2f %-13.2f %-14s\n",
                e.iter, e.elapsed, e.bestZ, e.deltaZ, e.source);
    }
    fprintf(f, "\n");

    // ── Convergence Table (human-readable) ────────────────────
    fprintf(f, "[Convergence Table — every %d iterations]\n", PRINT_EVERY);
    fprintf(f,
        "%-6s %-8s %-12s %-10s %-9s %-9s "
        "%-7s %-6s %-5s %-5s %-5s %-7s %-7s %-5s %-5s\n",
        "Iter", "Time(s)", "BestZ", "Delta",
        "S12delta", "S345delta",
        "PM_All", "Str", "Wk", "Neu", "Crit",
        "S2_Rep", "S4_Ph1", "Stag", "Trip");
    fprintf(f,
        "%-6s %-8s %-12s %-10s %-9s %-9s "
        "%-7s %-6s %-5s %-5s %-5s %-7s %-7s %-5s %-5s\n",
        "------", "--------", "------------", "----------",
        "---------", "---------",
        "-------", "------", "-----", "-----", "-----",
        "-------", "-------", "-----", "-----");
    for (const auto& s : diag.snapshots) {
        fprintf(f,
            "%-6d %-8.2f %-12.2f %-10.2f %-9.2f %-9.2f "
            "%-7d %-6d %-5d %-5d %-5d %-7d %-7d %-5d %-5d\n",
            s.iter, s.elapsed, s.bestZ, s.deltaZ,
            s.s12delta, s.s345delta,
            s.pmSize, s.pmStrong, s.pmWeak, s.pmNeutral, s.pmCritical,
            s.s2total, s.s4total, s.stagCount, s.tmSize);
    }
    fprintf(f, "\n");

    // ── CSV Section — for plotting in Excel/Python/MATLAB ─────
    fprintf(f, "[CSV — Convergence Data for Plotting]\n");
    fprintf(f, "iter,time,bestZ,deltaZ,s12delta,s345delta,"
               "pmAll,pmStrong,pmWeak,pmNeutral,pmCritical,"
               "s2repairs,s4phase1,stagCount,triplets\n");
    for (const auto& s : diag.snapshots) {
        fprintf(f, "%d,%.2f,%.2f,%.2f,%.2f,%.2f,"
                   "%d,%d,%d,%d,%d,"
                   "%d,%d,%d,%d\n",
                s.iter, s.elapsed, s.bestZ, s.deltaZ,
                s.s12delta, s.s345delta,
                s.pmSize, s.pmStrong, s.pmWeak, s.pmNeutral, s.pmCritical,
                s.s2total, s.s4total, s.stagCount, s.tmSize);
    }
    fprintf(f, "\n");

    // ── Best Schedule ─────────────────────────────────────────
    fprintf(f, "[Best Schedule]\n");
    if (ea.count > 0) {
        const Turtle& best = ea.turtles[0];
        for (int m = 0; m < M_Machine; m++) {
            fprintf(f, "  Machine %d: ", m + 1);
            for (int k = 0; k < best.machineCount[m]; k++) {
                int job = best.machineSeq[m][k];
                fprintf(f, "J%d(C=%.1f) ", job, best.ctimes[job - 1]);
            }
            fprintf(f, "\n");
        }
        fprintf(f, "  Rejected: ");
        for (int pos = 0; pos < N_Order; pos++) {
            if (best.M_select[pos] == 0)
                fprintf(f, "J%d ", best.Order_seq[pos]);
        }
        fprintf(f, "\n");
    }

    fclose(f);
    printf("[Report] Research report written to out.txt\n");
}

// ============================================================
// PRINT DIAGNOSTIC REPORT — console summary at end of run
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
    printf("║ Phase Attribution:                            ║\n");
    printf("║   Stage1/2 wins : %4d                        ║\n", diag.s12wins);
    printf("║   Stage3/4/5 wins: %4d                       ║\n", diag.s345wins);
    printf("║   Both contributed: %4d                      ║\n", diag.bothWins);
    printf("╠══════════════════════════════════════════════╣\n");

    printf("║ DIAGNOSIS:                                    ║\n");
    if (diag.iterBestImproved == diag.totalIterations) {
        printf("║ !! Best improved at last iter → need more    ║\n");
        printf("║    time: increase END_TIME in config.h       ║\n");
    } else if (diag.iterBestImproved < diag.totalIterations / 5) {
        printf("║ !! Converged early → may be stuck            ║\n");
        printf("║    Try: decrease DF in config.h              ║\n");
    } else {
        printf("║ OK Convergence pattern looks healthy         ║\n");
    }

    if (diag.stage2TotalRepairs == 0)
        printf("║ !! Stage 2 never triggered                   ║\n");
    else
        printf("║ OK Stage 2 active                            ║\n");

    if (diag.stage4Phase1Success == 0)
        printf("║ !! Stage 4 Phase 1 never succeeded           ║\n");
    else
        printf("║ OK Stage 4 Phase 1 active                    ║\n");

    printf("╚══════════════════════════════════════════════╝\n\n");
}

// ============================================================
// MAIN — STMO Algorithm Entry Point
// ============================================================
int main() {
    srand((unsigned int)time(NULL));
    setvbuf(stdout, NULL, _IONBF, 0); // unbuffered — every printf appears immediately

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║  Sea Turtle Migration Optimization (STMO)    ║\n");
    printf("║  OAS-SDST Problem — Wu et al. (2018)         ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");

    // ── Step 1: Load problem data ─────────────────────────────
    genData();
    printf("[Config] P=%d NC=%d DF=%.2f K_STAG=%d E=%d MAX_ITER=%d\n\n",
           P, NC, DF, K_STAG, E_SIZE, MAX_ITER);

    // ── Step 2: Declare persistent memory structures ──────────
    PairMemory    pairMem;
    TripletMemory tripletMem;
    static EliteArchive  eliteArchive;
    M0Pool        m0Pool;
    static StructuralMap structMaps[MAX_P];

    // ── Step 3: Initialize population ─────────────────────────
    static Population pop;
    printf("[Init] Initializing %d turtles randomly...\n", P);
    for (int p = 0; p < P; p++) {
        initializeTurtleRandom(pop[p]);
    }
    printf("[Init] Done.\n\n");

    // ── Step 4: Batch Evaluation 1 (startup) ──────────────────
    printf("[BatchEval 1] Evaluating initial population...\n");
    EvalSnapshot snap1 = batchEvaluate(pop, eliteArchive, m0Pool,
                                       0, true, true);
    updatePairMemory(pairMem, snap1);
    labelAllPairs(pairMem);

    float bestObj = eliteArchive.count > 0 ? eliteArchive.turtles[0].obj : 0.0f;
    printf("[BatchEval 1] Initial best Z = %.2f | PM entries = %d\n\n",
           bestObj, (int)pairMem.size());

    // ── Step 5: Main iteration loop ───────────────────────────
    DiagCounters diag;
    int   stagnationCounter = 0;
    clock_t startClock      = clock();
    float   elapsed         = 0.0f;

    // Cumulative stage counters (for snapshots)
    int cumS2repairs = 0;
    int cumS4phase1  = 0;
    float prevSnapZ  = bestObj; // for deltaZ in snapshot

    auto timeUp = [&]() -> bool {
        elapsed = (float)(clock() - startClock) / CLOCKS_PER_SEC;
        return elapsed >= (float)END_TIME;
    };

    printf("[Loop] Starting main loop...\n");
    printf("[Loop] Stopping when time >= %.0fs OR iter >= %d\n\n",
           (float)END_TIME, MAX_ITER);
    printf("%-50s  (detailed PM breakdown every 50 iters)\n",
           "[** NEW BEST **] lines appear whenever best Z improves");

    int iter = 0;
    while (true) {
        iter++;
        elapsed = (float)(clock() - startClock) / CLOCKS_PER_SEC;

        if (elapsed >= (float)END_TIME) {
            printf("[Stop] Time limit reached (%.1fs)\n", elapsed);
            break;
        }
        if (iter > MAX_ITER) {
            printf("[Stop] Iteration limit reached (%d)\n", MAX_ITER);
            break;
        }

        // Record best before this iteration for phase attribution
        float preIterBest = bestObj;

        // ── Stage 1: OceanCurrentDrift ────────────────────────
        stage1_OceanCurrentDrift(pop);
        if (timeUp()) { printf("[Stop] Time limit (%.1fs) after Stage 1\n", elapsed); break; }

        // ── Stage 2: MemoryAwareDrift ─────────────────────────
        stage2_MemoryAwareDrift(pop, pairMem, m0Pool);
        if (timeUp()) { printf("[Stop] Time limit (%.1fs) after Stage 2\n", elapsed); break; }

        // ── Batch Evaluation 2 ────────────────────────────────
        EvalSnapshot snap2 = batchEvaluate(pop, eliteArchive, m0Pool,
                                           iter, false, false);
        if (timeUp()) { printf("[Stop] Time limit (%.1fs) after BatchEval2\n", elapsed); break; }

        // Measure Stage1/2 population-level contribution
        float s12PopBest = preIterBest;
        for (int p = 0; p < P; p++) {
            if (pop[p].cacheValid && pop[p].obj > s12PopBest)
                s12PopBest = pop[p].obj;
        }
        float s12delta = s12PopBest - preIterBest; // 0 if no improvement

        // ── Stage 3: ACMM ─────────────────────────────────────
        stage3_ACMM(pop, pairMem, tripletMem, snap2, structMaps);
        if (timeUp()) { printf("[Stop] Time limit (%.1fs) after Stage 3\n", elapsed); break; }

        // ── Stage 4: MFBO ─────────────────────────────────────
        stage4_MFBO(pop, structMaps, pairMem);
        if (timeUp()) { printf("[Stop] Time limit (%.1fs) after Stage 4\n", elapsed); break; }

        // ── Stage 5: CMA ──────────────────────────────────────
        stage5_CMA(pop, eliteArchive, pairMem, tripletMem, m0Pool);
        if (timeUp()) { printf("[Stop] Time limit (%.1fs) after Stage 5\n", elapsed); break; }

        // ── Batch Evaluation 3 ────────────────────────────────
        EvalSnapshot snap3 = batchEvaluate(pop, eliteArchive, m0Pool,
                                           iter, true, true);

        // Measure Stage3/4/5 population-level contribution
        float s345PopBest = s12PopBest;
        for (int p = 0; p < P; p++) {
            if (pop[p].cacheValid && pop[p].obj > s345PopBest)
                s345PopBest = pop[p].obj;
        }
        float s345delta = s345PopBest - s12PopBest;

        // ── Track best objective ──────────────────────────────
        float currentBest = eliteArchive.count > 0
                          ? eliteArchive.turtles[0].obj : 0.0f;

        if (currentBest > bestObj) {
            float delta = currentBest - bestObj;
            bestObj = currentBest;
            diag.iterBestImproved = iter;
            stagnationCounter = 0;

            // Attribute source
            bool s12contrib  = (s12delta  > 0.001f);
            bool s345contrib = (s345delta > 0.001f);
            const char* src;
            if (s12contrib && s345contrib) { src = "Both";       diag.bothWins++; }
            else if (s12contrib)           { src = "Stage1/2";   diag.s12wins++;  }
            else                           { src = "Stage3/4/5"; diag.s345wins++; }

            // Console new-best line
            printf("[** NEW BEST **] Iter %4d | %.1fs | Z = %8.2f | "
                   "delta = %+.2f | %s\n",
                   iter, elapsed, bestObj, delta, src);

            // Record event
            BestEvent ev;
            ev.iter    = iter;
            ev.elapsed = elapsed;
            ev.bestZ   = bestObj;
            ev.deltaZ  = delta;
            ev.source  = src;
            diag.bestEvents.push_back(ev);
        } else {
            stagnationCounter++;
        }

        // ── Accumulate per-turtle counters ────────────────────
        for (int p = 0; p < P; p++) {
            cumS2repairs        += pop[p].stage2_repairs;
            cumS4phase1         += pop[p].stage4_phase1;
            diag.stage2TotalRepairs += pop[p].stage2_repairs;
            diag.stage4Phase1Success += pop[p].stage4_phase1;
            pop[p].stage2_repairs = 0;
            pop[p].stage4_phase1  = 0;
        }

        // ── Stagnation check ──────────────────────────────────
        if (stagnationCounter >= K_STAG) {
            stagnationReset(pairMem);
            stagnationCounter = 0;
            diag.stagnationResets++;
        }

        // ── Capture snapshot every PRINT_EVERY iters ─────────
        if (iter % PRINT_EVERY == 0) {
            PMLabels lbl = countPMLabels(pairMem);

            printProgress(iter, bestObj, elapsed,
                         (int)pairMem.size(), cumS2repairs, cumS4phase1);

            // Every 50 iters: detailed PM breakdown on console
            if (iter % 50 == 0) {
                printDetail(iter, elapsed, bestObj, lbl,
                            stagnationCounter, (int)tripletMem.size());
            }

            // Record research snapshot
            IterSnapshot snap;
            snap.iter       = iter;
            snap.elapsed    = elapsed;
            snap.bestZ      = bestObj;
            snap.deltaZ     = bestObj - prevSnapZ;
            snap.s12delta   = s12delta;
            snap.s345delta  = s345delta;
            snap.pmSize     = (int)pairMem.size();
            snap.pmStrong   = lbl.strong;
            snap.pmWeak     = lbl.weak;
            snap.pmNeutral  = lbl.neutral;
            snap.pmCritical = lbl.critical;
            snap.s2total    = cumS2repairs;
            snap.s4total    = cumS4phase1;
            snap.stagCount  = stagnationCounter;
            snap.tmSize     = (int)tripletMem.size();
            diag.snapshots.push_back(snap);

            prevSnapZ = bestObj;
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

    if (eliteArchive.count > 0) {
        const Turtle& best = eliteArchive.turtles[0];
        printf("\nBest Schedule:\n");
        for (int m = 0; m < M_Machine; m++) {
            printf("  Machine %d: ", m + 1);
            for (int k = 0; k < best.machineCount[m]; k++) {
                int job = best.machineSeq[m][k];
                printf("J%d(C=%.1f) ", job, best.ctimes[job - 1]);
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

    // ── Step 7: Reports ───────────────────────────────────────
    if (PRINT_DIAG) {
        printDiagnosticReport(diag, pairMem, tripletMem, eliteArchive);
    }
    writeResearchReport(diag, pairMem, tripletMem, eliteArchive);

    // ── Step 8: Cleanup ───────────────────────────────────────
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
