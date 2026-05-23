# STMO Changelog

All changes to this project are documented here.
Format: Date → What changed → Why → Effect on results.

---

## [2026-05-20] — Bug Fixes + Research Output

**Branch:** `dev`
**Author:** Vimal De Souza
**Collaborator:** Claude Code (AI assistant)

### Why this update was made
The algorithm was compiling but producing wrong results and hanging
indefinitely (never stopping at the 120-second time limit). Six separate
bugs were found and fixed. Research output was also added to support
thesis analysis.

---

### Bug Fix 1 — Infinite loop in all random move functions
**File:** `problem.h`
**Functions:** `moveOrderInsert`, `moveMachineRevise`, `moveOrderSwap`

**Problem:** Each function has a do-while loop to pick a random valid
position. The random value was only generated if `k < 0`, but after
the first assignment `k` is no longer negative — so if the first random
value happened to be invalid, the loop ran forever. Probability of
hitting this: ~70% per iteration in Stage 1 alone.

**Fix:** Moved the do-while inside the `if (k < 0)` block so it retries
correctly until a valid value is found.

**Effect:** Algorithm now completes iterations normally instead of hanging.

---

### Bug Fix 2 — Population changes were silently discarded
**File:** `stages.h`, `memory_ops.h`
**Functions:** All stage functions + memory update functions

**Problem:** All 9 functions that receive the population took it by
**value** (`Population pop`) instead of by **reference** (`Population& pop`).
This meant every improvement computed inside a stage was thrown away
when the function returned. The algorithm was running but learning
nothing — like doing homework and then throwing it in the bin.

**Fix:** Changed all 6 stage functions and 3 memory functions to use
`Population&` (modify) or `const Population&` (read-only).

**Effect:** Algorithm now actually improves solutions between iterations.

---

### Bug Fix 3 — Stack overflow on startup
**File:** `STMO.cpp`

**Problem:** Three large arrays (`Population` ~1.2 MB, `EliteArchive`,
`StructuralMap[50]`) were declared as local variables in `main()`.
Windows default stack is 1 MB — these exceeded it immediately.

**Fix:** Added `static` keyword so they live in the data segment instead.

**Effect:** Program no longer crashes on startup.

---

### Bug Fix 4 — Algorithm ran past the 120-second time limit
**File:** `STMO.cpp`

**Problem:** The time check only ran at the START of each iteration.
If one stage (e.g. Stage 3 ACMM) took a long time, the algorithm would
overshoot `END_TIME` significantly with no way to stop mid-iteration.

**Fix:** Added a `timeUp()` lambda and called it after every stage:
after Stage 1, Stage 2, BatchEval2, Stage 3, Stage 4, Stage 5.

**Effect:** Algorithm stops within one stage of the time limit.

---

### Bug Fix 5 — Infinite loop in Stage 5 (specific edge case)
**File:** `stages.h`
**Function:** `stage5_CMA`

**Problem:** Stage 5 calls `moveOrderInsert(t, pos_jj, targetPos)` with
an explicit target position. When `pos_jj == targetPos - 1`, the move
is a no-op AND the do-while exit condition is never satisfied → infinite
loop. This triggers when the elite job is already adjacent to where we
want to move it.

**Fix:** Added a guard: `if (pos_jj_in_t == targetPos - 1) continue;`
before the `moveOrderInsert` call.

**Effect:** Eliminates the remaining infinite-loop case in Stage 5.

---

### Bug Fix 6 — runODTP was O(S²) — too slow for large PairMemory
**File:** `memory_ops.h`
**Function:** `runODTP`

**Problem:** The original implementation used a double loop over all
strong pairs to find overlapping triplets — O(S²) complexity. With
S ~500 strong pairs and P=30 turtles × M=9 machines × N=150 jobs,
this made each iteration take several seconds once PairMemory grew.

**Fix:** Built a successor hash index (`map<int, vector<int>> strongNext`)
so for each strong pair (A→B) we look up STRONG pairs starting from B
in O(1) instead of scanning all S pairs again.

**Effect:** Stage 3 is now fast regardless of PairMemory size.

---

### New Feature — Research output to `out.txt`
**File:** `STMO.cpp`

**Purpose:** Enable thesis analysis — convergence curves, phase
attribution, PairMemory evolution over time.

**What is written to `out.txt` after every run:**
- Configuration block (all parameters, reproducibility)
- Final summary (best Z, iterations used, time used)
- Phase attribution: which % of improvements came from Stage 1/2
  (exploration) vs Stage 3/4/5 (memory intelligence)
- PairMemory label distribution at end (STRONG/WEAK/NEUTRAL/CRITICAL %)
- New-best event log: every time best Z improved — iter, time, delta, source stage
- Convergence table (every 10 iters): Z, PM size, label counts, S2 repairs, stagnation
- CSV section: same data comma-separated for Excel / Python / MATLAB plotting
- Best schedule (machine assignments + rejected jobs)

**Console additions:**
- `[** NEW BEST **]` line printed immediately whenever best Z improves
- Every 50 iterations: PM label breakdown + TripletMemory size

---

### Parameter change — MAX_ITER and END_TIME
**File:** `config.h`

| Parameter | Before | After | Reason |
|-----------|--------|-------|--------|
| `MAX_ITER` | 500 | 2500 | Old limit hit at 23s; full 120s budget allows ~2100 iters |
| `END_TIME` | 120.0 | 300.0 | User extended for longer research runs |

---

### Results comparison

| Metric | Before fixes | After fixes |
|--------|-------------|-------------|
| Completed? | No (hung forever) | Yes |
| Best Z (120s) | N/A | 7963.80 |
| Iterations in 120s | 0 | ~2100 |
| Stage 2 repairs | 0 (discarded) | 289,995 |
| Stage 4 Phase1 hits | 0 (discarded) | 58 |

---

## [2026-05-19] — Initial Working Version

**Branch:** `dev`

### What was in this version
- All 5 STMO stages implemented: OceanCurrentDrift, MemoryAwareDrift,
  ACMM, MFBO (Phase 1 only), CMA
- PairMemory with two-pass decay + labelling
- TripletMemory via ODTP
- EliteArchive (top-5 solutions)
- M0Pool for rejected jobs
- Problem infrastructure adapted from professor's WFA code (Wu et al.)
- Basic console output every 10 iterations

### What was not yet working
- See Bug Fixes 1–6 above (all present in this version)
- Algorithm compiled and ran but produced no meaningful results

---

## How to update this file

Each time you are about to push changes to GitHub, add a new entry at
the TOP of this file (above the previous entries) using this template:

```
## [YYYY-MM-DD] — Short title describing this update

**Branch:** dev
**Author:** Vimal De Souza

### Why this update was made
(1–3 sentences: what problem or question motivated these changes)

### Changes
- File X: what changed and why
- File Y: what changed and why

### Results comparison (if applicable)
| Metric | Before | After |
|--------|--------|-------|
| Best Z | X | Y |
```

Then in VS Code Source Control:
1. Stage your changed files
2. Type a short commit message (1 line summary)
3. Click Commit
4. Click the "..." menu → Push (or the cloud/arrow icon)
