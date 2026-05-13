// ============================================================
// stmo_types.h — STMO Data Structures
// ============================================================
// Three-Layer State Hierarchy:
//
//   LAYER 1 — Authoritative Evaluated State (Turtle)
//     Written ONLY by decodeAndEval() in problem.h
//     Read by all stages
//
//   LAYER 2 — Persistent Memory (survives across iterations)
//     PairMemory  : written ONLY by ACMM (Stage 3)
//     TripletMemory: written ONLY by ODTP inside ACMM
//     EliteArchive: written ONLY by batchEvaluate after Stage 5
//     M0Pool      : written ONLY by batchEvaluate
//
//   LAYER 3 — Ephemeral (rebuilt each iteration, then discarded)
//     StructuralMap : built by ACMM, consumed by Stage 4, then gone
//     EvalSnapshot  : output of batchEvaluate, read by ACMM, then gone
//
// SE RULE: Never write to a structure from an unauthorized stage.
// SE RULE: Never read Layer 1 without checking cacheValid first.
// ============================================================

#ifndef STMO_TYPES_H
#define STMO_TYPES_H

#include "config.h"
#include <map>
#include <vector>
#include <utility>  // std::pair

// ============================================================
// LAYER 1 — TURTLE (Authoritative Evaluated State)
// ============================================================

struct Turtle {

    // --- Encoding (Two-Row Representation) ---
    // Row 1: job sequence — sorted by Order_seqval random keys
    int   Order_seq[MAX_N];      // job IDs in sequence order (1-indexed)
    float Order_seqval[MAX_N];   // random float keys used for sorting

    // Row 2: machine assignment
    int   M_select[MAX_N];       // machine for each position: 0=M0(rejected), 1..M=machine
    float M_selectval[MAX_N];    // random float keys for machine assignment

    // --- Evaluated State ---
    // SE RULE: these fields are ONLY written by decodeAndEval()
    // SE RULE: never read obj or ctimes unless cacheValid == true
    float obj;                           // current objective Z value
    bool  cacheValid;                    // true ONLY after decodeAndEval()

    // Decoded machine sequences (output of decoding step)
    int   machineSeq[MAX_M][MAX_N];      // jobs assigned to each machine, in order
    int   machineCount[MAX_M];           // number of jobs on each machine
    float ctimes[MAX_N];                 // completion time for each job (indexed by job ID - 1)

    // --- Diagnostic counters (for output reporting) ---
    int   stage2_repairs;    // how many Stage 2 repairs applied this iteration
    int   stage4_phase1;     // how many Stage 4 Phase 1 swaps succeeded
};

// ============================================================
// LAYER 2 — PERSISTENT MEMORY
// ============================================================

// --- PairMemory ---
// Stores quality information about consecutive job transitions.
// Key: (ji, jj) — machine-agnostic 2-tuple of job IDs
// SE RULE: ONLY Stage 3 ACMM writes to PairMemory.
// SE RULE: Stage 2 and Stage 4 may only READ (lookup).

struct PairRecord {
    float avgScore;     // running weighted average score
    int   count;        // total times this pair has been observed
    int   age;          // iterations since last observation (for ghost cleanup)
    char  label;        // 'S'=STRONG 'W'=WEAK 'C'=CRITICAL 'N'=NEUTRAL ' '=unlabelled

    PairRecord() : avgScore(0.0f), count(0), age(0), label(' ') {}
};

// PairMemory: map from (ji, jj) → PairRecord
// Using std::map for O(log n) lookup — sufficient for N<=200
typedef std::map<std::pair<int,int>, PairRecord> PairMemory;

// --- TripletMemory ---
// Stores three-job patterns promoted by ODTP.
// Only created when two overlapping STRONG pairs share a middle job.
// SE RULE: ONLY ODTP (inside ACMM) writes to TripletMemory.
// SE RULE: Stage 5 CMA reads TripletMemory for elite transfer.

struct TripletRecord {
    int   ja, jb, jc;   // three jobs in consecutive order: Ja → Jb → Jc
    float score;         // triplet score (computed fresh by ODTP)
    int   count;         // times this triplet has been promoted
    int   age;           // iterations since last promotion
};

typedef std::vector<TripletRecord> TripletMemory;

// --- EliteArchive ---
// Keeps the top-E turtles by objective value seen so far.
// Updated after Batch Evaluation 3 (end of each iteration).
// SE RULE: ONLY batchEvaluate writes to EliteArchive.
// SE RULE: Stage 5 CMA reads EliteArchive for pattern transfer.

struct EliteArchive {
    Turtle turtles[MAX_P];   // stored elite turtles (MAX_P is upper bound, E_SIZE used)
    int    count;             // current number of elites stored (max = E_SIZE)

    EliteArchive() : count(0) {}
};

// --- M0Pool ---
// Tracks jobs that are currently rejected (M_select = 0) across the population.
// Used by Stage 2 (insert rejected job to repair weak pair)
// and Stage 5 CMA (try reinserting high-value rejected jobs).
// SE RULE: ONLY batchEvaluate writes to M0Pool.

struct M0Entry {
    int   jobId;             // job ID (1-indexed)
    float marginalProfit;    // estimated profit if accepted: revenue - expected_penalty
    int   frequency;         // how many turtles currently reject this job
};

typedef std::vector<M0Entry> M0Pool;

// ============================================================
// LAYER 3 — EPHEMERAL (per-iteration only, then discarded)
// ============================================================

// --- StructuralMap ---
// Built by ACMM after Batch Eval 2 for each turtle.
// Consumed by Stage 4 MFBO.
// Discarded after Stage 4 completes.
// SE RULE: never store StructuralMap across iterations.
// SE RULE: never pass StructuralMap to any stage other than Stage 4.

struct StructuralMap {
    // Target: the worst-scoring consecutive pair → Stage 4 attacks this
    int   worstPair_ji;      // first job of worst pair
    int   worstPair_jj;      // second job of worst pair (Jj = repair target)
    float worstPairScore;    // its score (for reference/logging)

    // Protected: STRONG pairs → Stage 4 must never disrupt these
    std::vector<std::pair<int,int>> strongPairs;  // list of (ji, jj) STRONG pairs

    // Valid flag: false if turtle has no weak pairs (Stage 4 can skip this turtle)
    bool  hasTarget;

    StructuralMap() : worstPair_ji(-1), worstPair_jj(-1),
                      worstPairScore(0.0f), hasTarget(false) {}
};

// One StructuralMap per turtle per iteration
typedef StructuralMap StructuralMapArray[MAX_P];

// --- EvalSnapshot ---
// Output of batchEvaluate() — population-level pair score data.
// Read by ACMM to update PairMemory.
// Discarded after ACMM consumes it.
// SE RULE: EvalSnapshot is READ-ONLY outside of batchEvaluate.
// SE RULE: EvalSnapshot is NOT turtle state — do not copy into turtles.

struct PairObservation {
    int   ji, jj;        // the pair
    float score;         // score computed this iteration
    int   turtleIdx;     // which turtle this pair came from
};

struct EvalSnapshot {
    std::vector<PairObservation> observations;  // all pairs seen this iteration
    float popMu;      // mean pair score across all observations this iteration
    float popSigma;   // standard deviation across all observations
    int   iteration;  // which iteration this snapshot was taken

    EvalSnapshot() : popMu(0.0f), popSigma(0.0f), iteration(0) {}
};

// ============================================================
// POPULATION TYPE
// ============================================================

// The population is a simple array of turtles.
// SE RULE: always pass population as pointer or reference — never copy the whole array.
// SE RULE: population size is always exactly P (from config.h).

typedef Turtle Population[MAX_P];

// ============================================================
// HELPER: Label characters for readability
// ============================================================
#define LABEL_STRONG    'S'
#define LABEL_WEAK      'W'
#define LABEL_CRITICAL  'C'
#define LABEL_NEUTRAL   'N'
#define LABEL_NONE      ' '

// ============================================================
// HELPER: Move type codes (used in Stage 1)
// ============================================================
#define MOVE_INSERT     0   // Order Insert  (Neighborhood1)
#define MOVE_REVISED    1   // Machine Revised (Neighborhood2)
#define MOVE_SWAP       2   // Order Swap (Neighborhood3)
#define MOVE_MSWAP      3   // Machine Swap (Neighborhood4)
#define MOVE_COMBO      4   // Combo: Insert + Revised

#endif // STMO_TYPES_H
