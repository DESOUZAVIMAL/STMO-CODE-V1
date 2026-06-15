#!/usr/bin/env python3
"""
deep_scan.py — exhaustive per-instance scan of EVERY Run 009 data stream.

For all 45 data instances it parses:
  code_health_map.txt  -> per-stage prop/acc/strict/gbest  (TRUE decode cost)
  trajectory.csv       -> bestZ curve, pm_size, ccrit
  stage_window.csv     -> per-stage accept-rate / mean-delta time series
  diversity.csv        -> hamming + objective-space spread time series
  pairmemory.csv       -> label dynamics (strong/weak/critical), churn
  m0_pool.csv          -> rejection rate, incumbent tardiness/revenue
  best_events.csv      -> gain distribution (early vs late)
  basin_signature.csv  -> incumbent/elite hash churn, distinct-in-pop
  population_dump.csv   -> final machine load balance, reject count, elite distinctness
  final_summary/runtime -> scalars
  stdout.txt           -> PM_WARN / restart / error lines

Writes deep_metrics.csv (one row per instance) and prints aggregate findings
plus a per-instance anomaly scan.
"""
import os, re, glob, sys
import numpy as np, pandas as pd

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "results", "run_009_diag"))
OUT  = os.path.dirname(__file__)
INST_RE = re.compile(r"^N(\d+)_M(\d+)_S(\d+)$")

def rd(path):
    try: return pd.read_csv(path)
    except Exception: return None

def parse_health(path):
    """code_health_map.txt -> {stage: {prop,acc,strict,gbest}}"""
    d = {}
    if not os.path.exists(path): return d
    for line in open(path):
        m = re.match(r"\s*(S\d)\s+\w+.*?:\s*(.*)", line)
        if not m: continue
        st, rest = m.group(1), m.group(2)
        rec = {}
        for k in ["prop","acc","strict","gbest"]:
            mm = re.search(rf"{k}=(\d+)", rest)
            if mm: rec[k] = int(mm.group(1))
        if rec: d[st] = rec
    return d

def parse_kv(path):
    d = {}
    if not os.path.exists(path): return d
    for line in open(path):
        for k, v in re.findall(r"([A-Za-z0-9_]+)\s*=\s*([^\s=]+)", line):
            d[k] = v
    return d

def fget(d,k,default=np.nan):
    try: return float(d.get(k,default))
    except: return default

rows=[]
anomalies=[]
for folder in sorted(glob.glob(os.path.join(ROOT,"N*"))):
    if folder.endswith("_repro"): continue
    name=os.path.basename(folder); m=INST_RE.match(name)
    if not m: continue
    N,M,S=int(m.group(1)),int(m.group(2)),int(m.group(3))
    rep=os.path.join(folder,"repeat1")
    r=dict(instance=name,N=N,M=M,Seed=S)

    # ---- code_health: proposals (decodes) per stage ----
    h=parse_health(os.path.join(rep,"code_health_map.txt"))
    tot_prop=0
    for st in ["S1","S2","S4","S5"]:
        p=h.get(st,{}).get("prop",np.nan); a=h.get(st,{}).get("acc",np.nan)
        g=h.get(st,{}).get("gbest",np.nan)
        r[f"{st}_prop"]=p; r[f"{st}_acc"]=a; r[f"{st}_gbest"]=g
        if not np.isnan(p): tot_prop+=p
    r["S2_strict"]=h.get("S2",{}).get("strict",np.nan)
    r["total_prop"]=tot_prop
    for st in ["S1","S2","S4","S5"]:
        r[f"{st}_prop_share"]=r[f"{st}_prop"]/tot_prop if tot_prop>0 else np.nan
    # acceptance efficiency = accepts / proposals
    for st in ["S1","S2","S4","S5"]:
        pr,ac=r[f"{st}_prop"],r[f"{st}_acc"]
        r[f"{st}_acc_rate"]= ac/pr if (pr and pr>0) else np.nan

    # ---- scalars ----
    summ=parse_kv(os.path.join(rep,"final_summary.txt"))
    rt=parse_kv(os.path.join(rep,"runtime.txt"))
    r["final_bestZ"]=fget(summ,"final_bestZ"); r["best_at_iter"]=fget(summ,"best_at_iter")
    r["final_iter"]=fget(summ,"final_iter"); r["final_time_s"]=fget(rt,"final_time_s")
    r["iters_per_sec"]=fget(rt,"iters_per_sec"); r["pm_peak"]=fget(summ,"pm_peak")
    r["pm_final"]=fget(summ,"pm_final"); r["ccrit_peak"]=fget(summ,"ccrit_peak")
    r["restarts"]=fget(summ,"restarts")
    r["total_prop_per_iter"]= tot_prop/r["final_iter"] if r["final_iter"]>0 else np.nan

    # ---- trajectory: early vs late gain split ----
    tj=rd(os.path.join(rep,"trajectory.csv"))
    if tj is not None and len(tj)>2:
        z=tj["bestZ"].values; it=tj["iter"].values
        z0,zf=z[0],z[-1]; total_gain=zf-z0
        # gain achieved by 10% of final iter
        i10=it[-1]*0.10
        z_at_10=z[np.searchsorted(it,i10,side="right")-1]
        r["gain_frac_by_10pct_iters"]=(z_at_10-z0)/total_gain if total_gain!=0 else np.nan
        r["bestZ_at_iter10pct"]=z_at_10
    # ---- m0_pool: rejection + incumbent tardiness ----
    m0=rd(os.path.join(rep,"m0_pool.csv"))
    if m0 is not None and len(m0)>0:
        last=m0.iloc[-1]
        r["incumbent_accepted_jobs"]=float(last.get("incumbent_accepted_jobs",np.nan))
        r["reject_count"]=N-r["incumbent_accepted_jobs"]
        r["reject_frac"]=r["reject_count"]/N
        r["incumbent_tardiness"]=float(last.get("incumbent_total_tardiness",np.nan))
        r["incumbent_revenue"]=float(last.get("incumbent_accepted_revenue",np.nan))
        r["m0_size_final"]=float(last.get("m0_size",np.nan))
    # ---- pairmemory: label dynamics ----
    pm=rd(os.path.join(rep,"pairmemory.csv"))
    if pm is not None and len(pm)>0:
        r["pm_nstrong_mean"]=pm["n_strong"].mean() if "n_strong" in pm else np.nan
        r["pm_nweak_mean"]=pm["n_weak"].mean() if "n_weak" in pm else np.nan
        r["pm_ncrit_mean"]=pm["n_critical"].mean() if "n_critical" in pm else np.nan
        r["pm_score_min"]=pm["score_min"].min() if "score_min" in pm else np.nan
        if "created_in_window" in pm and "deleted_in_window" in pm:
            r["pm_created_total"]=pm["created_in_window"].sum()
            r["pm_deleted_total"]=pm["deleted_in_window"].sum()
    # ---- diversity: objective-space + hamming ----
    dv=rd(os.path.join(rep,"diversity.csv"))
    if dv is not None and len(dv)>0:
        r["distinct_full_min"]=dv["distinct_full"].min() if "distinct_full" in dv else np.nan
        r["ham_order_mean_first"]=dv["ham_order_mean"].iloc[0] if "ham_order_mean" in dv else np.nan
        r["ham_order_mean_last"]=dv["ham_order_mean"].iloc[-1] if "ham_order_mean" in dv else np.nan
        r["obj_std_first"]=dv["obj_std"].iloc[0] if "obj_std" in dv else np.nan
        r["obj_std_last"]=dv["obj_std"].iloc[-1] if "obj_std" in dv else np.nan
    # ---- best_events: count + early concentration ----
    be=rd(os.path.join(rep,"best_events.csv"))
    if be is not None and len(be)>0 and "delta" in be:
        tg=be["delta"].sum()
        n_early=max(1,int(len(be)*0.10))
        r["best_events_n"]=len(be)
        r["gain_frac_first10pct_events"]=be["delta"].iloc[:n_early].sum()/tg if tg!=0 else np.nan
        r["njobs_changed_mean"]=be["n_jobs_changed"].mean() if "n_jobs_changed" in be else np.nan
    # ---- population_dump: final machine balance + elite distinctness ----
    pdp=rd(os.path.join(rep,"population_dump.csv"))
    if pdp is not None and len(pdp)>0 and "iter" in pdp:
        last_it=pdp["iter"].max()
        el=pdp[(pdp["iter"]==last_it)&(pdp["source"]=="elite")]
        if len(el)>0:
            r["elite_count"]=len(el)
            r["elite_distinct_order"]=el["order_seq"].nunique()
            # full distinctness = order+machine
            if "machine_assign" in el:
                full=(el["order_seq"].astype(str)+"|"+el["machine_assign"].astype(str))
                r["elite_distinct_full"]=full.nunique()
    # ---- stdout: warnings/restarts ----
    so=os.path.join(rep,"stdout.txt")
    if os.path.exists(so):
        txt=open(so,errors="ignore").read()
        r["pm_warn_hit"]=("PM_WARN" in txt)
        r["restart_lines"]=txt.count("[Restart]")
    rows.append(r)

df=pd.DataFrame(rows).sort_values(["N","M","Seed"]).reset_index(drop=True)
df.to_csv(os.path.join(OUT,"deep_metrics.csv"),index=False)

pd.set_option("display.width",220,"display.max_columns",80,"display.float_format",lambda x:f"{x:,.3f}")
print("\n================ DEEP SCAN — ALL 45 INSTANCES ================")
print("loaded:",len(df))

print("\n=== PROPOSALS (decodes) per stage — mean by N — THE TRUE COMPUTE COST ===")
print(df.groupby("N")[["S1_prop","S2_prop","S4_prop","S5_prop","total_prop_per_iter"]].mean().round(0))

print("\n=== PROPOSAL SHARE per stage (% of all decodes) by N ===")
print((df.groupby("N")[["S1_prop_share","S2_prop_share","S4_prop_share","S5_prop_share"]].mean()*100).round(1))

print("\n=== ACCEPTANCE RATE per stage (accepts/proposals) by N ===")
print((df.groupby("N")[["S1_acc_rate","S2_acc_rate","S4_acc_rate","S5_acc_rate"]].mean()*100).round(4))

print("\n=== CONVERGENCE: gain captured early ===")
print(df.groupby("N")[["gain_frac_by_10pct_iters","best_events_n","gain_frac_first10pct_events"]].mean().round(3))

print("\n=== OAS QUALITY: rejection fraction + incumbent tardiness by N ===")
print(df.groupby("N")[["reject_frac","reject_count","incumbent_tardiness","incumbent_revenue"]].mean().round(2))

print("\n=== PAIRMEMORY label means + churn by N ===")
print(df.groupby("N")[["pm_nstrong_mean","pm_nweak_mean","pm_ncrit_mean","pm_score_min","pm_created_total","pm_deleted_total"]].mean().round(1))

print("\n=== DIVERSITY: distinct_full_min + objective spread (std first->last) by N ===")
print(df.groupby("N")[["distinct_full_min","obj_std_first","obj_std_last","ham_order_mean_last"]].mean().round(2))

print("\n=== ELITE distinctness (full = order+machine) by N ===")
print(df.groupby("N")[["elite_count","elite_distinct_order","elite_distinct_full"]].mean().round(2))

# ---- per-instance anomaly scan ----
print("\n================ PER-INSTANCE ANOMALY SCAN ================")
def flag(cond,msg):
    for _,row in df[cond].iterrows():
        anomalies.append((row["instance"],msg))
# anomalies relative to per-N norms
for N,g in df.groupby("N"):
    for col,lbl in [("iters_per_sec","slow"),("reject_frac","high-reject"),
                    ("final_bestZ","low-Z"),("S5_prop","S5-heavy")]:
        med=g[col].median(); mad=(g[col]-med).abs().median()+1e-9
        for _,row in g.iterrows():
            z=(row[col]-med)/mad
            if abs(z)>3.5:
                anomalies.append((row["instance"],f"{lbl}: {col}={row[col]:.2f} vs N-median {med:.2f} (z={z:.1f})"))
if df["restarts"].fillna(0).gt(0).any():
    for _,row in df[df["restarts"].fillna(0)>0].iterrows():
        anomalies.append((row["instance"],f"restarts={row['restarts']:.0f}"))
if len(anomalies)==0:
    print("No per-instance outliers beyond robust z=3.5 within each N group.")
else:
    for inst,msg in anomalies: print(f"  [{inst}] {msg}")

# ---- corrected-story figures ----
import matplotlib
matplotlib.use("Agg"); import matplotlib.pyplot as plt
FIG=os.path.join(OUT,"figures"); os.makedirs(FIG,exist_ok=True)
def sf(fn): plt.tight_layout(); plt.savefig(os.path.join(FIG,fn),dpi=130); plt.close()

# D1: proposal (decode) share per stage — the corrected bottleneck
g=df.groupby("N")[["S1_prop_share","S2_prop_share","S4_prop_share","S5_prop_share"]].mean()*100
plt.figure(figsize=(7,4.2)); bottom=np.zeros(len(g))
for col,lab in [("S1_prop_share","S1"),("S2_prop_share","S2"),("S4_prop_share","S4"),("S5_prop_share","S5")]:
    plt.bar(g.index.astype(str),g[col].values,bottom=bottom,label=lab); bottom+=g[col].values
plt.ylabel("% of all decodes (proposals)"); plt.xlabel("N")
plt.title("TRUE compute cost: Stage 5 dominates decodes (95%+ at N200)"); plt.legend()
sf("D1_proposal_share.png")

# D2: gain captured by 10% of iterations (the plateau)
plt.figure(figsize=(7,4.2))
s=df.groupby("N")["gain_frac_by_10pct_iters"].mean()*100
plt.bar(s.index.astype(str),s.values,color="darkorange")
plt.ylabel("% of total gain achieved by 10% of iterations"); plt.xlabel("N")
plt.ylim(90,100); plt.title("STMO plateaus early: ~99% of gain in first 10% of iters")
sf("D2_early_plateau.png")

# D3: rejection fraction by N
plt.figure(figsize=(7,4.2))
s=df.groupby("N")["reject_frac"].mean()*100
plt.plot(s.index,s.values,"o-",color="firebrick")
plt.ylabel("% of orders rejected (incumbent)"); plt.xlabel("N")
plt.title("Rejection rate grows to 75% at N200 (feeds S5 M0-reinsertion)"); plt.grid(alpha=.3)
sf("D3_rejection_rate.png")

# D4: S5 acceptance rate (log) — wasted scanning
plt.figure(figsize=(7,4.2))
sr=df.groupby("N")[["S1_acc_rate","S2_acc_rate","S4_acc_rate","S5_acc_rate"]].mean()*100
for col,lab in [("S1_acc_rate","S1"),("S2_acc_rate","S2"),("S4_acc_rate","S4"),("S5_acc_rate","S5")]:
    plt.plot(sr.index,sr[col].values,"o-",label=lab)
plt.yscale("log"); plt.ylabel("accept rate = accepts/proposals (%)"); plt.xlabel("N")
plt.title("Stage 5 accepts ~0.001% of what it evaluates"); plt.legend(); plt.grid(alpha=.3)
sf("D4_accept_rates.png")

print("\nWrote deep_metrics.csv and corrected-story figures D1-D4")
