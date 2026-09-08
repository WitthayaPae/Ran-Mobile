#!/bin/bash
#  Push SOURCE's port branch to the branch the iOS workflow compiles.
#
#  It cannot simply be pushed. mobile-port/effects-resolution-and-text carries
#  49 Visual Studio IntelliSense files - a 186 MB Browse.VC.db and a dozen
#  174 MB .ipch - and GitHub refuses any blob over 100 MB, so the push is
#  rejected outright. ci/ios-source is the same working tree squashed onto main
#  without them.
#
#  Doing this by hand went wrong once in the obvious way: "git checkout <port>
#  -- ." restores the port branch's .gitignore too, which does NOT ignore .vs/,
#  so the caches came straight back into the index. The order below is the
#  point of the script.
#
#      ./tools/sync-ci-source.sh
#
#  Run it after every SOURCE change, or the runner compiles the old file.
set -e
SRC="$(cd "$(dirname "$0")/../../SOURCE" && pwd)"
PORT=mobile-port/effects-resolution-and-text
CI=ci/ios-source
cd "$SRC"

[ -z "$(git status --porcelain)" ] || { echo "SOURCE has uncommitted changes - commit them on $PORT first"; exit 1; }

BACK="$(git rev-parse --abbrev-ref HEAD)"
git checkout -q "$CI"
git reset -q --hard origin/"$CI"
git checkout -q "$PORT" -- .

#  The .gitignore that came with the tree does not know about .vs/. Say so
#  again, then drop the caches from the index - in that order.
grep -q '^\.vs/$' .gitignore || printf '\n# Visual Studio IntelliSense caches: Browse.VC.db and the .ipch files are\n# 150-190 MB each, over the 100 MB GitHub refuses outright.\n.vs/\n' >> .gitignore
git rm -r --cached .vs -q 2>/dev/null || true
git add -A

LEFT=$(git diff --cached --name-only | grep -c '^\.vs/' || true)
[ "$LEFT" = 0 ] || { echo "still $LEFT .vs files staged - refusing to push"; git checkout -q "$BACK"; exit 1; }

if git diff --cached --quiet; then
  echo "nothing to sync"
else
  git commit -q -m "${1:-Sync the port branch engine changes for the iOS build}"
  git push -q origin "$CI"
  echo "pushed $(git rev-parse --short HEAD) to $CI"
fi
git checkout -q "$BACK"
echo "back on $BACK"
