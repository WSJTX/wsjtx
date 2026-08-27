#!/usr/bin/env bash
set -euo pipefail

# GitHub cache entries cannot be replaced. Per-attempt refresh keys let a
# forced rebuild supersede an older archive for the same SHA through prefix
# restoration without duplicating caches for ordinary reruns.
cache_key_suffix() {
  local recache=$1
  local run_id=$2
  local run_attempt=$3

  case "$recache" in
    false)
      return 0
      ;;
    true)
      if [ -z "$run_id" ] || [ -z "$run_attempt" ]; then
        echo "GITHUB_RUN_ID and GITHUB_RUN_ATTEMPT are required for a ccache refresh key" >&2
        return 2
      fi
      printf '%s' "-refresh-${run_id}-${run_attempt}"
      ;;
    *)
      echo "RECACHE must be true or false" >&2
      return 2
      ;;
  esac
}

if [ "${1:-}" = --self-test ]; then
  test -z "$(cache_key_suffix false 123 1)"
  test -z "$(cache_key_suffix false '' '')"
  test "$(cache_key_suffix true 123 1)" = "-refresh-123-1"
  test "$(cache_key_suffix true 123 2)" = "-refresh-123-2"
  if cache_key_suffix true "" 1 >/dev/null 2>&1; then
    echo "self-test failed: empty run ID was accepted" >&2
    exit 1
  fi
  if cache_key_suffix invalid 123 1 >/dev/null 2>&1; then
    echo "self-test failed: invalid recache value was accepted" >&2
    exit 1
  fi
  echo "select-ccache-key-suffix.sh self-test passed"
  exit 0
fi

suffix="$(cache_key_suffix \
  "${RECACHE:-false}" \
  "${GITHUB_RUN_ID:-}" \
  "${GITHUB_RUN_ATTEMPT:-}")"

echo "ccache key suffix: ${suffix:-<none>}"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
  echo "suffix=$suffix" >> "$GITHUB_OUTPUT"
fi
