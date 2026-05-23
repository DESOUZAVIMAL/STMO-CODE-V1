// ============================================================
// problem.h — Problem Infrastructure
// ============================================================
// Contains ONLY problem-level functions adapted from professor's
// WFA source code (test1_case4.cpp).
//
// What is kept from professor's code (adapted):
//   GenData()       → reads param.txt, generates data files
//   CalObj()        → renamed decodeAndEval()
//   CalObj_single() → renamed partialReeval()
//   bubblesort()    → renamed sortByKeys()
//   Neighborhood1-4 → renamed move functions, return copies
//
// What is NEW (not in professor's code):
//   calcPairScore()         → pair scoring formula (Eq. 4.1 thesis)
//   initializeTurtleRandom()→ random turtle init (calls decodeAndEval)
//   generateRandomMove()    → Stage 1 move dispatcher
//   moveCombo()             → combo operator (Insert + Revised)
//
// What is REMOVED from professor's code:
//   All WFA structures (Curr[600], SubFlow, Neigh, W_0, velocity_0)
//   All WFA functions (Split_Flow, Merg_Flow, Flow_eva, Rainfall_*)
//   EDD initialization (removed per professor instruction)
//   All global algorithm state
//
// SE RULES:
//   decodeAndEval() is a PURE function — no side effects beyond turtle
//   All move functions return COPIES — never mutate original turtle
//   Problem data arrays are READ-ONLY after genData() completes
//   cacheValid set TRUE only by decodeAndEval()
//   cacheValid set FALSE by every move function on returned copy
// ============================================================

#ifndef PROBLEM_H
#define PROBLEM_H

#include "config.h"
#include "stmo_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <algorithm>
#include <vector>
#include <utility>

// ============================================================
// PROBLEM DATA — READ-ONLY after genData() completes
// SE RULE: no stage or memory function may modify these arrays
// ============================================================

// Problem dimensions (set by genData, never changed after)
int N_Order   = 0;    // number of orders/jobs
int M_Machine = 0;    // number of machines
int Seed      = 0;    // random seed

// Parameter bounds (read from param.txt)
float Set_0_LB, Set_0_UB;   // initial setup time bounds
float Set_LB,   Set_UB;     // sequence-dependent setup time bounds
float Proc_LB,  Proc_UB;    // processing time bounds
float Reve_LB,  Reve_UB;    // revenue bounds
float Pena_LB,  Pena_UB;    // penalty weight bounds
float Due_LB,   Due_UB;     // due date bounds
float END_TIME_param;        // time budget from param.txt (use END_TIME from config.h)

// Problem data arrays (allocated in genData)
float*  settime_0;           // initial setup time s_0i for each job
float** settime;             // setup time matrix s_ij[i][j]
float*  proctime;            // processing time p_i for each job
float*  revenue;             // revenue r_i for each job
float*  penaweight;          // penalty weight w_i for each job
float*  duedate;             // due date d_i for each job

// Timing
clock_t start_time_clock;

// ============================================================
// UTILITY FUNCTIONS
// ============================================================

// Timing function (identical to professor's code)
double timing(clock_t start_t) {
    return (double)(clock() - start_t) / CLOCKS_PER_SEC;
}

// Random float in [0, 1]
float RandomF() {
    return (float)1.0 / (float)(RAND_MAX) * (float)rand();
}

// Random float in [LB, UB]
float RandomRge(float LB, float UB) {
    return LB + (UB - LB) / (RAND_MAX) * (float)rand();
}

// ============================================================
// SORT — adapted from professor's bubblesort
// Sorts Order_seq and M_select arrays by their float key arrays.
// Used during initialization to decode random float keys into
// a valid job sequence and machine assignment.
// ============================================================
void sortByKeys(int* order, float* orderKeys,
                int* mach,  float* machKeys,
                int n) {
    for (int a = 0; a < n - 1; a++) {
        for (int b = a + 1; b < n; b++) {
            if (orderKeys[a] > orderKeys[b]) {
                // Swap order keys
                float tmpf = orderKeys[a];
                orderKeys[a] = orderKeys[b];
                orderKeys[b] = tmpf;
                // Swap order sequence
                int tmpi = order[a];
                order[a] = order[b];
                order[b] = tmpi;
                // Swap machine keys
                tmpf = machKeys[a];
                machKeys[a] = machKeys[b];
                machKeys[b] = tmpf;
                // Swap machine assignment
                tmpi = mach[a];
                mach[a] = mach[b];
                mach[b] = tmpi;
            }
        }
    }
}

// ============================================================
// GEN DATA — identical to professor's GenData()
// Reads param.txt, generates all problem data files,
// then reads them back into the problem data arrays.
// SE RULE: called ONCE at program start. Never called again.
// ============================================================
void genData() {
    char buf[10], line[150], dis[11];
    int count_dis;
    FILE *fp1, *fp2, *fp3, *fp4, *fp5, *fp6, *param;

    // Read param.txt
    param = fopen("param.txt", "r");
    if (!param) {
        printf("ERROR: Cannot open param.txt\n");
        exit(1);
    }
    if (fgets(line, 150, param) == NULL) {
        printf("ERROR: Failed to read param.txt\n");
        exit(1);
    }

    // Parse param.txt (same byte offsets as professor's code)
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i-1];   N_Order   = atoi(buf);
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i+5];   M_Machine = atoi(buf);
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i+11];  Seed      = atoi(buf);
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i+17];  Set_0_LB  = (float)atof(buf);
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i+23];  Set_0_UB  = (float)atof(buf);
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i+29];  Set_LB    = (float)atof(buf);
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i+35];  Set_UB    = (float)atof(buf);
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i+41];  Proc_LB   = (float)atof(buf);
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i+47];  Proc_UB   = (float)atof(buf);
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i+53];  Reve_LB   = (float)atof(buf);
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i+59];  Reve_UB   = (float)atof(buf);
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i+65];  Pena_LB   = (float)atof(buf);
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i+71];  Pena_UB   = (float)atof(buf);
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i+77];  Due_LB    = (float)atof(buf);
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i+83];  Due_UB    = (float)atof(buf);
    for (int i = 1; i <= 6; i++) buf[i-1] = line[i+113]; END_TIME_param = (float)atof(buf);
    fclose(param);

    // Allocate problem data arrays
    settime_0  = new float[N_Order];
    proctime   = new float[N_Order];
    revenue    = new float[N_Order];
    penaweight = new float[N_Order];
    duedate    = new float[N_Order];
    settime    = new float*[N_Order];
    for (int i = 0; i < N_Order; i++)
        settime[i] = new float[N_Order];

    // Generate data files using Seed (identical to professor's code)
    srand(Seed);
    fp1 = fopen("settime_0.txt", "w");
    fp2 = fopen("settime.txt",   "w");
    fp3 = fopen("proctime.txt",  "w");
    fp4 = fopen("revenue.txt",   "w");
    fp5 = fopen("penaweight.txt","w");
    fp6 = fopen("duedate.txt",   "w");

    for (int i = 1; i <= N_Order; i++)
        fprintf(fp1, "%10.2f\n", RandomRge(Set_0_LB, Set_0_UB));
    for (int i = 1; i <= N_Order; i++) {
        for (int j = 1; j <= N_Order; j++)
            fprintf(fp2, "%10.2f", RandomRge(Set_LB, Set_UB));
        fprintf(fp2, "\n");
    }
    for (int i = 1; i <= N_Order; i++) fprintf(fp3, "%10.2f\n", RandomRge(Proc_LB, Proc_UB));
    for (int i = 1; i <= N_Order; i++) fprintf(fp4, "%10.2f\n", RandomRge(Reve_LB, Reve_UB));
    for (int i = 1; i <= N_Order; i++) fprintf(fp5, "%10.2f\n", RandomRge(Pena_LB, Pena_UB));
    for (int i = 1; i <= N_Order; i++) fprintf(fp6, "%10.2f\n", RandomRge(Due_LB,  Due_UB));

    fclose(fp1); fclose(fp2); fclose(fp3);
    fclose(fp4); fclose(fp5); fclose(fp6);

    // Read data files back into arrays (identical to professor's code)
    count_dis = 0;
    fp1 = fopen("settime_0.txt", "r");
    int idx = 0;
    while (fgets(dis, 11, fp1) != NULL) {
        count_dis++;
        if (count_dis % 2 != 0) settime_0[idx++] = (float)atof(dis);
    }
    fclose(fp1);

    count_dis = 0;
    fp2 = fopen("settime.txt", "r");
    while (fgets(dis, 11, fp2) != NULL) {
        count_dis++;
        int ci = (int)count_dis % (N_Order + 1);
        int ri = 1 + (int)count_dis / (N_Order + 1);
        if (ci != 0) settime[ri-1][ci-1] = (float)atof(dis);
    }
    fclose(fp2);

    count_dis = 0; idx = 0;
    fp3 = fopen("proctime.txt", "r");
    while (fgets(dis, 11, fp3) != NULL) {
        count_dis++;
        if (count_dis % 2 != 0) proctime[idx++] = (float)atof(dis);
    }
    fclose(fp3);

    count_dis = 0; idx = 0;
    fp4 = fopen("revenue.txt", "r");
    while (fgets(dis, 11, fp4) != NULL) {
        count_dis++;
        if (count_dis % 2 != 0) revenue[idx++] = (float)atof(dis);
    }
    fclose(fp4);

    count_dis = 0; idx = 0;
    fp5 = fopen("penaweight.txt", "r");
    while (fgets(dis, 11, fp5) != NULL) {
        count_dis++;
        if (count_dis % 2 != 0) penaweight[idx++] = (float)atof(dis);
    }
    fclose(fp5);

    count_dis = 0; idx = 0;
    fp6 = fopen("duedate.txt", "r");
    while (fgets(dis, 11, fp6) != NULL) {
        count_dis++;
        if (count_dis % 2 != 0) duedate[idx++] = (float)atof(dis);
    }
    fclose(fp6);

    printf("[genData] N=%d M=%d Seed=%d\n", N_Order, M_Machine, Seed);
}

// ============================================================
// DECODE AND EVALUATE — adapted from professor's CalObj()
// Decodes the two-row encoding into machine sequences,
// computes completion times and objective Z.
//
// SE RULE: This is the ONLY function that sets cacheValid = true.
// SE RULE: Pure function — only modifies the passed turtle.
// SE RULE: Call this after ANY change to Order_seq or M_select.
// ============================================================
void decodeAndEval(Turtle& t) {

    // Reset machine counts
    for (int m = 0; m < M_Machine; m++)
        t.machineCount[m] = 0;

    // Decode: assign jobs to machines in sequence order
    for (int pos = 0; pos < N_Order; pos++) {
        int mach = t.M_select[pos];
        if (mach == 0) continue;    // M0 = rejected, skip

        int m   = mach - 1;         // 0-indexed machine
        int cnt = t.machineCount[m];
        int job = t.Order_seq[pos]; // 1-indexed job ID

        t.machineSeq[m][cnt] = job;
        t.machineCount[m]++;
    }

    // Compute completion times along each machine
    for (int m = 0; m < M_Machine; m++) {
        for (int k = 0; k < t.machineCount[m]; k++) {
            int job = t.machineSeq[m][k]; // 1-indexed

            if (k == 0) {
                // First job on machine: initial setup + processing
                t.ctimes[job-1] = settime_0[job-1] + proctime[job-1];
            } else {
                int prevJob = t.machineSeq[m][k-1]; // 1-indexed
                t.ctimes[job-1] = t.ctimes[prevJob-1]
                                 + settime[prevJob-1][job-1]
                                 + proctime[job-1];
            }
        }
    }

    // Compute objective Z = sum(revenue - penalty * max(0, C - d))
    t.obj = 0.0f;
    for (int m = 0; m < M_Machine; m++) {
        for (int k = 0; k < t.machineCount[m]; k++) {
            int   job      = t.machineSeq[m][k]; // 1-indexed
            float tard     = t.ctimes[job-1] - duedate[job-1];
            if (tard <= 0.0f)
                t.obj += revenue[job-1];
            else
                t.obj += revenue[job-1] - penaweight[job-1] * tard;
        }
    }

    // SE: mark cache as valid — completed successfully
    t.cacheValid = true;
}

// ============================================================
// PARTIAL REEVAL — adapted from professor's CalObj_single()
// Recomputes completion times and objective for ONE machine only.
// Used by Stage 2 and Stage 4 for cheap evaluation after repair.
// Cost: O(L) where L = jobs on affected machine
// vs decodeAndEval: O(N) full recomputation
//
// SE RULE: only call this when only ONE machine's sequence changed.
// SE RULE: caller is responsible for undoing if result is worse.
// Returns the new objective value (does NOT modify turtle.obj).
// ============================================================
float partialReeval(Turtle& t, int machine) {
    int m = machine - 1; // convert to 0-indexed

    // Recompute completion times for jobs on this machine only
    for (int k = 0; k < t.machineCount[m]; k++) {
        int job = t.machineSeq[m][k]; // 1-indexed
        if (k == 0) {
            t.ctimes[job-1] = settime_0[job-1] + proctime[job-1];
        } else {
            int prevJob = t.machineSeq[m][k-1];
            t.ctimes[job-1] = t.ctimes[prevJob-1]
                             + settime[prevJob-1][job-1]
                             + proctime[job-1];
        }
    }

    // Recompute full objective using updated ctimes
    // (other machines use their cached ctimes — still valid)
    float newObj = 0.0f;
    for (int mm = 0; mm < M_Machine; mm++) {
        for (int k = 0; k < t.machineCount[mm]; k++) {
            int   job  = t.machineSeq[mm][k];
            float tard = t.ctimes[job-1] - duedate[job-1];
            if (tard <= 0.0f)
                newObj += revenue[job-1];
            else
                newObj += revenue[job-1] - penaweight[job-1] * tard;
        }
    }
    return newObj;
}

// ============================================================
// CALC PAIR SCORE — new function (thesis Eq. 4.1)
// Computes the signed-deviation pair score for pair (ji, jj).
// ji and jj are 1-indexed job IDs.
// ctime_jj is the completion time of job jj (from turtle.ctimes).
//
// Formula: Score(Ji, Jj) = (r_jj - w_jj * (C_jj - d_jj))
//                          / (1 + s_ij)
//
// Note: NO max(0, ...) — signed deviation is intentional.
//   Positive = jj finishes on time (bonus for early completion)
//   Negative = jj is very late (strong penalty signal)
//   Denominator: 1 + setup time normalises for transition cost
//
// SE RULE: pure function — reads global problem data, no side effects.
// ============================================================
float calcPairScore(int ji, int jj, float ctime_jj) {
    // ji and jj are 1-indexed
    float numerator   = revenue[jj-1]
                      - penaweight[jj-1] * (ctime_jj - duedate[jj-1]);
    float denominator = 1.0f + settime[ji-1][jj-1];
    return numerator / denominator;
}

// ============================================================
// MOVE FUNCTIONS — adapted from professor's Neighborhood1-4
// Each function returns a NEW Turtle copy with the move applied.
// The original turtle is NEVER modified.
// The returned copy has cacheValid = false (must call decodeAndEval).
//
// SE RULE: all move functions return copies, never mutate original.
// SE RULE: cacheValid = false on every returned copy.
// ============================================================

// --- Order Insert (Neighborhood1) ---
// Removes job at position k1 and inserts it before position k2.
// k1 and k2 are chosen randomly if -1 is passed.
Turtle moveOrderInsert(const Turtle& t, int k1 = -1, int k2 = -1) {
    Turtle result = t;                   // full copy
    result.cacheValid = false;           // SE: invalidate cache

    // Random positions if not specified
    if (k1 < 0) k1 = (int)RandomRge(0.0f, (float)N_Order - 0.00001f);
    if (k2 < 0) {
        do { k2 = (int)RandomRge(0.0f, (float)(N_Order + 1) - 0.00001f);
        } while (k1 == k2 || k1 == (k2 - 1));
    }

    // Save the job being moved
    int savedOrder = result.Order_seq[k1];
    int savedMach  = result.M_select[k1];
    float savedOval = result.Order_seqval[k1];
    float savedMval = result.M_selectval[k1];

    if (k1 > k2) {
        // Shift right: move elements [k2..k1-1] one step right
        for (int q = k1; q >= k2 + 1; q--) {
            result.Order_seq[q]    = result.Order_seq[q-1];
            result.M_select[q]     = result.M_select[q-1];
            result.Order_seqval[q] = result.Order_seqval[q-1];
            result.M_selectval[q]  = result.M_selectval[q-1];
        }
        result.Order_seq[k2]    = savedOrder;
        result.M_select[k2]     = savedMach;
        result.Order_seqval[k2] = savedOval;
        result.M_selectval[k2]  = savedMval;
    } else {
        // Shift left: move elements [k1+1..k2-2] one step left
        for (int q = k1; q <= k2 - 2; q++) {
            result.Order_seq[q]    = result.Order_seq[q+1];
            result.M_select[q]     = result.M_select[q+1];
            result.Order_seqval[q] = result.Order_seqval[q+1];
            result.M_selectval[q]  = result.M_selectval[q+1];
        }
        result.Order_seq[k2-2]    = savedOrder;
        result.M_select[k2-2]     = savedMach;
        result.Order_seqval[k2-2] = savedOval;
        result.M_selectval[k2-2]  = savedMval;
    }
    return result;
}

// --- Machine Revised (Neighborhood2) ---
// Changes job at position j to machine k (including M0 = reject).
// j and k are chosen randomly if -1 is passed.
Turtle moveMachineRevise(const Turtle& t, int j = -1, int k = -1) {
    Turtle result = t;
    result.cacheValid = false;

    if (j < 0) j = (int)RandomRge(0.0f, (float)N_Order - 0.00001f);

    // Try a different machine than current
    int currentMach = result.M_select[j];
    if (k < 0) {
        do { k = (int)RandomRge(0.0f, (float)(M_Machine + 1) - 0.00001f);
        } while (k == currentMach);
    }

    result.M_select[j] = k;
    return result;
}

// --- Order Swap (Neighborhood3) ---
// Swaps the sequence positions of two jobs.
// k1 and k2 are chosen randomly if -1 is passed.
Turtle moveOrderSwap(const Turtle& t, int k1 = -1, int k2 = -1) {
    Turtle result = t;
    result.cacheValid = false;

    if (k1 < 0) k1 = (int)RandomRge(0.0f, (float)N_Order - 0.00001f);
    if (k2 < 0) {
        do { k2 = (int)RandomRge(0.0f, (float)N_Order - 0.00001f);
        } while (k1 == k2);
    }

    // Swap sequence positions
    std::swap(result.Order_seq[k1],    result.Order_seq[k2]);
    std::swap(result.Order_seqval[k1], result.Order_seqval[k2]);
    return result;
}

// --- Machine Swap (Neighborhood4) ---
// Swaps machine assignments of two jobs that are on different machines.
// k1 and k2 are chosen randomly if -1 is passed.
Turtle moveMachineSwap(const Turtle& t, int k1 = -1, int k2 = -1) {
    Turtle result = t;
    result.cacheValid = false;

    int attempts = 0;
    int maxAttempts = (M_Machine + 1) * M_Machine;

    if (k1 < 0) k1 = (int)RandomRge(0.0f, (float)N_Order - 0.00001f);
    do {
        k2 = (int)RandomRge(0.0f, (float)N_Order - 0.00001f);
        attempts++;
        if (attempts >= maxAttempts) return t; // give up, return original
    } while (k1 == k2 || result.M_select[k1] == result.M_select[k2]);

    std::swap(result.M_select[k1],    result.M_select[k2]);
    std::swap(result.M_selectval[k1], result.M_selectval[k2]);
    return result;
}

// --- Combo Move ---
// Applies Order Insert followed by Machine Revised.
// Gives a combined move for more diverse exploration in Stage 1.
Turtle moveCombo(const Turtle& t) {
    Turtle afterInsert  = moveOrderInsert(t);    // random insert
    Turtle afterRevised = moveMachineRevise(afterInsert); // random machine change
    afterRevised.cacheValid = false;
    return afterRevised;
}

// ============================================================
// GENERATE RANDOM MOVE — Stage 1 dispatcher
// Picks a move type by weighted probability (from config.h)
// and applies it to turtle t, returning a new copy.
//
// Move type probabilities (W_INSERT + W_REVISED + W_SWAP +
//                          W_MSWAP + W_COMBO must = 1.0):
//   MOVE_INSERT  : Order Insert
//   MOVE_REVISED : Machine Revised
//   MOVE_SWAP    : Order Swap
//   MOVE_MSWAP   : Machine Swap
//   MOVE_COMBO   : Combo (Insert + Revised)
//
// SE RULE: returns a copy — original turtle never modified.
// ============================================================
Turtle generateRandomMove(const Turtle& t) {
    float r = RandomF();
    float cumulative = 0.0f;

    cumulative += W_INSERT;
    if (r < cumulative) return moveOrderInsert(t);

    cumulative += W_REVISED;
    if (r < cumulative) return moveMachineRevise(t);

    cumulative += W_SWAP;
    if (r < cumulative) return moveOrderSwap(t);

    cumulative += W_MSWAP;
    if (r < cumulative) return moveMachineSwap(t);

    return moveCombo(t);  // default: COMBO
}

// ============================================================
// INITIALIZE TURTLE — RANDOM
// Generates a fully random turtle encoding and evaluates it.
//
// SE RULE: calls decodeAndEval() before returning.
//          turtle.cacheValid = true when returned.
//          turtle.obj is valid when returned.
// ============================================================
void initializeTurtleRandom(Turtle& t) {
    // Assign job IDs 1..N in random sequence order
    for (int i = 0; i < N_Order; i++) {
        t.Order_seq[i]    = i + 1;   // job ID (1-indexed)
        t.Order_seqval[i] = RandomRge(0.0f, (float)(N_Order + 1) - 0.00001f);
        t.M_selectval[i]  = RandomRge(0.0f, (float)(M_Machine + 1) - 0.00001f);
        t.M_select[i]     = (int)t.M_selectval[i]; // 0=M0, 1..M=machine
    }

    // Sort by random keys to get the encoded sequence
    sortByKeys(t.Order_seq, t.Order_seqval,
               t.M_select,  t.M_selectval, N_Order);

    // Reset diagnostic counters
    t.stage2_repairs = 0;
    t.stage4_phase1  = 0;

    // SE FIX: always evaluate — no caller can forget this step
    decodeAndEval(t);
    // t.cacheValid = true here, t.obj is valid
}

#endif // PROBLEM_H
