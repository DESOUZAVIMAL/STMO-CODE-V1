# Run 002 — Lab PC Session Report
## For: Laptop Claude AI (Learning & Future Reference)

---

## 1. STARTING SITUATION

- **Branch on lab PC:** `dev-labpc`
- **Goal:** Pull laptop changes (from `dev-laptop` branch, merged into `main` via PR #1) and run the full 45-instance experiment as Run 002.
- **Laptop had made these changes:** adaptive config C1-C4, Stage 4 top-3 pairs (D2), and various code improvements.

---

## 2. PULLING LATEST CHANGES FROM GITHUB

**Action:** Fetch + merge `origin/main` into `dev-labpc`.

```bash
git fetch origin
git merge origin/main --no-edit
```

**Result:** Clean merge. 5 source files updated:
- `STMO.cpp` (+282 / -567 lines net)
- `config.h`
- `memory_ops.h`
- `stages.h`
- `stmo_types.h`

Note: The laptop merge had also **deleted** all `results/logs/`, `params/`, `run_all_experiments.sh`, and `fix_csv.sh` from the repo — but these files were local on the lab PC and not overwritten (git only deletes tracked versions). The `params/` folder remained on disk with all 45 files.

---

## 3. VERIFYING THE 5 CODE CHANGES

All 5 requested changes were checked by reading each file carefully before touching anything.

**Findings:**

| Change | File | Status |
|--------|------|--------|
| 1 — Add adaptive `#define` constants | `config.h` | **BUG** — `END_TIME_LARGE 180.0` was missing; instead there were two duplicate `#define END_TIME 120.0` lines |
| 2 — Adaptive config block in `main()` after `genData()` | `STMO.cpp` | Already done by laptop — but references `END_TIME_LARGE` which was undefined (caused by Change 1 bug) |
| 3 — `StructuralMap` with `WeakTarget` struct + vector | `stmo_types.h` | Already done by laptop |
| 4 — `buildStructuralMap()` collecting top-3 weak pairs | `memory_ops.h` | Already done by laptop |
| 5 — Stage 4 loop over `sm.targets` | `stages.h` | Already done by laptop |

**Fix applied to config.h:**
```cpp
// BEFORE (bug — duplicate line):
#define END_TIME    120.0
#define END_TIME    120.0   // <-- should have been END_TIME_LARGE 180.0

// AFTER (fixed):
#define END_TIME        120.0
#define END_TIME_LARGE  180.0
```

---

## 4. COMPILER NOT FOUND — ROOT CAUSE AND FIX

**Error:** `g++: command not found` in the VS Code terminal (PowerShell).

**Root cause:** The lab PC has no standalone C++ compiler in the system PATH.
- `C:\mingw64` exists but is the **Git for Windows bundled MinGW** — shell utilities only, no g++.
- Git Bash uses `/mingw64/bin` — also Git's bundled MinGW, no g++.

**Why Run 001 worked:** Run 001 used `run_all_experiments.sh` which fell back to the pre-compiled `STMO.exe` already in the repo, or was run from a different environment.

**Fix:** Installed MSYS2 via winget, then installed g++ inside MSYS2:
```powershell
winget install -e --id MSYS2.MSYS2
```
Then from MSYS2 bash (or via bash.exe call):
```bash
pacman -S --noconfirm mingw-w64-x86_64-gcc
```

MSYS2 g++ is at: `C:\msys64\mingw64\bin\g++.exe`

**CRITICAL — MSYS2 g++ cannot be called from PowerShell or Git Bash directly.**
It crashes silently (exit code 1, zero error output) because the MSYS2 runtime environment is not active.
It **must** be called from inside the MSYS2 bash shell:
```powershell
& "C:\msys64\usr\bin\bash.exe" -c "export PATH=/mingw64/bin:/usr/bin:`$PATH; g++ ..."
```
Note: The backtick before `$PATH` prevents PowerShell from expanding it before passing to bash.

**User PATH updated:** Added `C:\msys64\mingw64\bin` to Windows user PATH permanently:
```powershell
[System.Environment]::SetEnvironmentVariable("PATH", $userPath + ";C:\msys64\mingw64\bin", "User")
```
System-level PATH required elevation and failed, so user-level PATH was used instead — sufficient for this user.

---

## 5. COMPILATION ATTEMPT — STACK OVERFLOW (WINDOWS-SPECIFIC)

**First compile (without stack flag):**
```bash
g++ -O2 -std=c++11 -o STMO.exe STMO.cpp -lm
```

**Result:** Compiled OK, but running STMO.exe crashed immediately:
- Exit code: `-1073741571` = `0xC00000FD` = **STATUS_STACK_OVERFLOW**
- Zero output produced
- `settime_0.txt` was NOT created (crash before `genData()` even ran)

**Root cause:** Windows default stack size is **1 MB**.
STMO allocates two large structures as local variables in `main()`:

| Variable | Type | Size |
|----------|------|------|
| `Population pop` | `Turtle[MAX_P]` = `Turtle[50]` | ~1.14 MB |
| `EliteArchive eliteArchive` | contains `Turtle[MAX_P]` | ~1.14 MB |
| **Total** | | **~2.28 MB** |

Each `Turtle` struct breakdown:
- `Order_seq[MAX_N]`          = 300 x 4 bytes = 1,200 bytes
- `Order_seqval[MAX_N]`       = 300 x 4 bytes = 1,200 bytes
- `M_select[MAX_N]`           = 300 x 4 bytes = 1,200 bytes
- `M_selectval[MAX_N]`        = 300 x 4 bytes = 1,200 bytes
- `machineSeq[MAX_M][MAX_N]`  = 15 x 300 x 4  = 18,000 bytes
- `machineCount[MAX_M]`       = 15 x 4         = 60 bytes
- `ctimes[MAX_N]`             = 300 x 4         = 1,200 bytes
- misc fields                                   = ~12 bytes
- **Total per Turtle: ~24,072 bytes (~23.5 KB)**

50 Turtles x 24,072 = ~1.14 MB per array. Two arrays = ~2.28 MB. Windows 1 MB stack = immediate crash.

**On Linux (laptop):** Default stack is 8 MB — never crashes. This bug is Windows-only.

**Fix:** Add linker flag to set stack to 8 MB:
```bash
g++ -O2 -std=c++11 -static -static-libgcc -static-libstdc++ -Wl,--stack,8388608 -o STMO.exe STMO.cpp -lm
```

`-static -static-libgcc -static-libstdc++` added so the exe has no MSYS2 runtime DLL dependencies and runs standalone on Windows without MSYS2 in PATH.

**This flag was added to `run_all_experiments.sh`** so all future lab PC compilations use it automatically.

---

## 6. PARAM.TXT FORMAT BUG

**Problem:** A test `param.txt` was written from PowerShell using `Set-Content` which silently adds a **UTF-8 BOM** (bytes `EF BB BF`) at the start of the file.

The `genData()` function in `problem.h` reads param.txt using **fixed byte offsets** (not scanf), so the 3-byte BOM shifted all 25 field readings by 3 bytes — wrong values parsed — crash.

**Diagnosis:** Checked the first 3 bytes of param.txt:
```
0xEF 0xBB 0xBF  <-- UTF-8 BOM, must not be here
```

**Fix:** Use ASCII encoding when writing param.txt from PowerShell:
```powershell
[System.IO.File]::WriteAllText("E:\STMO-CODE-V1\param.txt", $content, [System.Text.Encoding]::ASCII)
```

**Best practice for future:** Always copy from the `params/` folder, never write manually:
```bash
cp params/param_N30_M3_S10.txt param.txt
```

---

## 7. RUN_ALL_EXPERIMENTS.SH — OLD out.txt BUG (CRITICAL)

**Problem discovered:** After Run 002 launched, all completed N=30 instances showed the **same result**: `Z=8650.6221 iters=1447 t=120.04s best@1395`. This is impossible — different M values and seeds must give different results.

**Root cause:** A leftover `out.txt` file from **Run 001** (dated May 24) was sitting in `E:\STMO-CODE-V1\`. The script had this logic:
```bash
if [ -f "./out.txt" ]; then
    cp ./out.txt "$LOG_DIR/out_N${N}_M${M}_S${Seed}.txt"
    OUT="$LOG_DIR/out_N${N}_M${M}_S${Seed}.txt"   # <-- uses old Run 001 out.txt!
fi
```

In the old code (Run 001), STMO created `./out.txt` as a separate output file.
In the new code (Run 002+), STMO only writes to stdout — `./out.txt` is never created or refreshed.
So the same Run 001 `out.txt` was copied as the result file for every single instance.

**Additionally:** The grep patterns in the script were wrong for the current STMO output format:

| Old pattern | What it matched | What awk gave |
|---|---|---|
| `grep -m1 "Best Z"` | Progress line: `[Iter 10 | Time 0.0s] Best Z = 3025.73 | PM=552 S2=395 S4p1=36` | `awk '{print $NF}'` → `S4p1=36` (WRONG) |
| `grep -m1 "Total iters"` | `"  Total iterations = 1447"` | `awk '{print $4}'` → `1447` (OK) |
| `grep -m1 "Time used"` | No match in current STMO output | empty |
| `grep -m1 "Best at iter"` | No match in current STMO output | empty |

**Fixes applied to `run_all_experiments.sh`:**
```bash
# 1. Delete old out.txt before each instance run
rm -f ./out.txt

# 2. Always use stdout file directly (STMO no longer creates out.txt)
OUT="$LOG_DIR/stdout_N${N}_M${M}_S${Seed}.txt"

# 3. Fixed grep patterns matching actual STMO.cpp output format:
# Matches: "  Best Z = 8650.6221"
BEST_Z=$(grep -m1 "^  Best Z ="           "$OUT" | awk '{print $4}')
# Matches: "  Total iterations = 1447"
ITERS=$(  grep -m1 "^  Total iterations =" "$OUT" | awk '{print $4}')
# Matches: "  Time = 120.04 seconds"
TIME_S=$( grep -m1 "^  Time ="             "$OUT" | awk '{print $3}')
# Matches: "Best improved at iter: 1395"  (inside box-drawing chars)
BEST_AT=$(grep -m1 "Best improved at iter" "$OUT" | grep -oP '\d+\s*║' | grep -oP '\d+')
# Matches: "Stage 2 total repairs:  NNN"
S2REP=$(  grep -m1 "Stage 2 total repairs" "$OUT" | grep -oP ':\s*\d+' | grep -oP '\d+')
# Matches: "Stage 4 Phase1 hits  :  NNN"
S4HIT=$(  grep -m1 "Stage 4 Phase1 hits"   "$OUT" | grep -oP ':\s*\d+' | grep -oP '\d+')
# Matches: "Stagnation resets    :  NNN"
STAG=$(   grep -m1 "Stagnation resets"     "$OUT" | grep -oP ':\s*\d+' | grep -oP '\d+')
```

---

## 8. SED PATTERN BUG IN RUN SCRIPT

**Original script line:**
```bash
sed -i 's/#define END_TIME.*/#define END_TIME    120.0/' config.h
```

**Problem:** The `.*` wildcard also matched `#define END_TIME_LARGE  180.0`, overwriting it with `#define END_TIME    120.0` — destroying the large-N time budget.

**Fix:**
```bash
sed -i 's/#define END_TIME  *[0-9][0-9]*\.[0-9]*$/#define END_TIME        120.0/' config.h
```
The `$` end-of-line anchor ensures it only matches lines ending in a plain number, not lines with suffix text like `_LARGE`.

---

## 9. DEFINITIVE COMPILE COMMAND FOR LAB PC (WINDOWS + MSYS2)

```bash
g++ -O2 -std=c++11 -static -static-libgcc -static-libstdc++ -Wl,--stack,8388608 -o STMO.exe STMO.cpp -lm
```

**Flags explained:**

| Flag | Reason |
|------|--------|
| `-O2` | Optimisation level 2 |
| `-std=c++11` | C++11 required (lambdas, range-for, etc.) |
| `-static` | Statically link all libraries |
| `-static-libgcc` | Bundle libgcc — no external DLL needed |
| `-static-libstdc++` | Bundle libstdc++ — no external DLL needed |
| `-Wl,--stack,8388608` | Set Windows stack to 8 MB — **MANDATORY** — without this STMO crashes immediately on startup |
| `-lm` | Link math library |

**How to invoke from PowerShell (lab PC):**
```powershell
& "C:\msys64\usr\bin\bash.exe" -c "export PATH=/mingw64/bin:/usr/bin:`$PATH; cd /e/STMO-CODE-V1 && g++ -O2 -std=c++11 -static -static-libgcc -static-libstdc++ -Wl,--stack,8388608 -o STMO.exe STMO.cpp -lm && echo COMPILE_OK"
```

---

## 10. DEFINITIVE RUN COMMAND FOR LAB PC

```powershell
& "C:\msys64\usr\bin\bash.exe" -c "export PATH=/mingw64/bin:/usr/bin:`$PATH; cd /e/STMO-CODE-V1 && bash run_all_experiments.sh 'Run003_description'"
```

The run script now handles everything: compile with correct flags, set param.txt per instance, collect results to CSV.

---

## 11. COMMITS MADE ON DEV-LABPC THIS SESSION

| Hash | Message |
|------|---------|
| Merge commit | Merge origin/main into dev-labpc (laptop changes) |
| `f0047ef` | Run 002: fix END_TIME_LARGE define + Windows stack + compile flags |
| `031154c` | Fix run script: remove out.txt dependency, fix grep patterns |

---

## 12. CURRENT STATE AT END OF SESSION

- **Branch:** `dev-labpc`, 4 commits ahead of `origin/dev-labpc`
- **Run 002:** Running — instance [1/45] N=30 M=3 Seed=10 underway
- **Expected duration:** ~90-100 minutes total (120s per N<=100 instance, 180s per N>=150 instance)
- **Results saving to:** `results/run_002_2026-05-26_01-00_Run002_adaptive_config/`
- **STMO.exe:** Compiled fresh with all correct flags, passed 15-second quick test

---

## 13. KEY LESSONS FOR LAPTOP CLAUDE — DO NOT REPEAT THESE

### L1 — Windows stack overflow (CRITICAL)
Always compile for the lab PC with `-Wl,--stack,8388608`.
STMO's `Population` + `EliteArchive` in `main()` = ~2.28 MB stack usage.
Windows default = 1 MB. Without this flag: immediate crash, zero output, exit code `0xC00000FD`.
Linux default = 8 MB so it never fails on the laptop — this is Windows-only.

### L2 — MSYS2 g++ cannot run from PowerShell or Git Bash
Must use: `& "C:\msys64\usr\bin\bash.exe" -c "export PATH=/mingw64/bin:/usr/bin:$PATH; g++ ..."`
Calling `C:\msys64\mingw64\bin\g++.exe` directly from PowerShell: silent crash, no output.
Calling it from Git Bash: also fails (different MinGW runtime).

### L3 — config.h must define END_TIME_LARGE
Whenever `STMO.cpp` uses `END_TIME_LARGE` in the adaptive block, `config.h` must define it.
The laptop accidentally left a duplicate `#define END_TIME 120.0` instead of `#define END_TIME_LARGE 180.0`.
Would cause a compile error or silent wrong value at runtime.

### L4 — Never write param.txt from PowerShell Set-Content
PowerShell `Set-Content` writes UTF-8 BOM. `genData()` uses fixed byte offsets — BOM shifts all 25 fields.
Always use: `cp params/param_N${N}_M${M}_S${Seed}.txt param.txt`

### L5 — Delete out.txt before every run script execution
Old `out.txt` from any previous run poisons all result parsing.
Add `rm -f ./out.txt` at the start of each instance loop iteration in the run script.

### L6 — STMO no longer creates out.txt
Current code writes everything to stdout only.
`run_all_experiments.sh` must parse `stdout_N${N}_M${M}_S${Seed}.txt`, not `out.txt`.

### L7 — sed pattern must use $ anchor when editing config.h
`sed 's/#define END_TIME.*/.../g'` also matches `END_TIME_LARGE` — destroys that line.
Use: `sed 's/#define END_TIME  *[0-9][0-9]*\.[0-9]*$/.../g'`

### L8 — grep patterns must match actual STMO.cpp output exactly
Check the actual printf strings in STMO.cpp before writing grep patterns in the run script.
Current STMO output format (Final Result section — plain ASCII):
```
  Best Z = 8650.6221
  Total iterations = 1447
  Time = 120.04 seconds
```
Diagnostic report format (inside box-drawing Unicode characters):
```
Best Z value         :  8650.62
Total iterations     : 1447 / 10000
Time used            :  120.0s / 120.0s
Best improved at iter: 1395
Stage 2 total repairs:  NNN
Stage 4 Phase1 hits  :  NNN
Stagnation resets    :  NNN
```
Use `grep -oP` with regex to extract numbers from inside the box characters.
