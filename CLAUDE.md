# STMO — Sea Turtle Migration Optimization
## CLAUDE.md — Context File for Claude Code

This file gives Claude Code full context about this project.
Read this before making ANY changes to any file.

---

## Project Overview

**Algorithm:** STMO (Sea Turtle Migration Optimization)
**Problem:** Order Acceptance and Scheduling on Identical Parallel Machines
            with Sequence-Dependent Setup Times (OAS-SDST)
**Reference:** Wu et al. (2018) — Applied Soft Computing
**Author:** Vimal De Souza | Student 1135446 | Yuan Ze University
**Advisor:** Dr. Gen-Han Wu
**Language:** Pure C++ (C++11), single compilation unit

---

## How to Build

```bash
g++ -O2 -std=c++11 -o STMO STMO.cpp -lm
```

## How to Run

```bash
./STMO
```
`param.txt` must be in the same directory as the executable.

---

## File Structure

```
STMO/
├── config.h       ← ALL parameters here — change settings ONLY here
├── stmo_types.h   ← ALL data structures (Turtle, PairMemory, etc.)
├── problem.h      ← Problem infrastructure (GenData, CalObj, operators)
├── memory_ops.h   ← Memory operations (PairMemory update, labelling, etc.)
├── stages.h       ← All 5 algorithm stages + batchEvaluate
├── STMO.cpp       ← main() — iteration loop + diagnostic output
└── CLAUDE.md      ← This file
```

Include order (important): config.h → stmo_types.h → problem.h → memory_ops.h → stages.h → STMO.cpp

---

## Algorithm Design — 5 Stages

```
STARTUP:
  genData() → initializeTurtleRandom() × P → Batch Eval 1

EACH ITERATION:
  Stage 1: OceanCurrentDrift    → random exploration, nc=3 candidates
  Stage 2: MemoryAwareDrift     → repair weak pairs, machine budget k=3
  Batch Eval 2                  → decode all, extract pairs for ACMM
  Stage 3: ACMM                 → update PairMemory, label, ODTP, StructuralMap
  Stage 4: MFBO                 → Phase 1 Weak Pool Guided Swap (Phase 2 deferred)
  Stage 5: CMA                  → elite subpath transfer + M0 reinsertion
  Batch Eval 3                  → update EliteArchive, M0Pool, check stagnation

TERMINATION: time >= 120s OR iter >= 500 OR stagnation K=50
```

---

## Critical Design Decisions — DO NOT CHANGE WITHOUT PERMISSION

| Decision | Value | Reason |
|---|---|---|
| PairMemory key | `pair<int,int>` (ji, jj) — NO machine_id | Machine-agnostic by design |
| Score formula | `(rj - wj*(Cj-dj)) / (1+sij)` | Signed deviation — no max() |
| Stage 1 acceptance | ΔZ >= 0 (non-worsening) | Best of NC candidates |
| Stage 2 acceptance | ΔZ >= 0 (non-worsening) | First improvement repair |
| Stage 4 acceptance | ΔZ > 0 (strict) | Intensification only |
| Stage 5 acceptance | ΔZ > 0 (strict) | Elite transfer |
| EDD initialization | REMOVED | Per professor instruction |
| Stage 4 Phase 2 VNS | DEFERRED | Per professor instruction |
| Two-pass decay | Decay non-observed FIRST, then update | Fresh obs get full weight |
| cacheValid flag | Set TRUE only by decodeAndEval() | Never skip evaluation |
| Move functions | Return COPIES — never mutate original | SE principle |

---

## Three-Layer State Hierarchy

**Layer 1 — Authoritative (Turtle struct)**
- Written ONLY by `decodeAndEval()`
- `cacheValid` must be true before reading `obj` or `ctimes`

**Layer 2 — Persistent Memory**
- PairMemory: written ONLY by ACMM (Stage 3)
- TripletMemory: written ONLY by ODTP inside ACMM
- EliteArchive: written ONLY by batchEvaluate (after Stage 5)
- M0Pool: written ONLY by batchEvaluate

**Layer 3 — Ephemeral**
- StructuralMap: built by ACMM, consumed by Stage 4, then DISCARDED
- EvalSnapshot: built by batchEvaluate, consumed by ACMM, then DISCARDED

---

## What Came from Professor's Code

These functions are adapted from the professor's WFA source code.
They are PROBLEM infrastructure — not WFA algorithm logic.

| Function in STMO | Original in WFA | What changed |
|---|---|---|
| `genData()` | `GenData()` | Global → parameters; error handling added |
| `decodeAndEval()` | `CalObj()` | Global Curr[] → Turtle parameter |
| `partialReeval()` | `CalObj_single()` | Returns float instead of modifying global |
| `sortByKeys()` | `bubblesort()` | Renamed; takes explicit arrays |
| `moveOrderInsert()` | `Neighborhood1()` | Returns COPY; cacheValid=false |
| `moveMachineRevise()` | `Neighborhood2()` | Returns COPY; cacheValid=false |
| `moveOrderSwap()` | `Neighborhood3()` | Returns COPY; cacheValid=false |
| `moveMachineSwap()` | `Neighborhood4()` | Returns COPY; cacheValid=false |

Everything else (PairMemory, TripletMemory, all stages) is original STMO.

---

## Parameters — Where to Find and Change

ALL parameters are in `config.h`. Never hardcode values in other files.

| Parameter | Default | Where used |
|---|---|---|
| P | 30 | Population size |
| NC | 3 | Stage 1 candidates |
| MAX_ITER | 500 | Backup stop condition |
| END_TIME | 120.0 | Primary stop condition (seconds) |
| DF | 0.95 | PairMemory decay factor |
| MIN_COUNT | 3 | Min observations before labelling |
| MACHINE_BUDGET | 3 | Stage 2 repairs per machine |
| MAX_ATTEMPTS | 5 | Stage 2 repair attempts per pair |
| K_STAG | 50 | Stagnation threshold |
| E_SIZE | 5 | Elite archive size |
| KN | 3 | Stage 4 VNS candidates (future use) |

---

## What Is Deferred (Not Yet Implemented)

| Feature | Status | File | Comment |
|---|---|---|---|
| Stage 4 Phase 2 VNS | DEFERRED | stages.h | Placeholder comment at end of stage4_MFBO() |
| EDD initialization | REMOVED | — | Per professor instruction, not to be added back |

To add Stage 4 Phase 2 VNS: find the comment
`// Phase 2: Full VNS on target region` in stages.h and implement there.
Use all 4 move functions, kn=KN candidates each, accept best if ΔZ > 0.
STRONG pairs from structMaps[p].strongPairs must be checked and protected.

---

## Diagnostic Output Guide

After each run, STMO prints a diagnostic report. Use it to tune config.h:

| Report says | What it means | What to change |
|---|---|---|
| Best improved at last iteration | Not enough time | Increase END_TIME |
| Converged early (< 20% of iters) | Stuck in local optima | Decrease DF |
| Stage 2 never triggered | PairMemory not building | Decrease MIN_COUNT |
| Stage 4 Phase 1 never succeeded | Weak pool empty | Check PairMemory size |
| PairMemory very small (< 5) | Memory not building | Decrease MIN_COUNT to 1 |

---

## Branch Strategy

- `main`: stable, compiling, tested versions only
- `dev`: current work in progress
- `feature/stage4-vns`: use this when adding Stage 4 Phase 2 VNS

Always work on `dev`. Merge to `main` only after:
1. Code compiles without warnings
2. Runs to completion on at least one param.txt case
3. Diagnostic report shows reasonable behaviour

---

## Contact

For design questions: refer to the claude.ai project "Thesis Brainstorm"
All design decisions are documented there and in the thesis draft:
STMO_Thesis_Revised_v2.docx
