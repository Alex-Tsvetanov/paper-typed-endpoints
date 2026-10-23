#!/usr/bin/env bash
# Development builds only (no timing). Runs inside papers-dev.
set -u
for p in "$@"; do
  echo "=== $p"
  cmake --preset "$p" 2>&1 | grep -E "tep:|Error"
  cmake --build "build/$p" -j4 2>&1 | grep -E "error|warning|FAILED" | head -30
  ctest --test-dir "build/$p" --output-on-failure 2>&1 | sed -E 's/ +[0-9.]+ sec//; s/Total Test time.*//' | grep -vE "^\s*Start |^$" | tail -16
done
