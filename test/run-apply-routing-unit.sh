#!/usr/bin/env bash
# Run the apply-routing unit against the production Fortran modules.
# Usage: test/run-apply-routing-unit.sh <build dir>

set -euo pipefail

BUILD_DIR="${1:?usage: $0 <build dir>}"
cmake --build "$BUILD_DIR" --target test_streaming_apply
# ctest runs in the build directory: --test-dir needs CMake 3.20, the project 3.12.
(cd "$BUILD_DIR" && ctest -R '^test_streaming_apply$' --output-on-failure)
