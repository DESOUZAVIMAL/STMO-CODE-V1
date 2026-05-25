// ============================================================
// stages.h — STMO Algorithm Stages
// ============================================================
// Contains all 5 stages and batchEvaluate function.
//
// Stage ownership and memory access:
//   batchEvaluate → reads population, writes EvalSnapshot,
//                   calls updateEliteArchive, updateM0Pool
//   Stage 1       → reads population, reads nothing from memory
//                   writes population (improved turtles)
//   Stage 2       → reads population, reads PairMemory (lookup only)
//                   reads M0Pool (lookup only)
//                   writes population (repaired turtles)
//   Stage 3 ACMM  → reads population + EvalSnapshot
//                   writes PairMemory, TripletMemory
//                   writes StructuralMap[P] (ephemeral)
//   Stage 4 MFBO  → reads population + StructuralMap + PairMemory
//                   writes population (improved turtles)
//                   StructuralMap discarded after this stage
//   Stage 5 CMA   → reads population + EliteArchive +
//                   TripletMemory + M0Pool
//                   writes population (improved turtles)
//
// SE RULES:
//   Each stage receives ONLY what it needs (dependency injection)
//   No stage accesses global state
//   All move operations use problem.h functions (return copies)
//   Acceptance criteria enforced per stage:
//     Stage 1: deltaZ >= 0 (best of nc candidates)
//     Stage 2: deltaZ >= 0 (non-worsening, first improvement)
//     Stage 4: deltaZ >  0 (strict improvement only)
//     Stage 5: deltaZ >  0 (strict improvement only)
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
// Decodes and evaluates all turtles in population.
// Extracts pair observations for ACMM.
// Updates EliteArchive and M0Pool.
//
// Called THREE times per iteration:
//   Batch Eval 1: startup (after init)
//   Batch Eval 2: after Stage 1 + Stage 2 (feeds ACMM)
//   Batch Eval 3: after Stage 5 (updates elite + M0Pool)
//
// Returns EvalSnapshot for ACMM to consume.
// ============================================================
EvalSnapshot batchEvaluate(Population pop,
                            EliteArchive& ea,
                            M0Pool& m0,
                            int iteration,
                            bool updateElite,
                            bool updateM0) {
    EvalSnapshot snap;
    snap.iteration = iteration;

    // Decode + evaluate all turtles
    for (int p = 0; p < P; p++) {
        decodeAndEval(pop[p]);
    }

    // Extract pair observations for ACMM
    for (int p = 0; p < P; p++) {
        const Turtle& t = pop[p];
        for (int m = 0; m < M_Machine; m++) {
            for (int k = 0; k < t.machineCount[m] - 1; k++) {
                int ji = t.machineSeq[m][k];     // 1-indexed
                int jj = t.machineSeq[m][k+1];   // 1-indexed

                PairObservation obs;
                obs.ji        = ji;
                obs.jj        = jj;
                obs.score     = calcPairScore(ji, jj, t.ctimes[jj-1]);
                obs.turtleIdx = p;
                snap.observations.push_back(obs);
            }
        }
    }

    // Compute population mu and sigma over all pair scores
    if (!snap.observations.empty()) {
        float sum = 0.0f;
        for (const auto& obs : snap.observations)
            sum += obs.score;
        snap.popMu = sum / (float)snap.observations.size();

        float varSum = 0.0f;
        for (const auto& obs : snap.observations)
            varSum += (obs.score - snap.popMu) * (obs.score - snap.popMu);
        snap.popSigma = sqrtf(varSum / (float)snap.observations.size());
    }

    // Update EliteArchive if requested (Batch Eval 1 and 3)
    if (updateElite) updateEliteArchive(ea, pop);

    // Update M0Pool if requested (Batch Eval 1 and 3)
    if (updateM0) updateM0Pool(m0, pop);

    return snap;
}

// ============================================================
// STAGE 1 — OceanCurrentDrift
// Random exploration — no memory used.
// Biology: juvenile turtles drifting randomly on ocean currents.
//
// For each turtle:
//   Generate nc=NC random candidate moves
//   Each candidate uses a randomly weighted operator
//   Keep best candidate if it improves objective (>= current)
//   Otherwise keep original
//
// SE RULE: uses generateRandomMove() which returns copies
// SE RULE: decodeAndEval called on each candidate
// SE RULE: cacheValid maintained correctly throughout
// ============================================================
void stage1_OceanCurrentDrift(Population pop) {
    for (int p = 0; p < P; p++) {
        Turtle& t   = pop[p];
        Turtle  best = t;   // copy — best so far (starts as original)

        for (int c = 0; c < NC; c++) {
            // Generate random candidate (returns copy, cacheValid=false)
            Turtle candidate = generateRandomMove(t);

            // Evaluate candidate
            decodeAndEval(candidate);

            // Keep if better than current best
            if (candidate.obj > best.obj) {
                best = candidate;
            }
        }

        // Accept best if it improved (non-worsening: >= original)
        if (best.obj >= t.obj) {
            pop[p] = best;
        }
        // else: original t kept unchanged
    }
}

// ============================================================
// STAGE 2 — MemoryAwareDrift
// Memory-guided repair of weak consecutive job pairs.
// Biology: young turtles learning to avoid dangerous zones.
//
// For each turtle:
//   If PairMemory is empty → skip (Iteration 1)
//   For each machine independently:
//     Find weak/critical pairs, sort worst first
//     Repair up to k = min(MACHINE_BUDGET, count) pairs
//     Each repair: try MAX_ATTEMPTS options, accept first ΔZ >= 0
//
// Repair operators (on decoded machine sequence):
//   (a) Move Jj to different position on same machine
//   (b) Reject Jj → M0 (machine = 0)
//   (c) Insert a different job between Ji and Jj
//
// SE RULE: acceptance is ΔZ >= 0 (non-worsening)
// SE RULE: only reads PairMemory — never writes
// SE RULE: operators work on DECODED sequence via move functions
// SE RULE: partialReeval used for cheap evaluation
// ============================================================
void stage2_MemoryAwareDrift(Population pop,
                              const PairMemory& pm,
                              const M0Pool& m0) {
    // Skip entirely if PairMemory is empty (Iteration 1)
    if (pm.empty()) return;

    for (int p = 0; p < P; p++) {
        Turtle& t = pop[p];
        if (!t.cacheValid) decodeAndEval(t);

        t.stage2_repairs = 0; // reset diagnostic counter

        // Process each machine independently
        for (int m = 0; m < M_Machine; m++) {
            if (t.machineCount[m] < 2) continue; // need at least 2 jobs

            // Collect weak/critical pairs on this machine
            struct WeakPair {
                int   posK;     // position of Ji in machineSeq[m]
                int   ji, jj;   // job IDs (1-indexed)
                float score;    // pair score (lower = worse)
            };
            std::vector<WeakPair> weakPairs;

            for (int k = 0; k < t.machineCount[m] - 1; k++) {
                int ji = t.machineSeq[m][k];
                int jj = t.machineSeq[m][k+1];
                char lbl = lookupPairLabel(pm, ji, jj);
                if (lbl == LABEL_WEAK || lbl == LABEL_CRITICAL) {
                    WeakPair wp;
                    wp.posK  = k;
                    wp.ji    = ji;
                    wp.jj    = jj;
                    wp.score = lookupPairScore(pm, ji, jj);
                    weakPairs.push_back(wp);
                }
            }

            if (weakPairs.empty()) continue;

            // Sort worst first (ascending score)
            std::sort(weakPairs.begin(), weakPairs.end(),
                [](const WeakPair& a, const WeakPair& b){
                    return a.score < b.score;
                });

            // Apply machine budget
            int budget = std::min((int)weakPairs.size(), MACHINE_BUDGET);

            for (int wi = 0; wi < budget; wi++) {
                int jj = weakPairs[wi].jj; // the problem job

                bool repaired = false;

                // Find position of jj in the ENCODING (Order_seq)
                int encPos = -1;
                for (int pos = 0; pos < N_Order; pos++) {
                    if (t.Order_seq[pos] == jj) {
                        encPos = pos;
                        break;
                    }
                }
                if (encPos < 0) continue;

                // Try MAX_ATTEMPTS repair options
                for (int attempt = 0; attempt < MAX_ATTEMPTS && !repaired; attempt++) {
                    Turtle candidate;
                    float  newObj = t.obj;

                    if (attempt < 2) {
                        // Option A: Move Jj to a different position
                        // on the same machine (Order Insert)
                        candidate = moveOrderInsert(t, encPos, -1);
                        decodeAndEval(candidate);
                        newObj = candidate.obj;
                    }
                    else if (attempt == 2) {
                        // Option B: Reject Jj → M0
                        candidate = moveMachineRevise(t, encPos, 0);
                        decodeAndEval(candidate);
                        newObj = candidate.obj;
                    }
                    else {
                        // Option C: Swap Jj with a job from M0Pool
                        // Try reinserting a rejected job near this position
                        if (!m0.empty()) {
                            int m0JobId = m0[attempt % (int)m0.size()].jobId;
                            // Find m0 job in encoding
                            int m0Pos = -1;
                            for (int pos = 0; pos < N_Order; pos++) {
                                if (t.Order_seq[pos] == m0JobId &&
                                    t.M_select[pos] == 0) {
                                    m0Pos = pos;
                                    break;
                                }
                            }
                            if (m0Pos >= 0) {
                                candidate = moveOrderSwap(t, encPos, m0Pos);
                                // Also change machine of swapped job
                                candidate.M_select[encPos] = m + 1;
                                candidate.cacheValid = false;
                                decodeAndEval(candidate);
                                newObj = candidate.obj;
                            } else {
                                continue;
                            }
                        } else {
                            continue;
                        }
                    }

                    // Accept if non-worsening (Stage 2 criterion)
                    if (newObj >= t.obj) {
                        t = candidate;
                        t.stage2_repairs++;
                        repaired = true;
                    }
                } // end attempts
            } // end budget loop
        } // end machine loop
    } // end population loop
}

// ============================================================
// STAGE 3 — ACMM (Adaptive Cluster-Memory Mechanism)
// The intelligence core — builds and updates all memory.
// Biology: adult turtle building internal geomagnetic map.
//
// Step 1: updatePairMemory (two-pass decay + update)
// Step 2: labelAllPairs (mu/sigma → labels)
// Step 3: runODTP (overlapping STRONG → TripletMemory)
// Step 4: buildStructuralMap per turtle (for Stage 4)
//
// SE RULE: ONLY this function writes to PairMemory and TripletMemory
// SE RULE: StructuralMap is ephemeral — caller discards after Stage 4
// ============================================================
void stage3_ACMM(Population pop,
                 PairMemory& pm,
                 TripletMemory& tm,
                 const EvalSnapshot& snap,
                 StructuralMap* structMaps) {

    // Step 1: Two-pass PairMemory update
    updatePairMemory(pm, snap);

    // Step 2: Label all pairs using population statistics
    labelAllPairs(pm);

    // Step 3: ODTP — promote overlapping STRONG pairs to triplets
    runODTP(pm, tm, pop);

    // Step 4: Build StructuralMap for each turtle
    for (int p = 0; p < P; p++) {
        if (pop[p].cacheValid) {
            structMaps[p] = buildStructuralMap(pop[p], pm);
        } else {
            structMaps[p] = StructuralMap(); // empty map — no target
        }
    }
}

// ============================================================
// STAGE 4 — MFBO (Magnetic-Field Boundary Optimization)
// Targeted repair using StructuralMap.
// Phase 1 only — Phase 2 VNS deferred per professor instruction.
// Biology: adult turtle navigating precisely toward feeding zones.
//
// For each turtle:
//   Read StructuralMap → worst pair (Ji, Jj), Jj = target job
//   Build Weak Pool = all OTHER weak second jobs in this turtle
//   Phase 1: Search PairMemory for STRONG pair (Jj, Jk), Jk ∈ Pool
//     If found: try swap Jj ↔ Jk → accept if ΔZ > 0 → DONE
//   Phase 1b: If no STRONG found → try NEUTRAL pairs same way
//   Phase 2: VNS — DEFERRED (placeholder comment only)
//
// SE RULE: acceptance is ΔZ > 0 (STRICT — not non-worsening)
// SE RULE: STRONG pairs in StructuralMap are never disrupted
// SE RULE: StructuralMap consumed here — discarded after this stage
// ============================================================
void stage4_MFBO(Population pop,
                 const StructuralMap* structMaps,
                 const PairMemory& pm) {

    for (int p = 0; p < P; p++) {
        Turtle& t = pop[p];
        const StructuralMap& sm = structMaps[p];

        // Skip if no weak pair found for this turtle
        if (!sm.hasTarget) continue;

        t.stage4_phase1 = 0; // reset diagnostic counter

        // Run 002 Design Change D2:
        // Loop through top-3 worst pairs (sm.targets) instead of just 1.
        // Try to fix the worst pair first. If PairMemory has no matching
        // STRONG/NEUTRAL candidate for it, fall through to 2nd worst, then 3rd.
        // Stop as soon as any target produces a strict improvement (ΔZ > 0).

        bool anyImproved = false;

        for (int ti = 0; ti < (int)sm.targets.size() && !anyImproved; ti++) {

            int jj = sm.targets[ti].jj; // the repair target for this attempt

            // Build Weak Pool: all OTHER weak second jobs in this turtle's schedule
            std::vector<int> weakPool;
            for (int m = 0; m < M_Machine; m++) {
                for (int k = 0; k < t.machineCount[m] - 1; k++) {
                    int ji_k = t.machineSeq[m][k];
                    int jk   = t.machineSeq[m][k+1];
                    if (jk == jj) continue; // skip the target job itself

                    char lbl = lookupPairLabel(pm, ji_k, jk);
                    if (lbl == LABEL_WEAK || lbl == LABEL_CRITICAL) {
                        weakPool.push_back(jk);
                    }
                }
            }

            if (weakPool.empty()) continue; // try next target pair

            // Remove duplicates
            std::sort(weakPool.begin(), weakPool.end());
            weakPool.erase(std::unique(weakPool.begin(), weakPool.end()),
                           weakPool.end());

            // Helper lambda: try PairMemory candidates of a given label
            auto tryCandidates = [&](char targetLabel) -> bool {
                struct Candidate { int jk; float score; };
                std::vector<Candidate> candidates;

                for (int jk : weakPool) {
                    auto key = std::make_pair(jj, jk);
                    auto it  = pm.find(key);
                    if (it == pm.end()) continue;
                    if (it->second.label != targetLabel) continue;
                    if (it->second.count < g_minCount) continue;

                    // Protect jobs that are part of STRONG pairs
                    bool isProtected = false;
                    for (const auto& sp : sm.strongPairs) {
                        if (sp.first == jk || sp.second == jk) {
                            isProtected = true; break;
                        }
                    }
                    if (isProtected) continue;

                    Candidate c; c.jk = jk; c.score = it->second.avgScore;
                    candidates.push_back(c);
                }

                // Best candidate first (highest score)
                std::sort(candidates.begin(), candidates.end(),
                    [](const Candidate& a, const Candidate& b){
                        return a.score > b.score;
                    });

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

                    if (candidate.obj > t.obj) { // strict improvement
                        t = candidate;
                        t.stage4_phase1++;
                        return true;
                    }
                }
                return false;
            }; // end lambda

            // Phase 1a: STRONG candidates first, then NEUTRAL
            bool improved = tryCandidates(LABEL_STRONG);
            if (!improved) improved = tryCandidates(LABEL_NEUTRAL);
            if (improved) anyImproved = true;

        } // end target loop (top-3 pairs)

        // Phase 2: Full VNS — DEFERRED per professor instruction.
        // stage4_phase2_VNS(t, sm, pm);
    }
}

// ============================================================
// STAGE 5 — CMA (Collective Memory Archive)
// Elite pattern transfer to weaker turtles.
// Biology: adult turtles returning to natal beach via
//          imprinted long-term magnetic memory.
//
// For each turtle:
//   Operation 1: Elite subpath transfer
//     For each elite in EliteArchive:
//       Find STRONG pairs in elite not present in this turtle
//       Try injecting that subpath (move job to match pattern)
//       Accept if ΔZ > 0 (strict)
//       Break on first success
//
//   Operation 2: M0 reinsertion
//     For each job in M0Pool (sorted by marginal profit):
//       Try inserting job into turtle's schedule
//       Accept if ΔZ > 0 (strict)
//       Break on first success
//
// SE RULE: acceptance is ΔZ > 0 (STRICT)
// SE RULE: reads EliteArchive and TripletMemory — never writes them
// ============================================================
void stage5_CMA(Population pop,
                const EliteArchive& ea,
                const PairMemory& pm,
                const TripletMemory& tm,
                const M0Pool& m0) {

    for (int p = 0; p < P; p++) {
        Turtle& t = pop[p];
        if (!t.cacheValid) decodeAndEval(t);

        bool improved = false;

        // ── Operation 1: Elite subpath transfer ──────────────────
        for (int e = 0; e < ea.count && !improved; e++) {
            const Turtle& elite = ea.turtles[e];
            if (!elite.cacheValid) continue;

            // Find STRONG pairs in elite that are missing from turtle t
            for (int em = 0; em < M_Machine && !improved; em++) {
                for (int k = 0; k < elite.machineCount[em] - 1 && !improved; k++) {
                    int e_ji = elite.machineSeq[em][k];
                    int e_jj = elite.machineSeq[em][k+1];

                    // Check this pair is STRONG in PairMemory
                    if (!pairHasLabel(pm, e_ji, e_jj, LABEL_STRONG)) continue;

                    // Check if this STRONG pair exists in turtle t's schedule
                    bool presentInT = false;
                    for (int tm2 = 0; tm2 < M_Machine && !presentInT; tm2++) {
                        for (int tk = 0; tk < t.machineCount[tm2] - 1; tk++) {
                            if (t.machineSeq[tm2][tk]   == e_ji &&
                                t.machineSeq[tm2][tk+1] == e_jj) {
                                presentInT = true;
                                break;
                            }
                        }
                    }
                    if (presentInT) continue; // already has this pattern

                    // Try to inject: move e_jj to follow e_ji in turtle t
                    int pos_ji = -1, pos_jj_in_t = -1;
                    for (int pos = 0; pos < N_Order; pos++) {
                        if (t.Order_seq[pos] == e_ji) pos_ji = pos;
                        if (t.Order_seq[pos] == e_jj) pos_jj_in_t = pos;
                    }
                    if (pos_ji < 0 || pos_jj_in_t < 0) continue;

                    // Move e_jj to position right after e_ji
                    int targetPos = pos_ji + 1;
                    if (targetPos >= N_Order) targetPos = pos_ji;
                    if (targetPos == pos_jj_in_t) continue;

                    Turtle candidate = moveOrderInsert(t, pos_jj_in_t, targetPos);
                    // Also match machine assignment from elite
                    int machPos = -1;
                    for (int pos = 0; pos < N_Order; pos++) {
                        if (candidate.Order_seq[pos] == e_jj) {
                            machPos = pos;
                            break;
                        }
                    }
                    if (machPos >= 0) {
                        candidate.M_select[machPos] = em + 1; // match elite's machine
                        candidate.cacheValid = false;
                    }

                    decodeAndEval(candidate);

                    // Accept STRICT improvement only (Stage 5 criterion)
                    if (candidate.obj > t.obj) {
                        t = candidate;
                        improved = true;
                    }
                }
            }
        }

        // ── Operation 2: M0 reinsertion ──────────────────────────
        // Try inserting high-value rejected jobs into schedule
        for (int mi = 0; mi < (int)m0.size() && !improved; mi++) {
            int rejJobId = m0[mi].jobId;

            // Find position of this rejected job in encoding
            int rejPos = -1;
            for (int pos = 0; pos < N_Order; pos++) {
                if (t.Order_seq[pos] == rejJobId && t.M_select[pos] == 0) {
                    rejPos = pos;
                    break;
                }
            }
            if (rejPos < 0) continue; // this turtle doesn't reject this job

            // Try assigning it to each machine
            for (int mach = 1; mach <= M_Machine && !improved; mach++) {
                Turtle candidate = moveMachineRevise(t, rejPos, mach);
                decodeAndEval(candidate);

                if (candidate.obj > t.obj) {
                    t = candidate;
                    improved = true;
                }
            }
        }
    }
}

#endif // STAGES_H
