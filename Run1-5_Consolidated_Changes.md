# STMO — Consolidated Run-by-Run Change Log (Run 1 → Run 5)
## Code-wise history for deep analysis in Claude AI

**Algorithm:** STMO (Sea Turtle Migration Optimization)
**Problem:** Order Acceptance and Scheduling on Identical Parallel Machines with Sequence-Dependent Setup Times (OAS-SDST)
**Reference:** Wu et al. (2018), Applied Soft Computing
**Author:** Vimal De Souza | Student 1135446 | Yuan Ze University
**Compiled:** 2026-06-08

> **How to read this file.** Each run section gives: (1) the exact code/config state,
> (2) what changed coming INTO that run vs the previous run, with code blocks,
> (3) the measured result, and (4) the interpretation (why it went up or down).
> The final sections give the master data table and the honest good/bad attribution.
> **Objective sense: HIGHER Best Z = BETTER** (total weighted profit from accepted orders).

---

## 0. Run map & commit trail

| Run | Label | Commit(s) | Branch | Nature of change |
|-----|-------|-----------|--------|------------------|
| Run 1 | baseline | `aae6c08` (code), `55776d6` (results) | dev/main | First clean run after 6 bug fixes |
| Run 2 | adaptive_config | `03fe3a0`, `903bb98` | dev-laptop / dev-labpc | **Config only** (A-series), no logic change |
| Run 3 | PhaseAB (failed) | `97249e0` | dev-labpc | Same code as Run 4; incomplete/aborted — no clean 45/45 |
| Run 4 | PhaseAB | `97249e0`, `a94a80b` | dev-labpc | A-series + **B-series logic** (B2/B3/B4) + bigger budgets |
| Run 5 | Stage4_Swap_Control | `bb2bad6`, `20ba4ae` | dev-labpc | **Revert B3 only**, keep everything else |

> Note: the results folder `run_004_*` is labeled "Run003_PhaseAB". Run 3 and Run 4 share the
> same code; Run 3 had 2 failed/incomplete attempts, Run 4 is the completed 45/45 run.

**45-instance test matrix (identical for every run):**
N ∈ {30, 50, 100, 150, 200} × M ∈ {3, 6, 9} × Seed ∈ {10, 20, 30}.

---

## 1. MASTER RESULTS TABLE — Avg Best Z by problem size

| N (orders) | Run 1 baseline | Run 2 adaptive | Run 4 PhaseAB | Run 5 swap-control |
|---|---|---|---|---|
| 30  | 3360.5 | **3503.5** ✅ | 3375.4 ❌ | 3375.4 |
| 50  | 4536.6 | **4716.3** ✅ | 4602.1 ❌ | 4602.1 |
| 100 | 4915.5 | 4998.4 | **5025.8** | 5020.3 |
| 150 | 5977.6 | 5997.5 | **6208.7** | 6202.4 |
| 200 | 6179.2 | 6284.5 | **6530.9** | 6522.9 |
| **ALL 45** | **4993.9** | **5100.0** | **5148.6** | **5144.6** |

Run-level summary metadata (from `results/all_runs_summary.csv`):

| Run | Date | END_TIME | MAX_ITER | DF | MIN_COUNT | K_STAG | AvgBestZ | MaxBestZ | MinBestZ | AvgIters | AvgTimeSec |
|-----|------|---------|----------|----|-----------|--------|----------|----------|----------|----------|------------|
| 001 | 2026-05-24 | 120.0 | 2500 | 0.95 | 3 | 50 | 4993.88 | 8650.62 | 2119.87 | 2376.9 | 62.93 |
| 002 | 2026-05-26 | adaptive | adaptive | 0.95 | adaptive | adaptive | 5100.03 | 8951.78 | 2228.48 | 5500.0 | 44.42 |
| 004 | 2026-06-02 | adaptive | 50000 | 0.95 | adaptive | adaptive | 5148.55 | 9278.40 | 2132.80 | 20777.3 | 167.75 |
| 005 | 2026-06-02 | adaptive | 50000 | 0.95 | adaptive | adaptive | 5144.59 | 9278.40 | 2132.80 | ~19000 | ~150 |

---

## 2. RUN 1 — BASELINE (`aae6c08`)

### 2.1 Code state
First clean, fully-working run after the 6 foundational bug fixes:
1. Infinite loop in all random move functions (`do-while` retry bug) — `problem.h`
2. Population changes silently discarded (pass-by-value → pass-by-reference) — `stages.h`, `memory_ops.h`
3. Stack overflow on startup (`static` arrays in `main()`) — `STMO.cpp`
4. Algorithm ran past 120 s time limit (per-stage `timeUp()` check added) — `STMO.cpp`
5. Infinite loop in Stage 5 edge case (`pos_jj == targetPos-1` guard) — `stages.h`
6. `runODTP` was O(S²) → successor hash index, now O(1) lookup — `memory_ops.h`

All 5 stages active: Stage 1 OceanCurrentDrift, Stage 2 MemoryAwareDrift, Stage 3 ACMM
(PairMemory + TripletMemory via ODTP), Stage 4 MFBO Phase 1, Stage 5 CMA.

### 2.2 Config (uniform, NON-adaptive — same values for every instance)
```c
#define P           30
#define MAX_ITER    2500     // backup stop
#define END_TIME    120.0    // primary stop (seconds)
#define NC          3        // Stage 1 candidates
#define DF          0.95f    // PairMemory decay
#define MIN_COUNT   3        // min observations before labelling
#define MACHINE_BUDGET 3     // Stage 2 repairs per machine
#define K_STAG      50       // stagnation threshold
#define E_SIZE      5        // elite archive size
```
- **Stage 4 operator:** `moveOrderSwap` (pure job swap).
- **Stage 5 TripletMemory:** built by ODTP but **never used** in transfer.

### 2.3 Result
- Avg Z = **4993.9** (45/45 instances).
- AvgTime = 62.9 s, AvgIters = 2377.

### 2.4 Interpretation
Working but inefficient on small N. Because `MAX_ITER=2500` is a hard cap, N=30 and N=50
hit the iteration ceiling in **10–26 s** and then stopped — wasting ~75–90% of the 120 s
budget. Large N (150, 200) actually ran out of *time* before iterations. This size-blind
single config is the thing Run 2 fixes.

---

## 3. RUN 1 → RUN 2 — ADAPTIVE CONFIG ("A-series") (`03fe3a0`)

> **This is the change that created the good value. Config/parameters ONLY — zero algorithm logic touched.**

### 3.1 `config.h` — single constants replaced by size-tiered constants
```c
// BEFORE (Run 1):
#define MAX_ITER    2500
#define END_TIME    300.0     // (later 120 at runtime)

// AFTER (Run 2):
#define MAX_ITER         2500    // default (N >= 100)
#define MAX_ITER_SMALL  10000    // N <= 50   (was 2500)  ← key change
#define END_TIME        120.0    // default (N <= 100)
#define END_TIME_LARGE  180.0    // N >= 150  (still improving at 94-97% of run)
#define K_STAG_SMALL    100      // N <= 50   (was 50)  slower memory reset
#define K_STAG_LARGE     50      // N >= 100  (unchanged)
#define MIN_COUNT_SMALL   3      // N <= 100  (unchanged)
#define MIN_COUNT_LARGE   1      // N >= 150  (was 3) faster pair labelling
// Runtime globals, set in STMO.cpp after reading N:
extern int   g_maxIter;
extern float g_endTime;
extern int   g_kStag;
extern int   g_minCount;
```
Also bumped for N≥150: `NC` 3→5, `MACHINE_BUDGET` 3→5, `E_SIZE` 5→10.

### 3.2 `STMO.cpp` — adaptive block added after `genData()`
Reads N, then sets the `g_*` runtime globals used by the main loop and stages:
```cpp
g_maxIter = ...;          // size-dependent ceiling
if (N_Order <= 50) {      // small N: more iterations, slower reset
    g_endTime = 120; g_kStag = K_STAG_SMALL; g_minCount = MIN_COUNT_SMALL;
    g_nc = NC_BASE; g_machineBudget = MACHINE_BUDGET_BASE;
} else if (N_Order >= 150) {   // large N: more time, faster labelling, more candidates
    g_endTime = END_TIME_LARGE; g_kStag = K_STAG_LARGE; g_minCount = MIN_COUNT_LARGE;
    g_nc = NC_LARGE; g_machineBudget = MACHINE_BUDGET_LARGE;
} else {                  // N=100: defaults
    g_endTime = 120; g_kStag = K_STAG_LARGE; g_minCount = MIN_COUNT_SMALL;
    g_nc = NC_BASE; g_machineBudget = MACHINE_BUDGET_BASE;
}
```
Stages updated to read the globals (`g_nc` in Stage 1, `g_machineBudget` in Stage 2,
`g_minCount` in labelling, `g_kStag` in the stagnation check).

### 3.3 Result
- Avg Z **4993.9 → 5100.0 (+2.1%)**.
- Improvement at **every** problem size (see master table).
- AvgTime dropped to 44.4 s (small N now use ~10k iters in ~26 s instead of capping at 2.5k).

### 3.4 Interpretation — WHY IT WENT UP
- **Small N (+143 at N=30, +180 at N=50):** the `MAX_ITER_SMALL=10000` lift let small
  instances actually use their search budget instead of stopping at 2500 iters.
- **Large N (+105 at N=200):** `END_TIME_LARGE=180` + `MIN_COUNT_LARGE=1` (faster labelling)
  + more Stage 1/2 candidates let big instances build PairMemory sooner and search longer.
- **Verdict: KEEP. This is the reliable foundation of all later runs.** Low risk because
  no algorithm logic changed — only how much budget each problem size gets.

---

## 4. RUN 2 → RUN 4 — PHASE AB ("B-series" logic + bigger budgets) (`97249e0`, `a94a80b`)

> Kept all A-series config. Added **new algorithm logic** (B2/B3/B4) AND pushed small-N
> iterations much harder (`MAX_ITER`→50000, small N now runs the **full 120 s**, large N
> `END_TIME`→240 s). This run is where the picture becomes MIXED.

### 4.1 B-series code changes

| Tag | File / Stage | Change | Design intent |
|-----|--------------|--------|---------------|
| **B2** | `stages.h` Stage 5 CMA | TripletMemory transfer activated (was built, never used) | Use learned (ja,jb,jc) triplets in elite transfer |
| **B3** | `stages.h` Stage 4 MFBO | `moveOrderSwap` → `relocateAfter` + **forced machine assignment** | Actually FORM the strong (jj,jk) adjacency |
| B4 | `stages.h` Stage 4 | Reset `stage4_phase1` counter before `hasTarget` check | Cosmetic / diagnostic only |

**B3 — the critical regression (code):**
```cpp
// Run 2 (GOOD): pure swap — moves problem job jj AWAY from its bad predecessor ji
Turtle candidate = moveOrderSwap(t, pos_jj, pos_jk);
decodeAndEval(candidate);
if (candidate.obj > t.obj) { t = candidate; ... }   // strict ΔZ > 0

// Run 4 (B3, BROKEN): build (jj,jk) adjacency, leave weak ji→jj pair INTACT,
//                      and force jk onto jj's machine (double perturbation)
int machJj = t.M_select[pos_jj];
if (machJj == 0) continue;                       // jj rejected — cannot anchor
Turtle candidate = relocateAfter(t, pos_jk, pos_jj);
int newPosJk = -1;
for (int pos = 0; pos < N_Order; pos++)
    if (candidate.Order_seq[pos] == jk) { newPosJk = pos; break; }
if (newPosJk >= 0) candidate.M_select[newPosJk] = machJj;   // FORCED machine change
candidate.cacheValid = false;
decodeAndEval(candidate);
```

**B2 — Stage 5 triplet transfer (code, `stage5_CMA` Operation 0):**
```cpp
// For each learned STRONG triplet (ja,jb,jc):
//   require it appears in at least one elite (inElite), and
//   skip if turtle already has it consecutive (inT)
if (!inElite) continue;
if (inT) continue;
Turtle candidate = relocateAfter(t, pb, pa);   // jb after ja  (+ force jb onto ja's machine)
// then relocateAfter(jc, jb) + force jc onto ja's machine
```
Same "forced machine change" flaw as B3, but only **~11 triplet entries exist for N=30**
after 50k iters, so it rarely fires → minor factor.

### 4.2 Budget changes (config / STMO.cpp)
- `g_maxIter` ceiling raised to **50000** (small N now run the full **120 s**, ~45–50k iters).
- Large-N `END_TIME_LARGE` raised **180 → 240 s** (A2).

### 4.3 Result (MIXED)
- Avg Z **5100.0 → 5148.6 (+0.95% overall)** — but the average hides opposite movements:
  - **Small N REGRESSED:** N=30 3503.5 → 3375.4 (**−3.7%**); N=50 4716.3 → 4602.1 (**−2.4%**)
    — *despite running ~5× longer.*
  - **Large N IMPROVED:** N=150 5997.5 → 6208.7; N=200 6284.5 → 6530.9.

### 4.4 Diagnostic deltas (N=30, Run 2 vs Run 4)

| Metric | Run 2 (N=30) | Run 4 (N=30) | Change |
|--------|-------------|-------------|--------|
| Stage 4 hits / 1000 iters | **8.02** | **0.78** | **10.3× worse** |
| Avg iters per Stage 4 hit | 125 | 1294 | 10.3× |
| BestAt% (when best Z is found, as % of run) | 38.9% | **8.3%** | severe premature convergence |
| Stagnation resets (total) | 91 | **441** | 4.8× more |
| Stagnation resets / 1000 iters | 9.1 | 9.9 | ~same rate |
| Stage 2 repairs / 1000 iters | 74,311 | 81,134 | +9.2% |

### 4.5 Interpretation — WHY SMALL N WENT DOWN, LARGE N WENT UP
- **Large-N gains are TIME-driven, not logic-driven:** the only reason N=150/200 improved is
  the extra wall-clock (240 s vs 180 s) and more iterations — not B2/B3.
- **Small-N regression (the Run-4 report blamed B3):** B3 tries to BUILD a strong (jj,jk)
  adjacency without REMOVING the weak (ji→jj) pair, and the forced machine change is a double
  perturbation → Stage 4 almost never improves (0.78/1000). Best Z is found at only ~8% of the
  run, then no improvement for the remaining 92%. With 50k iters and ~440 stagnation resets,
  PairMemory is repeatedly wiped and the learned structure is lost.

---

## 5. RUN 4 → RUN 5 — STAGE 4 SWAP CONTROL (revert B3) (`bb2bad6`, `20ba4ae`)

> Acted on the Run-4 recommendation: **revert B3 only**; keep A-series config, keep B2,
> keep the 50000-iter / extended-time budget.

### 5.1 Code change (the ONLY logic change vs Run 4)
```cpp
// Run 5 stage4_MFBO — reverted to Run 2 swap logic:
Turtle candidate = moveOrderSwap(t, pos_jj, pos_jk);
decodeAndEval(candidate);
if (candidate.obj > t.obj) {      // strict ΔZ > 0
    t = candidate;
    t.stage4_phase1++;
    return true;
}
```
Removed: the `machJj` lookup, `relocateAfter`, the `newPosJk` search, and the forced
`M_select[newPosJk] = machJj`. B2 (Stage 5 triplet transfer) and B4 left in place.

### 5.2 Result
- Avg Z **5148.6 → 5144.6** — essentially flat (−0.08%, within noise).
- **Small N did NOT recover:** N=30 stayed at **3375.4** (identical to Run 4 to the decimal),
  N=50 at 4602.1 — both still well below Run 2's 3503.5 / 4716.3.
- Large N essentially unchanged (N=150 6202.4, N=200 6522.9 — marginally below Run 4, noise).

### 5.3 Interpretation — THE KEY FINDING
Reverting B3 changed the final Best Z **almost not at all**. The N=30 best-Z values are
*byte-identical* between Run 4 and Run 5 even though iteration counts and S2-repair counts
differ. This proves:

1. **Stage 4 is nearly irrelevant to the final score** at these settings — its hit rate is so
   low (~0.8/1000) that the best solution for small/medium N comes from Stage 1/2 exploration,
   not Stage 4. So swapping Stage 4's operator (swap vs relocate) barely moves the number.
2. **B3 was NOT the true cause of the small-N regression.** Since the revert didn't recover
   Run 2's small-N performance, the real culprit is the **iteration/time budget change** in
   PhaseAB: giving N=30 the full 120 s / ~45–50k iters (vs Run 2's ~10k / ~26 s) caused
   premature convergence + ~440 stagnation resets that repeatedly destroy learned PairMemory.
   **More compute actively hurt small N here.**

---

## 6. CONSOLIDATED GOOD / BAD ATTRIBUTION

| Change | Run | Effect | Verdict |
|--------|-----|--------|---------|
| 6 foundational bug fixes | 1 | Algorithm works at all | Mandatory |
| **A-series adaptive config** (MAX_ITER_SMALL, END_TIME_LARGE, K_STAG/MIN_COUNT/NC/MACHINE_BUDGET per N) | 2 | **+2.1% everywhere** | **KEEP — the good value** |
| Budget: small-N MAX_ITER → 50000 (full 120 s) | 4 | **Small N −2.4 to −3.7%** | **REVERT for N≤50** — too many iters + resets destroy memory |
| Budget: large-N END_TIME → 240 s | 4 | Large N up | Keep for N≥150 (gain is time-driven) |
| **B3: Stage 4 relocateAfter + forced machine** | 4 | Conceptually wrong; in practice near-zero effect on final Z | Reverted in Run 5; not where the damage was |
| B3 revert → moveOrderSwap | 5 | Flat (small N did NOT recover) | Confirms B3 wasn't the cause |
| B2: Stage 5 TripletMemory transfer | 4–5 | Rarely fires (~11 triplets for N=30) | Minor; gate on min-count or drop |

### Bottom-line story
- **Run 1 → 2: clear win** from pure parameter tuning (adaptive budgets per problem size).
- **Run 2 → 4: mixed** — large N up (extra time), small N down. The average (+0.95%) masks a
  real small-N regression.
- **Run 4 → 5: flat** — reverting the suspected B3 culprit changed nothing, revealing that
  (a) Stage 4 is currently almost irrelevant to the final score, and (b) the small-N
  regression was caused by the **iteration/time budget**, not the Stage 4 operator.

### Recommendation for the next run (Run 6)
1. For **N ≤ 50**, return to Run 2's shorter budget (~10k iters / ~26 s). The 50k-iter regime
   under aggressive stagnation resets is destroying learned memory — more time made it worse.
2. Keep A-series adaptive config and the extended time **only for N ≥ 150**.
3. Don't spend tuning effort on the Stage 4 operator until Stage 4's **hit rate** is raised
   (it currently contributes ~0.8 improvements per 1000 iters). Investigate raising `K_STAG`
   for the high-iteration regime, or gating stagnation resets so PairMemory isn't wiped so often.
4. Re-run a clean Run 2-config control to confirm reproducibility before adding new logic.

---

## 7. APPENDIX — Full per-instance Best Z (all four comparable runs)

> Columns: N, M, Seed, then Best Z for Run1 / Run2 / Run4 / Run5.
> Source files: `results/results.csv` (Run1), `run_002_*` (Run2), `run_004_*` (Run4),
> `run_005_*` (Run5).

| N | M | Seed | Run1 | Run2 | Run4 | Run5 |
|---|---|------|------|------|------|------|
| 30 | 3 | 10 | 2119.87 | 2228.48 | 2132.80 | 2132.80 |
| 30 | 3 | 20 | 2212.17 | 2250.54 | 2233.52 | 2233.52 |
| 30 | 3 | 30 | 2504.21 | 3042.95 | 2538.64 | 2538.64 |
| 30 | 6 | 10 | 3329.52 | 3355.97 | 3340.21 | 3340.21 |
| 30 | 6 | 20 | 3362.20 | 3481.71 | 3363.80 | 3363.80 |
| 30 | 6 | 30 | 3845.07 | 3916.26 | 3861.84 | 3861.84 |
| 30 | 9 | 10 | 4180.28 | 4339.48 | 4207.77 | 4207.77 |
| 30 | 9 | 20 | 4047.54 | 4153.53 | 4056.15 | 4056.15 |
| 30 | 9 | 30 | 4643.43 | 4762.22 | 4643.43 | 4643.43 |
| 50 | 3 | 10 | 3087.52 | 3149.45 | 3165.87 | 3165.87 |
| 50 | 3 | 20 | 2915.01 | 3031.58 | 2942.86 | 2942.86 |
| 50 | 3 | 30 | 2856.47 | 2851.22 | 2891.14 | 2891.14 |
| 50 | 6 | 10 | 4940.75 | 4963.46 | 4974.38 | 4974.38 |
| 50 | 6 | 20 | 4638.79 | 4800.41 | 4709.71 | 4709.71 |
| 50 | 6 | 30 | 4476.51 | 4632.49 | 4580.24 | 4580.24 |
| 50 | 9 | 10 | 6259.65 | 6739.99 | 6284.09 | 6284.09 |
| 50 | 9 | 20 | 5918.45 | 6091.76 | 6045.97 | 6045.97 |
| 50 | 9 | 30 | 5736.37 | 6186.31 | 5824.23 | 5824.23 |
| 100 | 3 | 10 | 2951.21 | 2952.86 | — | 3017.07 |
| 100 | 3 | 20 | 2862.00 | 2768.30 | — | 2936.25 |
| 100 | 3 | 30 | 3247.79 | 3221.16 | — | 3286.65 |
| 100 | 6 | 10 | 4963.51 | 5005.48 | — | 5117.16 |
| 100 | 6 | 20 | 4864.11 | 5331.70 | — | 4989.05 |
| 100 | 6 | 30 | 5184.47 | 5235.90 | — | 5313.37 |
| 100 | 9 | 10 | 6698.10 | 6800.17 | — | 6824.21 |
| 100 | 9 | 20 | 6569.19 | 6761.42 | — | 6686.75 |
| 100 | 9 | 30 | 6898.76 | 6908.77 | — | 7012.57 |
| 150 | 3 | 10 | 3409.89 | 3362.87 | — | 3565.34 |
| 150 | 3 | 20 | 3884.28 | 3929.86 | — | 4037.53 |
| 150 | 3 | 30 | 3706.66 | 3664.54 | — | 3752.05 |
| 150 | 6 | 10 | 5921.68 | 5884.78 | — | 6163.12 |
| 150 | 6 | 20 | 6458.72 | 6425.84 | — | 6727.09 |
| 150 | 6 | 30 | 6061.79 | 6111.17 | — | 6176.23 |
| 150 | 9 | 10 | 7963.80 | 8025.49 | — | 8374.79 |
| 150 | 9 | 20 | 8463.88 | 8557.11 | — | 8804.30 |
| 150 | 9 | 30 | 7927.87 | 8015.98 | — | 8220.75 |
| 200 | 3 | 10 | 3647.83 | 3617.44 | — | 3795.47 |
| 200 | 3 | 20 | 3640.82 | 3721.33 | — | 3845.58 |
| 200 | 3 | 30 | 3893.74 | 3792.01 | — | 4043.61 |
| 200 | 6 | 10 | 6188.77 | 6271.94 | — | 6535.35 |
| 200 | 6 | 20 | 6376.14 | 6380.46 | — | 6575.51 |
| 200 | 6 | 30 | 6599.96 | 6554.52 | — | 6843.57 |
| 200 | 9 | 10 | 8317.86 | 8605.89 | — | 8835.26 |
| 200 | 9 | 20 | 8297.46 | 8664.67 | — | 8952.99 |
| 200 | 9 | 30 | 8650.62 | 8951.78 | — | 9278.40 |

> Run 4 N≥100 cells are blank: the Run-4 analysis report captured only the completed
> N=30/50 instances at write time (the run was still in progress). The Run 4 per-N *averages*
> in the master table come from the completed `run_004_*` results folder. For N≥100, compare
> Run 1 / Run 2 / Run 5 directly.

---

## 8. SOURCE FILES IN THIS REPO (for cross-checking)

- `CHANGELOG.md` — the 6 foundational bug fixes (pre-Run-1) in full detail.
- `Run002_LabPC_Session_Report.md` — Run 2 environment/build issues (Windows stack, MSYS2, param BOM, run-script bugs).
- `Run004_PhaseAB_Analysis_Report.md` — the deep Run 2 vs Run 4 diagnostic analysis (root-cause of B3).
- `results/all_runs_summary.csv` — run-level summary metadata.
- `results/results.csv` — Run 1 per-instance.
- `results/run_002_*`, `run_004_*`, `run_005_*` — per-run per-instance CSVs + logs.
- `config.h`, `STMO.cpp`, `stages.h`, `memory_ops.h` — current (Run 5) source.
</content>
</invoke>
