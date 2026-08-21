#!/usr/bin/env bash
# Local soak for overlapping GUI live-audio smokes. Not used by CI.
#
# --smoke checks the harness only (no GUI tests). Use that before a long soak.
set -u
set -o pipefail

usage () {
  cat <<'EOF'
Usage: run-live-audio-parallel-experiment.sh --build-dir DIR [options]

  --phase 1|2|3|all     Default: all
  --repeats-soak N      Full-suite passes per soak (serial then -j3). Default: 20
  --repeats-pair N      Full-suite passes per pairwise set. Default: 10
  --smoke               Validate the script and ctest listing; do not run GUI tests
EOF
}

build_dir=""
phase="all"
repeats_soak=20
repeats_pair=10
smoke=0
live_re='test_wsjtx_live_audio_ft8|test_wsjtx_live_audio_jtty|test_wsjtx_jtty_tx_loopback'
xvfb=(xvfb-run -a -s '-screen 0 1280x1024x24')
failures=0
ran=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) build_dir=$2; shift 2 ;;
    --phase) phase=$2; shift 2 ;;
    --repeats-soak) repeats_soak=$2; shift 2 ;;
    --repeats-pair) repeats_pair=$2; shift 2 ;;
    --smoke) smoke=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ -z "${build_dir}" || ! -d "${build_dir}" ]]; then
  echo "--build-dir must be an existing CMake build directory" >&2
  exit 2
fi

if ! command -v xvfb-run >/dev/null 2>&1; then
  echo "xvfb-run is required" >&2
  exit 2
fi

if ! command -v ctest >/dev/null 2>&1; then
  echo "ctest is required" >&2
  exit 2
fi

listed=$(ctest --test-dir "${build_dir}" -N -R "${live_re}" | awk '/Test #/{print $3}')
expected=$'test_wsjtx_live_audio_ft8\ntest_wsjtx_live_audio_jtty\ntest_wsjtx_jtty_tx_loopback'
if [[ "${listed}" != "${expected}" ]]; then
  echo "ctest did not list the three live-audio tests:" >&2
  echo "${listed}" >&2
  exit 2
fi

if [[ "${smoke}" -eq 1 ]]; then
  echo "smoke: xvfb-run and ctest listing ok"
  echo "smoke: tests:"
  printf '  %s\n' ${listed}
  echo "smoke: would run phase=${phase} soak=${repeats_soak} pair=${repeats_pair}"
  echo "======== 3 load start/stop ========"
  pids=()
  for ((i = 0; i < 2; i++)); do
    sleep 30 &
    pids+=($!)
  done
  kill "${pids[@]}" 2>/dev/null || true
  wait "${pids[@]}" 2>/dev/null || true
  echo "smoke: harness ok"
  exit 0
fi

run_ctest () {
  local label=$1
  shift
  echo
  echo "======== ${label} ========"
  printf '+'
  printf ' %q' "${xvfb[@]}" ctest --test-dir "${build_dir}" "$@"
  echo
  ran=$((ran + 1))
  if "${xvfb[@]}" ctest --test-dir "${build_dir}" "$@"; then
    echo "RESULT ${label}: pass"
  else
    echo "RESULT ${label}: fail (exit $?)"
    failures=$((failures + 1))
  fi
}

# Repeat whole matching suites N times. Do not use --repeat-until-fail:
# one flaky FT8 case must not skip later phases or later passes.
run_suite_repeats () {
  local label=$1
  local n=$2
  shift 2
  local i
  for ((i = 1; i <= n; i++)); do
    run_ctest "${label} pass ${i}/${n}" "$@"
  done
}

phase1 () {
  run_suite_repeats "1 serial" "${repeats_soak}" \
    -R "${live_re}" --output-on-failure
  run_suite_repeats "1 parallel -j3" "${repeats_soak}" \
    -j3 -R "${live_re}" --output-on-failure
}

phase2 () {
  run_suite_repeats "2 FT8 live || JTTY live" "${repeats_pair}" \
    -j2 -R 'test_wsjtx_live_audio_ft8|test_wsjtx_live_audio_jtty' \
    --output-on-failure
  run_suite_repeats "2 FT8 live || JTTY TX loopback" "${repeats_pair}" \
    -j2 -R 'test_wsjtx_live_audio_ft8|test_wsjtx_jtty_tx_loopback' \
    --output-on-failure
  run_suite_repeats "2 JTTY live || JTTY TX loopback" "${repeats_pair}" \
    -j2 -R 'test_wsjtx_live_audio_jtty|test_wsjtx_jtty_tx_loopback' \
    --output-on-failure
  run_suite_repeats "2 all three -j3" "${repeats_pair}" \
    -j3 -R "${live_re}" --output-on-failure
}

phase3 () {
  echo
  echo "======== 3 contended: background gzip load ========"
  local pids=()
  local i
  for ((i = 0; i < 4; i++)); do
    (while true; do dd if=/dev/zero bs=1M count=64 2>/dev/null | gzip -1 >/dev/null; done) &
    pids+=($!)
  done
  run_suite_repeats "3 parallel -j3 under load" "${repeats_pair}" \
    -j3 -R "${live_re}" --output-on-failure
  kill "${pids[@]}" 2>/dev/null || true
  wait "${pids[@]}" 2>/dev/null || true
}

case "${phase}" in
  1) phase1 ;;
  2) phase2 ;;
  3) phase3 ;;
  all) phase1; phase2; phase3 ;;
  *) echo "phase must be 1, 2, 3, or all" >&2; exit 2 ;;
esac

echo
echo "======== summary ========"
echo "suites_run=${ran} suites_failed=${failures}"
if [[ "${failures}" -ne 0 ]]; then
  exit 1
fi
exit 0
