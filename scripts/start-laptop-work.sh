#!/usr/bin/env bash
set -euo pipefail

TARGET_BRANCH="dev-laptop"
REMOTE="origin"

cd "$(git rev-parse --show-toplevel)"

echo "Laptop start check"
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

if [[ "$behind_count" == "0" ]]; then
  echo "You are already updated from main."
  exit 0
fi

echo
echo "Recent changes available from main:"
git log --date=short --pretty=format:"- %cd %h %s" HEAD.."$REMOTE/main" | head -n 12
echo

read -r -p "Merge these main changes into $TARGET_BRANCH now? [y/N] " answer
case "$answer" in
  y|Y|yes|YES)
    git merge --no-edit "$REMOTE/main"
    echo
    echo "$TARGET_BRANCH is now updated from main."
    ;;
  *)
    echo
    echo "Skipped update. Be careful: this branch is behind main."
    ;;
esac

