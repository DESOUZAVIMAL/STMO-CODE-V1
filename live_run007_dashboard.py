from __future__ import annotations

import argparse
import csv
import json
import subprocess
import time
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parent
REPO = ROOT / "STMO-CODE-V1-dev-labpc"
RESULTS = REPO / "results"
REFERENCE = ROOT / "comparison_run002_by_instance.csv"

OUT_HTML = ROOT / "run007_live_comparison_dashboard.html"
OUT_CSV = ROOT / "run007_live_comparison.csv"
OUT_JSON = ROOT / "run007_live_comparison.json"

PRIOR_RUNS = [
    ("Run001", RESULTS / "run_001_2026-05-24_baseline/results.csv"),
    ("Run004", RESULTS / "run_004_2026-06-02_10-31_Run003_PhaseAB/results.csv"),
    ("Run005", RESULTS / "run_005_2026-06-02_temp_Run005_Stage4_Swap_Control/results.csv"),
    ("Run006", RESULTS / "run_006_2026-06-08_19-57_Run006_GhostFix/results.csv"),
]

METHOD_LABELS = {
    "PSOIV": "PSO-IV",
    "Har vns4": "Hybrid VNS-4",
    "WFA.II": "Reference WFA.II",
}


def configure_paths(args: argparse.Namespace) -> None:
    global ROOT, REPO, RESULTS, REFERENCE, OUT_HTML, OUT_CSV, OUT_JSON, PRIOR_RUNS

    REPO = Path(args.repo_root).expanduser().resolve() if args.repo_root else REPO
    RESULTS = REPO / "results"

    if args.output_dir:
        ROOT = Path(args.output_dir).expanduser().resolve()
    else:
        ROOT = Path(__file__).resolve().parent
    ROOT.mkdir(parents=True, exist_ok=True)

    if args.reference_csv:
        REFERENCE = Path(args.reference_csv).expanduser().resolve()
    else:
        candidates = [
            ROOT / "comparison_run002_by_instance.csv",
            REPO / "comparison_run002_by_instance.csv",
            Path(__file__).resolve().parent / "comparison_run002_by_instance.csv",
        ]
        REFERENCE = next((path for path in candidates if path.exists()), candidates[0])

    OUT_HTML = ROOT / "run007_live_comparison_dashboard.html"
    OUT_CSV = ROOT / "run007_live_comparison.csv"
    OUT_JSON = ROOT / "run007_live_comparison.json"

    PRIOR_RUNS = [
        ("Run001", RESULTS / "run_001_2026-05-24_baseline/results.csv"),
        ("Run004", RESULTS / "run_004_2026-06-02_10-31_Run003_PhaseAB/results.csv"),
        ("Run005", RESULTS / "run_005_2026-06-02_temp_Run005_Stage4_Swap_Control/results.csv"),
        ("Run006", RESULTS / "run_006_2026-06-08_19-57_Run006_GhostFix/results.csv"),
    ]


def pct(value: float, base: float) -> float:
    return (value - base) / base * 100.0 if base else 0.0


def avg(values: list[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def read_results(path: Path) -> dict[tuple[int, int, int], dict[str, float]]:
    out: dict[tuple[int, int, int], dict[str, float]] = {}
    if not path.exists():
        return out
    with path.open(newline="", encoding="utf-8", errors="replace") as f:
        reader = csv.DictReader(f)
        if not reader.fieldnames or not {"N", "M", "Seed", "BestZ"}.issubset(reader.fieldnames):
            return out
        for row in reader:
            try:
                key = (int(row["N"]), int(row["M"]), int(row["Seed"]))
                out[key] = {
                    "BestZ": float(row["BestZ"]),
                    "Iterations": float(row.get("Iterations") or 0),
                    "TimeUsed": float(row.get("TimeUsed") or 0),
                    "BestAtIter": float(row.get("BestAtIter") or 0),
                    "S2Repairs": float(row.get("S2Repairs") or 0),
                    "S4Phase1Hits": float(row.get("S4Phase1Hits") or 0),
                    "StagnationResets": float(row.get("StagnationResets") or 0),
                }
            except (TypeError, ValueError):
                continue
    return out


def read_reference() -> dict[tuple[int, int, int], dict[str, object]]:
    out: dict[tuple[int, int, int], dict[str, object]] = {}
    if not REFERENCE.exists():
        raise FileNotFoundError(
            f"Reference benchmark CSV not found: {REFERENCE}. "
            "Copy comparison_run002_by_instance.csv next to this script, into the repo root, "
            "or pass --reference-csv <path>."
        )
    with REFERENCE.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            key = (int(row["N"]), int(row["M"]), int(row["Seed"]))
            out[key] = {
                "WFAII": float(row["ProfWFAII"]),
                "BKRS": float(row["ProfBest"]),
                "BKRSMethod": METHOD_LABELS.get(row["ProfBestAlgorithm"], row["ProfBestAlgorithm"]),
            }
    return out


def find_run007_csv(explicit: str | None = None) -> tuple[Path | None, str]:
    if explicit:
        path = Path(explicit).expanduser()
        return (path if path.exists() else None, f"explicit: {path}")

    candidates = list(RESULTS.glob("run_007*/results.csv"))
    candidates += list(RESULTS.glob("run007*/results.csv"))
    candidates += list(RESULTS.glob("*Run007*/results.csv"))
    if candidates:
        selected = max(candidates, key=lambda path: path.stat().st_mtime)
        return selected, f"detected folder: {selected.relative_to(REPO)}"

    return None, "waiting for results/run_007*/results.csv"


def git_pull_if_requested(enabled: bool) -> str:
    if not enabled:
        return "git pull disabled"
    try:
        completed = subprocess.run(
            ["git", "pull", "--ff-only", "origin", "dev-labpc"],
            cwd=REPO,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=60,
        )
        tail = completed.stdout.strip().splitlines()[-3:]
        return " | ".join(tail) if tail else "git pull completed"
    except Exception as exc:
        return f"git pull failed: {exc}"


def compare_counts(rows: list[dict[str, object]], field: str, ref_field: str) -> dict[str, object]:
    wins = losses = ties = 0
    changes: list[float] = []
    for row in rows:
        diff = float(row[field]) - float(row[ref_field])
        changes.append(pct(float(row[field]), float(row[ref_field])))
        if diff > 0.01:
            wins += 1
        elif diff < -0.01:
            losses += 1
        else:
            ties += 1
    return {"wins": wins, "losses": losses, "ties": ties, "avgPct": avg(changes)}


def build_payload(run7_csv: Path | None, source_note: str, pull_note: str, refresh_seconds: int) -> dict[str, object]:
    reference = read_reference()
    prior_data = {name: read_results(path) for name, path in PRIOR_RUNS}
    all_keys = sorted(reference)

    prior_best: dict[tuple[int, int, int], dict[str, object]] = {}
    for key in all_keys:
        values = {name: data[key]["BestZ"] for name, data in prior_data.items() if key in data}
        if values:
            best_value = max(values.values())
            best_runs = [name for name, value in values.items() if best_value - value <= 0.01]
            prior_best[key] = {"value": best_value, "runs": " / ".join(best_runs)}

    run7_data = read_results(run7_csv) if run7_csv else {}
    completed_keys = sorted(key for key in run7_data if key in reference and key in prior_best)
    pending_keys = [key for key in all_keys if key not in completed_keys]

    rows: list[dict[str, object]] = []
    for key in completed_keys:
        n, m, seed = key
        run7 = run7_data[key]["BestZ"]
        prior = float(prior_best[key]["value"])
        ref = reference[key]
        wfaii = float(ref["WFAII"])
        bkrs = float(ref["BKRS"])
        row = {
            "Key": f"N{n}-M{m}-S{seed}",
            "N": n,
            "M": m,
            "Seed": seed,
            "Run007": run7,
            "PriorBestSTMO": prior,
            "PriorBestRun": prior_best[key]["runs"],
            "Run007MinusPriorBest": run7 - prior,
            "Run007VsPriorBestPct": pct(run7, prior),
            "ReferenceWFAII": wfaii,
            "Run007MinusWFAII": run7 - wfaii,
            "Run007ErrorPctWFAII": pct(run7, wfaii),
            "BKRS": bkrs,
            "BKRSMethod": ref["BKRSMethod"],
            "Run007MinusBKRS": run7 - bkrs,
            "Run007ErrorPctBKRS": pct(run7, bkrs),
            "Iterations": run7_data[key]["Iterations"],
            "TimeUsed": run7_data[key]["TimeUsed"],
            "BestAtIter": run7_data[key]["BestAtIter"],
            "S4Per1k": (run7_data[key]["S4Phase1Hits"] / run7_data[key]["Iterations"] * 1000.0) if run7_data[key]["Iterations"] else 0.0,
        }
        if row["Run007MinusPriorBest"] > 0.01:
            row["Outcome"] = "New STMO best"
        elif row["Run007MinusPriorBest"] < -0.01:
            row["Outcome"] = "Below prior best"
        else:
            row["Outcome"] = "Tied prior best"
        rows.append(row)

    by_n: list[dict[str, object]] = []
    for n in sorted({key[0] for key in all_keys}):
        bucket = [row for row in rows if int(row["N"]) == n]
        by_n.append(
            {
                "N": n,
                "Completed": len(bucket),
                "Total": 9,
                "Run007Avg": avg([float(row["Run007"]) for row in bucket]),
                "PriorBestAvg": avg([float(row["PriorBestSTMO"]) for row in bucket]),
                "VsPriorPct": avg([float(row["Run007VsPriorBestPct"]) for row in bucket]),
                "ErrorPctWFAII": avg([float(row["Run007ErrorPctWFAII"]) for row in bucket]),
                "ErrorPctBKRS": avg([float(row["Run007ErrorPctBKRS"]) for row in bucket]),
                "S4Per1k": avg([float(row["S4Per1k"]) for row in bucket]),
            }
        )

    summary = {
        "GeneratedAt": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "Run7CSV": str(run7_csv) if run7_csv else "",
        "SourceNote": source_note,
        "PullNote": pull_note,
        "Completed": len(rows),
        "Pending": len(pending_keys),
        "Total": len(all_keys),
        "RefreshSeconds": refresh_seconds,
        "Run007Avg": avg([float(row["Run007"]) for row in rows]),
        "PriorBestAvg": avg([float(row["PriorBestSTMO"]) for row in rows]),
        "VsPrior": compare_counts(rows, "Run007", "PriorBestSTMO"),
        "VsWFAII": compare_counts(rows, "Run007", "ReferenceWFAII"),
        "VsBKRS": compare_counts(rows, "Run007", "BKRS"),
        "AvgDiffWFAII": avg([float(row["Run007MinusWFAII"]) for row in rows]),
        "AvgErrorWFAII": avg([float(row["Run007ErrorPctWFAII"]) for row in rows]),
        "AvgDiffBKRS": avg([float(row["Run007MinusBKRS"]) for row in rows]),
        "AvgErrorBKRS": avg([float(row["Run007ErrorPctBKRS"]) for row in rows]),
    }

    return {
        "summary": summary,
        "rows": rows,
        "byN": by_n,
        "pending": [f"N{n}-M{m}-S{seed}" for n, m, seed in pending_keys],
    }


def write_csv(rows: list[dict[str, object]]) -> None:
    fields = [
        "Key",
        "N",
        "M",
        "Seed",
        "Run007",
        "PriorBestSTMO",
        "PriorBestRun",
        "Run007MinusPriorBest",
        "Run007VsPriorBestPct",
        "ReferenceWFAII",
        "Run007MinusWFAII",
        "Run007ErrorPctWFAII",
        "BKRS",
        "BKRSMethod",
        "Run007MinusBKRS",
        "Run007ErrorPctBKRS",
        "Outcome",
        "Iterations",
        "TimeUsed",
        "BestAtIter",
        "S4Per1k",
    ]
    with OUT_CSV.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fields})


def write_html(payload: dict[str, object]) -> None:
    data = json.dumps(payload, separators=(",", ":"))
    refresh = int(payload["summary"]["RefreshSeconds"])
    html = f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <meta http-equiv="refresh" content="{refresh}" />
  <title>Run007 Live STMO Comparison</title>
  <style>
    :root {{ --ink:#172033; --muted:#5b6475; --line:#dfe5ee; --bg:#f6f8fb; --panel:#fff; --good:#087f5b; --bad:#b42318; --warn:#b45309; --blue:#2563eb; }}
    * {{ box-sizing:border-box; }}
    body {{ margin:0; font-family:Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; background:var(--bg); color:var(--ink); letter-spacing:0; }}
    header {{ background:#fff; border-bottom:1px solid var(--line); }}
    .wrap {{ max-width:1500px; margin:0 auto; padding:22px 28px; }}
    .top {{ display:flex; justify-content:space-between; gap:18px; align-items:flex-start; }}
    h1 {{ margin:0; font-size:28px; line-height:1.15; }}
    .sub {{ color:var(--muted); margin:8px 0 0; max-width:980px; font-size:14px; line-height:1.5; }}
    .badge {{ display:inline-flex; border:1px solid var(--line); border-radius:999px; padding:7px 10px; background:#fff; font-size:12px; color:#334155; margin:0 0 8px 8px; }}
    main {{ max-width:1500px; margin:0 auto; padding:22px 28px 42px; }}
    .grid {{ display:grid; gap:16px; }}
    .kpis {{ grid-template-columns:repeat(6,minmax(0,1fr)); }}
    .two {{ grid-template-columns:1fr 1fr; }}
    .panel,.kpi {{ background:var(--panel); border:1px solid var(--line); border-radius:8px; box-shadow:0 14px 36px rgba(23,32,51,.07); }}
    .kpi {{ padding:15px; min-height:112px; }}
    .klabel {{ font-size:12px; color:var(--muted); font-weight:800; text-transform:uppercase; }}
    .kvalue {{ margin-top:9px; font-size:25px; line-height:1.1; font-weight:800; }}
    .knote {{ margin-top:7px; font-size:12px; color:var(--muted); line-height:1.35; }}
    .positive {{ color:var(--good); }} .negative {{ color:var(--bad); }} .warning {{ color:var(--warn); }}
    .head {{ display:flex; justify-content:space-between; align-items:baseline; gap:12px; padding:15px 16px 0; }}
    h2 {{ margin:0; font-size:16px; }} .meta {{ color:var(--muted); font-size:12px; }}
    .body {{ padding:14px 16px 16px; }}
    .chart {{ min-height:300px; width:100%; }} svg {{ width:100%; height:100%; display:block; }}
    .table-wrap {{ overflow:auto; max-height:690px; border-top:1px solid var(--line); }}
    table {{ width:100%; border-collapse:collapse; min-width:1500px; font-size:12px; }}
    th,td {{ border-bottom:1px solid var(--line); padding:9px 10px; white-space:nowrap; text-align:right; }}
    th {{ position:sticky; top:0; z-index:1; background:#f8fafc; color:#334155; }}
    th:first-child,td:first-child,td:last-child {{ text-align:left; }}
    .chip {{ border-radius:999px; padding:4px 8px; border:1px solid var(--line); font-size:11px; }}
    .chip.good {{ color:var(--good); background:#ecfdf5; border-color:#bbf7d0; }}
    .chip.bad {{ color:var(--bad); background:#fef2f2; border-color:#fecaca; }}
    .chip.warn {{ color:var(--warn); background:#fffbeb; border-color:#fed7aa; }}
    .pending {{ color:#475569; font-size:13px; line-height:1.55; }}
    @media (max-width:1100px) {{ .kpis,.two {{ grid-template-columns:1fr; }} .top {{ flex-direction:column; }} }}
  </style>
</head>
<body>
  <header>
    <div class="wrap">
      <div class="top">
        <div>
          <h1>Run007 Live STMO Comparison</h1>
          <p class="sub">Auto-refreshing partial-run dashboard. Compares completed Run007 instances against the prior best clean STMO result, Reference WFA.II, and BKRS. Keep this file open while the watcher regenerates it.</p>
        </div>
        <div>
          <span class="badge">Refresh {refresh}s</span>
          <span class="badge" id="sourceBadge"></span>
        </div>
      </div>
    </div>
  </header>
  <main>
    <div class="grid kpis" id="kpis"></div>
    <div class="grid two" style="margin-top:16px">
      <section class="panel"><div class="head"><h2>Run007 Progress by N</h2><div class="meta">Completed cases and average movement</div></div><div class="body"><div class="chart" id="byNChart"></div></div></section>
      <section class="panel"><div class="head"><h2>Pending Instances</h2><div class="meta">Waiting for result rows</div></div><div class="body"><div class="pending" id="pending"></div></div></section>
    </div>
    <section class="panel" style="margin-top:16px">
      <div class="head"><h2>Completed Run007 Instance Comparison</h2><div class="meta">Latest generated table</div></div>
      <div class="body" style="padding-bottom:0"><div id="statusLine" class="pending"></div></div>
      <div class="table-wrap"><table id="resultTable"></table></div>
    </section>
  </main>
  <script>
    const DATA = {data};
    const s = DATA.summary;
    const rows = DATA.rows;
    const byN = DATA.byN;
    const $ = sel => document.querySelector(sel);
    const fmt = (v,d=2) => Number(v || 0).toLocaleString(undefined, {{minimumFractionDigits:d, maximumFractionDigits:d}});
    const pct = (v,d=2) => `${{Number(v || 0) >= 0 ? "+" : ""}}${{fmt(v,d)}}%`;
    const esc = text => String(text).replace(/[&<>"']/g, c => ({{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}}[c]));
    const cls = v => Number(v) >= 0 ? "positive" : "negative";
    $("#sourceBadge").textContent = s.Completed ? `${{s.Completed}}/${{s.Total}} complete` : "waiting for Run007";
    function kpis() {{
      const cards = [
        ["Completed", `${{s.Completed}}/${{s.Total}}`, s.GeneratedAt, s.Completed ? "positive" : "warning"],
        ["Run007 vs Prior STMO", `${{s.VsPrior.wins}}W / ${{s.VsPrior.losses}}L / ${{s.VsPrior.ties}}T`, `Avg ${{pct(s.VsPrior.avgPct,3)}}`, s.VsPrior.avgPct >= 0 ? "positive" : "warning"],
        ["Run007 vs WFA.II", `${{s.VsWFAII.wins+s.VsWFAII.ties}}/${{s.Completed || 0}}`, `Err ${{pct(s.AvgErrorWFAII,3)}} | diff ${{fmt(s.AvgDiffWFAII,2)}}`, s.AvgErrorWFAII >= 0 ? "positive" : "warning"],
        ["Run007 vs BKRS", `${{s.VsBKRS.wins+s.VsBKRS.ties}}/${{s.Completed || 0}}`, `Err ${{pct(s.AvgErrorBKRS,3)}} | diff ${{fmt(s.AvgDiffBKRS,2)}}`, "warning"],
        ["Run007 Avg", fmt(s.Run007Avg,2), "completed rows only", "positive"],
        ["Prior Best Avg", fmt(s.PriorBestAvg,2), "same completed rows", "warning"],
      ];
      $("#kpis").innerHTML = cards.map(c => `<div class="kpi"><div class="klabel">${{c[0]}}</div><div class="kvalue ${{c[3]}}">${{c[1]}}</div><div class="knote">${{c[2]}}</div></div>`).join("");
    }}
    function svgEl(name, attrs={{}}) {{ const el=document.createElementNS("http://www.w3.org/2000/svg",name); Object.entries(attrs).forEach(([k,v])=>el.setAttribute(k,v)); return el; }}
    function bars() {{
      const el=$("#byNChart"); el.innerHTML="";
      const w=700,h=300,m={{l:54,r:20,t:18,b:42}}; const vals=byN.flatMap(d=>[d.VsPriorPct,d.ErrorPctWFAII,d.ErrorPctBKRS]);
      const minV=Math.min(...vals,0), maxV=Math.max(...vals,1); const xBand=(w-m.l-m.r)/byN.length; const bw=18;
      const y=v=>h-m.b-(v-minV)/(maxV-minV||1)*(h-m.t-m.b); const zero=y(0); const svg=svgEl("svg",{{viewBox:`0 0 ${{w}} ${{h}}`}});
      svg.appendChild(svgEl("line",{{x1:m.l,y1:zero,x2:w-m.r,y2:zero,stroke:"#94a3b8"}}));
      byN.forEach((d,i)=>{{ const cx=m.l+i*xBand+xBand/2; [["VsPriorPct","#0f766e"],["ErrorPctWFAII","#7c3aed"],["ErrorPctBKRS","#b45309"]].forEach((pair,j)=>{{ const v=d[pair[0]]||0; svg.appendChild(svgEl("rect",{{x:cx-30+j*22,y:Math.min(y(v),zero),width:bw,height:Math.max(2,Math.abs(zero-y(v))),rx:4,fill:pair[1]}})); }}); const lab=svgEl("text",{{x:cx,y:h-13,"text-anchor":"middle",fill:"#5b6475","font-size":"12"}}); lab.textContent=`N=${{d.N}} (${{d.Completed}}/9)`; svg.appendChild(lab); }});
      el.appendChild(svg);
    }}
    function chip(outcome) {{ if(outcome==="New STMO best") return "good"; if(outcome==="Below prior best") return "bad"; return "warn"; }}
    function table() {{
      $("#statusLine").textContent = `${{s.SourceNote}} | ${{s.PullNote}} | Generated ${{s.GeneratedAt}}`;
      const cols = [["Key","Instance"],["Run007","Run007"],["PriorBestSTMO","Prior STMO"],["PriorBestRun","Prior Run"],["Run007MinusPriorBest","Delta Prior"],["Run007VsPriorBestPct","% Prior"],["ReferenceWFAII","WFA.II"],["Run007MinusWFAII","Delta WFA.II"],["Run007ErrorPctWFAII","Err WFA.II"],["BKRS","BKRS"],["Run007MinusBKRS","Delta BKRS"],["Run007ErrorPctBKRS","Err BKRS"],["S4Per1k","S4/1k"],["Outcome","Outcome"]];
      const head = `<thead><tr>${{cols.map(c=>`<th>${{c[1]}}</th>`).join("")}}</tr></thead>`;
      const body = rows.map(r => `<tr><td>${{esc(r.Key)}}</td><td>${{fmt(r.Run007,2)}}</td><td>${{fmt(r.PriorBestSTMO,2)}}</td><td>${{esc(r.PriorBestRun)}}</td><td class="${{cls(r.Run007MinusPriorBest)}}">${{fmt(r.Run007MinusPriorBest,2)}}</td><td class="${{cls(r.Run007VsPriorBestPct)}}">${{pct(r.Run007VsPriorBestPct,3)}}</td><td>${{fmt(r.ReferenceWFAII,2)}}</td><td class="${{cls(r.Run007MinusWFAII)}}">${{fmt(r.Run007MinusWFAII,2)}}</td><td class="${{cls(r.Run007ErrorPctWFAII)}}">${{pct(r.Run007ErrorPctWFAII,3)}}</td><td>${{fmt(r.BKRS,2)}}</td><td class="${{cls(r.Run007MinusBKRS)}}">${{fmt(r.Run007MinusBKRS,2)}}</td><td class="${{cls(r.Run007ErrorPctBKRS)}}">${{pct(r.Run007ErrorPctBKRS,3)}}</td><td>${{fmt(r.S4Per1k,2)}}</td><td><span class="chip ${{chip(r.Outcome)}}">${{esc(r.Outcome)}}</span></td></tr>`).join("");
      $("#resultTable").innerHTML = head + `<tbody>${{body}}</tbody>`;
      $("#pending").innerHTML = DATA.pending.length ? DATA.pending.join(", ") : "All benchmark instances completed.";
    }}
    kpis(); bars(); table();
  </script>
</body>
</html>
"""
    OUT_HTML.write_text(html, encoding="utf-8")


def generate_once(args: argparse.Namespace) -> dict[str, object]:
    pull_note = git_pull_if_requested(args.pull)
    run7_csv, source_note = find_run007_csv(args.run7_csv)
    payload = build_payload(run7_csv, source_note, pull_note, args.refresh)
    OUT_JSON.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    write_csv(payload["rows"])
    write_html(payload)
    return payload


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate or watch a live Run007 comparison dashboard.")
    parser.add_argument("--watch", action="store_true", help="Regenerate repeatedly.")
    parser.add_argument("--interval", type=int, default=30, help="Seconds between regeneration in watch mode.")
    parser.add_argument("--refresh", type=int, default=20, help="HTML auto-refresh interval in seconds.")
    parser.add_argument("--pull", action="store_true", help="Run git pull before each refresh.")
    parser.add_argument("--run7-csv", default=None, help="Explicit path to the live Run007 results.csv.")
    parser.add_argument("--repo-root", default=None, help="Path to the STMO-CODE-V1 repository root.")
    parser.add_argument("--output-dir", default=None, help="Directory where the live dashboard files should be written.")
    parser.add_argument("--reference-csv", default=None, help="Path to comparison_run002_by_instance.csv.")
    args = parser.parse_args()
    configure_paths(args)

    if args.watch:
        while True:
            payload = generate_once(args)
            summary = payload["summary"]
            print(
                f"[{summary['GeneratedAt']}] {summary['Completed']}/{summary['Total']} complete | "
                f"vs prior {summary['VsPrior']['wins']}/{summary['VsPrior']['losses']}/{summary['VsPrior']['ties']} | "
                f"{summary['SourceNote']}"
            )
            time.sleep(max(5, args.interval))
    else:
        payload = generate_once(args)
        summary = payload["summary"]
        print(f"Wrote {OUT_HTML}")
        print(f"Wrote {OUT_CSV}")
        print(f"Wrote {OUT_JSON}")
        print(f"Completed {summary['Completed']}/{summary['Total']} | {summary['SourceNote']}")


if __name__ == "__main__":
    main()
