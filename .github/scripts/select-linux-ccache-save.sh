#!/usr/bin/env bash
set -euo pipefail

# Base-branch caches are visible to pull requests. Keep publication limited to
# automatic develop builds and explicit maintainer requests on develop.
should_save_linux_ccache() {
  local event_name=$1
  local ref=$2
  local requested_save=$3
  local recache=$4

  if [ "$ref" != "refs/heads/develop" ]; then
    return 1
  fi

  case "$event_name" in
    push)
      return 0
      ;;
    workflow_dispatch)
      [ "$requested_save" = true ] || [ "$recache" = true ]
      ;;
    *)
      return 1
      ;;
  esac
}

expect_save() {
  if ! should_save_linux_ccache "$@"; then
    echo "self-test failed: expected cache save for $*" >&2
    exit 1
  fi
}

expect_no_save() {
  if should_save_linux_ccache "$@"; then
    echo "self-test failed: expected no cache save for $*" >&2
    exit 1
  fi
}

if [ "${1:-}" = --self-test ]; then
  expect_save push refs/heads/develop false false
  expect_save workflow_dispatch refs/heads/develop true false
  expect_save workflow_dispatch refs/heads/develop false true
  expect_save workflow_dispatch refs/heads/develop true true

  expect_no_save pull_request refs/pull/421/merge true true
  expect_no_save workflow_dispatch refs/heads/topic true false
  expect_no_save workflow_dispatch refs/heads/develop false false
  expect_no_save schedule refs/heads/develop true true

  echo "select-linux-ccache-save.sh self-test passed"
  exit 0
fi

save_ccache=false
if should_save_linux_ccache \
  "${EVENT_NAME:-}" \
  "${GITHUB_REF:-}" \
  "${REQUEST_SAVE_CCACHE:-false}" \
  "${RECACHE:-false}"; then
  save_ccache=true
fi

echo "Linux ccache save: $save_ccache"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
  echo "save_ccache=$save_ccache" >> "$GITHUB_OUTPUT"
fi
