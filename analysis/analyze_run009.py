#!/usr/bin/env python3
"""
analyze_run009.py — STMO Run 009 diagnostic analysis pipeline.

Ingests results/run_009_diag/ and produces:
  - run009_metrics.csv : one aggregate row per data instance (the master table)
  - figures/*.png      : thesis-ready figures
  - prints a findings summary to stdout

Hypothesis-driven (see RUN009 analysis plan). Separates SPEED vs QUALITY.
"""
import os, re, sys, glob
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = os.path.join(os.path.dirname(__file__), "..", "results", "run_009_diag")
ROOT = os.path.abspath(ROOT)
OUT  = os.path.dirname(__file__)
FIG  = os.path.join(OUT, "figures")
os.makedirs(FIG, exist_ok=True)

INST_RE = re.compile(r"^N(\d+)_M(\d+)_S(\d+)$")   # data instances only (no _repro)

def parse_kv(path):
    """Parse key=value tokens into a dict. Handles BOTH 'key = value' (one per
    line, final_summary.txt) AND 'k1=v1 k2=v2 k3=v3' (multiple per line,
    runtime.txt). Values are single whitespace-free tokens."""
    d = {}
    if not os.path.exists(path): return d
    for line in open(path):
        for k, v in re.findall(r"([A-Za-z0-9_]+)\s*=\s*([^\s=]+)", line):
            d[k] = v
    return d

def fnum(d, k, default=np.nan):
    try: return float(d.get(k, default))
    except Exception:
        # handle "N=200 M=9 ..." style compound lines already split out
        return default

def last_row(path):
    if not os.path.exists(path): return None
    try:
        df = pd.read_csv(path)
        if len(df) == 0: return None
        return df.iloc[-1]
    except Exception:
        return None

def load_instance(folder):
    name = os.path.basename(folder)
    m = INST_RE.match(name)
    if not m: return None
    N, M, S = int(m.group(1)), int(m.group(2)), int(m.group(3))
    rep = os.path.join(folder, "repeat1")
    summ = parse_kv(os.path.join(rep, "final_summary.txt"))
    rt   = parse_kv(os.path.join(rep, "runtime.txt"))
    traj = last_row(os.path.join(rep, "trajectory.csv"))

    row = dict(instance=name, N=N, M=M, Seed=S)

    # runtime.txt
    row["iters_per_sec"] = fnum(rt, "iters_per_sec")
    row["end_time"]      = fnum(rt, "end_time")
    row["max_iter_cap"]  = fnum(rt, "max_iterations")
    row["final_time_s"]  = fnum(rt, "final_time_s")
    row["final_cpu_s"]   = fnum(rt, "final_cpu_s")

    # final_summary.txt
    row["final_bestZ"]   = fnum(summ, "final_bestZ")
    row["best_at_iter"]  = fnum(summ, "best_at_iter")
    row["final_iter"]    = fnum(summ, "final_iter")
    row["pm_peak"]       = fnum(summ, "pm_peak")
    row["pm_final"]      = fnum(summ, "pm_final")
    row["ccrit_peak"]    = fnum(summ, "ccrit_peak")
    row["ccrit_zero_iter"]= fnum(summ, "ccrit_zero_iter")
    row["distinct_first"]= fnum(summ, "distinct_full_first")
    row["distinct_last"] = fnum(summ, "distinct_full_last")
    row["total_best_events"]= fnum(summ, "total_best_events")
    row["restarts"]      = fnum(summ, "restarts")

    # trajectory last row: per-stage cumulative accepts and global-best hits
    if traj is not None:
        for c in ["s1acc_cum","s2acc_cum","s2strict_cum","s4acc_cum","s5acc_cum",
                  "s1g_cum","s2g_cum","s4g_cum","s5g_cum","pm_size"]:
            row[c] = float(traj.get(c, np.nan))
        if np.isnan(row.get("final_iter", np.nan)):
            row["final_iter"] = float(traj.get("iter", np.nan))
        if np.isnan(row.get("final_bestZ", np.nan)):
            row["final_bestZ"] = float(traj.get("bestZ", np.nan))
    return row

# ---- load all 45 data instances ------------------------------------------
rows = []
for folder in sorted(glob.glob(os.path.join(ROOT, "N*"))):
    if folder.endswith("_repro"): continue
    r = load_instance(folder)
    if r: rows.append(r)
df = pd.DataFrame(rows).sort_values(["N","M","Seed"]).reset_index(drop=True)
if len(df) == 0:
    print("NO instances loaded from", ROOT); sys.exit(1)

# ---- derived metrics ------------------------------------------------------
df["stop_reason"] = np.where(df["final_time_s"] >= 0.97*df["end_time"], "TIME", "ITER")
df["time_to_best_frac"] = df["best_at_iter"] / df["final_iter"]
df["dead_tail_frac"]    = 1.0 - df["time_to_best_frac"]
df["s2_accepts_per_iter"]= df["s2acc_cum"] / df["final_iter"]
df["s2_sideways_frac"]  = (df["s2acc_cum"] - df["s2strict_cum"]) / df["s2acc_cum"].replace(0,np.nan)
df["s1_accepts_per_iter"]= df["s1acc_cum"] / df["final_iter"]
df["s4_accepts_per_run"] = df["s4acc_cum"]
df["s5_accepts_per_run"] = df["s5acc_cum"]
df["pm_churn"]          = df["pm_peak"] / df["pm_final"].replace(0,np.nan)
df["ms_per_iter"]       = 1000.0 / df["iters_per_sec"]

df.to_csv(os.path.join(OUT, "run009_metrics.csv"), index=False)

# ---- console findings -----------------------------------------------------
pd.set_option("display.width", 200, "display.max_columns", 60)
def by_N(col, agg="mean"):
    return df.groupby("N")[col].agg(agg)

print("\n================ RUN 009 — AGGREGATE FINDINGS ================\n")
print("Instances loaded:", len(df), " | N levels:", sorted(df.N.unique()),
      " | M levels:", sorted(df.M.unique()))

print("\n--- STOP REASON (per N) ---")
print(df.groupby(["N","stop_reason"]).size().unstack(fill_value=0))

print("\n--- SPEED: iters/sec and ms/iter by N ---")
spd = df.groupby("N").agg(iters_per_sec=("iters_per_sec","mean"),
                          ms_per_iter=("ms_per_iter","mean"),
                          final_iter=("final_iter","mean")).round(2)
print(spd)

print("\n--- CONVERGENCE: time-to-best fraction & dead-tail by N ---")
conv = df.groupby("N").agg(time_to_best_frac=("time_to_best_frac","mean"),
                           dead_tail_frac=("dead_tail_frac","mean"),
                           total_best_events=("total_best_events","mean")).round(3)
print(conv)

print("\n--- STAGE 2 WORKLOAD: accepts/iter and sideways(non-improving) fraction by N ---")
s2 = df.groupby("N").agg(s2_accepts_per_iter=("s2_accepts_per_iter","mean"),
                         s2_sideways_frac=("s2_sideways_frac","mean"),
                         s2acc_total=("s2acc_cum","mean")).round(3)
print(s2)

print("\n--- PER-STAGE ACTIVITY (run totals, mean by N): accepts ---")
acc = df.groupby("N").agg(S1=("s1acc_cum","mean"), S2=("s2acc_cum","mean"),
                          S4=("s4acc_cum","mean"), S5=("s5acc_cum","mean")).round(0)
print(acc)
print("\n--- PER-STAGE global-best-contribution counts (mean by N) ---")
gb = df.groupby("N").agg(S1g=("s1g_cum","mean"), S2g=("s2g_cum","mean"),
                         S4g=("s4g_cum","mean"), S5g=("s5g_cum","mean")).round(0)
print(gb)

print("\n--- PAIRMEMORY: peak / final / churn by N ---")
pm = df.groupby("N").agg(pm_peak=("pm_peak","mean"), pm_final=("pm_final","mean"),
                         pm_churn=("pm_churn","mean"), ccrit_peak=("ccrit_peak","mean")).round(1)
print(pm)

print("\n--- DIVERSITY: distinct full-solutions first vs last (pop size 30) ---")
dv = df.groupby("N").agg(distinct_first=("distinct_first","mean"),
                         distinct_last=("distinct_last","mean")).round(1)
print(dv)

# ---- figures --------------------------------------------------------------
def savefig(fn):
    plt.tight_layout(); plt.savefig(os.path.join(FIG, fn), dpi=130); plt.close()

# F1: speed scaling
plt.figure(figsize=(7,4.2))
for M in sorted(df.M.unique()):
    s = df[df.M==M].groupby("N")["iters_per_sec"].mean()
    plt.plot(s.index, s.values, "o-", label=f"M={M}")
plt.xlabel("N (orders)"); plt.ylabel("iterations / second")
plt.title("Throughput collapse with N (Run 009)"); plt.legend(); plt.grid(alpha=.3)
savefig("F1_speed_scaling.png")

# F2: ms per iteration vs N (log)
plt.figure(figsize=(7,4.2))
s = df.groupby("N")["ms_per_iter"].mean()
plt.plot(s.index, s.values, "s-", color="crimson")
plt.xlabel("N"); plt.ylabel("milliseconds / iteration")
plt.title("Per-iteration cost vs N"); plt.grid(alpha=.3)
savefig("F2_cost_per_iter.png")

# F3: Stage 2 sideways fraction + accepts/iter
fig, ax1 = plt.subplots(figsize=(7,4.2))
s = df.groupby("N")
ax1.bar(s["s2_accepts_per_iter"].mean().index.astype(str),
        s["s2_accepts_per_iter"].mean().values, color="steelblue", alpha=.7)
ax1.set_ylabel("Stage 2 accepts / iteration", color="steelblue")
ax1.set_xlabel("N")
ax2 = ax1.twinx()
ax2.plot(range(len(s)), s["s2_sideways_frac"].mean().values*100, "ro-")
ax2.set_ylabel("% of S2 accepts that are sideways (non-improving)", color="red")
ax2.set_ylim(0,100)
plt.title("Stage 2 does massive, mostly non-improving work")
savefig("F3_stage2_workload.png")

# F4: per-stage global-best contribution share (stacked)
plt.figure(figsize=(7,4.2))
g = df.groupby("N")[["s1g_cum","s2g_cum","s4g_cum","s5g_cum"]].mean()
gn = g.div(g.sum(axis=1), axis=0)*100
bottom = np.zeros(len(gn))
for col,lab in [("s1g_cum","S1"),("s2g_cum","S2"),("s4g_cum","S4"),("s5g_cum","S5")]:
    plt.bar(gn.index.astype(str), gn[col].values, bottom=bottom, label=lab)
    bottom += gn[col].values
plt.ylabel("% of global-best-contribution events"); plt.xlabel("N")
plt.title("Which stage drives improvement (by N)"); plt.legend()
savefig("F4_stage_contribution.png")

# F5: convergence timing
plt.figure(figsize=(7,4.2))
s = df.groupby("N")["time_to_best_frac"].mean()
plt.bar(s.index.astype(str), s.values, color="seagreen")
plt.ylabel("best_at_iter / final_iter"); plt.xlabel("N")
plt.title("How late the final best is found (1.0 = at the very end)")
plt.ylim(0,1); savefig("F5_convergence_timing.png")

# F6: PairMemory bloat
plt.figure(figsize=(7,4.2))
s = df.groupby("N")[["pm_peak","pm_final"]].mean()
plt.plot(s.index, s["pm_peak"].values, "o-", label="peak")
plt.plot(s.index, s["pm_final"].values, "s-", label="final")
plt.xlabel("N"); plt.ylabel("PairMemory entries"); plt.yscale("log")
plt.title("PairMemory bloat then collapse"); plt.legend(); plt.grid(alpha=.3)
savefig("F6_pairmemory.png")

# F7: representative convergence curves (one per N, M6 S20)
plt.figure(figsize=(7,4.2))
for N in sorted(df.N.unique()):
    p = os.path.join(ROOT, f"N{N}_M6_S20", "repeat1", "trajectory.csv")
    if os.path.exists(p):
        t = pd.read_csv(p)
        z = t["bestZ"].values
        x = t["iter"].values / t["iter"].values[-1]
        plt.plot(x, z, label=f"N={N}")
plt.xlabel("fraction of run (iter / final_iter)"); plt.ylabel("best Z")
plt.title("Convergence curves (M=6, S=20)"); plt.legend(); plt.grid(alpha=.3)
savefig("F7_convergence_curves.png")

print("\nWrote run009_metrics.csv and", len(glob.glob(os.path.join(FIG,'*.png'))), "figures to", FIG)
print("\nDONE.")
