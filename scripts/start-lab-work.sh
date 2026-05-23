#!/usr/bin/env bash
set -euo pipefail

TARGET_BRANCH="dev-labpc"
REMOTE="origin"

cd "$(git rev-parse --show-toplevel)"

echo "Lab PC start check"
echo "Repository: $(basename "$(pwd)")"

if [[ -n "$(git status --porcelain)" ]]; then
  echo
  echo "Stop: you have uncommitted local changes."
  echo "Review, commit, or stash them before updating from main."
  git status --short
  exit 1
fi

echo
echo "Fetching latest GitHub state..."
git fetch "$REMOTE"

if git show-ref --verify --quiet "refs/heads/$TARGET_BRANCH"; then
  git switch "$TARGET_BRANCH"
else
  git switch -c "$TARGET_BRANCH" --track "$REMOTE/$TARGET_BRANCH"
fi

git pull --ff-only "$REMOTE" "$TARGET_BRANCH"

behind_count="$(git rev-list --count HEAD.."$REMOTE/main")"

echo
echo "Current branch: $TARGET_BRANCH"
echo "Commits available from main: $behind_count"

if [[ "$behind_count" != "0" ]]; then
  echo
  echo "Recent changes available from main:"
  git log --date=short --pretty=format:"- %cd %h %s" HEAD.."$REMOTE/main" | head -n 15
  echo
fi

if [[ -f logs/experiments.md ]]; then
  echo
  echo "Recent experiment notes:"
  sed -n '1,80p' logs/experiments.md
fi

if [[ "$behind_count" == "0" ]]; then
  echo
  echo "You are already updated from main."
  exit 0
fi

echo
if ! read -r -p "Merge these main changes into $TARGET_BRANCH before experiments? [y/N] " answer; then
  answer=""
  echo
fi
case "$answer" in
  y|Y|yes|YES)
    git merge --no-edit "$REMOTE/main"
    echo
    echo "$TARGET_BRANCH is now updated from main."
    ;;
  *)
    echo
    echo "Skipped update. Do not run experiments unless you intentionally want old code."
    ;;
esac
