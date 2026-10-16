#!/usr/bin/env bash
# Validate a `ctest -V` log from the linux job.
#
# Kept as a script rather than inline YAML because it is the guard that decides
# whether the test suite actually ran: it has to be runnable and testable
# outside CI. See .github/scripts/README.md.
#
# Usage: doctest-summary.sh <ctest.log> [min-test-cases]
#
# Exits non-zero with an ::error:: annotation on any of:
#   - no doctest summary line (the binary never ran, or ctest aborted)
#   - unparseable pass/fail counts (a failure or skip changes the line's shape)
#   - any failing test case
#   - fewer than MIN_TEST_CASES passing
#
# MIN_TEST_CASES is a floor, not an exact count, on purpose: pinning the exact
# number turns every honest test addition into a red build, and a guard that
# cries wolf gets rubber-stamped. Raise it as tests are added; lowering it means
# tests were deliberately removed and should be justified in the commit.

set -euo pipefail

LOG="${1:?usage: doctest-summary.sh <ctest.log> [min-test-cases]}"
MIN_TEST_CASES="${2:-75}"

if [ ! -f "$LOG" ]; then
  echo "::error::ctest log not found: $LOG"
  exit 1
fi

# doctest right-aligns each count into a fixed-width column and colours it when
# it thinks it is on a terminal, so normalise the log before parsing instead of
# depending on the exact spacing.
summary="$(sed -e 's/\x1b\[[0-9;]*m//g' -e 's/  */ /g' "$LOG" \
  | grep -m1 'test cases:' || true)"
if [ -z "$summary" ]; then
  echo "::error::no doctest summary line in $LOG — did the test binary run?"
  exit 1
fi
echo "doctest summary: $summary"

# Pull the "N passed" and "N failed" fields out. awk with a whitespace-and-pipe
# field split avoids relying on grep's handling of a space-quantifier, which is
# not portable across grep builds (GNU grep here rejected 'test cases: +[0-9]+').
counts="$(printf '%s\n' "$summary" \
  | awk -F'[ |]+' '/test cases:/ { p=""; f=""; for (i = 1; i <= NF; i++) { if ($i == "passed") p = $(i - 1); if ($i == "failed") f = $(i - 1) } print p, f; exit }')"
passed="${counts%% *}"
failed="${counts##* }"
if [ -z "$passed" ] || [ -z "$failed" ]; then
  echo "::error::could not parse the pass/fail counts out of: $summary"
  echo "::error::every case must pass — a failure or skip changes this line's shape"
  exit 1
fi
if [ "$failed" -ne 0 ]; then
  echo "::error::doctest reported $failed failing test case(s)"
  exit 1
fi
if [ "$passed" -lt "$MIN_TEST_CASES" ]; then
  echo "::error::only $passed test cases passed, expected at least $MIN_TEST_CASES"
  echo "::error::a whole test TU may have been dropped from tests/CMakeLists.txt"
  echo "::error::if tests were intentionally removed, lower MIN_TEST_CASES"
  exit 1
fi

echo "OK: $passed test cases passed (floor $MIN_TEST_CASES)"
