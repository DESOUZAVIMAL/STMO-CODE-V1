# STMO Run 009 — Diagnostic Findings & Improvement Plan

**Dataset:** 45 data instances (N∈{30,50,100,150,200} × M∈{3,6,9} × Seed∈{10,20,30}),
each run once under the per-N budget (N≤50: 120s/50k iters; N≥100: 300s/20k iters),
plus a 5-instance ×3 reproducibility probe. Generated on the lab PC, analyzed offline.

---

## 0. Validity gate — PASSED

| Check | Result |
|---|---|
| **Reproducibility** | ✅ all 5 probe instances **DETERMINISTIC** — identical Z *and* trajectory hash across 3 repeats |
| **Objective integrity** | ✅ **0 violations** across 60 integrity files — every accepted move's recomputed objective matched the incremental |
| Sanitizer (ASan/UBSan) | ⚠️ not in dataset (lab-PC mingw64 can't link sanitizers; needs a UCRT64/CLANG64 or Linux pass) |

**The Run 7 `unordered_map` did NOT break determinism**, and the evaluation engine has no
feasibility/objective bugs. The data is trustworthy.

---

## 1. Executive summary — the bottleneck

> **The mid/large-N problem is a SPEED problem, not a search-quality problem. Stage 2
> (MemoryAwareDrift) performs ~2–3 million accepted moves per run, of which 99.3–99.9% are
> non-improving "sideways" moves — each costing a full O(N) re-decode. This workload grows
> with N (65 → 202 accepts/iteration), collapsing throughput ~10× from N30 to N200. Because
> large-N runs are still improving when they hit the time limit (best found at ~90% of the
> run), making the algorithm faster would translate almost directly into better solutions.**

Two distinct regimes emerged:

| Regime | N | Symptom | Root cause |
|---|---|---|---|
| **Starved** | 100–200 | improves until the budget runs out (best @ 81–90% of run) | too slow → too few iterations |
| **Stalled** | 30–50 | converges at ~50% then runs a dead tail | nothing left to find / weak late-search |

---

## 2. SPEED findings

### 2.1 Throughput collapses ~10× with N  *(F1, F2)*

| N | iters/sec | ms/iter | final_iter (mean) |
|---|---|---|---|
| 30 | 425.7 | 2.38 | 47,940 |
| 50 | 225.5 | 4.51 | 27,059 |
| 100 | 97.5 | 11.27 | 19,771 |
| 150 | 57.5 | 19.46 | 15,434 |
| 200 | 41.5 | 27.77 | 12,439 |

Per-iteration cost grows ~11.7× while N grows 6.7× → **super-linear (~N¹·⁴)**, not the O(N)
a single decode would predict. The extra factor is the Stage 2 workload (below).

### 2.2 Stage 2 is the sink: millions of mostly-useless evaluations  *(F3)*

| N | S2 accepts/iter | % sideways (non-improving) | S2 accepts/run |
|---|---|---|---|
| 30 | 65.3 | 99.9% | 3,130,257 |
| 100 | 153.2 | 99.6% | 3,011,594 |
| 200 | 202.2 | 99.3% | 2,181,841 |

Per-stage accepted moves at **N200** (mean): **S1 = 373k, S2 = 2.18M, S4 = 515, S5 = 2,953.**
Stage 2 is ~85% of all accepted moves and dwarfs every other stage by 1000×. Of its 2.18M
accepts, only **1,839 (0.08%)** ever improved the global best.

**Why:** Stage 2 accepts on `ΔZ ≥ 0` (non-worsening), so every equal-objective move is taken
and re-decoded. PairMemory flags more weak/critical pairs as N grows (§4), giving Stage 2 ever
more repair targets → the accepts/iter climb from 65 to 202.

---

## 3. QUALITY / CONVERGENCE findings

### 3.1 Large N is iteration-STARVED, not stagnated  *(F5, F7)*

| N | best found at (frac of run) | dead-tail frac | best events |
|---|---|---|---|
| 30 | 0.49 | 0.51 | 39 |
| 50 | 0.60 | 0.40 | 59 |
| 100 | 0.81 | 0.19 | 89 |
| 150 | 0.87 | 0.13 | 96 |
| 200 | 0.90 | 0.11 | 104 |

This **refutes the "early stagnation at large N" hypothesis.** At N200 the best keeps improving
to 90% of the run — it stops because it runs out of budget, not because it's stuck. **Therefore
speed gains ⇒ quality gains at large N:** more iterations in the same 300s would yield more of
the 104 improvement events.

Conversely, **small N (30–50) does stall** — it converges at ~50% and then wastes the rest. A
different problem (late-search intensification / nothing left to exploit), lower priority.

### 3.2 Stage contribution  *(F4)*

Global-best-contribution counts at N200 (mean): **S2g = 1,839, S5g = 184, S4g = 75, S1g = 42.**
Stage 2 is both the speed sink *and* the dominant improver — it is inefficient, not useless.
**Stage 4 barely fires** (~500 accepts/run across all N) and contributes little, consistent
with the review finding that its guidance criterion no longer matches its move (swap vs the
pair it scores). Not a bottleneck, but a wasted mechanism.

---

## 4. PairMemory behavior  *(F6)*

| N | peak | final | churn (peak/final) | ccrit peak |
|---|---|---|---|---|
| 30 | 768 | 131 | 6.5× | 191 |
| 100 | 5,917 | 357 | 20.5× | 3,405 |
| 200 | 14,547 | 619 | 28.7× | 12,239 |

PairMemory **bloats to ~15k entries at N200 then collapses to ~600** (28× churn), and the
critical-pair count peaks at ~12k. This bloat is what feeds Stage 2 more targets → more
sideways work. Memory growth and the Stage 2 explosion are the same story.

---

## 5. Diversity  *(refutes collapse)*

`distinct_full` = **30/30 at first and last checkpoint for every N.** The population never
collapses — ironically because Stage 2's constant sideways churn keeps it moving. Elite-archive
duplication was inconclusive (N30/N200 fully distinct; N100 showed possible order-sequence
overlap that may differ in machine assignment). **Diversity is not a problem here.**

---

## 6. Hypothesis scorecard (vs the pre-run code review)

| Hypothesis | Verdict |
|---|---|
| Stage 2 does huge low-value work | ✅ **CONFIRMED** — 99%+ sideways, 2–3M accepts/run |
| No delta-eval → cost dominates | ✅ **CONFIRMED** — super-linear per-iter cost |
| Stage 4 criterion mismatch → wasted | ✅ **CONFIRMED** (weakly) — fires rarely, low contribution |
| Early stagnation at large N | ❌ **REFUTED** — large N improves to ~90% of run (starved) |
| Elite-archive duplication starves S5 | ⚠️ **INCONCLUSIVE** — not a primary effect |
| Population diversity collapse | ❌ **REFUTED** — 30/30 distinct throughout |

---

## 7. Improvement plan — Run 010 (prioritized, data-justified)

Each fix is isolated and gate-verifiable; Run 009 is the **before** baseline.

| Priority | Fix | Evidence | Predicted payoff |
|---|---|---|---|
| **P1** | **Incremental (delta) evaluation** — recompute only the edited machine's tail; use the already-present `partialReeval`/`machineObj[]` (currently unused). Apply→eval→undo to kill the 24 KB copies. | §2: 2–3M full O(N) decodes/run dominate cost | **5–15× speed at N200**; since large N is starved (§3.1), this directly improves Best Z |
| **P2** | **Tighten / throttle Stage 2** — accept on strict `ΔZ > 0`, or cap sideways moves per machine, or add don't-look bits so a machine that failed repair is skipped until it changes. | §2.2: 99% of S2 accepts are non-improving | removes most wasted work; compounds with P1 |
| **P3** | **Bound PairMemory growth** — cap/evict to stop the 15k bloat that feeds S2 targets. | §4: 28× churn, 12k critical pairs at N200 | fewer S2 targets → less wasted work |
| **P4** | **Fix Stage 4's guidance criterion** — score the pair the swap actually creates (or restore relocate semantics). | §3.2: S4 fires rarely, low yield | recovers S4 as a real improver |
| P5 | Small-N late-search (dead-tail) — stronger intensification or earlier stop. | §3.1: N30 dead-tail 0.51 | reclaims wasted small-N budget |

**Sequence:** P1 first (biggest lever, and it makes large-N quality improve for free), each
change gate-verified (DIAG 0 vs 1 identical is not required post-fix, but a FULL-vs-incremental
objective cross-check is — the `integrity_violations` machinery already does this). Measure
Best Z and iters/sec before/after on the same 45 instances.

---

## 8. Figures (`analysis/figures/`)

F1 speed scaling · F2 cost/iter · F3 Stage 2 workload · F4 stage contribution ·
F5 convergence timing · F6 PairMemory bloat · F7 convergence curves.

Master table: `analysis/run009_metrics.csv` (one row per instance).
