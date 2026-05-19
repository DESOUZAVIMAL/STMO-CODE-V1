// ============================================================
// memory_ops.h — STMO Memory Operations
// ============================================================
// All functions that READ or WRITE persistent memory structures.
//
// SE RULES enforced here:
//   1. Only ACMM (Stage 3) calls updatePairMemory and labelAllPairs
//   2. Only ODTP (inside ACMM) calls runODTP
//   3. Only batchEvaluate calls updateEliteArchive and updateM0Pool
//   4. Two-pass update: decay non-observed FIRST, then update fresh
//   5. Ghost cleanup: remove entries if score < GHOST_THRESHOLD
//   6. Labels computed ONLY after ALL scores are updated
//   7. mu/sigma computed from ALL pairs before ANY labelling
//
// Function ownership (who may call each function):
//   updatePairMemory()   → ACMM only
//   labelAllPairs()      → ACMM only (after updatePairMemory)
//   runODTP()            → ACMM only (after labelAllPairs)
//   buildStructuralMap() → ACMM only (after labelAllPairs)
//   updateEliteArchive() → batchEvaluate only
//   updateM0Pool()       → batchEvaluate only
//   stagnationReset()    → main loop only (when K_STAG triggered)
//   lookupPairLabel()    → Stage 2 and Stage 4 (read only)
//   lookupPairScore()    → Stage 2 and Stage 4 (read only)
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
// PAIR MEMORY — LOOKUP HELPERS
// Read-only access for Stage 2 and Stage 4.
// ============================================================

// Returns the label of pair (ji, jj) from PairMemory.
// Returns LABEL_NONE (' ') if pair not found or count < MIN_COUNT.
// ji and jj are 1-indexed job IDs.
char lookupPairLabel(const PairMemory& pm, int ji, int jj) {
    auto key = std::make_pair(ji, jj);
    auto it = pm.find(key);
    if (it == pm.end()) return LABEL_NONE;
    if (it->second.count < MIN_COUNT) return LABEL_NONE;
    return it->second.label;
}

// Returns the avgScore of pair (ji, jj).
// Returns 0.0f if pair not found.
float lookupPairScore(const PairMemory& pm, int ji, int jj) {
    auto key = std::make_pair(ji, jj);
    auto it = pm.find(key);
    if (it == pm.end()) return 0.0f;
    return it->second.avgScore;
}

// Returns true if pair (ji, jj) exists with label == targetLabel
bool pairHasLabel(const PairMemory& pm, int ji, int jj, char targetLabel) {
    char lbl = lookupPairLabel(pm, ji, jj);
    return (lbl == targetLabel);
}

// ============================================================
// UPDATE PAIR MEMORY — Two-pass algorithm
// Called by ACMM after Batch Evaluate 2.
//
// PASS 1: Decay all entries NOT observed this iteration
//         avgScore *= DF, age++
//
// PASS 2: Update entries OBSERVED this iteration
//         running weighted average, reset age to 0
//
// THEN: Ghost cleanup — remove if avgScore < GHOST_THRESHOLD
//
// SE RULE: decay-first ensures fresh observations receive full
//          weight (not diluted by their own decay).
// SE RULE: labels are NOT updated here — call labelAllPairs() after.
// ============================================================
void updatePairMemory(PairMemory& pm, const EvalSnapshot& snap) {

    // Build set of pairs observed this iteration for fast lookup
    std::map<std::pair<int,int>, float> freshScores;
    for (const auto& obs : snap.observations) {
        auto key = std::make_pair(obs.ji, obs.jj);
        // If same pair observed multiple times (different turtles),
        // take the average of observed scores this iteration
        auto it = freshScores.find(key);
        if (it == freshScores.end()) {
            freshScores[key] = obs.score;
        } else {
            it->second = (it->second + obs.score) * 0.5f;
        }
    }

    // PASS 1: Decay non-observed entries
    for (auto& entry : pm) {
        auto key = entry.first;
        if (freshScores.find(key) == freshScores.end()) {
            // Not observed this iteration — apply decay
            entry.second.avgScore *= DF;
            entry.second.age++;
            entry.second.label = LABEL_NONE; // reset label — will be re-labelled
        }
    }

    // PASS 2: Update fresh observations (running weighted average)
    for (const auto& fresh : freshScores) {
        auto key = fresh.first;
        float newScore = fresh.second;

        auto it = pm.find(key);
        if (it == pm.end()) {
            // New pair — create record
            PairRecord rec;
            rec.avgScore = newScore;
            rec.count    = 1;
            rec.age      = 0;
            rec.label    = LABEL_NONE; // will be labelled by labelAllPairs
            pm[key]      = rec;
        } else {
            // Existing pair — weighted running average
            PairRecord& rec = it->second;
            rec.avgScore = (rec.avgScore * rec.count + newScore)
                         / (float)(rec.count + 1);
            rec.count++;
            rec.age  = 0;
            rec.label = LABEL_NONE; // reset — will be re-labelled
        }
    }

    // Ghost cleanup: remove entries that have faded below threshold
    // This keeps PairMemory lean and removes stale irrelevant entries
    std::vector<std::pair<int,int>> toRemove;
    for (const auto& entry : pm) {
        if (entry.second.avgScore < GHOST_THRESHOLD) {
            toRemove.push_back(entry.first);
        }
    }
    for (const auto& key : toRemove) {
        pm.erase(key);
    }
}

// ============================================================
// LABEL ALL PAIRS — Population-level adaptive labelling
// Called by ACMM after updatePairMemory().
//
// Step 1: Collect ALL avgScores from PairMemory
// Step 2: Compute population mu (mean) and sigma (std deviation)
// Step 3: Label each pair using mu and sigma thresholds
//
// Labels:
//   CRITICAL : score < 0
//   WEAK     : 0 <= score < max(0, mu - WEAK_SIGMA_MULT * sigma)
//   STRONG   : score >= mu + STRONG_SIGMA_MULT * sigma
//   NEUTRAL  : everything else
//
// SE RULE: mu and sigma computed BEFORE any labels assigned.
//          Never label before computing population statistics.
// SE RULE: Only pairs with count >= MIN_COUNT get labelled.
//          Others remain LABEL_NONE (not enough evidence).
// ============================================================
void labelAllPairs(PairMemory& pm) {
    if (pm.empty()) return;

    // Step 1: Collect all scores (only from pairs with enough evidence)
    std::vector<float> scores;
    scores.reserve(pm.size());
    for (const auto& entry : pm) {
        if (entry.second.count >= MIN_COUNT) {
            scores.push_back(entry.second.avgScore);
        }
    }
    if (scores.empty()) return;

    // Step 2: Compute mu and sigma
    float mu = 0.0f;
    for (float s : scores) mu += s;
    mu /= (float)scores.size();

    float sigma = 0.0f;
    for (float s : scores) sigma += (s - mu) * (s - mu);
    sigma = sqrtf(sigma / (float)scores.size());

    // Compute thresholds
    float strongThresh = mu + STRONG_SIGMA_MULT * sigma;
    float weakThresh   = mu - WEAK_SIGMA_MULT   * sigma;
    if (weakThresh < 0.0f) weakThresh = 0.0f; // WEAK never overlaps CRITICAL

    // Step 3: Label each pair
    for (auto& entry : pm) {
        PairRecord& rec = entry.second;

        if (rec.count < MIN_COUNT) {
            rec.label = LABEL_NONE; // insufficient evidence
            continue;
        }

        if (rec.avgScore < 0.0f) {
            rec.label = LABEL_CRITICAL;
        } else if (rec.avgScore < weakThresh) {
            rec.label = LABEL_WEAK;
        } else if (rec.avgScore >= strongThresh) {
            rec.label = LABEL_STRONG;
        } else {
            rec.label = LABEL_NEUTRAL;
        }
    }
}

// ============================================================
// RUN ODTP — Overlap-Driven Triplet Promotion
// Called by ACMM after labelAllPairs().
//
// For each STRONG pair (Ja, Jb):
//   Check if (Jb, Jc) is also STRONG in PairMemory
//   If yes: check if Ja→Jb→Jc appears on same machine in any turtle
//   If confirmed and triplet score > 0 and count >= TRIPLET_MIN_COUNT:
//     Promote (Ja, Jb, Jc) to TripletMemory
//
// SE RULE: ONLY called from ACMM. Never call from other stages.
// SE RULE: Does NOT modify PairMemory — read only.
// ============================================================
void runODTP(const PairMemory& pm,
             TripletMemory& tm,
             Population pop) {

    // Collect all STRONG pairs
    std::vector<std::pair<int,int>> strongPairs;
    for (const auto& entry : pm) {
        if (entry.second.label == LABEL_STRONG) {
            strongPairs.push_back(entry.first);
        }
    }

    // For each STRONG pair (Ja, Jb), check if (Jb, Jc) also STRONG
    for (const auto& ab : strongPairs) {
        int ja = ab.first;
        int jb = ab.second;

        // Find all Jc where (Jb, Jc) is STRONG
        for (const auto& bc : strongPairs) {
            if (bc.first != jb) continue; // must share middle job Jb
            int jc = bc.second;
            if (jc == ja) continue;       // no self-loops

            // Verify Ja→Jb→Jc appears consecutively on same machine
            // by checking across the population
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
                            // Triplet score = score(Ja,Jb) + score(Jb,Jc) / 2
                            float s1 = lookupPairScore(pm, ja, jb);
                            float s2 = lookupPairScore(pm, jb, jc);
                            tripScore = (s1 + s2) * 0.5f;
                        }
                    }
                }
            }

            if (coCount >= TRIPLET_MIN_COUNT && tripScore > 0.0f) {
                // Check if triplet already exists in TripletMemory
                bool found = false;
                for (auto& tr : tm) {
                    if (tr.ja == ja && tr.jb == jb && tr.jc == jc) {
                        // Update existing triplet
                        tr.count++;
                        tr.score = (tr.score * (tr.count-1) + tripScore)
                                 / (float)tr.count;
                        tr.age = 0;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // Add new triplet
                    TripletRecord tr;
                    tr.ja    = ja;
                    tr.jb    = jb;
                    tr.jc    = jc;
                    tr.score = tripScore;
                    tr.count = coCount;
                    tr.age   = 0;
                    tm.push_back(tr);
                }
            }
        }
    }

    // Age all triplets not reinforced this iteration
    for (auto& tr : tm) {
        if (tr.age > 0) tr.age++; // already aged — not seen this iter
    }

    // Remove very old triplets (age > 2 * K_STAG)
    tm.erase(std::remove_if(tm.begin(), tm.end(),
        [](const TripletRecord& tr){ return tr.age > 2 * K_STAG; }),
        tm.end());
}

// ============================================================
// BUILD STRUCTURAL MAP — Per-turtle target identification
// Called by ACMM after labelAllPairs() for each turtle.
//
// Scans turtle's decoded machine sequences to find:
//   1. worst-scored consecutive pair → Stage 4 target
//   2. all STRONG consecutive pairs → Stage 4 protection list
//
// Returns StructuralMap for this turtle.
// The StructuralMap is ephemeral — caller discards after Stage 4.
//
// SE RULE: only reads PairMemory — does NOT write to it.
// SE RULE: returns a fresh StructuralMap each call.
// ============================================================
StructuralMap buildStructuralMap(const Turtle& t, const PairMemory& pm) {
    StructuralMap sm;

    float worstScore = 1e9f;   // track minimum score found
    bool  foundWeak  = false;

    for (int m = 0; m < M_Machine; m++) {
        for (int k = 0; k < t.machineCount[m] - 1; k++) {
            int ji = t.machineSeq[m][k];       // 1-indexed
            int jj = t.machineSeq[m][k+1];     // 1-indexed

            auto key = std::make_pair(ji, jj);
            auto it  = pm.find(key);
            if (it == pm.end()) continue;

            const PairRecord& rec = it->second;

            // Collect STRONG pairs for protection list
            if (rec.label == LABEL_STRONG) {
                sm.strongPairs.push_back(key);
            }

            // Track worst WEAK or CRITICAL pair as the target
            if ((rec.label == LABEL_WEAK || rec.label == LABEL_CRITICAL) &&
                rec.avgScore < worstScore) {
                worstScore       = rec.avgScore;
                sm.worstPair_ji  = ji;
                sm.worstPair_jj  = jj;
                sm.worstPairScore = rec.avgScore;
                sm.hasTarget     = true;
                foundWeak        = true;
            }
        }
    }

    return sm;
}

// ============================================================
// UPDATE ELITE ARCHIVE — Keep top-E turtles by objective
// Called by batchEvaluate after Stage 5 (Batch Eval 3).
//
// Merges current population with existing elite archive,
// keeps the best E_SIZE turtles, sorted by objective desc.
//
// SE RULE: ONLY batchEvaluate calls this function.
// SE RULE: Only turtles with cacheValid == true are considered.
// ============================================================
void updateEliteArchive(EliteArchive& ea, Population pop) {
    // Collect all candidates: current elites + new population
    std::vector<Turtle> candidates;

    // Add existing elites
    for (int i = 0; i < ea.count; i++) {
        candidates.push_back(ea.turtles[i]);
    }
    // Add current population (valid turtles only)
    for (int i = 0; i < P; i++) {
        if (pop[i].cacheValid) {
            candidates.push_back(pop[i]);
        }
    }

    // Sort by objective descending (highest Z first)
    std::sort(candidates.begin(), candidates.end(),
        [](const Turtle& a, const Turtle& b){
            return a.obj > b.obj;
        });

    // Keep top E_SIZE
    ea.count = std::min((int)candidates.size(), E_SIZE);
    for (int i = 0; i < ea.count; i++) {
        ea.turtles[i] = candidates[i];
    }
}

// ============================================================
// UPDATE M0 POOL — Track rejected jobs by marginal profit
// Called by batchEvaluate after Batch Eval 1 and Batch Eval 3.
//
// Scans all turtles, counts how often each job is rejected (M0).
// Computes marginal profit estimate: revenue - expected_tardiness.
// Sorts by marginal profit descending (best candidates to reinsert).
//
// SE RULE: ONLY batchEvaluate calls this function.
// ============================================================
void updateM0Pool(M0Pool& m0, Population pop) {
    m0.clear();

    // Count rejections and estimate marginal profit per job
    std::map<int, int> rejCount;     // jobId → rejection count
    std::map<int, float> profitSum;  // jobId → sum of marginal profits

    for (int p = 0; p < P; p++) {
        if (!pop[p].cacheValid) continue;
        const Turtle& t = pop[p];

        for (int pos = 0; pos < N_Order; pos++) {
            if (t.M_select[pos] == 0) { // rejected
                int jobId = t.Order_seq[pos]; // 1-indexed
                rejCount[jobId]++;
                // Marginal profit estimate: just revenue for now
                // (could be improved with expected completion time)
                profitSum[jobId] += revenue[jobId-1];
            }
        }
    }

    // Build M0Pool entries
    for (const auto& entry : rejCount) {
        int   jobId   = entry.first;
        int   count   = entry.second;
        float avgProf = profitSum[jobId] / (float)count;

        M0Entry e;
        e.jobId          = jobId;
        e.marginalProfit = avgProf;
        e.frequency      = count;
        m0.push_back(e);
    }

    // Sort by marginal profit descending (best reinsert candidates first)
    std::sort(m0.begin(), m0.end(),
        [](const M0Entry& a, const M0Entry& b){
            return a.marginalProfit > b.marginalProfit;
        });
}

// ============================================================
// STAGNATION RESET — Surgical memory cleanup
// Called by main loop when best objective unchanged for K_STAG iters.
//
// Removes oldest 25% of WEAK-labelled records from PairMemory.
// Does NOT clear STRONG, NEUTRAL, or CRITICAL records.
// Does NOT clear TripletMemory or EliteArchive.
//
// Purpose: refresh exploration when population is stuck.
//
// SE RULE: ONLY main loop calls this function.
// ============================================================
void stagnationReset(PairMemory& pm) {
    // Collect all WEAK entries
    std::vector<std::pair<std::pair<int,int>, int>> weakEntries; // (key, age)

    for (const auto& entry : pm) {
        if (entry.second.label == LABEL_WEAK) {
            weakEntries.push_back({entry.first, entry.second.age});
        }
    }

    if (weakEntries.empty()) return;

    // Sort by age descending (oldest first)
    typedef std::pair<std::pair<int,int>, int> WeakEntry;
    std::sort(weakEntries.begin(), weakEntries.end(),
        [](const WeakEntry& a, const WeakEntry& b){
            return a.second > b.second;
        });

    // Remove oldest 25%
    int removeCount = (int)(weakEntries.size() * STAG_RESET_FRACTION);
    if (removeCount < 1) removeCount = 1;

    for (int i = 0; i < removeCount; i++) {
        pm.erase(weakEntries[i].first);
    }

    printf("[StagnationReset] Removed %d/%d WEAK records from PairMemory\n",
           removeCount, (int)weakEntries.size());
}

#endif // MEMORY_OPS_H
