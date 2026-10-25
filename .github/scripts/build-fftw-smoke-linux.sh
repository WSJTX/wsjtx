#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "Usage: build-fftw-smoke-linux.sh OUTPUT PREFIX" >&2
  exit 2
fi

output=$1
prefix=$2
source_file="$(dirname "${BASH_SOURCE[0]}")/test_fftw_runtime.c"
if [ ! -f "$source_file" ]; then
  source_file="$(dirname "${BASH_SOURCE[0]}")/../../tests/dependencies/test_fftw_runtime.c"
fi
mkdir -p "$(dirname "$output")"
# CFLAGS and LDFLAGS contain compiler argument lists supplied by the image recipe.
# shellcheck disable=SC2086
${CC:-cc} ${CFLAGS:-} -I"$prefix/include" \
  "$source_file" \
  -L"$prefix/lib" ${LDFLAGS:-} -lfftw3f_threads -lfftw3f -lm -lpthread -o "$output"
