// ============================================================
// stages.h — STMO Algorithm Stages
// ============================================================
// Run003 changes in this file:
//   A3  Stage 1 uses g_nc          (was NC)
//   A4  Stage 2 uses g_machineBudget (was MACHINE_BUDGET)
//   B2  Stage 5 uses TripletMemory  (triplet transfer — was unused)
//   B3  Stage 4 breaks the weak (ji,jj) pair via moveOrderSwap (Run005 revert)
//   B4  Stage 4 resets stage4_phase1 before the hasTarget check
//
// Acceptance criteria:
//   Stage 1: deltaZ >= 0   Stage 2: deltaZ >= 0
//   Stage 4: deltaZ >  0   Stage 5: deltaZ >  0
// ============================================================

#ifndef STAGES_H
#define STAGES_H

#include "config.h"
#include "stmo_types.h"
#include "problem.h"
#include "memory_ops.h"
#include <stdio.h>
#include <math.h>
#include <algorithm>
#include <vector>
#include <utility>

// ============================================================
// BATCH EVALUATE
// ============================================================
EvalSnapshot batchEvaluate(Population pop, EliteArchive& ea, M0Pool& m0,
                            int iteration, bool updateElite, bool updateM0) {
    EvalSnapshot snap;
    snap.iteration = iteration;

    for (int p = 0; p < P; p++) decodeAndEval(pop[p]);

    for (int p = 0; p < P; p++) {
        const Turtle& t = pop[p];
        for (int m = 0; m < M_Machine; m++) {
            for (int k = 0; k < t.machineCount[m] - 1; k++) {
                int ji = t.machineSeq[m][k];
                int jj = t.machineSeq[m][k+1];
                PairObservation obs;
                obs.ji = ji; obs.jj = jj;
                obs.score = calcPairScore(ji, jj, t.ctimes[jj-1]);
                obs.turtleIdx = p;
                snap.observations.push_back(obs);
            }
        }
    }

    if (!snap.observations.empty()) {
        float sum = 0.0f;
        for (const auto& obs : snap.observations) sum += obs.score;
        snap.popMu = sum / (float)snap.observations.size();
        float varSum = 0.0f;
        for (const auto& obs : snap.observations)
            varSum += (obs.score - snap.popMu) * (obs.score - snap.popMu);
        snap.popSigma = sqrtf(varSum / (float)snap.observations.size());
    }

    if (updateElite) updateEliteArchive(ea, pop);
    if (updateM0)    updateM0Pool(m0, pop);
    return snap;
}

// ============================================================
// STAGE 1 — OceanCurrentDrift   (A3: g_nc candidates)
// ============================================================
void stage1_OceanCurrentDrift(Population pop) {
    for (int p = 0; p < P; p++) {
        Turtle& t   = pop[p];
        Turtle  best = t;
        for (int c = 0; c < g_nc; c++) {           // A3: adaptive candidates
            Turtle candidate = generateRandomMove(t);
            decodeAndEval(candidate);
            if (candidate.obj > best.obj) best = candidate;
        }
        if (best.obj >= t.obj && validateTurtle(best)) pop[p] = best;   // guard accept
        ASSERT_VALID(t);
    }
}

// ============================================================
// STAGE 2 — MemoryAwareDrift   (A4: g_machineBudget)
// ============================================================
void stage2_MemoryAwareDrift(Population pop, const PairMemory& pm, const M0Pool& m0) {
    if (pm.empty()) return;

    for (int p = 0; p < P; p++) {
        Turtle& t = pop[p];
        if (!t.cacheValid) decodeAndEval(t);
        t.stage2_repairs = 0;

        for (int m = 0; m < M_Machine; m++) {
            if (t.machineCount[m] < 2) continue;

            struct WeakPair { int posK, ji, jj; float score; };
            std::vector<WeakPair> weakPairs;

            for (int k = 0; k < t.machineCount[m] - 1; k++) {
                int ji = t.machineSeq[m][k];
                int jj = t.machineSeq[m][k+1];
                char lbl = lookupPairLabel(pm, ji, jj);
                if (lbl == LABEL_WEAK || lbl == LABEL_CRITICAL) {
                    WeakPair wp; wp.posK = k; wp.ji = ji; wp.jj = jj;
                    wp.score = lookupPairScore(pm, ji, jj);
                    weakPairs.push_back(wp);
                }
            }
            if (weakPairs.empty()) continue;

            std::sort(weakPairs.begin(), weakPairs.end(),
                [](const WeakPair& a, const WeakPair& b){ return a.score < b.score; });

            int budget = std::min((int)weakPairs.size(), g_machineBudget); // A4

            for (int wi = 0; wi < budget; wi++) {
                int jj = weakPairs[wi].jj;
                bool repaired = false;

                int encPos = -1;
                for (int pos = 0; pos < N_Order; pos++)
                    if (t.Order_seq[pos] == jj) { encPos = pos; break; }
                if (encPos < 0) continue;

                for (int attempt = 0; attempt < MAX_ATTEMPTS && !repaired; attempt++) {
                    Turtle candidate;
                    float  newObj = t.obj;

                    if (attempt < 2) {
                        candidate = moveOrderInsert(t, encPos, -1);
                        decodeAndEval(candidate);
                        newObj = candidate.obj;
                    } else if (attempt == 2) {
                        candidate = moveMachineRevise(t, encPos, 0);
                        decodeAndEval(candidate);
                        newObj = candidate.obj;
                    } else {
                        if (!m0.empty()) {
                            int m0JobId = m0[attempt % (int)m0.size()].jobId;
                            int m0Pos = -1;
                            for (int pos = 0; pos < N_Order; pos++)
                                if (t.Order_seq[pos] == m0JobId && t.M_select[pos] == 0) { m0Pos = pos; break; }
                            if (m0Pos >= 0) {
                                candidate = moveOrderSwap(t, encPos, m0Pos);
                                candidate.M_select[encPos] = m + 1;
                                candidate.cacheValid = false;
                                decodeAndEval(candidate);
                                newObj = candidate.obj;
                            } else continue;
                        } else continue;
                    }

                    if (newObj >= t.obj && validateTurtle(candidate)) {
                        t = candidate;
                        t.stage2_repairs++;
                        repaired = true;
                    }
                }
            }
        }
        ASSERT_VALID(t);
    }
}

// ============================================================
// STAGE 3 — ACMM
// ============================================================
void stage3_ACMM(Population pop, PairMemory& pm, TripletMemory& tm,
                 const EvalSnapshot& snap, StructuralMap* structMaps) {
    updatePairMemory(pm, snap);
    labelAllPairs(pm);
    runODTP(pm, tm, pop);
    for (int p = 0; p < P; p++) {
        if (pop[p].cacheValid) structMaps[p] = buildStructuralMap(pop[p], pm);
        else structMaps[p] = StructuralMap();
        ASSERT_VALID(pop[p]);   // ACMM is memory-only; turtles must be untouched
    }
}

// ============================================================
// STAGE 4 — MFBO (Phase 1)
// B3: break the weak (ji,jj) pair via moveOrderSwap (Run005 revert; not relocate).
// B4: reset stage4_phase1 BEFORE the hasTarget check.
// ============================================================
void stage4_MFBO(Population pop, const StructuralMap* structMaps, const PairMemory& pm) {
    for (int p = 0; p < P; p++) {
        Turtle& t = pop[p];
        const StructuralMap& sm = structMaps[p];

        t.stage4_phase1 = 0;            // B4: reset unconditionally
        if (!sm.hasTarget) continue;

        bool anyImproved = false;

        for (int ti = 0; ti < (int)sm.targets.size() && !anyImproved; ti++) {
            int jj = sm.targets[ti].jj;

            // Build Weak Pool: other weak second jobs in this turtle
            std::vector<int> weakPool;
            for (int m = 0; m < M_Machine; m++) {
                for (int k = 0; k < t.machineCount[m] - 1; k++) {
                    int ji_k = t.machineSeq[m][k];
                    int jk   = t.machineSeq[m][k+1];
                    if (jk == jj) continue;
                    char lbl = lookupPairLabel(pm, ji_k, jk);
                    if (lbl == LABEL_WEAK || lbl == LABEL_CRITICAL) weakPool.push_back(jk);
                }
            }
            if (weakPool.empty()) continue;

            std::sort(weakPool.begin(), weakPool.end());
            weakPool.erase(std::unique(weakPool.begin(), weakPool.end()), weakPool.end());

            auto tryCandidates = [&](char targetLabel) -> bool {
                struct Candidate { int jk; float score; };
                std::vector<Candidate> candidates;
                for (int jk : weakPool) {
                    auto key = std::make_pair(jj, jk);
                    auto it  = pm.find(key);
                    if (it == pm.end()) continue;
                    if (it->second.label != targetLabel) continue;
                    if (it->second.count < g_minCount) continue;
                    bool isProtected = false;
                    for (const auto& sp : sm.strongPairs)
                        if (sp.first == jk || sp.second == jk) { isProtected = true; break; }
                    if (isProtected) continue;
                    Candidate c; c.jk = jk; c.score = it->second.avgScore;
                    candidates.push_back(c);
                }
                std::sort(candidates.begin(), candidates.end(),
                    [](const Candidate& a, const Candidate& b){ return a.score > b.score; });

                for (const auto& cand : candidates) {
                    int jk = cand.jk;
                    int pos_jj = -1, pos_jk = -1;
                    for (int pos = 0; pos < N_Order; pos++) {
                        if (t.Order_seq[pos] == jj) pos_jj = pos;
                        if (t.Order_seq[pos] == jk) pos_jk = pos;
                    }
                    if (pos_jj < 0 || pos_jk < 0) continue;

                    Turtle candidate = moveOrderSwap(t, pos_jj, pos_jk);
                    decodeAndEval(candidate);

                    if (candidate.obj > t.obj && validateTurtle(candidate)) {   // strict
                        t = candidate;
                        t.stage4_phase1++;
                        return true;
                    }
                }
                return false;
            };

            bool improved = tryCandidates(LABEL_STRONG);
            if (!improved) improved = tryCandidates(LABEL_NEUTRAL);
            if (improved) anyImproved = true;
        }
        // Phase 2 VNS — DEFERRED (professor approval).
        ASSERT_VALID(t);
    }
}

// ============================================================
// STAGE 5 — CMA
// B2: Operation 0 — TripletMemory transfer (was built but unused).
//     Then Operation 1 (elite pair transfer) and Operation 2 (M0).
//
// Triplet transfer: for each learned STRONG triplet (ja,jb,jc) that
// an elite holds but this turtle lacks, relocate jb after ja and jc
// after jb on ja's machine — forming the 3-job pattern. Accept ΔZ>0.
// ============================================================
void stage5_CMA(Population pop, const EliteArchive& ea, const PairMemory& pm,
                const TripletMemory& tm, const M0Pool& m0) {

    for (int p = 0; p < P; p++) {
        Turtle& t = pop[p];
        if (!t.cacheValid) decodeAndEval(t);
        bool improved = false;

        // ── Operation 0 (B2): Triplet transfer from TripletMemory ──
        for (int tIdx = 0; tIdx < (int)tm.size() && !improved; tIdx++) {
            const TripletRecord& tr = tm[tIdx];
            if (tr.score <= 0.0f) continue;
            int ja = tr.ja, jb = tr.jb, jc = tr.jc;

            // Require the triplet to appear in at least one elite
            bool inElite = false;
            for (int e = 0; e < ea.count && !inElite; e++) {
                const Turtle& el = ea.turtles[e];
                if (!el.cacheValid) continue;
                for (int m = 0; m < M_Machine && !inElite; m++)
                    for (int k = 0; k < el.machineCount[m] - 2; k++)
                        if (el.machineSeq[m][k]   == ja &&
                            el.machineSeq[m][k+1] == jb &&
                            el.machineSeq[m][k+2] == jc) { inElite = true; break; }
            }
            if (!inElite) continue;

            // Skip if turtle already has the triplet consecutive
            bool inT = false;
            for (int m = 0; m < M_Machine && !inT; m++)
                for (int k = 0; k < t.machineCount[m] - 2; k++)
                    if (t.machineSeq[m][k]   == ja &&
                        t.machineSeq[m][k+1] == jb &&
                        t.machineSeq[m][k+2] == jc) { inT = true; break; }
            if (inT) continue;

            // Locate ja in t and use its machine as the anchor
            int pa = -1;
            for (int pos = 0; pos < N_Order; pos++)
                if (t.Order_seq[pos] == ja) { pa = pos; break; }
            if (pa < 0) continue;
            int machJa = t.M_select[pa];
            if (machJa == 0) continue;   // ja rejected — skip

            // Step 1: relocate jb immediately after ja, set jb's machine
            int pb = -1;
            for (int pos = 0; pos < N_Order; pos++)
                if (t.Order_seq[pos] == jb) { pb = pos; break; }
            if (pb < 0) continue;
            Turtle candidate = relocateAfter(t, pb, pa);
            for (int pos = 0; pos < N_Order; pos++)
                if (candidate.Order_seq[pos] == jb) { candidate.M_select[pos] = machJa; break; }

            // Step 2: relocate jc immediately after jb, set jc's machine
            int npb = -1, npc = -1;
            for (int pos = 0; pos < N_Order; pos++) {
                if (candidate.Order_seq[pos] == jb) npb = pos;
                if (candidate.Order_seq[pos] == jc) npc = pos;
            }
            if (npb >= 0 && npc >= 0) {
                candidate = relocateAfter(candidate, npc, npb);
                for (int pos = 0; pos < N_Order; pos++)
                    if (candidate.Order_seq[pos] == jc) { candidate.M_select[pos] = machJa; break; }
            }
            candidate.cacheValid = false;
            decodeAndEval(candidate);

            if (candidate.obj > t.obj && validateTurtle(candidate)) { t = candidate; improved = true; }
        }

        // ── Operation 1: Elite pair transfer (STRONG pairs) ──────
        for (int e = 0; e < ea.count && !improved; e++) {
            const Turtle& elite = ea.turtles[e];
            if (!elite.cacheValid) continue;
            for (int em = 0; em < M_Machine && !improved; em++) {
                for (int k = 0; k < elite.machineCount[em] - 1 && !improved; k++) {
                    int e_ji = elite.machineSeq[em][k];
                    int e_jj = elite.machineSeq[em][k+1];
                    if (!pairHasLabel(pm, e_ji, e_jj, LABEL_STRONG)) continue;

                    bool presentInT = false;
                    for (int tm2 = 0; tm2 < M_Machine && !presentInT; tm2++)
                        for (int tk = 0; tk < t.machineCount[tm2] - 1; tk++)
                            if (t.machineSeq[tm2][tk]   == e_ji &&
                                t.machineSeq[tm2][tk+1] == e_jj) { presentInT = true; break; }
                    if (presentInT) continue;

                    int pos_ji = -1, pos_jj_in_t = -1;
                    for (int pos = 0; pos < N_Order; pos++) {
                        if (t.Order_seq[pos] == e_ji) pos_ji = pos;
                        if (t.Order_seq[pos] == e_jj) pos_jj_in_t = pos;
                    }
                    if (pos_ji < 0 || pos_jj_in_t < 0) continue;

                    int machJi = t.M_select[pos_ji];
                    if (machJi == 0) continue;

                    // Use relocateAfter to actually place e_jj right after e_ji
                    Turtle candidate = relocateAfter(t, pos_jj_in_t, pos_ji);
                    for (int pos = 0; pos < N_Order; pos++)
                        if (candidate.Order_seq[pos] == e_jj) { candidate.M_select[pos] = machJi; break; }
                    candidate.cacheValid = false;
                    decodeAndEval(candidate);

                    if (candidate.obj > t.obj && validateTurtle(candidate)) { t = candidate; improved = true; }
                }
            }
        }

        // ── Operation 2: M0 reinsertion ──────────────────────────
        for (int mi = 0; mi < (int)m0.size() && !improved; mi++) {
            int rejJobId = m0[mi].jobId;
            int rejPos = -1;
            for (int pos = 0; pos < N_Order; pos++)
                if (t.Order_seq[pos] == rejJobId && t.M_select[pos] == 0) { rejPos = pos; break; }
            if (rejPos < 0) continue;
            for (int mach = 1; mach <= M_Machine && !improved; mach++) {
                Turtle candidate = moveMachineRevise(t, rejPos, mach);
                decodeAndEval(candidate);
                if (candidate.obj > t.obj && validateTurtle(candidate)) { t = candidate; improved = true; }
            }
        }
        ASSERT_VALID(t);
    }
}

#endif // STAGES_H
