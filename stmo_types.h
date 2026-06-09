// ============================================================
// stmo_types.h — STMO Data Structures
// ============================================================

#ifndef STMO_TYPES_H
#define STMO_TYPES_H

#include "config.h"
#include <map>
#include <unordered_map>
#include <vector>
#include <utility>

// ============================================================
// LAYER 1 — TURTLE
// ============================================================
struct Turtle {
    int   Order_seq[MAX_N];
    float Order_seqval[MAX_N];
    int   M_select[MAX_N];
    float M_selectval[MAX_N];

    float obj;
    bool  cacheValid;

    int   machineSeq[MAX_M][MAX_N];
    int   machineCount[MAX_M];
    float ctimes[MAX_N];

    // Run003 C2: per-machine objective contribution for incremental eval.
    // Valid only when cacheValid == true (written by decodeAndEval).
    float machineObj[MAX_M];

    int   stage2_repairs;
    int   stage4_phase1;
};

// ============================================================
// LAYER 2 — PERSISTENT MEMORY
// ============================================================
struct PairRecord {
    float avgScore;
    int   count;
    int   age;
    char  label;
    PairRecord() : avgScore(0.0f), count(0), age(0), label(' ') {}
};

struct PairHash {
    std::size_t operator()(const std::pair<int,int>& p) const noexcept {
        // Perfect hash for job-ID pairs: IDs are 1..N where N <= 200 < 256.
        return static_cast<std::size_t>(p.first) * 256u + static_cast<std::size_t>(p.second);
    }
};

typedef std::unordered_map<std::pair<int,int>, PairRecord, PairHash> PairMemory;

struct TripletRecord {
    int   ja, jb, jc;
    float score;
    int   count;
    int   age;
};
typedef std::vector<TripletRecord> TripletMemory;

struct EliteArchive {
    Turtle turtles[MAX_P];
    int    count;
    EliteArchive() : count(0) {}
};

struct M0Entry {
    int   jobId;
    float marginalProfit;
    int   frequency;
};
typedef std::vector<M0Entry> M0Pool;

// ============================================================
// LAYER 3 — EPHEMERAL
// ============================================================
struct StructuralMap {
    struct WeakTarget {
        int   ji;
        int   jj;
        float score;
    };
    std::vector<WeakTarget> targets;            // top-3 worst pairs (Run002 D2)
    std::vector<std::pair<int,int>> strongPairs;
    bool hasTarget;
    StructuralMap() : hasTarget(false) {}
};

typedef StructuralMap StructuralMapArray[MAX_P];

struct PairObservation {
    int   ji, jj;
    float score;
    int   turtleIdx;
};

struct EvalSnapshot {
    std::vector<PairObservation> observations;
    float popMu;
    float popSigma;
    int   iteration;
    EvalSnapshot() : popMu(0.0f), popSigma(0.0f), iteration(0) {}
};

typedef Turtle Population[MAX_P];

// ============================================================
// HELPER CONSTANTS
// ============================================================
#define LABEL_STRONG    'S'
#define LABEL_WEAK      'W'
#define LABEL_CRITICAL  'C'
#define LABEL_NEUTRAL   'N'
#define LABEL_NONE      ' '

#define MOVE_INSERT     0
#define MOVE_REVISED    1
#define MOVE_SWAP       2
#define MOVE_MSWAP      3
#define MOVE_COMBO      4

#endif // STMO_TYPES_H
