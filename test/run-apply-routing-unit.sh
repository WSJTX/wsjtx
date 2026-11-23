#!/usr/bin/env bash
# apply-routing unit regression: runs the driver test/apply_routing_test.f90,
# which ctest builds and runs as test_streaming_apply. Guards the APPLY side: each configure_fields member -> its CORRECT
# params_block slot, plus the order-sensitive logic (rxfreq/tx_audio_offset
# decouple, emedelay-before-per-mode-policy, nfa/nfb baseline restore, ntol
# per-mode cap/restore).
#
# WHY a Fortran unit driver: the cfg->params apply routing used to live in a
# nested INTERNAL subroutine (streaming_io.f90 apply_configure_), unreachable from
# a unit driver, AND most Phase 4/5 fields are inert on the FT8/JT9 fixtures, so a
# mis-route (e.g. his_call -> hisgrid) passed every fixture gate byte-identical.
# The mapping was extracted to the module-public
# streaming_apply::apply_configure_fields so this driver can assert it directly.
# This is the structural close of the apply-routing gap.
#
# Run by hand, not by ctest or CI (ctest runs the driver itself). The driver
# now needs most of the codec's Fortran library (streaming_apply uses
# msk144_stream, jt9_params_init uses packjt77_grammar), so it is built by the
# project's CMake build rather than compiled here from a few sources.
#
# Usage:
#   test/run-apply-routing-unit.sh <build dir>

set -euo pipefail

BUILD_DIR="${1:?usage: $0 <build dir>}"
cmake --build "$BUILD_DIR" --target test_streaming_apply
# ctest runs in the build directory: --test-dir needs CMake 3.20, the project 3.12.
(cd "$BUILD_DIR" && ctest -R '^test_streaming_apply$' --output-on-failure)
