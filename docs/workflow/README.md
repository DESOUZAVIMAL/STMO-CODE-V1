# Laptop and Lab PC Workflow

This folder documents the Git workflow used for this research codebase.
Use it as the reference when changing the branch strategy, scripts, logs, or VS Code setup.

## Purpose

The project is developed from two machines:

- Laptop: research, discussion, coding, cleanup, planning.
- Lab PC: experiments, high-spec runs, debugging, parameter tuning.

The workflow is designed so each machine starts from the latest trusted code and important changes are not forgotten after a gap of several days.

## Branch Model

```text
main
  latest trusted working code

dev-laptop
  laptop workspace

dev-labpc
  lab PC workspace

dev
  preserved backup of the old development branch
```

The main rule is:

```text
Before work: main -> current machine branch
After useful work: current machine branch -> main
```

Do not normally merge `dev-laptop` directly into `dev-labpc`, or `dev-labpc` directly into `dev-laptop`.
Use `main` as the central trusted branch.

## Daily Usage

On the laptop:

```bash
bash scripts/start-laptop-work.sh
```

On the lab PC:

```bash
bash scripts/start-lab-work.sh
```

When finished on either machine:

```bash
bash scripts/finish-work.sh
```

The finish script commits and pushes the current machine branch, then asks before merging into `main`.
Nothing is merged into `main` without permission.

## Script Responsibilities

`scripts/start-laptop-work.sh`

- Stops if local uncommitted changes exist.
- Fetches GitHub state.
- Switches to `dev-laptop`.
- Pulls latest `dev-laptop`.
- Checks whether `main` has new commits.
- Shows recent commits from `main`.
- Asks before merging `main` into `dev-laptop`.

`scripts/start-lab-work.sh`

- Stops if local uncommitted changes exist.
- Fetches GitHub state.
- Switches to `dev-labpc`.
- Pulls latest `dev-labpc`.
- Checks whether `main` has new commits.
- Shows recent commits from `main`.
- Shows recent experiment notes from `logs/experiments.md`.
- Asks before merging `main` into `dev-labpc`.

`scripts/finish-work.sh`

- Must be run from `dev-laptop` or `dev-labpc`.
- Shows changed files.
- Reminds which log file to update.
- Creates a prefixed commit message:
  - `laptop: ...` from `dev-laptop`
  - `lab: ...` from `dev-labpc`
- Pushes the current branch.
- Asks before merging into `main`.
- Returns to the original machine branch after updating `main`.

## Logs

Use the log files for memory and research continuity.

```text
logs/laptop.md
  laptop research and coding notes

logs/labpc.md
  lab PC fixes, environment issues, parameter changes

logs/experiments.md
  experiment runs, datasets, parameters, results, next steps
```

Keep entries short but useful.
Commit messages say what changed.
Logs explain why, what happened, and what to do next.

## Commit Message Style

Everyday examples:

```text
laptop: fix label mismatch in dataset v3 loader
lab: reduce batch size after CUDA memory error
lab: record successful dataset v3 run
```

For important work, add details in the relevant log file before running `finish-work.sh`.

## VS Code Setup

The repository includes `.vscode/tasks.json` with tasks:

- Start Laptop Work
- Start Lab Work
- Finish Work

In VS Code, open the command palette and run:

```text
Tasks: Run Task
```

Then choose the correct task.

Optional local setup on each machine:

```bash
git config commit.template .gitmessage
```

## Safety Rules

- Always run the start script before coding or running experiments.
- Always run the finish script before leaving a machine.
- If a start script reports uncommitted changes, stop and review them first.
- Do not run lab experiments if `dev-labpc` is behind `main`, unless that is intentional.
- Keep `dev` as a backup unless you are sure it is no longer needed.

## Future Improvements

Safe improvements:

- Add a clearer sync report with dates and author names.
- Add a script that shows "last laptop changes" and "last lab changes".
- Add Git tags for important thesis milestones.
- Add a `scripts/status-report.sh` command for quick branch health checks.
- Add PowerShell versions if Git Bash is inconvenient on the lab PC.

Risky improvements that need care:

- Automatic merges on VS Code startup.
- Deleting the old `dev` branch.
- Making `main` protected without testing the scripts against branch protection.
- Storing large experiment outputs in Git.

## Current Implementation Baseline

This workflow was introduced after the old `dev` branch was merged into `main`.
At the time of setup, these active branches were synchronized:

```text
main
dev-laptop
dev-labpc
```

The old `dev` branch was intentionally preserved as a backup.

