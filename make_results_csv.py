#!/usr/bin/env python3
# make_results_csv.py — build results.csv (old run_00X format) from
# run_010_final/stdout_N*_M*_S*.txt files. Run AFTER the batch (or any time;
# only fully-completed instances are included).  Usage:
#   python3 make_results_csv.py [results/run_010_final]
import os, re, sys
RESULTS_DIR = sys.argv[1] if len(sys.argv) > 1 else "results/run_010_final"
OUT = os.path.join(RESULTS_DIR, "results.csv")
HEADER = "N,M,Seed,BestZ,Iterations,TimeUsed,BestAtIter,S2Repairs,S4Phase1Hits,ConvergenceRestarts"

def g(pat, t, d=""):
    m = re.search(pat, t); return m.group(1) if m else d

rows, incomplete, missing = [], [], []
for N in (30,50,100,150,200):
  for M in (3,6,9):
    for S in (10,20,30):
      f = os.path.join(RESULTS_DIR, f"stdout_N{N}_M{M}_S{S}.txt")
      if not os.path.exists(f): missing.append(f"N{N}_M{M}_S{S}"); continue
      t = open(f, encoding="utf-8", errors="replace").read()
      iters = g(r'Total iterations\s*=\s*(\d+)', t)          # only in the FINAL RESULT block
      if not iters:                                          # run didn't finish cleanly
          incomplete.append(f"N{N}_M{M}_S{S}"); continue
      bz = re.findall(r'Best Z\s*=\s*([0-9.]+)', t)          # last = FINAL RESULT value
      rows.append(",".join([str(N),str(M),str(S), (bz[-1] if bz else ""), iters,
          g(r'Time\s*=\s*([0-9.]+)\s*seconds', t),
          g(r'Best improved at iter\s*:\s*(\d+)', t),
          g(r'Stage 2 total repairs\s*:\s*(\d+)', t),
          g(r'Stage 4 Phase1 hits\s*:\s*(\d+)', t),
          g(r'Convergence restarts\s*:\s*(\d+)', t)]))

with open(OUT,"w") as o:
    o.write(HEADER+"\n"); o.write("\n".join(rows)+("\n" if rows else ""))
print(f"Wrote {OUT} — {len(rows)} complete instances")
if incomplete: print(f"  incomplete (no FINAL RESULT, skipped): {len(incomplete)} -> {', '.join(incomplete)}")
if missing:    print(f"  not yet run: {len(missing)}")
