#!/bin/bash
# Launcher for Run 003 — handles its own I/O so PowerShell doesn't interfere
export PATH=/mingw64/bin:/usr/bin:$PATH
cd /e/STMO-CODE-V1
bash run_all_experiments.sh 'Run003_PhaseAB' > /c/Temp/run003_log.txt 2>&1
echo "=== EXPERIMENT COMPLETE ===" >> /c/Temp/run003_log.txt
