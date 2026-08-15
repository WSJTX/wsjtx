#!/usr/bin/env bash
# Decide whether CI should set CCACHE_RECACHE=1.
#
# Recache only when a changed path can invalidate compiled objects:
# compiler flags, top-level CMake, shared CMake modules, or the scripts
# that build Hamlib / pFUnit / other prefixes. Test CMakeLists, workflow
# YAML, and composite actions do not recache. workflow_dispatch still
# has an explicit recache input for unusual cases.
#
# Usage (CI):
#   EVENT_NAME=... PR_BASE_SHA=... BEFORE_SHA=... CURRENT_SHA=... \
#   FORCE_RECACHE=... .github/scripts/detect-ccache-recache.sh
#
# Usage (local):
#   .github/scripts/detect-ccache-recache.sh --self-test

set -euo pipefail

path_triggers_recache() {
  case "$1" in
    CMakeLists.txt|CMake/*|cmake/*|.github/scripts/build-*.sh)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

expect_trigger() {
  if ! path_triggers_recache "$1"; then
    echo "self-test failed: expected recache for $1" >&2
    exit 1
  fi
}

expect_no_trigger() {
  if path_triggers_recache "$1"; then
    echo "self-test failed: did not expect recache for $1" >&2
    exit 1
  fi
}

if [ "${1:-}" = "--self-test" ]; then
  expect_trigger "CMakeLists.txt"
  expect_trigger "CMake/CompilerFlags.cmake"
  expect_trigger "CMake/Utils.cmake"
  expect_trigger ".github/scripts/build-hamlib-linux.sh"
  expect_trigger ".github/scripts/build-pfunit-linux.sh"

  expect_no_trigger "tests/unit/jtty/CMakeLists.txt"
  expect_no_trigger "tests/CMakeLists.txt"
  expect_no_trigger "map65/CMakeLists.txt"
  expect_no_trigger "qmap/CMakeLists.txt"
  expect_no_trigger "tests/integration/jt9/compare_decoder_output.cmake"
  expect_no_trigger ".github/workflows/ci.yml"
  expect_no_trigger ".github/workflows/build-linux.yml"
  expect_no_trigger ".github/actions/build-linux-sanitizers/action.yml"
  expect_no_trigger ".github/actions/build-linux-payload/action.yml"
  expect_no_trigger "Logger.cpp"
  expect_no_trigger ""

  echo "detect-ccache-recache.sh self-test passed"
  exit 0
fi

if [ "${EVENT_NAME:-}" = "pull_request" ]; then
  base="${PR_BASE_SHA:-}"
  head="${CURRENT_SHA:-}"
elif [ -n "${BEFORE_SHA:-}" ] && [ "$BEFORE_SHA" != "0000000000000000000000000000000000000000" ]; then
  base="$BEFORE_SHA"
  head="${CURRENT_SHA:-}"
else
  base="${CURRENT_SHA:-}^"
  head="${CURRENT_SHA:-}"
fi

recache=false
reason=""

if ! git cat-file -e "${base}^{commit}" 2>/dev/null; then
  git fetch --no-tags --depth=1 origin "$base" || true
fi

if git cat-file -e "${base}^{commit}" 2>/dev/null && git cat-file -e "${head}^{commit}" 2>/dev/null; then
  changed="$(git diff --name-only "$base" "$head")"
else
  echo "::notice::Could not compare $base..$head; forcing ccache recache"
  changed=""
  recache=true
  reason="unreadable $base..$head"
fi

if [ "$recache" != "true" ]; then
  while IFS= read -r path; do
    [ -z "$path" ] && continue
    if path_triggers_recache "$path"; then
      recache=true
      reason="$path"
      break
    fi
  done <<< "$changed"
fi

if [ "${FORCE_RECACHE:-false}" = "true" ]; then
  recache=true
  reason="${reason:-workflow recache input}"
fi

echo "Build-system recache: $recache"
if [ "$recache" = "true" ] && [ -n "$reason" ]; then
  echo "::notice::ccache recache: $reason"
fi
if [ -n "${GITHUB_OUTPUT:-}" ]; then
  echo "recache=$recache" >> "$GITHUB_OUTPUT"
fi
