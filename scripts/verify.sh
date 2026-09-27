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
# Knobs (used by the script tests in scripts/tests; a gate run sets neither):
#   VERIFY_BUILD_DIR  build directory every stage reads and writes (build-gtest)
#   VERIFY_STAGES     comma list drawn from
#                     host-tests,int-collect,audit,script-tests,pre-commit,rtm;
#                     a partial run is labelled and is not a gate result
#
# Runs from any checkout path (no `make`); uses the project venv interpreter.
set -u
cd "$(dirname "$0")/.."

# The script-tests stage runs pytest with VERIFY_NESTED=1, and those tests run
# this gate again with an explicit VERIFY_STAGES. A nested run without one would
# re-enter pytest and recurse, so refuse it.
if [ -n "${VERIFY_NESTED:-}" ] && [ -z "${VERIFY_STAGES:-}" ]; then
  echo "nested verify.sh without VERIFY_STAGES"; exit 2
fi

ENV="${VERIFY_ENV:-host}"
PY=fprime-venv/bin/python3
[ -x "$PY" ] || PY=python3
UT=PROVESFlightControllerReference/test/unit-tests
INT=PROVESFlightControllerReference/test/int
BUILD="${VERIFY_BUILD_DIR:-build-gtest}"
ALL_STAGES="host-tests,int-collect,audit,script-tests,pre-commit,rtm"
STAGES="${VERIFY_STAGES:-$ALL_STAGES}"
PARTIAL="${VERIFY_STAGES:+1}"
fail=0
unverified=()
deferred=()

say() { printf '\n== %s\n' "$*"; }
stage() { case ",$STAGES," in *",$1,"*) return 0;; *) return 1;; esac; }

mkdir -p "$BUILD"

say "environment: $ENV"
case "$ENV" in
  host) levels="Unit" ;;
  desk) levels="Unit Board(uart)" ;;
  rig)  levels="Unit Board(uart) Board(radio)" ;;
  *) echo "unknown VERIFY_ENV '$ENV' (host|desk|rig)"; exit 2 ;;
esac
echo "executable levels: $levels"

if stage host-tests; then
  say "host unit tests"
  # Always from an empty build directory: make rebuilds by mtime, so a source
  # restored with an mtime older than its object is otherwise never recompiled.
  rm -rf "$BUILD"
  if cmake -S "$UT" -B "$BUILD" >/dev/null 2>&1 \
     && cmake --build "$BUILD" > "$BUILD/build.log" 2>&1 \
     && ctest --test-dir "$BUILD" --output-junit junit.xml >/dev/null 2>&1; then
    for t in "$BUILD"/test_*; do [ -x "$t" ] && echo "  $(basename "$t"): $("$t" 2>&1 | grep -E '^\[  (PASSED|FAILED)' | tr -d '[]' | xargs)"; done
  else
    echo "  host unit tests FAILED (see $BUILD/build.log)"; grep -E "error" "$BUILD/build.log" 2>/dev/null | head -5
    fail=1; unverified+=("host unit tests")
  fi
fi

if stage int-collect; then
  say "integration tests: collect + lint (import/syntax only)"
  if "$PY" -m pytest "$INT" --collect-only -q 2>&1 | tail -1 && "$PY" -m ruff check "$INT" 2>&1 | tail -1; then :; else fail=1; unverified+=("int test collection/lint"); fi
fi

if stage audit; then
  say "capacity audit (scripts/check_capacity.py)"
  # Every fixed table size the tree can outgrow: the packetizer and dispatcher
  # tables assert at boot, the rate-group / TaskGate / FaultIn arrays are loud
  # only at a target build this fork's CI never runs, the fault mask is silent.
  aud_out=$("$PY" scripts/check_capacity.py 2>&1); aud_rc=$?
  printf '%s\n' "$aud_out" | sed 's/^/  /'
  if [ "$aud_rc" -eq 1 ]; then fail=1; unverified+=("capacity audit"); fi
  if [ "$aud_rc" -eq 2 ]; then fail=1; unverified+=("capacity audit: input error"); fi
  aud_skips=$(printf '%s\n' "$aud_out" | sed -n 's/^\([A-Za-z_][A-Za-z0-9_]*\): ?\/.*/\1/p')
  if [ -n "$aud_skips" ]; then
    while IFS= read -r name; do
      deferred+=("capacity audit: $name (needs a target-build dictionary)")
    done <<< "$aud_skips"
  fi
fi

if stage script-tests; then
  say "script tests (scripts/tests)"
  # VERIFY_NESTED stops a test that runs this gate from re-entering pytest.
  if VERIFY_NESTED=1 "$PY" -m pytest scripts/tests -q --junitxml="$BUILD/scripts-junit.xml" > "$BUILD/script-tests.log" 2>&1; then
    tail -1 "$BUILD/script-tests.log" | sed 's/^/  /'
  else
    echo "  script tests FAILED (see $BUILD/script-tests.log):"
    grep -E "^(FAILED|ERROR)" "$BUILD/script-tests.log" | head -10 | sed 's/^/    /'
    tail -1 "$BUILD/script-tests.log" | sed 's/^/  /'
    fail=1; unverified+=("script tests")
  fi
fi

case "$ENV" in
  host)
    deferred+=("Board-level integration tests (no board in host env)")
    deferred+=("Zephyr target build (run from the clean-path copy; see CLAUDE.md)")
    ;;
  desk)
    say "integration tests on board (UART)"
    if [ -z "${UART_DEVICE:-}" ]; then echo "  UART_DEVICE not set"; fail=1; unverified+=("board int tests: UART_DEVICE unset"); else
      "$PY" -m pytest "$INT" --junitxml="$BUILD/int-junit.xml" -q 2>&1 | tail -3 || { fail=1; unverified+=("board int tests"); }
    fi
    deferred+=("radio-only integration tests (no LoRa passthrough receiver in desk env)")
    ;;
  rig)
    say "integration tests on rig are run by CI jobs integration-uart / integration-radio"
    ;;
esac

if stage pre-commit; then
  say "pre-commit hooks (formatting, cpplint, codespell, ruff)"
  # The git hook records the interpreter pre-commit was installed with; reuse it so
  # this gate runs the same hooks the commit will.
  HOOK_PY=$(sed -n 's/^INSTALL_PYTHON=//p' "$(git rev-parse --git-path hooks/pre-commit)" 2>/dev/null)
  if [ -n "$HOOK_PY" ] && [ -x "$HOOK_PY" ]; then PC=("$HOOK_PY" -mpre_commit)
  elif command -v pre-commit >/dev/null 2>&1; then PC=(pre-commit)
  else PC=(); fi
  if [ ${#PC[@]} -gt 0 ]; then
    # --all-files only visits tracked files; run untracked sources through the hooks too.
    NEWF=$(git ls-files --others --exclude-standard | grep -vE "\.(pdf|html)$" || true)
    if "${PC[@]}" run --all-files > "$BUILD/pre-commit.log" 2>&1 && { [ -z "$NEWF" ] || "${PC[@]}" run --files $NEWF >> "$BUILD/pre-commit.log" 2>&1; }; then echo "  all hooks passed"
    else echo "  hooks FAILED:"; grep -E "Failed|^[A-Za-z].*:[0-9]+:" "$BUILD/pre-commit.log" | head -20 | sed 's/^/    /'; fail=1; unverified+=("pre-commit hooks"); fi
  else echo "  pre-commit not available; hooks will run at commit time"; unverified+=("pre-commit hooks (not installed here)"); fi
fi

if stage rtm; then
  say "requirements traceability matrix"
  "$PY" scripts/generate_rtm.py --junit "$BUILD/junit.xml" --script-junit "$BUILD/scripts-junit.xml" --env "$ENV" 2> "$BUILD/rtm-warnings.txt"
  w=$(grep -c . "$BUILD/rtm-warnings.txt" || true)
  grep -E "linked to automated" docs-site/requirements-matrix.md | head -1
  if [ "$w" != "0" ]; then echo "  RTM warnings ($w):"; sed 's/^/    /' "$BUILD/rtm-warnings.txt"; fail=1; unverified+=("RTM warnings: $w"); fi
fi

say "summary"
[ -n "$PARTIAL" ] && echo "  stages: $STAGES (partial run — not a gate result)"
echo "  result:     $([ $fail -eq 0 ] && echo PASS || echo FAIL)"
echo "  deferred (expected in env '$ENV', not counted):"
for d in "${deferred[@]:-}"; do [ -n "$d" ] && echo "    - $d"; done
echo "  unverified (counts against the loop):"
if [ ${#unverified[@]} -eq 0 ]; then echo "    (none)"; else for u in "${unverified[@]}"; do echo "    - $u"; done; fi
exit $fail
