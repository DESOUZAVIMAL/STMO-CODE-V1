#!/usr/bin/env bash
set -euo pipefail

REMOTE="origin"

cd "$(git rev-parse --show-toplevel)"

current_branch="$(git branch --show-current)"

case "$current_branch" in
  dev-laptop)
    prefix="laptop"
    log_hint="logs/laptop.md"
    ;;
  dev-labpc)
    prefix="lab"
    log_hint="logs/labpc.md and/or logs/experiments.md"
    ;;
  *)
    echo "Stop: finish-work should be run from dev-laptop or dev-labpc."
    echo "Current branch: $current_branch"
    exit 1
    ;;
esac

echo "Finish work check"
echo "Current branch: $current_branch"
echo

if [[ -z "$(git status --porcelain)" ]]; then
  echo "No local changes to commit."
else
  echo "Changed files:"
  git status --short
  echo
  echo "Before committing, update the relevant log file if this change matters:"
  echo "- $log_hint"
  echo
  read -r -p "Stage all current changes and continue? [y/N] " stage_answer
  case "$stage_answer" in
    y|Y|yes|YES)
      git add -A
      ;;
    *)
      echo "Stopped before staging."
      exit 1
      ;;
  esac

  read -r -p "Commit title after '$prefix: ' " title
  if [[ -z "$title" ]]; then
    echo "Stop: commit title cannot be empty."
    exit 1
  fi

  git commit -m "$prefix: $title"
fi

echo
echo "Pushing $current_branch to GitHub..."
git push -u "$REMOTE" "$current_branch"

echo
read -r -p "Merge $current_branch into main now? [y/N] " merge_answer
case "$merge_answer" in
  y|Y|yes|YES)
    git fetch "$REMOTE"
    git switch main
    git pull --ff-only "$REMOTE" main
    git merge --no-edit "$current_branch"
    git push "$REMOTE" main
    git switch "$current_branch"
    echo
    echo "main is updated, and you are back on $current_branch."
    ;;
  *)
    echo
    echo "Skipped main update. Your work is pushed on $current_branch only."
    ;;
esac

