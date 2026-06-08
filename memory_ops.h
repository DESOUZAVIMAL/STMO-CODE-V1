// ============================================================
// memory_ops.h — STMO Memory Operations
// ============================================================

#ifndef MEMORY_OPS_H
#define MEMORY_OPS_H

#include "config.h"
#include "stmo_types.h"
#include "problem.h"
#include <math.h>
#include <algorithm>
#include <vector>
#include <utility>

// ============================================================
// PAIR MEMORY — LOOKUP HELPERS (read-only)
// ============================================================
char lookupPairLabel(const PairMemory& pm, int ji, int jj) {
    auto key = std::make_pair(ji, jj);
    auto it = pm.find(key);
    if (it == pm.end()) return LABEL_NONE;
    if (it->second.count < g_minCount) return LABEL_NONE;
    return it->second.label;
}

float lookupPairScore(const PairMemory& pm, int ji, int jj) {
    auto key = std::make_pair(ji, jj);
    auto it = pm.find(key);
    if (it == pm.end()) return 0.0f;
    return it->second.avgScore;
}

bool pairHasLabel(const PairMemory& pm, int ji, int jj, char targetLabel) {
    return (lookupPairLabel(pm, ji, jj) == targetLabel);
}

// ============================================================
// UPDATE PAIR MEMORY — two-pass decay + update
// ============================================================
void updatePairMemory(PairMemory& pm, const EvalSnapshot& snap) {
    std::map<std::pair<int,int>, float> freshScores;
    for (const auto& obs : snap.observations) {
        auto key = std::make_pair(obs.ji, obs.jj);
        auto it = freshScores.find(key);
        if (it == freshScores.end()) freshScores[key] = obs.score;
        else it->second = (it->second + obs.score) * 0.5f;
    }

    // PASS 1: decay non-observed
    for (auto& entry : pm) {
        if (freshScores.find(entry.first) == freshScores.end()) {
            entry.second.avgScore *= DF;
            entry.second.age++;
            entry.second.label = LABEL_NONE;
        }
    }

    // PASS 2: update fresh
    for (const auto& fresh : freshScores) {
        auto key = fresh.first;
        float newScore = fresh.second;
        auto it = pm.find(key);
        if (it == pm.end()) {
            PairRecord rec;
            rec.avgScore = newScore; rec.count = 1; rec.age = 0; rec.label = LABEL_NONE;
            pm[key] = rec;
        } else {
            PairRecord& rec = it->second;
            rec.avgScore = (rec.avgScore * rec.count + newScore) / (float)(rec.count + 1);
            rec.count++; rec.age = 0; rec.label = LABEL_NONE;
        }
    }

    // Ghost cleanup — prune ONLY insignificant pairs, never by score sign.
    // ghost  = decayed near-zero POSITIVE noise.
    // stale  = not observed for GHOST_MAX_AGE iters (bounds memory; age resets
    //          to 0 whenever a pair is re-observed in PASS 2 above).
    // Negative-score pairs are CRITICAL repair targets and MUST survive to be
    // labelled by labelAllPairs.
    std::vector<std::pair<int,int>> toRemove;
    for (const auto& entry : pm) {
        const PairRecord& r = entry.second;
        bool positiveGhost = (r.avgScore >= 0.0f && r.avgScore < GHOST_THRESHOLD);
        bool stale         = (r.age > GHOST_MAX_AGE);
        if (positiveGhost || stale) toRemove.push_back(entry.first);
    }
    for (const auto& key : toRemove) pm.erase(key);
}

// ============================================================
// LABEL ALL PAIRS
// ============================================================
void labelAllPairs(PairMemory& pm) {
    if (pm.empty()) return;

    std::vector<float> scores;
    scores.reserve(pm.size());
    for (const auto& entry : pm)
        if (entry.second.count >= g_minCount) scores.push_back(entry.second.avgScore);
    if (scores.empty()) return;

    float mu = 0.0f;
    for (float s : scores) mu += s;
    mu /= (float)scores.size();

    float sigma = 0.0f;
    for (float s : scores) sigma += (s - mu) * (s - mu);
    sigma = sqrtf(sigma / (float)scores.size());

    float strongThresh = mu + STRONG_SIGMA_MULT * sigma;
    float weakThresh   = mu - WEAK_SIGMA_MULT   * sigma;
    if (weakThresh < 0.0f) weakThresh = 0.0f;

    for (auto& entry : pm) {
        PairRecord& rec = entry.second;
        if (rec.count < g_minCount) { rec.label = LABEL_NONE; continue; }
        if (rec.avgScore < 0.0f)              rec.label = LABEL_CRITICAL;
        else if (rec.avgScore < weakThresh)   rec.label = LABEL_WEAK;
        else if (rec.avgScore >= strongThresh)rec.label = LABEL_STRONG;
        else                                  rec.label = LABEL_NEUTRAL;
    }
}

// ============================================================
// RUN ODTP — Overlap-Driven Triplet Promotion
// Run003 B1: triplet aging fixed. Age ALL triplets at the start;
// reinforcement during the scan resets age to 0. This correctly
// expires triplets that were reinforced once then abandoned.
// ============================================================
void runODTP(const PairMemory& pm, TripletMemory& tm, Population pop) {

    // B1 FIX: age every existing triplet up-front. Reinforced ones
    // get reset to age=0 below. (Old code only aged age>0, so a
    // freshly-reinforced-then-dropped triplet never expired.)
    for (auto& tr : tm) tr.age++;

    // Collect STRONG pairs
    std::vector<std::pair<int,int>> strongPairs;
    for (const auto& entry : pm)
        if (entry.second.label == LABEL_STRONG) strongPairs.push_back(entry.first);

    for (const auto& ab : strongPairs) {
        int ja = ab.first;
        int jb = ab.second;
        for (const auto& bc : strongPairs) {
            if (bc.first != jb) continue;
            int jc = bc.second;
            if (jc == ja) continue;

            int coCount = 0;
            float tripScore = 0.0f;
            for (int p = 0; p < P; p++) {
                const Turtle& t = pop[p];
                if (!t.cacheValid) continue;
                for (int m = 0; m < M_Machine; m++) {
                    for (int k = 0; k < t.machineCount[m] - 2; k++) {
                        if (t.machineSeq[m][k]   == ja &&
                            t.machineSeq[m][k+1] == jb &&
                            t.machineSeq[m][k+2] == jc) {
                            coCount++;
                            float s1 = lookupPairScore(pm, ja, jb);
                            float s2 = lookupPairScore(pm, jb, jc);
                            tripScore = (s1 + s2) * 0.5f;
                        }
                    }
                }
            }

            if (coCount >= TRIPLET_MIN_COUNT && tripScore > 0.0f) {
                bool found = false;
                for (auto& tr : tm) {
                    if (tr.ja == ja && tr.jb == jb && tr.jc == jc) {
                        tr.count++;
                        tr.score = (tr.score * (tr.count-1) + tripScore) / (float)tr.count;
                        tr.age = 0;          // reinforced this iter
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    TripletRecord tr;
                    tr.ja = ja; tr.jb = jb; tr.jc = jc;
                    tr.score = tripScore; tr.count = coCount; tr.age = 0;
                    tm.push_back(tr);
                }
            }
        }
    }

    // Remove very old triplets (now ages correctly)
    tm.erase(std::remove_if(tm.begin(), tm.end(),
        [](const TripletRecord& tr){ return tr.age > 2 * g_kStag; }),
        tm.end());
}

// ============================================================
// BUILD STRUCTURAL MAP — top-3 worst pairs (Run002 D2)
// ============================================================
StructuralMap buildStructuralMap(const Turtle& t, const PairMemory& pm) {
    StructuralMap sm;
    struct CandidateTarget { int ji, jj; float score; };
    std::vector<CandidateTarget> weakTargets;

    for (int m = 0; m < M_Machine; m++) {
        for (int k = 0; k < t.machineCount[m] - 1; k++) {
            int ji = t.machineSeq[m][k];
            int jj = t.machineSeq[m][k+1];
            auto key = std::make_pair(ji, jj);
            auto it  = pm.find(key);
            if (it == pm.end()) continue;
            const PairRecord& rec = it->second;
            if (rec.label == LABEL_STRONG) sm.strongPairs.push_back(key);
            if (rec.label == LABEL_WEAK || rec.label == LABEL_CRITICAL) {
                CandidateTarget ct; ct.ji = ji; ct.jj = jj; ct.score = rec.avgScore;
                weakTargets.push_back(ct);
            }
        }
    }

    std::sort(weakTargets.begin(), weakTargets.end(),
        [](const CandidateTarget& a, const CandidateTarget& b){ return a.score < b.score; });

    int maxTargets = std::min((int)weakTargets.size(), 3);
    for (int i = 0; i < maxTargets; i++) {
        StructuralMap::WeakTarget wt;
        wt.ji = weakTargets[i].ji; wt.jj = weakTargets[i].jj; wt.score = weakTargets[i].score;
        sm.targets.push_back(wt);
    }
    sm.hasTarget = !sm.targets.empty();
    return sm;
}

// ============================================================
// UPDATE ELITE ARCHIVE — Run003 A5: uses g_eSize
// ============================================================
void updateEliteArchive(EliteArchive& ea, Population pop) {
    std::vector<Turtle> candidates;
    // Only feasible turtles may enter the archive (the archive head becomes the
    // reported BestZ). Existing entries are re-validated too — cheap, and keeps
    // the archive invariant airtight.
    for (int i = 0; i < ea.count; i++)
        if (validateTurtle(ea.turtles[i])) candidates.push_back(ea.turtles[i]);
    for (int i = 0; i < P; i++)
        if (pop[i].cacheValid && validateTurtle(pop[i])) candidates.push_back(pop[i]);

    std::sort(candidates.begin(), candidates.end(),
        [](const Turtle& a, const Turtle& b){ return a.obj > b.obj; });

    ea.count = std::min((int)candidates.size(), g_eSize);   // A5: adaptive
    for (int i = 0; i < ea.count; i++) ea.turtles[i] = candidates[i];
}

// ============================================================
// UPDATE M0 POOL
// ============================================================
void updateM0Pool(M0Pool& m0, Population pop) {
    m0.clear();
    std::map<int, int> rejCount;
    std::map<int, float> profitSum;

    for (int p = 0; p < P; p++) {
        if (!pop[p].cacheValid) continue;
        const Turtle& t = pop[p];
        for (int pos = 0; pos < N_Order; pos++) {
            if (t.M_select[pos] == 0) {
                int jobId = t.Order_seq[pos];
                rejCount[jobId]++;
                profitSum[jobId] += revenue[jobId-1];
            }
        }
    }

    for (const auto& entry : rejCount) {
        int jobId = entry.first;
        int count = entry.second;
        float avgProf = profitSum[jobId] / (float)count;
        M0Entry e; e.jobId = jobId; e.marginalProfit = avgProf; e.frequency = count;
        m0.push_back(e);
    }

    std::sort(m0.begin(), m0.end(),
        [](const M0Entry& a, const M0Entry& b){ return a.marginalProfit > b.marginalProfit; });
}

// ============================================================
// STAGNATION RESET
// ============================================================
void stagnationReset(PairMemory& pm) {
    std::vector<std::pair<std::pair<int,int>, int>> weakEntries;
    for (const auto& entry : pm)
        if (entry.second.label == LABEL_WEAK)
            weakEntries.push_back({entry.first, entry.second.age});
    if (weakEntries.empty()) return;

    typedef std::pair<std::pair<int,int>, int> WeakEntry;
    std::sort(weakEntries.begin(), weakEntries.end(),
        [](const WeakEntry& a, const WeakEntry& b){ return a.second > b.second; });

    int removeCount = (int)(weakEntries.size() * STAG_RESET_FRACTION);
    if (removeCount < 1) removeCount = 1;
    for (int i = 0; i < removeCount; i++) pm.erase(weakEntries[i].first);

    printf("[StagnationReset] Removed %d/%d WEAK records from PairMemory\n",
           removeCount, (int)weakEntries.size());
}

#endif // MEMORY_OPS_H
