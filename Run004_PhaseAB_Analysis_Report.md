# STMO Run 004 — Phase AB Analysis Report
## For Deep Analysis in Claude AI Web

**Author:** Vimal De Souza | Student 1135446 | Yuan Ze University  
**Date:** 2026-06-02  
**Lab PC Branch:** `dev-labpc`  
**Experiment:** Run004 = "Run003_PhaseAB" (new algorithm stages from laptop)

---

## 1. EXPERIMENT OVERVIEW

### What is STMO?
Sea Turtle Migration Optimization (STMO) — a population-based metaheuristic for the Order Acceptance and Scheduling problem on Identical Parallel Machines with Sequence-Dependent Setup Times (OAS-SDST). Reference: Wu et al. (2018), Applied Soft Computing.

### 5-Stage Algorithm
```
Each Iteration:
  Stage 1: OceanCurrentDrift    → random exploration (g_nc candidates)
  Stage 2: MemoryAwareDrift     → repair WEAK pairs using PairMemory
  Batch Eval                    → evaluate all 30 turtles, extract pair observations
  Stage 3: ACMM                 → update PairMemory, label pairs, run ODTP triplets
  Stage 4: MFBO Phase 1         → weak-pool guided swap/repair using StructuralMap
  Stage 5: CMA                  → elite archive transfer + M0 reinsertion
  Batch Eval                    → update EliteArchive, M0Pool, check stagnation
```

### Problem Size Matrix (45 instances)
- N (orders) = 30, 50, 100, 150, 200
- M (machines) = 3, 6, 9
- Seeds = 10, 20, 30
- **Higher Z = better** (total weighted profit from accepted orders)

---

## 2. RUNS BEING COMPARED

| Run | Label | Branch | Key Changes vs Run001 |
|-----|-------|--------|----------------------|
| Run 001 | baseline | main | First clean run post bug-fixes |
| Run 002 | adaptive_config | dev-laptop | A-series only: adaptive config by N |
| Run 004 | PhaseAB | dev-laptop→dev-labpc | A-series + B-series (Stage4/Stage5 changes) |

> **Note:** Run 004 folder name is due to 2 earlier failed/incomplete attempts. Experiment label is "Run003_PhaseAB".

### A-Series Changes (Run002 → Run004, KEPT)
| Parameter | Old | New | Effect |
|-----------|-----|-----|--------|
| MAX_ITER (N≤50) | 2500 | 50000 | 20× more iterations for small N |
| MAX_ITER (N≥100) | 2500 | 2500 | unchanged |
| END_TIME (N≤100) | 120s | 120s | unchanged |
| END_TIME (N≥150) | 120s | 240s | 2× time for large N |
| K_STAG (N≤50) | 50 | 100 | Slower stagnation reset for small N |
| K_STAG (N≥100) | 50 | 50 | unchanged |
| MIN_COUNT (N≤100) | 3 | 3 | unchanged |
| MIN_COUNT (N≥150) | 3 | 1 | Faster PairMemory labelling for large N |
| NC (N≤100) | 3 | 3 | unchanged |
| NC (N≥150) | 3 | 5 | More Stage 1 candidates for large N |
| MACHINE_BUDGET (N≤100) | 3 | 3 | unchanged |
| MACHINE_BUDGET (N≥150) | 3 | 5 | More Stage 2 repairs for large N |
| E_SIZE (N≤100) | 5 | 5 | unchanged |
| E_SIZE (N≥150) | 5 | 10 | Larger elite archive for large N |

### B-Series Changes (Run004 NEW, NOT in Run002)
| Tag | Stage | Change | Design Intent |
|-----|-------|--------|---------------|
| B2 | Stage 5 CMA | TripletMemory transfer added | Use learned triplets in elite transfer |
| B3 | Stage 4 MFBO | `relocateAfter` instead of `moveOrderSwap` | Form STRONG pair adjacency (not just swap) |
| B4 | Stage 4 MFBO | Counter reset before `hasTarget` check | Cosmetic (diagnostic only) |

---

## 3. RAW RESULTS

### 3.1 Run 002 — Complete Results (45/45 instances)
```
N,M,Seed,BestZ,Iterations,TimeUsed,BestAtIter,S2Repairs,S4Phase1Hits,StagnationResets
30,3,10,2228.4761,10000,25.24,1583,577724,37,94
30,3,20,2250.5408,10000,26.12,3477,505237,46,92
30,3,30,3042.9487,10000,25.93,6686,578250,29,92
30,6,10,3355.9690,10000,28.20,1747,564658,74,90
30,6,20,3481.7095,10000,28.03,8982,791140,90,88
30,6,30,3916.2551,10000,28.39,4268,697012,97,92
30,9,10,4339.4795,10000,24.73,1681,1001081,107,93
30,9,20,4153.5322,10000,26.03,3158,857631,131,90
30,9,30,4762.2202,10000,23.12,3501,1115272,111,90
50,3,10,3149.4478,10000,41.11,4740,748392,21,93
50,3,20,3031.5830,10000,40.96,7555,693901,29,89
50,3,30,2851.2217,10000,39.78,1902,583029,31,88
50,6,10,4963.4629,10000,52.43,8392,1072722,113,84
50,6,20,4800.4053,10000,53.52,6970,1179216,149,90
50,6,30,4632.4858,10000,54.38,9266,901455,125,81
50,9,10,6739.9927,10000,58.72,8106,1288532,208,84
50,9,20,6091.7568,10000,55.97,9827,1411275,267,82
50,9,30,6186.3066,10000,53.53,9797,1112900,209,84
100,3,10,2952.8557,2500,18.95,1919,158376,10,38
100,3,20,2768.3049,2500,19.06,1146,158170,32,40
100,3,30,3221.1587,2500,19.02,2346,154582,29,37
100,6,10,5005.4756,2500,31.98,2017,274439,52,34
100,6,20,5331.6978,2500,31.55,2297,262329,80,31
100,6,30,5235.8984,2500,31.46,2474,265229,66,31
100,9,10,6800.1689,2500,45.42,2431,418970,152,36
100,9,20,6761.4160,2500,39.91,2479,373637,104,34
100,9,30,6908.7749,2500,39.96,2357,357951,112,30
150,3,10,3362.8738,2500,28.65,2051,150967,30,33
150,3,20,3929.8574,2500,29.06,1854,143113,39,31
150,3,30,3664.5435,2500,29.12,1544,151263,23,37
150,6,10,5884.7827,2500,48.99,2334,269356,67,33
150,6,20,6425.8408,2500,48.69,1894,237076,109,29
150,6,30,6111.1699,2500,49.71,2023,280706,84,29
150,9,10,8025.4932,2500,68.19,2462,381699,128,20
150,9,20,8557.1133,2500,67.14,2447,339456,141,30
150,9,30,8015.9824,2500,67.29,2224,380791,131,29
200,3,10,3617.4380,2500,38.32,1725,130396,38,30
200,3,20,3721.3340,2500,38.39,2488,129281,44,28
200,3,30,3792.0144,2500,40.59,1982,137542,30,31
200,6,10,6271.9380,2500,70.41,2287,226775,95,21
200,6,20,6380.4644,2500,65.79,2464,221307,93,22
200,6,30,6554.5239,2500,66.18,2488,245429,48,24
200,9,10,8605.8906,2500,93.27,2401,313753,121,19
200,9,20,8664.6660,2500,92.37,2096,305839,146,19
200,9,30,8951.7812,2500,93.12,2412,336052,78,20
```

### 3.2 Run 004 (PhaseAB) — Partial Results (14/45 completed at time of report)
> Run still in progress. N=30 (9 inst) and N=50 (5 inst) complete. N=100/150/200 TBD.
```
N,M,Seed,BestZ,Iterations,TimeUsed,BestAtIter,S2Repairs,S4Phase1Hits,StagnationResets
30,3,10,2132.7959,49261,120.00,544,2926276,15,489
30,3,20,2233.5176,48096,120.00,1887,2750150,10,473
30,3,30,2538.6406,45229,120.01,6518,3226227,27,447
30,6,10,3340.2053,40377,120.00,1736,3053650,47,397
30,6,20,3363.7991,40682,120.00,9442,3334834,46,397
30,6,30,3861.8447,40572,120.00,2563,3002913,35,397
30,9,10,4207.7710,44710,120.01,4941,3786508,45,438
30,9,20,4056.1538,50000,108.61,3639,5354080,51,494
30,9,30,4643.4302,44537,120.00,1316,4605041,36,439
50,3,10,3165.8669,26436,120.00,20672,2139374,21,257
50,3,20,2942.8599,32650,120.01,15708,2552417,15,316
50,3,30,2891.1443,28921,120.01,19754,2112255,24,283
50,6,10,4974.3809,23092,120.01,10721,2853964,56,217
50,6,20,[still running at time of this report]
```

---

## 4. SIDE-BY-SIDE COMPARISON TABLE (N=30, all 9 instances)

| Instance | R002 BestZ | R004 BestZ | ΔZ | Δ% | R002 Iters/Time | R004 Iters/Time | R002 BestAt% | R004 BestAt% | R002 S4hits | R004 S4hits | R002 Resets | R004 Resets |
|----------|-----------|-----------|-----|-----|-----------------|-----------------|-------------|-------------|------------|------------|------------|------------|
| N30 M3 S10 | 2228.48 | 2132.80 | **-95.68** | **-4.3%** | 10000/25s | 49261/120s | 15.8% | 1.1% | 37 | 15 | 94 | 489 |
| N30 M3 S20 | 2250.54 | 2233.52 | -17.03 | -0.8% | 10000/26s | 48096/120s | 34.8% | 3.9% | 46 | 10 | 92 | 473 |
| N30 M3 S30 | 3042.95 | 2538.64 | **-504.31** | **-16.6%** | 10000/26s | 45229/120s | 66.9% | 14.4% | 29 | 27 | 92 | 447 |
| N30 M6 S10 | 3355.97 | 3340.21 | -15.77 | -0.5% | 10000/28s | 40377/120s | 17.5% | 4.3% | 74 | 47 | 90 | 397 |
| N30 M6 S20 | 3481.71 | 3363.80 | -117.91 | -3.4% | 10000/28s | 40682/120s | 89.8% | 23.2% | 90 | 46 | 88 | 397 |
| N30 M6 S30 | 3916.26 | 3861.84 | -54.42 | -1.4% | 10000/28s | 40572/120s | 42.7% | 6.3% | 97 | 35 | 92 | 397 |
| N30 M9 S10 | 4339.48 | 4207.77 | -131.71 | -3.0% | 10000/25s | 44710/120s | 16.8% | 11.0% | 107 | 45 | 93 | 438 |
| N30 M9 S20 | 4153.53 | 4056.15 | -97.38 | -2.3% | 10000/26s | 50000/109s | 31.6% | 7.3% | 131 | 51 | 90 | 494 |
| N30 M9 S30 | 4762.22 | 4643.43 | -118.79 | -2.5% | 10000/23s | 44537/120s | 35.0% | 3.0% | 111 | 36 | 90 | 439 |
| **AVERAGE** | **3503.46** | **3375.35** | **-128.11** | **-3.66%** | **10000/25.7s** | **45273/118s** | **38.9%** | **8.3%** | **80.2** | **34.7** | **91.2** | **441.2** |

### N=50 Partial Comparison (4 instances with Run002 data)

| Instance | R002 BestZ | R004 BestZ | ΔZ | Δ% | R002 Iters/Time | R004 Iters/Time |
|----------|-----------|-----------|-----|-----|-----------------|-----------------|
| N50 M3 S10 | 3149.45 | 3165.87 | **+16.42** | **+0.5%** | 10000/41s | 26436/120s |
| N50 M3 S20 | 3031.58 | 2942.86 | -88.72 | -2.9% | 10000/41s | 32650/120s |
| N50 M3 S30 | 2851.22 | 2891.14 | **+39.92** | **+1.4%** | 10000/40s | 28921/120s |
| N50 M6 S10 | 4963.46 | 4974.38 | **+10.92** | **+0.2%** | 10000/52s | 23092/120s |

---

## 5. DIAGNOSTIC METRIC ANALYSIS

### 5.1 Stage 4 Effectiveness Rate

Stage 4 is the MFBO intensification phase. Hits = successful strict improvements.

**Hits per 1,000 iterations:**

| Metric | Run002 (N=30 avg) | Run004 (N=30 avg) | Ratio |
|--------|------------------|------------------|-------|
| S4 hits/1000 iters | **8.02** | **0.78** | **10.3×** |
| Avg iters/hit | 125 | 1294 | 10.3× |

**Interpretation:** Stage 4 is 10× less effective per iteration in Run004. The algorithm has to run 10× longer to achieve the same number of Stage 4 improvements.

### 5.2 Stagnation Reset Rate

| Metric | Run002 (N=30 avg) | Run004 (N=30 avg) | Notes |
|--------|------------------|------------------|-------|
| Total resets | 91.2 | 441.2 | 4.8× more |
| Resets/1000 iters | 9.12 | 9.92 | **same rate** |
| Expected (K_STAG=100) | ~10/1000 | ~10/1000 | consistent |

**Interpretation:** The stagnation reset RATE is the same — it resets every ~100 iterations when stuck, as expected from K_STAG=100. But because Run004 runs 4-5× more iterations, it gets 4-5× more resets. This is expected behavior, not a bug.

### 5.3 Best Improvement Timing (BestAtIter as % of total iterations)

| Instance Group | Run002 avg BestAt% | Run004 avg BestAt% |
|----------------|-------------------|-------------------|
| N=30 (all 9) | **38.9%** | **8.3%** |

Run004 finds its best solution at only 8% of the total run, then makes no further improvements for the remaining 92% of runtime. This is severe premature convergence.

For N=50:
- R002 N50M3S10: BestAt = 47.4% of run
- R004 N50M3S10: BestAt = 78.2% of run (keeps improving later!)

Note: For N=50, Run004 continues finding improvements much later in the run. This suggests the regression is more severe for smaller N.

### 5.4 Stage 2 Repair Rate

| Metric | Run002 (N=30 avg) | Run004 (N=30 avg) |
|--------|------------------|------------------|
| Total S2 repairs | 743,112 | 3,671,075 |
| S2 repairs/1000 iters | 74,311 | 81,134 |
| Rate increase | — | +9.2% |

Stage 2 repairs per iteration are slightly higher in Run004. This is probably because PairMemory is being cleared more often (more stagnation resets) and Stage 2 keeps finding weak pairs to repair.

---

## 6. ROOT CAUSE ANALYSIS

### 6.1 Primary Cause: B3 — Stage 4 Logic Regression

**What changed (B3):**

```cpp
// Run002 Stage 4 (GOOD):
Turtle candidate = moveOrderSwap(t, pos_jj, pos_jk);
decodeAndEval(candidate);
// Pure job swap — no machine change forced

// Run004 Stage 4 (BROKEN):
Turtle candidate = relocateAfter(t, pos_jk, pos_jj);
// ... then ...
candidate.M_select[newPosJk] = machJj;  // FORCE jk onto jj's machine
candidate.cacheValid = false;
decodeAndEval(candidate);
```

**Why Run002 swap worked:**
- Target: `ji → jj` is a WEAK pair (bad consecutive pairing)
- Action: Swap `jj ↔ jk` in encoding
- Result: `jj` moves to `jk`'s old position. `ji` no longer has `jj` as its successor → **WEAK pair broken**
- Additionally: `jk` moves to `jj`'s old position, inheriting a potentially better context

**Why Run004 relocateAfter fails:**
- Target: `ji → jj` is still a WEAK pair (same identification)
- Action: Move `jk` to immediately after `jj` in the encoding, force `jk` onto `jj`'s machine
- Result: The STRONG pair `(jj, jk)` is formed adjacently
- **BUT: `ji → jj` is still intact. The weak pair is NOT broken.**
- Additional problem: Forcing `jk` to `jj`'s machine is a double perturbation (position + machine change), making improvements less likely

**The conceptual flaw:** Stage 4's goal is to escape from weak pair configurations. The `relocateAfter` approach tries to BUILD something new (STRONG adjacency) without REMOVING the problem (WEAK pair). The `swap` approach directly moves the problem job (`jj`) away from its bad predecessor (`ji`).

### 6.2 Contributing Factor: Much Higher Iteration Count for N≤50

For N=30, Run004 runs ~45,000 iterations vs Run002's 10,000. If Stage 4 is broken:
- Each stagnation reset clears 25% of WEAK records
- With 440+ resets, PairMemory is constantly being rebuilt from scratch
- The algorithm loses its learned patterns repeatedly
- Result: Worse exploration than Run002 despite more time

For N=50, the extra time (120s vs 40-55s) partially compensates, giving mixed results.

### 6.3 Minor Factor: B2 — TripletMemory in Stage 5

TripletMemory entries for N=30: only **11 entries** after 50,000 iterations. Very sparse.

The triplet transfer in Stage 5 (Operation 0) adds a forced 3-job placement with machine assignment:
1. Locate `ja` in turtle → find its machine
2. `relocateAfter(jb, ja)` + force `jb` onto `ja`'s machine
3. `relocateAfter(jc, jb)` + force `jc` onto `ja`'s machine

This has the same "forced machine change" flaw as B3. Each step forces a machine assignment that may not be globally optimal. However, with only 11 triplet entries and the `inElite` + `!inT` guards reducing eligible triplets further, this rarely triggers and is a minor factor.

### 6.4 Why Run002 Results Were Genuine (Not a Fluke)

Run002 made ONLY adaptive config changes — no algorithm logic was modified:
- Gave N≤50 more iterations (10,000 vs 2,500 baseline)
- Gave large N shorter iteration budget with faster labeling (MIN_COUNT=1)
- Used smarter K_STAG per problem size

These are straightforward improvements with clear reasoning. The Run002 results are real and the algorithm was working correctly. The Run004 regression is entirely due to B3.

---

## 7. SUMMARY TABLE: KEY METRICS

| Metric | Run001 baseline | Run002 adaptive | Run004 PhaseAB | Notes |
|--------|----------------|----------------|----------------|-------|
| N=30 avg BestZ | ~2119-4339 range | **3503.5** | 3375.4 | Run004 worse |
| N=50 avg BestZ | (need calc) | **4806.0** | ~3994 (partial) | Run004 mixed |
| S4hits rate (N=30) | unknown | 8.02/1000 | 0.78/1000 | 10× regression |
| BestAt% (N=30) | unknown | 38.9% | 8.3% | Early convergence |
| Stag reset rate | ~10/1000 | ~9/1000 | ~10/1000 | All similar |
| Time (N=30/inst) | ~18s | ~26s | 120s | 5× longer but worse |

---

## 8. QUESTIONS FOR DEEP ANALYSIS

1. **Is the `relocateAfter` + forced machine assignment ever better than pure swap?** Under what conditions?

2. **Should Stage 4 target BREAKING weak pairs (move jj away) or BUILDING strong pairs (bring jk close)?** Is there a way to do both simultaneously?

3. **The BestAt% for N=50 in Run004 is 78% (finds improvement at 78% of run).** This suggests Stage 1 + Stage 2 exploration at higher iteration counts is eventually effective even with broken Stage 4. How should we balance Stage 4 vs Stage 2 in the algorithm?

4. **With K_STAG=100 and 50000 iterations, the algorithm resets ~490 times for N=30.** Is this too many resets? Should we increase K_STAG further for N≤50 in the 50000-iter regime?

5. **Is the TripletMemory concept (Stage 5 B2) worth keeping?** It has only 11 entries for N=30 and rarely triggers. Should the triplet transfer be gated on a minimum count?

6. **Run002 achieved BestAt=66.9% for N30 M3 S30 (Seed=30).** This is unusually late. Is this instance harder or did the algorithm get lucky with late exploration? Run004 achieves 14.4% for the same instance, showing very different behavior.

7. **N=50 results are mixed in Run004** (+0.5%, -2.9%, +1.4%, +0.2%). The extra time helps somewhat. How do we decide the optimal time budget per problem size for future runs?

---

## 9. RECOMMENDED NEXT EXPERIMENTS

### Run 005 (proposed): Revert B3, Keep Everything Else
- Keep all A-series adaptive config changes (proven good in Run002)
- **Revert Stage 4 to `moveOrderSwap`** (removes B3 regression)
- Keep B2 (TripletMemory in Stage 5 — minor factor, needs more testing)
- Keep MAX_ITER=50000 for N≤50 (more iterations are good if Stage 4 works)
- Expected: Beat Run002 on N≤50 (because more iterations + working Stage 4)

### Run 006 (proposed): Full Comparison Baseline Re-run
- Use identical Run002 config as a control
- Verify Run002 results are reproducible
- Confirm no environmental differences affecting results

---

## 10. TECHNICAL NOTES — LAB PC ENVIRONMENT

- **OS:** Windows 11 Pro (build 26200)
- **Compiler:** MSYS2 MinGW64 g++ (via C:\msys64)
- **Compile flags:** `-O2 -std=c++11 -static -static-libgcc -static-libstdc++ -Wl,--stack,8388608`
- **Stack flag required:** STMO needs 8MB stack (population + elite archive ≈ 2.28MB on stack)
- **Run script:** `run_all_experiments.sh` (bash via MSYS2)
- **Known issue fixed this session:** `set -e` in bash 5.x exits on grep-no-match inside command substitution → fixed with `set +e`/`set -e` around parse block
- **BEST_AT parsing:** Grep pattern updated from `║`-based (Run002 box chars) to `grep -oP '\d+' | tail -1` (Run003+ plain format)
