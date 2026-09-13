#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: linux-ci-image-fingerprint.sh normal-noble|normal-bookworm|tsan-noble|armhf-cross-bookworm|armhf-runtime-bookworm" >&2
  exit 2
fi

profile=$1
. .github/scripts/linux-ci-image-inputs.sh
files=()
inputs="$(linux_ci_image_inputs "$profile")"
while IFS= read -r file; do
  files+=("$file")
done <<< "$inputs"

sha256sum "${files[@]}" | sha256sum | awk '{print $1}'
