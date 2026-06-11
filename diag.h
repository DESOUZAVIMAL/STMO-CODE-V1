// ============================================================
// diag.h — STMO Run 009 PASSIVE diagnostic instrumentation
// ============================================================
// EVERYTHING in this file compiles to nothing when DIAG_MODE == 0,
// so the binary is then byte-for-byte the Run 7/8 algorithm.
//
// THE ONE RULE (spec §0): this code is passive instrumentation. It
//   - NEVER calls rand()/srand() or consumes the RNG stream,
//   - NEVER mutates a turtle / population / elite / memory / counter
//     that the algorithm reads,
//   - NEVER changes control flow, acceptance, stage order, or stops.
// Where it must recompute an objective it COPIES the turtle first and
// decodes the copy. All sorts are over local copies, never algorithm
// arrays. No sampling RNG is used (full O(P^2) metrics; P is small).
//
// Include LAST in STMO.cpp (after stages.h) so every type, global
// counter, problem array, and helper (decodeAndEval, validateTurtle,
// calcPairScore) is already visible.
// ============================================================

#ifndef DIAG_H
#define DIAG_H

#include "config.h"

#if DIAG_MODE

#include "stmo_types.h"
#include "problem.h"
#include "memory_ops.h"
#include "stages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <chrono>
#include <vector>
#include <set>
#include <algorithm>
#include <sys/stat.h>
#include <sys/types.h>

namespace diag {

// ------------------------------------------------------------
// Run-config metadata (set by begin()).
// ------------------------------------------------------------
static int   g_N = 0, g_M = 0, g_Seed = 0, g_repeat = 1;
static char  g_dir[512]     = {0};   // results/run_009_diag/N{N}_M{M}_S{Seed}/repeat{r}
static char  g_instDir[512] = {0};   // results/run_009_diag/N{N}_M{M}_S{Seed}
static char  g_rootDir[256] = {0};   // results/run_009_diag

// File handles (opened once per instance, flushed at checkpoints).
static FILE* f_traj    = NULL;
static FILE* f_pop     = NULL;
static FILE* f_div     = NULL;
static FILE* f_best    = NULL;
static FILE* f_swin    = NULL;
static FILE* f_pm      = NULL;
static FILE* f_basin   = NULL;
static FILE* f_m0      = NULL;
static FILE* f_integ   = NULL;

// Wall / CPU clocks (diag-owned; do NOT touch the algorithm's clock()).
static std::chrono::steady_clock::time_point g_wallStart;

// ------------------------------------------------------------
// Window accumulators (spec §4: "small running accumulators that you
// add, reset each window"). These are diagnostic-only; the algorithm
// never reads them.
// ------------------------------------------------------------
// cumulative stage counters snapshot at the last window boundary
static long w_s1_acc=0, w_s1_g=0;
static long w_s2_acc=0, w_s2_strict=0, w_s2_g=0;
static long w_s4_acc=0, w_s4_g=0;
static long w_s5_acc=0, w_s5_g=0;
// accepted-ΔZ sums / proposed counts live in the global g_diag_* arrays
// (defined in STMO.cpp, fed by the DIAG_ACCEPT / DIAG_PROPOSE macros).
// pairmemory churn within the current window
static long   pm_created_win = 0, pm_deleted_win = 0;
static long   pm_crit_created_win = 0, pm_crit_deleted_win = 0;
static int    pm_last_size = 0;
// m0 churn within the current window
static std::set<int> m0_last_set;
// run-wide bookkeeping for final_summary
static int    g_firstCkptDistinct = -1;
static int    g_lastDistinct = 0;
static int    g_ccrit_peak = 0;
static int    g_ccrit_zero_iter = -1;
static int    g_pm_peak = 0;
static long   g_bestEvents = 0;
static int    g_bestAtIter = 0;
static float  g_bestZseen  = -1e30f;
// reproducibility trajectory hash (FNV-1a over every-100-iter bestZ)
static uint64_t g_trajHash = 0;

// forward declaration (used by writeCodeHealthMap, defined below)
static const char* verdict(long prop, long acc, long g);

// checkpoint iterations (geometric) — FINAL handled separately.
static const int CKPTS[] = {0,10,25,50,100,200,400,800,1500,3000,6000,12000,20000};
static const int N_CKPT  = (int)(sizeof(CKPTS)/sizeof(CKPTS[0]));

// ------------------------------------------------------------
// FNV-1a 64-bit helpers (deterministic, stable across the run).
// ------------------------------------------------------------
static inline uint64_t fnvInit() { return 1469598103934665603ULL; }
static inline uint64_t fnvByte(uint64_t h, unsigned char b) { h ^= b; h *= 1099511628211ULL; return h; }
static inline uint64_t fnvU32(uint64_t h, uint32_t v) {
    for (int k = 0; k < 4; ++k) { h = fnvByte(h, (unsigned char)(v & 0xFFu)); v >>= 8; }
    return h;
}
static inline uint64_t fnvFloat(uint64_t h, float f) {
    uint32_t v; memcpy(&v, &f, 4); return fnvU32(h, v);
}
static uint64_t fnvIntVec(const std::vector<int>& a) {
    uint64_t h = fnvInit();
    for (size_t i = 0; i < a.size(); ++i) h = fnvU32(h, (uint32_t)a[i]);
    return h;
}

// ------------------------------------------------------------
// Per-turtle derived signatures (read-only).
// ------------------------------------------------------------
// accept_hash = FNV over the SORTED set of accepted (M_select != 0) job IDs.
static uint64_t acceptHash(const Turtle& t) {
    std::vector<int> ids;
    for (int pos = 0; pos < N_Order; ++pos)
        if (t.M_select[pos] != 0) ids.push_back(t.Order_seq[pos]);
    std::sort(ids.begin(), ids.end());
    return fnvIntVec(ids);
}
// seq_hash = FNV over the decoded per-machine sequences (the actual
// sequencing). Requires cacheValid; caller guarantees decoded state.
static uint64_t seqHash(const Turtle& t) {
    std::vector<int> v;
    for (int m = 0; m < M_Machine; ++m) {
        v.push_back(-1);                       // machine separator
        for (int k = 0; k < t.machineCount[m]; ++k) v.push_back(t.machineSeq[m][k]);
    }
    return fnvIntVec(v);
}
static uint64_t orderHash(const Turtle& t) {
    std::vector<int> v(t.Order_seq, t.Order_seq + N_Order);
    return fnvIntVec(v);
}
static uint64_t machineHash(const Turtle& t) {
    std::vector<int> v(t.M_select, t.M_select + N_Order);
    return fnvIntVec(v);
}
static uint64_t fullHash(const Turtle& t) {
    std::vector<int> v;
    v.reserve(2 * N_Order + 1);
    for (int i = 0; i < N_Order; ++i) v.push_back(t.Order_seq[i]);
    v.push_back(-1);
    for (int i = 0; i < N_Order; ++i) v.push_back(t.M_select[i]);
    return fnvIntVec(v);
}

// adjacency set of consecutive (job_i, job_{i+1}) pairs in the decoded
// machine sequences, encoded as job_i * (N+1) + job_{i+1}, sorted unique.
static void adjacencySet(const Turtle& t, std::vector<long>& out) {
    out.clear();
    for (int m = 0; m < M_Machine; ++m)
        for (int k = 0; k < t.machineCount[m] - 1; ++k)
            out.push_back((long)t.machineSeq[m][k] * (long)(N_Order + 1) + t.machineSeq[m][k+1]);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

// ------------------------------------------------------------
// Directory + file setup.
// ------------------------------------------------------------
static void makeDirs() {
    // mkdir each path component; ignore EEXIST. (POSIX; on MSYS2 this
    // maps to Windows dirs.) Pure filesystem — no RNG, no algorithm state.
    char path[768];
    const char* parts[] = { "results", "results/run_009_diag" };
    for (int i = 0; i < 2; ++i) mkdir(parts[i], 0775);
    snprintf(g_rootDir, sizeof(g_rootDir), "results/run_009_diag");
    snprintf(g_instDir, sizeof(g_instDir), "results/run_009_diag/N%d_M%d_S%d", g_N, g_M, g_Seed);
    mkdir(g_instDir, 0775);
    snprintf(g_dir, sizeof(g_dir), "%s/repeat%d", g_instDir, g_repeat);
    mkdir(g_dir, 0775);
    (void)path;
}
static FILE* openOut(const char* name, const char* header) {
    char p[1024];
    snprintf(p, sizeof(p), "%s/%s", g_dir, name);
    FILE* f = fopen(p, "w");
    if (f && header) fprintf(f, "%s\n", header);
    return f;
}

// ============================================================
// PUBLIC: begin() — call once per instance, right after BatchEval 1.
// ============================================================
static void begin(int N, int M, int Seed, int repeat) {
    g_N = N; g_M = M; g_Seed = Seed; g_repeat = repeat;
    makeDirs();
    g_wallStart = std::chrono::steady_clock::now();

    f_traj = openOut("trajectory.csv",
        "iter,time_s,cpu_s,bestZ,pm_size,ccrit,cweak,restarts,"
        "s1acc_cum,s2acc_cum,s2strict_cum,s4acc_cum,s5acc_cum,"
        "s1g_cum,s2g_cum,s4g_cum,s5g_cum");
    f_pop = openOut("population_dump.csv",
        "iter,source,member_id,objective,accepted_count,order_seq,machine_assign,reject_set,keys");
    f_div = openOut("diversity.csv",
        "iter,time_s,pop_size,distinct_order,distinct_machine,distinct_full,"
        "ham_order_mean,ham_order_min,ham_order_max,ham_machine_mean,adj_jaccard_mean,"
        "obj_min,obj_mean,obj_median,obj_max,obj_std,accepted_min,accepted_mean,accepted_max");
    f_best = openOut("best_events.csv",
        "iter,time_s,stage,operator,member_id,oldBest,newBest,delta,"
        "ccrit_now,cweak_now,pm_now,n_jobs_changed,accept_set_hash,seq_hash");
    f_swin = openOut("stage_window.csv",
        "iter,window,s1acc,s1g,s2acc,s2strict,s2g,s4acc,s4g,s5acc,s5g,"
        "s2_accept_rate,s2_mean_accepted_delta,s4_mean_accepted_delta,s5_mean_accepted_delta");
    f_pm = openOut("pairmemory.csv",
        "iter,pm_size,n_strong,n_neutral,n_weak,n_critical,"
        "score_min,score_mean,score_max,age_mean,age_max,"
        "created_in_window,deleted_in_window,critical_created_in_window,critical_deleted_in_window");
    f_basin = openOut("basin_signature.csv",
        "iter,incumbent_accept_hash,incumbent_seq_hash,elite_accept_hash,elite_seq_hash,"
        "distinct_accept_hashes_in_pop,distinct_seq_hashes_in_pop");
    f_m0 = openOut("m0_pool.csv",
        "iter,m0_size,m0_in_window,m0_out_window,"
        "incumbent_accepted_jobs,incumbent_accepted_revenue,incumbent_total_tardiness");
    f_integ = openOut("integrity_violations.csv",
        "iter,violation_type,detail,incumbentZ_incremental,incumbentZ_recomputed,mismatch");

    g_trajHash = fnvInit();
    pm_last_size = 0;
    m0_last_set.clear();
}

// ------------------------------------------------------------
// Population / elite dump at a checkpoint.
// ------------------------------------------------------------
static void writeMember(FILE* f, int iter, const char* source, int memberId, const Turtle& t) {
    int accepted = 0;
    for (int pos = 0; pos < N_Order; ++pos) if (t.M_select[pos] != 0) accepted++;
    fprintf(f, "%d,%s,%d,%.6f,%d,", iter, source, memberId, t.obj, accepted);
    // order_seq
    for (int pos = 0; pos < N_Order; ++pos) fprintf(f, "%s%d", pos ? " " : "", t.Order_seq[pos]);
    fprintf(f, ",");
    // machine_assign
    for (int pos = 0; pos < N_Order; ++pos) fprintf(f, "%s%d", pos ? " " : "", t.M_select[pos]);
    fprintf(f, ",");
    // reject_set (job ids with M_select==0)
    bool first = true;
    for (int pos = 0; pos < N_Order; ++pos)
        if (t.M_select[pos] == 0) { fprintf(f, "%s%d", first ? "" : " ", t.Order_seq[pos]); first = false; }
    fprintf(f, ",");
    // keys (order random keys — optional but dumped for full reconstruction)
    for (int pos = 0; pos < N_Order; ++pos) fprintf(f, "%s%.6f", pos ? " " : "", t.Order_seqval[pos]);
    fprintf(f, "\n");
}
static void dumpPopulation(int iter, const Turtle* pop, const EliteArchive& ea) {
    if (!f_pop) return;
    for (int p = 0; p < P; ++p)      writeMember(f_pop, iter, "pop",   p, pop[p]);
    for (int e = 0; e < ea.count; ++e) writeMember(f_pop, iter, "elite", e, ea.turtles[e]);
    fflush(f_pop);
}

// ------------------------------------------------------------
// trajectory.csv  (every DIAG_TRAJ_EVERY iters)
// ------------------------------------------------------------
static void writeTrajectory(int iter, float wall_s, float cpu_s, float bestZ, int pmSize) {
    if (!f_traj) return;
    fprintf(f_traj, "%d,%.3f,%.3f,%.6f,%d,%d,%d,%d,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n",
        iter, wall_s, cpu_s, bestZ, pmSize, g_ccrit, g_cweak, g_restartCount,
        g_s1_acc, g_s2_acc, g_s2_strict, g_s4_acc, g_s5_acc,
        g_s1_gbest, g_s2_gbest, g_s4_gbest, g_s5_gbest);
}

// ------------------------------------------------------------
// diversity.csv  (every DIAG_WINDOW_EVERY iters) — read-only over pop.
// ------------------------------------------------------------
static void writeDiversity(int iter, float wall_s, const Turtle* pop) {
    if (!f_div) return;
    // distinct counts
    std::set<uint64_t> sOrder, sMach, sFull;
    for (int p = 0; p < P; ++p) { sOrder.insert(orderHash(pop[p])); sMach.insert(machineHash(pop[p])); sFull.insert(fullHash(pop[p])); }
    // pairwise Hamming (order + machine), full O(P^2)
    double hoSum = 0, hmSum = 0; int hoMin = N_Order + 1, hoMax = -1; long pairs = 0;
    for (int a = 0; a < P; ++a) for (int b = a + 1; b < P; ++b) {
        int do_ = 0, dm = 0;
        for (int i = 0; i < N_Order; ++i) {
            if (pop[a].Order_seq[i] != pop[b].Order_seq[i]) do_++;
            if (pop[a].M_select[i]  != pop[b].M_select[i])  dm++;
        }
        hoSum += do_; hmSum += dm; if (do_ < hoMin) hoMin = do_; if (do_ > hoMax) hoMax = do_; pairs++;
    }
    double hoMean = pairs ? hoSum / pairs : 0, hmMean = pairs ? hmSum / pairs : 0;
    if (pairs == 0) { hoMin = 0; hoMax = 0; }
    // adjacency Jaccard distance mean
    std::vector< std::vector<long> > adj(P);
    for (int p = 0; p < P; ++p) adjacencySet(pop[p], adj[p]);
    double jSum = 0; long jPairs = 0;
    for (int a = 0; a < P; ++a) for (int b = a + 1; b < P; ++b) {
        // intersection size of two sorted unique vectors
        size_t i = 0, j = 0; long inter = 0;
        while (i < adj[a].size() && j < adj[b].size()) {
            if (adj[a][i] == adj[b][j]) { inter++; i++; j++; }
            else if (adj[a][i] < adj[b][j]) i++; else j++;
        }
        long uni = (long)adj[a].size() + (long)adj[b].size() - inter;
        double jac = uni ? (1.0 - (double)inter / (double)uni) : 0.0;
        jSum += jac; jPairs++;
    }
    double jMean = jPairs ? jSum / jPairs : 0;
    // objective + accepted-count stats
    std::vector<float> objs(P); std::vector<int> accs(P);
    float oMin = 1e30f, oMax = -1e30f; double oSum = 0; int aMin = N_Order + 1, aMax = -1; double aSum = 0;
    for (int p = 0; p < P; ++p) {
        objs[p] = pop[p].obj; oSum += pop[p].obj;
        if (pop[p].obj < oMin) oMin = pop[p].obj; if (pop[p].obj > oMax) oMax = pop[p].obj;
        int ac = 0; for (int pos = 0; pos < N_Order; ++pos) if (pop[p].M_select[pos] != 0) ac++;
        accs[p] = ac; aSum += ac; if (ac < aMin) aMin = ac; if (ac > aMax) aMax = ac;
    }
    double oMean = oSum / P;
    double oVar = 0; for (int p = 0; p < P; ++p) oVar += (objs[p] - oMean) * (objs[p] - oMean);
    double oStd = sqrt(oVar / P);
    std::vector<float> osort = objs; std::sort(osort.begin(), osort.end());
    double oMed = (P % 2) ? osort[P/2] : 0.5 * (osort[P/2 - 1] + osort[P/2]);

    g_lastDistinct = (int)sFull.size();
    if (g_firstCkptDistinct < 0) g_firstCkptDistinct = (int)sFull.size();

    fprintf(f_div, "%d,%.3f,%d,%d,%d,%d,%.4f,%d,%d,%.4f,%.6f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%.4f,%d\n",
        iter, wall_s, P, (int)sOrder.size(), (int)sMach.size(), (int)sFull.size(),
        hoMean, hoMin, hoMax, hmMean, jMean,
        oMin, oMean, oMed, oMax, oStd, aMin, aSum / P, aMax);
}

// ------------------------------------------------------------
// basin_signature.csv  (every DIAG_WINDOW_EVERY iters)
// ------------------------------------------------------------
static void writeBasin(int iter, const Turtle* pop, const EliteArchive& ea) {
    if (!f_basin) return;
    uint64_t incA = 0, incS = 0, eliA = 0, eliS = 0;
    if (ea.count > 0) { incA = acceptHash(ea.turtles[0]); incS = seqHash(ea.turtles[0]); eliA = incA; eliS = incS; }
    std::set<uint64_t> dA, dS;
    for (int p = 0; p < P; ++p) { dA.insert(acceptHash(pop[p])); dS.insert(seqHash(pop[p])); }
    fprintf(f_basin, "%d,%llu,%llu,%llu,%llu,%d,%d\n",
        iter, (unsigned long long)incA, (unsigned long long)incS,
        (unsigned long long)eliA, (unsigned long long)eliS,
        (int)dA.size(), (int)dS.size());
}

// ------------------------------------------------------------
// m0_pool.csv  (every DIAG_WINDOW_EVERY iters)
// ------------------------------------------------------------
static void writeM0(int iter, const M0Pool& m0, const EliteArchive& ea) {
    if (!f_m0) return;
    std::set<int> cur;
    for (size_t i = 0; i < m0.size(); ++i) cur.insert(m0[i].jobId);
    int inWin = 0, outWin = 0;
    for (std::set<int>::iterator it = cur.begin(); it != cur.end(); ++it)
        if (m0_last_set.find(*it) == m0_last_set.end()) inWin++;
    for (std::set<int>::iterator it = m0_last_set.begin(); it != m0_last_set.end(); ++it)
        if (cur.find(*it) == cur.end()) outWin++;
    m0_last_set = cur;
    // incumbent (elite head) accepted jobs / revenue / total tardiness
    int accJobs = 0; double accRev = 0, totTard = 0;
    if (ea.count > 0) {
        const Turtle& t = ea.turtles[0];
        for (int m = 0; m < M_Machine; ++m)
            for (int k = 0; k < t.machineCount[m]; ++k) {
                int job = t.machineSeq[m][k];
                accJobs++; accRev += revenue[job-1];
                float tard = t.ctimes[job-1] - duedate[job-1];
                if (tard > 0.0f) totTard += tard;
            }
    }
    fprintf(f_m0, "%d,%d,%d,%d,%d,%.4f,%.4f\n",
        iter, (int)m0.size(), inWin, outWin, accJobs, accRev, totTard);
}

// ------------------------------------------------------------
// stage_window.csv  (every DIAG_WINDOW_EVERY iters; deltas vs last)
// ------------------------------------------------------------
static void writeStageWindow(int iter, int window) {
    if (!f_swin) return;
    long d_s1a = g_s1_acc - w_s1_acc,    d_s1g = g_s1_gbest - w_s1_g;
    long d_s2a = g_s2_acc - w_s2_acc,    d_s2s = g_s2_strict - w_s2_strict, d_s2g = g_s2_gbest - w_s2_g;
    long d_s4a = g_s4_acc - w_s4_acc,    d_s4g = g_s4_gbest - w_s4_g;
    long d_s5a = g_s5_acc - w_s5_acc,    d_s5g = g_s5_gbest - w_s5_g;
    double s2rate = g_diag_prop_n[2] ? (double)g_diag_acc_n[2] / (double)g_diag_prop_n[2] : 0.0;
    double s2md = g_diag_acc_n[2] ? g_diag_acc_dz[2] / (double)g_diag_acc_n[2] : 0.0;
    double s4md = g_diag_acc_n[4] ? g_diag_acc_dz[4] / (double)g_diag_acc_n[4] : 0.0;
    double s5md = g_diag_acc_n[5] ? g_diag_acc_dz[5] / (double)g_diag_acc_n[5] : 0.0;
    fprintf(f_swin, "%d,%d,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%.4f,%.4f,%.4f,%.4f\n",
        iter, window, d_s1a, d_s1g, d_s2a, d_s2s, d_s2g, d_s4a, d_s4g, d_s5a, d_s5g,
        s2rate, s2md, s4md, s5md);
    // snapshot + reset window accumulators
    w_s1_acc = g_s1_acc; w_s1_g = g_s1_gbest;
    w_s2_acc = g_s2_acc; w_s2_strict = g_s2_strict; w_s2_g = g_s2_gbest;
    w_s4_acc = g_s4_acc; w_s4_g = g_s4_gbest;
    w_s5_acc = g_s5_acc; w_s5_g = g_s5_gbest;
    for (int s = 0; s < 6; ++s) { g_diag_acc_dz[s] = 0; g_diag_acc_n[s] = 0; g_diag_prop_n[s] = 0; }
}

// ------------------------------------------------------------
// pairmemory.csv  (every DIAG_PM_EVERY iters)
// ------------------------------------------------------------
static void writePairMemory(int iter, const PairMemory& pm) {
    if (!f_pm) return;
    int nS = 0, nN = 0, nW = 0, nC = 0;
    float sMin = 1e30f, sMax = -1e30f; double sSum = 0; int cnt = 0;
    double ageSum = 0; int ageMax = 0;
    for (PairMemory::const_iterator it = pm.begin(); it != pm.end(); ++it) {
        const PairRecord& r = it->second;
        switch (r.label) {
            case LABEL_STRONG:   nS++; break;
            case LABEL_NEUTRAL:  nN++; break;
            case LABEL_WEAK:     nW++; break;
            case LABEL_CRITICAL: nC++; break;
            default: break;
        }
        sSum += r.avgScore; cnt++;
        if (r.avgScore < sMin) sMin = r.avgScore;
        if (r.avgScore > sMax) sMax = r.avgScore;
        ageSum += r.age; if (r.age > ageMax) ageMax = r.age;
    }
    if (cnt == 0) { sMin = 0; sMax = 0; }
    int sz = (int)pm.size();
    // churn within window (created/deleted estimated from net size +
    // explicit critical tracking is updated by noteLabelCounts below).
    int created = (sz > pm_last_size) ? (sz - pm_last_size) : 0;
    int deleted = (sz < pm_last_size) ? (pm_last_size - sz) : 0;
    pm_last_size = sz;
    fprintf(f_pm, "%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.4f,%d,%d,%d,%ld,%ld\n",
        iter, sz, nS, nN, nW, nC, sMin, (cnt ? sSum / cnt : 0.0), sMax,
        (cnt ? ageSum / cnt : 0.0), ageMax,
        created, deleted, pm_crit_created_win, pm_crit_deleted_win);
    pm_crit_created_win = 0; pm_crit_deleted_win = 0;
    if (sz > g_pm_peak) g_pm_peak = sz;
    if (g_ccrit > g_ccrit_peak) g_ccrit_peak = g_ccrit;
    if (g_ccrit == 0 && g_ccrit_zero_iter < 0 && iter > 0) g_ccrit_zero_iter = iter;
}

// ------------------------------------------------------------
// integrity_violations.csv  (feasibility every iter; objective
// recompute every DIAG_PM_EVERY iters). Recompute is on a COPY.
// ------------------------------------------------------------
static void writeIntegrity(int iter, const EliteArchive& ea, bool doRecompute) {
    if (!f_integ || ea.count == 0) return;
    const Turtle& inc = ea.turtles[0];
    // (1) duplicate / missing jobs across machine sequences + M0
    std::vector<int> seenCount(N_Order + 1, 0);
    int scheduled = 0;
    for (int m = 0; m < M_Machine; ++m)
        for (int k = 0; k < inc.machineCount[m]; ++k) {
            int job = inc.machineSeq[m][k];
            if (job >= 1 && job <= N_Order) seenCount[job]++;
            scheduled++;
        }
    int rejected = 0;
    for (int pos = 0; pos < N_Order; ++pos) if (inc.M_select[pos] == 0) rejected++;
    // duplicates among scheduled
    for (int j = 1; j <= N_Order; ++j)
        if (seenCount[j] > 1)
            fprintf(f_integ, "%d,duplicate_job,job=%d count=%d,%.6f,,\n", iter, j, seenCount[j], inc.obj);
    // scheduled + rejected == N
    if (scheduled + rejected != N_Order)
        fprintf(f_integ, "%d,count_mismatch,scheduled=%d rejected=%d N=%d,%.6f,,\n",
            iter, scheduled, rejected, N_Order, inc.obj);
    // permutation validity (covers missing/out-of-range/dup in encoding)
    if (!validateTurtle(inc))
        fprintf(f_integ, "%d,invalid_encoding,validateTurtle=false,%.6f,,\n", iter, inc.obj);
    // (2) objective cross-check on a COPY (never touch the real turtle)
    if (doRecompute) {
        Turtle copy = inc;
        decodeAndEval(copy);
        double mismatch = fabs((double)copy.obj - (double)inc.obj);
        if (mismatch > 1e-6)
            fprintf(f_integ, "%d,objective_mismatch,recompute,%.6f,%.6f,%.6f\n",
                iter, inc.obj, copy.obj, mismatch);
    }
    fflush(f_integ);
}

// ============================================================
// PUBLIC: iterEnd() — call at the END of each main-loop iteration
// (and once with iter==0 right after BatchEval 1 for the checkpoint).
// ============================================================
static bool isCheckpoint(int iter) {
    for (int i = 0; i < N_CKPT; ++i) if (CKPTS[i] == iter) return true;
    return false;
}
static void iterEnd(int iter, float cpu_s, const Turtle* pop,
                    const EliteArchive& ea, const PairMemory& pm,
                    const M0Pool& m0, float bestZ) {
    float wall_s = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_wallStart).count();

    if (bestZ > g_bestZseen) { g_bestZseen = bestZ; g_bestAtIter = iter; }

    if (iter % DIAG_TRAJ_EVERY == 0) writeTrajectory(iter, wall_s, cpu_s, bestZ, (int)pm.size());

    // reproducibility trajectory hash: fold bestZ every 100 iters
    if (iter % 100 == 0) g_trajHash = fnvFloat(g_trajHash, bestZ);

    if (iter % DIAG_WINDOW_EVERY == 0) {
        writeDiversity(iter, wall_s, pop);
        writeBasin(iter, pop, ea);
        writeM0(iter, m0, ea);
        writeStageWindow(iter, DIAG_WINDOW_EVERY);
    }
    if (iter % DIAG_PM_EVERY == 0) writePairMemory(iter, pm);

    // integrity: feasibility every iter; objective recompute every PM interval
    writeIntegrity(iter, ea, (iter % DIAG_PM_EVERY == 0));

    if (isCheckpoint(iter)) {
        dumpPopulation(iter, pop, ea);
        if (f_traj) fflush(f_traj);
        if (f_div)  fflush(f_div);
        if (f_basin) fflush(f_basin);
        if (f_m0)   fflush(f_m0);
        if (f_swin) fflush(f_swin);
        if (f_pm)   fflush(f_pm);
    }
}

// ============================================================
// PUBLIC: bestEvent() — call inside the global-best-improved block.
// Both turtles must be decoded (cacheValid) — true after BatchEval 3.
// ============================================================
static void bestEvent(int iter, const Turtle& newBest, const Turtle& oldBest,
                      float oldZ, float newZ, const PairMemory& pm) {
    if (!f_best) return;
    float wall_s = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_wallStart).count();
    int nChanged = 0;
    for (int pos = 0; pos < N_Order; ++pos)
        if (newBest.Order_seq[pos] != oldBest.Order_seq[pos] ||
            newBest.M_select[pos]  != oldBest.M_select[pos]) nChanged++;
    // stage/operator attribution: best reconciliation is post-batch, so
    // we cannot attribute exactly (spec §3.4 caveat). Emit "NA".
    fprintf(f_best, "%d,%.3f,NA,NA,0,%.6f,%.6f,%.6f,%d,%d,%d,%d,%llu,%llu\n",
        iter, wall_s, oldZ, newZ, newZ - oldZ, g_ccrit, g_cweak, (int)pm.size(),
        nChanged, (unsigned long long)acceptHash(newBest), (unsigned long long)seqHash(newBest));
    fflush(f_best);
    g_bestEvents++;
}

// ============================================================
// PUBLIC: end() — call after the main loop exits.
//   Writes FINAL population dump, runtime.txt, final_summary.txt,
//   code_health_map.txt, and appends the reproducibility row.
// ============================================================
static void writeRuntime(int finalIter, float finalCpu, float finalWall, float bestZ) {
    char p[1024]; snprintf(p, sizeof(p), "%s/runtime.txt", g_dir);
    FILE* f = fopen(p, "w"); if (!f) return;
    fprintf(f, "build=Release_DIAG\n");
    fprintf(f, "clock_type=wall+cpu\n");
    fprintf(f, "N=%d M=%d Seed=%d repeat=%d\n", g_N, g_M, g_Seed, g_repeat);
    fprintf(f, "max_iterations=%d end_time=%.1f\n", g_maxIter, (float)g_endTime);
    fprintf(f, "final_iter=%d final_time_s=%.3f final_cpu_s=%.3f\n", finalIter, finalWall, finalCpu);
    fprintf(f, "iters_per_sec=%.2f\n", finalWall > 0 ? finalIter / finalWall : 0.0f);
    fprintf(f, "final_bestZ=%.6f\n", bestZ);
    fprintf(f, "traj_hash=%llu\n", (unsigned long long)g_trajHash);
    // compiler/flags/cpu/exe_sha256/config_sha256 appended by the batch script.
    fclose(f);
}
static void writeFinalSummary(int finalIter, float finalWall, float bestZ, const PairMemory& pm) {
    char p[1024]; snprintf(p, sizeof(p), "%s/final_summary.txt", g_dir);
    FILE* f = fopen(p, "w"); if (!f) return;
    fprintf(f, "STMO Run009 diagnostic — N=%d M=%d Seed=%d repeat=%d\n", g_N, g_M, g_Seed, g_repeat);
    fprintf(f, "final_bestZ          = %.6f\n", bestZ);
    fprintf(f, "best_at_iter         = %d\n", g_bestAtIter);
    fprintf(f, "final_iter           = %d\n", finalIter);
    fprintf(f, "final_time_s         = %.3f\n", finalWall);
    fprintf(f, "pm_peak              = %d\n", g_pm_peak);
    fprintf(f, "pm_final             = %d\n", (int)pm.size());
    fprintf(f, "ccrit_peak           = %d\n", g_ccrit_peak);
    fprintf(f, "ccrit_zero_iter      = %d\n", g_ccrit_zero_iter);
    fprintf(f, "distinct_full_first  = %d\n", g_firstCkptDistinct);
    fprintf(f, "distinct_full_last   = %d\n", g_lastDistinct);
    fprintf(f, "total_best_events    = %ld\n", g_bestEvents);
    fprintf(f, "restarts             = %d\n", g_restartCount);
    fprintf(f, "traj_hash            = %llu\n", (unsigned long long)g_trajHash);
    fclose(f);
}
static void writeCodeHealthMap(const PairMemory& pm, const TripletMemory& tm, const EliteArchive& ea) {
    char p[1024]; snprintf(p, sizeof(p), "%s/code_health_map.txt", g_dir);
    FILE* f = fopen(p, "w"); if (!f) return;
    fprintf(f, "STMO Run009 — static layer / dead-code map (N=%d M=%d Seed=%d repeat=%d)\n", g_N, g_M, g_Seed, g_repeat);
    fprintf(f, "================================================================\n\n");
    fprintf(f, "[STAGES] (proposals / accepts / global-bests this run)\n");
    fprintf(f, "  S1 OceanCurrentDrift : prop=%ld acc=%ld gbest=%ld  -> %s\n",
        g_diag_ch_prop[1], g_s1_acc, g_s1_gbest, verdict(g_diag_ch_prop[1], g_s1_acc, g_s1_gbest));
    fprintf(f, "  S2 MemoryAwareDrift  : prop=%ld acc=%ld strict=%ld gbest=%ld  -> %s\n",
        g_diag_ch_prop[2], g_s2_acc, g_s2_strict, g_s2_gbest, verdict(g_diag_ch_prop[2], g_s2_acc, g_s2_gbest));
    fprintf(f, "  S3 ACMM              : memory-update stage (no accept counter); fires every iter\n");
    fprintf(f, "  S4 MFBO              : prop=%ld acc=%ld gbest=%ld  -> %s\n",
        g_diag_ch_prop[4], g_s4_acc, g_s4_gbest, verdict(g_diag_ch_prop[4], g_s4_acc, g_s4_gbest));
    fprintf(f, "  S5 CMA               : prop=%ld acc=%ld gbest=%ld  -> %s\n",
        g_diag_ch_prop[5], g_s5_acc, g_s5_gbest, verdict(g_diag_ch_prop[5], g_s5_acc, g_s5_gbest));
    fprintf(f, "\n[MEMORY STRUCTURES]\n");
    fprintf(f, "  PairMemory     : written=yes  read-by-decision=yes  final_size=%d peak=%d\n", (int)pm.size(), g_pm_peak);
    fprintf(f, "  TripletMemory  : ENABLE_TRIPLETS=%d  final_size=%d  -> %s\n",
        ENABLE_TRIPLETS, (int)tm.size(),
        (ENABLE_TRIPLETS ? "compiled" : "DISABLED: never written, never read (runODTP + Stage5 triplet op #if'd out)"));
    fprintf(f, "  EliteArchive   : written=yes (batchEvaluate)  read-by-decision=yes (Stage5)  size=%d/%d\n", ea.count, g_eSize);
    fprintf(f, "  M0Pool         : written=yes (batchEvaluate)  read-by-decision=yes (Stage2/Stage5)\n");
    fprintf(f, "\n[NOTES]\n");
    fprintf(f, "  restarts this run = %d (RESTART_STAG=%d, fires when stagn>=RESTART_STAG && Ccrit==0 && t<0.8*end)\n", g_restartCount, RESTART_STAG);
    fprintf(f, "  stagnationReset() is defined in memory_ops.h but NOT called by Run7 main loop (superseded by convergenceRestart).\n");
    fprintf(f, "  ODTP/TripletMemory aging code present but inert while ENABLE_TRIPLETS=0.\n");
    fclose(f);
}
// helper: classify a stage as fires&contributes / fires-no-improve / never-fires
static const char* verdict(long prop, long acc, long g) {
    if (prop == 0 && acc == 0) return "NEVER FIRES";
    if (g > 0) return "fires & contributes (improves global best)";
    if (acc > 0) return "fires & accepts but never improves global best";
    return "fires but never accepts";
}

static void appendReproRow(int finalIter, float finalWall, float bestZ) {
    char p[1024]; snprintf(p, sizeof(p), "%s/reproducibility.csv", g_rootDir);
    bool exists = false;
    { FILE* t = fopen(p, "r"); if (t) { exists = true; fclose(t); } }
    FILE* f = fopen(p, "a"); if (!f) return;
    if (!exists) fprintf(f, "N,M,Seed,repeat_idx,final_bestZ,traj_hash,final_iter,wall_time_s\n");
    fprintf(f, "%d,%d,%d,%d,%.6f,%llu,%d,%.3f\n",
        g_N, g_M, g_Seed, g_repeat, bestZ, (unsigned long long)g_trajHash, finalIter, finalWall);
    fclose(f);
}

static void end(int finalIter, float finalCpu, const Turtle* pop, const EliteArchive& ea,
                const PairMemory& pm, const TripletMemory& tm, const M0Pool& m0, float bestZ) {
    (void)m0;
    float finalWall = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_wallStart).count();
    // FINAL population dump (always, regardless of where the loop landed)
    dumpPopulation(finalIter, pop, ea);
    g_lastDistinct = 0; { std::set<uint64_t> s; for (int p = 0; p < P; ++p) s.insert(fullHash(pop[p])); g_lastDistinct = (int)s.size(); }
    writeRuntime(finalIter, finalCpu, finalWall, bestZ);
    writeFinalSummary(finalIter, finalWall, bestZ, pm);
    writeCodeHealthMap(pm, tm, ea);
    appendReproRow(finalIter, finalWall, bestZ);
    // close handles
    FILE* handles[] = { f_traj, f_pop, f_div, f_best, f_swin, f_pm, f_basin, f_m0, f_integ };
    for (int i = 0; i < 9; ++i) if (handles[i]) fclose(handles[i]);
    f_traj=f_pop=f_div=f_best=f_swin=f_pm=f_basin=f_m0=f_integ=NULL;
}

} // namespace diag

// (DIAG_PROPOSE / DIAG_ACCEPT are defined in config.h so the stage hooks
//  see them; they expand to nothing when DIAG_MODE == 0.)

#endif // DIAG_MODE

#endif // DIAG_H
