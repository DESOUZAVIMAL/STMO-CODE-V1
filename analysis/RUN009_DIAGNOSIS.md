# STMO Run 009 — Diagnostic Findings & Improvement Plan

**Dataset:** 45 data instances (N∈{30,50,100,150,200} × M∈{3,6,9} × Seed∈{10,20,30}),
each run once under the per-N budget (N≤50: 120s/50k iters; N≥100: 300s/20k iters),
plus a 5-instance ×3 reproducibility probe. Generated on the lab PC.
Analysis pipeline: `analyze_run009.py` (aggregate) + `deep_scan.py` (every stream, every
instance). Master tables: `run009_metrics.csv`, `deep_metrics.csv`.

> **Note on revision:** an initial pass blamed Stage 2 (which dominates *accepts*). The full
> per-stream scan corrected this: the true compute cost is *proposals/decodes*, and there
> **Stage 5 dominates (95%+)**. This document reflects the corrected, all-streams analysis.

---

## 0. Validity gate — PASSED

| Check | Result |
|---|---|
| **Reproducibility** | ✅ all 5 probe instances **DETERMINISTIC** (identical Z + trajectory hash ×3) |
| **Objective integrity** | ✅ **0 violations** across 60 files — every accepted move's recompute matched |
| Sanitizer | ⚠️ not in dataset (lab-PC mingw64 can't link ASan; needs UCRT64/CLANG64 or Linux) |

Run 7's `unordered_map` did **not** break determinism; the evaluation engine is bug-free.

---

## 1. Executive summary

> **The mid/large-N problem has two coupled root causes, both computational waste:**
>
> 1. **Stage 5 (CMA) consumes ~95% of all candidate evaluations** — ~308 million decodes/run at
>    N200 — while accepting **0.001%** of them. It scans enormous neighborhoods that almost never
>    improve.
> 2. **The search plateaus after ~10% of iterations** — 99.8% of the total objective gain is
>    captured early; the remaining ~90% of the budget yields <0.2%. STMO spends most of its run
>    grinding (mostly inside Stage 5) for almost nothing.
>
> These connect through a third finding: **75% of orders are rejected at N200**, producing a huge
> M0 pool that Stage 5's reinsertion operator rescans every iteration → the decode explosion.

---

## 2. Compute bottleneck — Stage 5  *(D1, D4)*

**Proposals (decodes) per stage — share of all evaluations:**

| N | S1 | S2 | S4 | **S5** | decodes/iter |
|---|---|---|---|---|---|
| 30 | 5.8% | 18.0% | 1.2% | **74.9%** | 1,658 |
| 100 | 0.8% | 5.5% | 0.1% | **93.6%** | 12,251 |
| 200 | 0.6% | 3.8% | 0.0% | **95.6%** | 29,594 |

At N200, Stage 5 issues **~308 million decodes/run** (≈24,700 per iteration). **Acceptance rate by
stage** (accepts ÷ proposals): S1 ≈ 20–33%, S2 ≈ 18–23%, S4 ≈ 0.2–1.4%, **S5 ≈ 0.001%**. Stage 5
evaluates ~100,000 candidates to accept one. This is the single largest lever in the codebase.

*Why:* Stage 5 Operation 2 reinserts every rejected order into every machine (≈150 × 9 candidates
per turtle at N200), and Operation 1 scans elites × machines × positions — almost all rejected.

> Correction to the first pass: **Stage 2 dominates *accepts* (2M+/run) but is only 3.8% of
> *decodes***. Its accepts are 99% sideways (equal-objective) moves — real waste, but a minor
> compute cost next to S5. (kept as a secondary target, §7 P3.)

---

## 3. Search plateaus early, then wastes the budget  *(D2)*

**Fraction of total objective gain achieved within the first 10% of iterations:**

| N | gain by 10% of iters | best-event at (frac of run) |
|---|---|---|
| 30 | 98.7% | 0.49 |
| 100 | 99.4% | 0.81 |
| 200 | 99.8% | 0.90 |

99%+ of the quality is reached in the first ~10% of iterations. The late "best events" (at 90% of
the run for N200) are **marginal** — together <0.2% of the gain. So STMO is **plateaued, not
starved**: extra iterations would add almost nothing; the issue is that ~90% of the budget produces
negligible improvement (and most of that time is the Stage 5 decode explosion).

**Objective-space convergence** confirms it: population objective spread (`obj_std`) collapses from
~19,000 → ~97 at N200, i.e. all 30 turtles reach near-identical objective early — even though they
stay **structurally** distinct (`distinct_full` = 30/30 throughout, Hamming ≈ N). Many different
orderings give the same objective: a **flat plateau landscape** the current operators can't escape.

---

## 4. OAS-specific: high and growing rejection  *(D3)*

| N | 30 | 50 | 100 | 150 | 200 |
|---|---|---|---|---|---|
| **orders rejected** | 20% | 36% | 62% | 70% | **75%** |
| incumbent tardiness | 67 | 167 | 352 | 459 | 552 |

At large N the incumbent **rejects three of every four orders**. This may be over-conservative
(rejecting forgoes revenue to avoid tardiness) and is worth testing as a quality weakness — and it
mechanically **drives the Stage 5 explosion** (more rejects → bigger M0 pool → more reinsertion
decodes). Speed and quality share this root.

---

## 5. Small-N restart thrashing  *(new)*

Convergence-restarts per run: **N30 fires 11–22×**, N50 4–10×, N100 1–6×, N150/200 1–5×. Small N
plateaus, `ccrit`→0, restart fires, re-converges — repeatedly — yet the best is still found at only
~50% of the run, so the restarts rarely produce a better solution. The restart mechanism is busy
but largely ineffective at escaping the plateau.

---

## 6. Memory & diversity (secondary)

- **PairMemory churns hugely:** ~27,000 entries created and ~26,000 deleted per run at N200; peak
  ~15k → final ~600; critical-pair count grows 5 → 219; most-negative score −212 → −1,885.
- **No diversity collapse:** `distinct_full` never drops below 30/30. Elite-archive duplication is
  minor and only at small/mid N (≈4.6/5 distinct at N30; full distinctness at N200).

---

## 7. Hypothesis scorecard

| Hypothesis | Verdict |
|---|---|
| A stage does huge low-value work | ✅ **CONFIRMED** — but it's **Stage 5** (95% of decodes, 0.001% accept), not S2 |
| Stage 2 does mostly-useless work | ✅ partly — 99% sideways accepts, but only 3.8% of decodes |
| No delta-eval → cost dominates | ✅ confirmed — every decode is full O(N) |
| Stage 4 wasted | ✅ confirmed — <0.1% of decodes, rarely accepts |
| Early stagnation at large N | ✅ **CONFIRMED (revised)** — plateaus by ~10% of iters (first pass wrongly read it as "starved") |
| Elite/diversity collapse | ❌ refuted — population stays 30/30 distinct |
| (new) Over-rejection at large N | ⚠️ **flagged** — 75% reject at N200, needs a quality test |

---

## 8. Improvement plan — Run 010 (re-prioritized by *decode* cost)

| Pri | Fix | Evidence | Predicted payoff |
|---|---|---|---|
| **P1** | **Throttle Stage 5's neighborhood scan** — cap candidates to top-K by PairMemory score; skip Operation 1 when no STRONG pair exists; bound M0-reinsertion attempts. | §2: S5 = 95% of decodes at 0.001% accept | **largest single compute cut (~5–10×)** with negligible quality loss |
| **P2** | **Incremental (delta) evaluation** — recompute only the edited machine's tail (`partialReeval`/`machineObj[]` already exist, unused); apply→eval→undo to drop 24 KB copies. | §2: all decodes are full O(N) | multiplies P1; cheaper decodes everywhere |
| **P3** | **Tighten Stage 2 acceptance** to strict ΔZ>0 or cap sideways moves. | §2 note: 99% sideways accepts | removes residual waste |
| **P4** | **Attack the plateau** — redirect compute freed by P1/P2 into stronger diversification/perturbation (current restart is ineffective, §5). | §3: 90% of budget yields <0.2% | converts saved time into actual quality |
| **P5** | **Test the rejection balance** — is 75% reject optimal or leaving revenue? | §4 | potential Best-Z gain at large N |
| **P6** | **Fix Stage 4's guidance criterion** (score the pair the swap creates). | §2 | recovers S4 as an improver |

**Method:** each fix isolated and measured against this Run 009 baseline (Best Z + decodes/iter +
iters/sec on the same 45 instances); cross-check objective with the existing integrity machinery.

---

## 9. Figures (`analysis/figures/`)

Corrected story: **D1** proposal share (S5 dominance) · **D2** early plateau · **D3** rejection rate
· **D4** per-stage accept rates. Supporting: F1 speed scaling · F2 cost/iter · F5 convergence timing
· F6 PairMemory bloat · F7 convergence curves. (F3/F4 reflect the superseded accept-based view.)
