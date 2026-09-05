#!/usr/bin/env bash
# Dev-loop verification gate (Stage 4 of the CDH dev loop).
#
# Runs every check that the declared environment can execute and reports three
# outputs: what passed/failed, what was DEFERRED because this environment has no
# hardware for it, and what is UNVERIFIED (should have run here but did not).
# Deferred items are expected and do not count against the loop; unverified
# items do.
#
# Usage:  VERIFY_ENV=host scripts/verify.sh        # laptop, no board (default)
#         VERIFY_ENV=desk scripts/verify.sh        # board on USB, no radio rig
#         VERIFY_ENV=rig  scripts/verify.sh        # CI runner with probe + radio
#
# Runs from any checkout path (no `make`); uses the project venv interpreter.
set -u
cd "$(dirname "$0")/.."
ENV="${VERIFY_ENV:-host}"
PY=fprime-venv/bin/python3
[ -x "$PY" ] || PY=python3
UT=PROVESFlightControllerReference/test/unit-tests
INT=PROVESFlightControllerReference/test/int
fail=0
unverified=()
deferred=()

say() { printf '\n== %s\n' "$*"; }

say "environment: $ENV"
case "$ENV" in
  host) levels="Unit" ;;
  desk) levels="Unit Board(uart)" ;;
  rig)  levels="Unit Board(uart) Board(radio)" ;;
  *) echo "unknown VERIFY_ENV '$ENV' (host|desk|rig)"; exit 2 ;;
esac
echo "executable levels: $levels"

say "host unit tests"
if cmake -S "$UT" -B build-gtest >/dev/null 2>&1 \
   && cmake --build build-gtest > build-gtest/build.log 2>&1 \
   && ctest --test-dir build-gtest --output-junit junit.xml >/dev/null 2>&1; then
  for t in build-gtest/test_*; do [ -x "$t" ] && echo "  $(basename "$t"): $("$t" 2>&1 | grep -E '^\[  (PASSED|FAILED)' | tr -d '[]' | xargs)"; done
else
  echo "  host unit tests FAILED (see build-gtest/build.log)"; grep -E "error" build-gtest/build.log 2>/dev/null | head -5
  fail=1; unverified+=("host unit tests")
fi

say "integration tests: collect + lint (import/syntax only)"
if "$PY" -m pytest "$INT" --collect-only -q 2>&1 | tail -1 && "$PY" -m ruff check "$INT" 2>&1 | tail -1; then :; else fail=1; unverified+=("int test collection/lint"); fi

case "$ENV" in
  host)
    deferred+=("Board-level integration tests (no board in host env)")
    deferred+=("Zephyr target build (run from the clean-path copy; see CLAUDE.md)")
    ;;
  desk)
    say "integration tests on board (UART)"
    if [ -z "${UART_DEVICE:-}" ]; then echo "  UART_DEVICE not set"; fail=1; unverified+=("board int tests: UART_DEVICE unset"); else
      "$PY" -m pytest "$INT" --junitxml=build-gtest/int-junit.xml -q 2>&1 | tail -3 || { fail=1; unverified+=("board int tests"); }
    fi
    deferred+=("radio-only integration tests (no LoRa passthrough receiver in desk env)")
    ;;
  rig)
    say "integration tests on rig are run by CI jobs integration-uart / integration-radio"
    ;;
esac

say "pre-commit hooks (formatting, cpplint, codespell, ruff)"
# The git hook records the interpreter pre-commit was installed with; reuse it so
# this gate runs the same hooks the commit will.
HOOK_PY=$(sed -n 's/^INSTALL_PYTHON=//p' .git/hooks/pre-commit 2>/dev/null)
if [ -n "$HOOK_PY" ] && [ -x "$HOOK_PY" ]; then PC=("$HOOK_PY" -mpre_commit)
elif command -v pre-commit >/dev/null 2>&1; then PC=(pre-commit)
else PC=(); fi
if [ ${#PC[@]} -gt 0 ]; then
  if "${PC[@]}" run --all-files > build-gtest/pre-commit.log 2>&1; then echo "  all hooks passed"
  else echo "  hooks FAILED:"; grep -E "Failed|^[A-Za-z].*:[0-9]+:" build-gtest/pre-commit.log | head -20 | sed 's/^/    /'; fail=1; unverified+=("pre-commit hooks"); fi
else echo "  pre-commit not available; hooks will run at commit time"; unverified+=("pre-commit hooks (not installed here)"); fi

say "requirements traceability matrix"
"$PY" scripts/generate_rtm.py --junit build-gtest/junit.xml --env "$ENV" 2> build-gtest/rtm-warnings.txt
w=$(grep -c . build-gtest/rtm-warnings.txt || true)
grep -E "linked to automated" docs-site/requirements-matrix.md | head -1
if [ "$w" != "0" ]; then echo "  RTM warnings ($w):"; sed 's/^/    /' build-gtest/rtm-warnings.txt; fail=1; unverified+=("RTM warnings: $w"); fi

say "summary"
echo "  result:     $([ $fail -eq 0 ] && echo PASS || echo FAIL)"
echo "  deferred (expected in env '$ENV', not counted):"
for d in "${deferred[@]:-}"; do [ -n "$d" ] && echo "    - $d"; done
echo "  unverified (counts against the loop):"
if [ ${#unverified[@]} -eq 0 ]; then echo "    (none)"; else for u in "${unverified[@]}"; do echo "    - $u"; done; fi
exit $fail
