// ============================================================
// problem.h — Problem Infrastructure
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
// PROBLEM DATA — READ-ONLY after genData()
// ============================================================
int N_Order   = 0;
int M_Machine = 0;
int Seed      = 0;

float Set_0_LB, Set_0_UB;
float Set_LB,   Set_UB;
float Proc_LB,  Proc_UB;
float Reve_LB,  Reve_UB;
float Pena_LB,  Pena_UB;
float Due_LB,   Due_UB;
float END_TIME_param;

float*  settime_0;
float** settime;
float*  proctime;
float*  revenue;
float*  penaweight;
float*  duedate;

clock_t start_time_clock;

// ============================================================
// UTILITY
// ============================================================
double timing(clock_t start_t) {
    return (double)(clock() - start_t) / CLOCKS_PER_SEC;
}
float RandomF() {
    return (float)1.0 / (float)(RAND_MAX) * (float)rand();
}
float RandomRge(float LB, float UB) {
    return LB + (UB - LB) / (RAND_MAX) * (float)rand();
}

// ============================================================
// SORT
// ============================================================
void sortByKeys(int* order, float* orderKeys,
                int* mach,  float* machKeys, int n) {
    for (int a = 0; a < n - 1; a++) {
        for (int b = a + 1; b < n; b++) {
            if (orderKeys[a] > orderKeys[b]) {
                float tmpf = orderKeys[a]; orderKeys[a] = orderKeys[b]; orderKeys[b] = tmpf;
                int tmpi = order[a]; order[a] = order[b]; order[b] = tmpi;
                tmpf = machKeys[a]; machKeys[a] = machKeys[b]; machKeys[b] = tmpf;
                tmpi = mach[a]; mach[a] = mach[b]; mach[b] = tmpi;
            }
        }
    }
}

// ============================================================
// GEN DATA
// ============================================================
void genData() {
    char buf[10], line[150], dis[11];
    int count_dis;
    FILE *fp1, *fp2, *fp3, *fp4, *fp5, *fp6, *param;

    param = fopen("param.txt", "r");
    if (!param) { printf("ERROR: Cannot open param.txt\n"); exit(1); }
    if (fgets(line, 150, param) == NULL) { printf("ERROR: Failed to read param.txt\n"); exit(1); }

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

    settime_0  = new float[N_Order];
    proctime   = new float[N_Order];
    revenue    = new float[N_Order];
    penaweight = new float[N_Order];
    duedate    = new float[N_Order];
    settime    = new float*[N_Order];
    for (int i = 0; i < N_Order; i++) settime[i] = new float[N_Order];

    srand(Seed);
    fp1 = fopen("settime_0.txt", "w");
    fp2 = fopen("settime.txt",   "w");
    fp3 = fopen("proctime.txt",  "w");
    fp4 = fopen("revenue.txt",   "w");
    fp5 = fopen("penaweight.txt","w");
    fp6 = fopen("duedate.txt",   "w");

    for (int i = 1; i <= N_Order; i++) fprintf(fp1, "%10.2f\n", RandomRge(Set_0_LB, Set_0_UB));
    for (int i = 1; i <= N_Order; i++) {
        for (int j = 1; j <= N_Order; j++) fprintf(fp2, "%10.2f", RandomRge(Set_LB, Set_UB));
        fprintf(fp2, "\n");
    }
    for (int i = 1; i <= N_Order; i++) fprintf(fp3, "%10.2f\n", RandomRge(Proc_LB, Proc_UB));
    for (int i = 1; i <= N_Order; i++) fprintf(fp4, "%10.2f\n", RandomRge(Reve_LB, Reve_UB));
    for (int i = 1; i <= N_Order; i++) fprintf(fp5, "%10.2f\n", RandomRge(Pena_LB, Pena_UB));
    for (int i = 1; i <= N_Order; i++) fprintf(fp6, "%10.2f\n", RandomRge(Due_LB,  Due_UB));

    fclose(fp1); fclose(fp2); fclose(fp3); fclose(fp4); fclose(fp5); fclose(fp6);

    count_dis = 0;
    fp1 = fopen("settime_0.txt", "r");
    int idx = 0;
    while (fgets(dis, 11, fp1) != NULL) { count_dis++; if (count_dis % 2 != 0) settime_0[idx++] = (float)atof(dis); }
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
    while (fgets(dis, 11, fp3) != NULL) { count_dis++; if (count_dis % 2 != 0) proctime[idx++] = (float)atof(dis); }
    fclose(fp3);

    count_dis = 0; idx = 0;
    fp4 = fopen("revenue.txt", "r");
    while (fgets(dis, 11, fp4) != NULL) { count_dis++; if (count_dis % 2 != 0) revenue[idx++] = (float)atof(dis); }
    fclose(fp4);

    count_dis = 0; idx = 0;
    fp5 = fopen("penaweight.txt", "r");
    while (fgets(dis, 11, fp5) != NULL) { count_dis++; if (count_dis % 2 != 0) penaweight[idx++] = (float)atof(dis); }
    fclose(fp5);

    count_dis = 0; idx = 0;
    fp6 = fopen("duedate.txt", "r");
    while (fgets(dis, 11, fp6) != NULL) { count_dis++; if (count_dis % 2 != 0) duedate[idx++] = (float)atof(dis); }
    fclose(fp6);

    printf("[genData] N=%d M=%d Seed=%d\n", N_Order, M_Machine, Seed);
}

// ============================================================
// DECODE AND EVALUATE
// SE RULE: ONLY function that sets cacheValid = true.
// Run003 C2-prep: also fills machineObj[] (per-machine contribution).
// ============================================================
void decodeAndEval(Turtle& t) {
    for (int m = 0; m < M_Machine; m++) t.machineCount[m] = 0;

    for (int pos = 0; pos < N_Order; pos++) {
        int mach = t.M_select[pos];
        if (mach == 0) continue;
        int m   = mach - 1;
        int cnt = t.machineCount[m];
        int job = t.Order_seq[pos];
        t.machineSeq[m][cnt] = job;
        t.machineCount[m]++;
    }

    for (int m = 0; m < M_Machine; m++) {
        for (int k = 0; k < t.machineCount[m]; k++) {
            int job = t.machineSeq[m][k];
            if (k == 0) {
                t.ctimes[job-1] = settime_0[job-1] + proctime[job-1];
            } else {
                int prevJob = t.machineSeq[m][k-1];
                t.ctimes[job-1] = t.ctimes[prevJob-1] + settime[prevJob-1][job-1] + proctime[job-1];
            }
        }
    }

    t.obj = 0.0f;
    for (int m = 0; m < M_Machine; m++) {
        float mObj = 0.0f;
        for (int k = 0; k < t.machineCount[m]; k++) {
            int   job  = t.machineSeq[m][k];
            float tard = t.ctimes[job-1] - duedate[job-1];
            if (tard <= 0.0f) mObj += revenue[job-1];
            else              mObj += revenue[job-1] - penaweight[job-1] * tard;
        }
        t.machineObj[m] = mObj;   // C2-prep: per-machine contribution
        t.obj += mObj;
    }
    t.cacheValid = true;
}

// ============================================================
// PARTIAL REEVAL
// NOTE: only valid when EXACTLY ONE machine's sequence changed.
// Recomputes that machine's ctimes, then sums all machines.
// ============================================================
float partialReeval(Turtle& t, int machine) {
    int m = machine - 1;
    for (int k = 0; k < t.machineCount[m]; k++) {
        int job = t.machineSeq[m][k];
        if (k == 0) t.ctimes[job-1] = settime_0[job-1] + proctime[job-1];
        else {
            int prevJob = t.machineSeq[m][k-1];
            t.ctimes[job-1] = t.ctimes[prevJob-1] + settime[prevJob-1][job-1] + proctime[job-1];
        }
    }
    float newObj = 0.0f;
    for (int mm = 0; mm < M_Machine; mm++)
        for (int k = 0; k < t.machineCount[mm]; k++) {
            int   job  = t.machineSeq[mm][k];
            float tard = t.ctimes[job-1] - duedate[job-1];
            if (tard <= 0.0f) newObj += revenue[job-1];
            else              newObj += revenue[job-1] - penaweight[job-1] * tard;
        }
    return newObj;
}

// ============================================================
// CALC PAIR SCORE (thesis Eq. 4.1)
// ============================================================
float calcPairScore(int ji, int jj, float ctime_jj) {
    float numerator   = revenue[jj-1] - penaweight[jj-1] * (ctime_jj - duedate[jj-1]);
    float denominator = 1.0f + settime[ji-1][jj-1];
    return numerator / denominator;
}

// ============================================================
// MOVE FUNCTIONS (professor's Neighborhood1-4, return copies)
// ============================================================
Turtle moveOrderInsert(const Turtle& t, int k1 = -1, int k2 = -1) {
    Turtle result = t;
    result.cacheValid = false;
    if (k1 < 0) k1 = (int)RandomRge(0.0f, (float)N_Order - 0.00001f);
    if (k2 < 0) {
        do { k2 = (int)RandomRge(0.0f, (float)(N_Order + 1) - 0.00001f);
        } while (k1 == k2 || k1 == (k2 - 1));
    }
    int savedOrder = result.Order_seq[k1];
    int savedMach  = result.M_select[k1];
    float savedOval = result.Order_seqval[k1];
    float savedMval = result.M_selectval[k1];
    if (k1 > k2) {
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

Turtle moveMachineRevise(const Turtle& t, int j = -1, int k = -1) {
    Turtle result = t;
    result.cacheValid = false;
    if (j < 0) j = (int)RandomRge(0.0f, (float)N_Order - 0.00001f);
    int currentMach = result.M_select[j];
    if (k < 0) {
        do { k = (int)RandomRge(0.0f, (float)(M_Machine + 1) - 0.00001f);
        } while (k == currentMach);
    }
    result.M_select[j] = k;
    return result;
}

Turtle moveOrderSwap(const Turtle& t, int k1 = -1, int k2 = -1) {
    Turtle result = t;
    result.cacheValid = false;
    if (k1 < 0) k1 = (int)RandomRge(0.0f, (float)N_Order - 0.00001f);
    if (k2 < 0) {
        do { k2 = (int)RandomRge(0.0f, (float)N_Order - 0.00001f);
        } while (k1 == k2);
    }
    std::swap(result.Order_seq[k1],    result.Order_seq[k2]);
    std::swap(result.Order_seqval[k1], result.Order_seqval[k2]);
    return result;
}

Turtle moveMachineSwap(const Turtle& t, int k1 = -1, int k2 = -1) {
    Turtle result = t;
    result.cacheValid = false;
    int attempts = 0;
    int maxAttempts = (M_Machine + 1) * M_Machine;
    if (k1 < 0) k1 = (int)RandomRge(0.0f, (float)N_Order - 0.00001f);
    do {
        k2 = (int)RandomRge(0.0f, (float)N_Order - 0.00001f);
        attempts++;
        if (attempts >= maxAttempts) return t;
    } while (k1 == k2 || result.M_select[k1] == result.M_select[k2]);
    std::swap(result.M_select[k1],    result.M_select[k2]);
    std::swap(result.M_selectval[k1], result.M_selectval[k2]);
    return result;
}

Turtle moveCombo(const Turtle& t) {
    Turtle afterInsert  = moveOrderInsert(t);
    Turtle afterRevised = moveMachineRevise(afterInsert);
    afterRevised.cacheValid = false;
    return afterRevised;
}

// ============================================================
// RELOCATE AFTER — Run003 clean helper (SE: own, verified semantics)
// Moves the job at position `from` to be IMMEDIATELY AFTER the job
// currently at position `afterPos`, carrying all per-position data
// (job id, machine, keys). Returns a copy with cacheValid=false.
//
// This is used by Stage 4 (B3) and Stage 5 (B2) to actually FORM a
// target adjacency — unlike moveOrderSwap which only exchanges spots.
// Both index branches verified by hand for correctness.
// ============================================================
Turtle relocateAfter(const Turtle& t, int from, int afterPos) {
    Turtle r = t;
    r.cacheValid = false;
    if (from == afterPos || from == afterPos + 1) return r; // already in place

    int   sOrder = r.Order_seq[from];
    float sOval  = r.Order_seqval[from];
    int   sMach  = r.M_select[from];
    float sMval  = r.M_selectval[from];

    if (from > afterPos) {
        // Move left: shift (afterPos+1 .. from-1) right by 1; place at afterPos+1
        for (int q = from; q > afterPos + 1; q--) {
            r.Order_seq[q]    = r.Order_seq[q-1];
            r.Order_seqval[q] = r.Order_seqval[q-1];
            r.M_select[q]     = r.M_select[q-1];
            r.M_selectval[q]  = r.M_selectval[q-1];
        }
        r.Order_seq[afterPos+1]    = sOrder;
        r.Order_seqval[afterPos+1] = sOval;
        r.M_select[afterPos+1]     = sMach;
        r.M_selectval[afterPos+1]  = sMval;
    } else {
        // from < afterPos. Move right: shift (from+1 .. afterPos) left by 1; place at afterPos
        for (int q = from; q < afterPos; q++) {
            r.Order_seq[q]    = r.Order_seq[q+1];
            r.Order_seqval[q] = r.Order_seqval[q+1];
            r.M_select[q]     = r.M_select[q+1];
            r.M_selectval[q]  = r.M_selectval[q+1];
        }
        r.Order_seq[afterPos]    = sOrder;
        r.Order_seqval[afterPos] = sOval;
        r.M_select[afterPos]     = sMach;
        r.M_selectval[afterPos]  = sMval;
    }
    return r;
}

// ============================================================
// GENERATE RANDOM MOVE — Stage 1 dispatcher
// ============================================================
Turtle generateRandomMove(const Turtle& t) {
    float r = RandomF();
    float cumulative = 0.0f;
    cumulative += W_INSERT;  if (r < cumulative) return moveOrderInsert(t);
    cumulative += W_REVISED; if (r < cumulative) return moveMachineRevise(t);
    cumulative += W_SWAP;    if (r < cumulative) return moveOrderSwap(t);
    cumulative += W_MSWAP;   if (r < cumulative) return moveMachineSwap(t);
    return moveCombo(t);
}

// ============================================================
// INITIALIZE TURTLE — RANDOM
// ============================================================
void initializeTurtleRandom(Turtle& t) {
    for (int i = 0; i < N_Order; i++) {
        t.Order_seq[i]    = i + 1;
        t.Order_seqval[i] = RandomRge(0.0f, (float)(N_Order + 1) - 0.00001f);
        t.M_selectval[i]  = RandomRge(0.0f, (float)(M_Machine + 1) - 0.00001f);
        t.M_select[i]     = (int)t.M_selectval[i];
    }
    sortByKeys(t.Order_seq, t.Order_seqval, t.M_select, t.M_selectval, N_Order);
    t.stage2_repairs = 0;
    t.stage4_phase1  = 0;
    decodeAndEval(t);
}

#endif // PROBLEM_H
