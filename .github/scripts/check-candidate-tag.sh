#!/usr/bin/env bash

set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "Usage: check-candidate-tag.sh TAG EXPECTED_SHA" >&2
  exit 2
fi

tag=$1
expected_sha=$2
ref="refs/tags/$tag"

if git show-ref --verify --quiet "$ref"; then
  if ! current=$(git rev-parse --verify --quiet "$ref^{commit}"); then
    echo "::error::$tag does not identify a commit" >&2
    exit 1
  fi
  if [ "$current" != "$expected_sha" ]; then
    echo "::error::$tag already exists at $current; release tags are never moved" >&2
    exit 1
  fi
  printf 'exists=true\n'
  printf '%s already identifies %s; creation is idempotently satisfied.\n' "$tag" "$expected_sha" >&2
else
  status=$?
  if [ "$status" -ne 1 ]; then
    echo "::error::Could not inspect $ref (git show-ref exited $status)" >&2
    exit "$status"
  fi
  printf 'exists=false\n'
fi
