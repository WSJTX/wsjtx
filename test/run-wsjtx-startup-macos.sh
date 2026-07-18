#!/usr/bin/env bash
set -u

readonly DEFAULT_TIMEOUT_SECS=15
readonly SAMPLE_DURATION_SECS=3
readonly SAMPLE_INTERVAL_MSECS=10

diagnostics_dir="${1:?usage: run-wsjtx-startup-macos.sh <diagnostics-dir> <executable> [args...]}"
shift
executable="${1:?usage: run-wsjtx-startup-macos.sh <diagnostics-dir> <executable> [args...]}"
timeout_secs="${WSJTX_STARTUP_SMOKE_TIMEOUT_SECS:-$DEFAULT_TIMEOUT_SECS}"

case "$timeout_secs" in
  ''|*[!0-9]*)
    echo "startup smoke: timeout must be a positive integer: $timeout_secs"
    exit 2
    ;;
esac
if [ "$timeout_secs" -eq 0 ]; then
  echo "startup smoke: timeout must be greater than zero"
  exit 2
fi
if [ ! -x "$executable" ]; then
  echo "startup smoke: executable not found or not executable: $executable"
  exit 2
fi

if ! mkdir -p "$diagnostics_dir"; then
  echo "startup smoke: unable to create diagnostics directory: $diagnostics_dir"
  exit 2
fi
output_log="$diagnostics_dir/startup-smoke.log"
summary="$diagnostics_dir/diagnostic-summary.txt"
sample_log="$diagnostics_dir/hang-sample.txt"
sample_command_log="$diagnostics_dir/sample-command.log"
launch_marker="$diagnostics_dir/launch-start.marker"
: >"$output_log"
: >"$launch_marker"

copy_new_crash_reports ()
{
  if [ -z "${HOME:-}" ]; then
    return
  fi
  report_dir="$HOME/Library/Logs/DiagnosticReports"
  if [ ! -d "$report_dir" ]; then
    return
  fi
  for report in \
    "$report_dir"/wsjtx*.ips \
    "$report_dir"/wsjtx*.crash \
    "$report_dir"/WSJT-X*.ips \
    "$report_dir"/WSJT-X*.crash
  do
    if [ -e "$report" ] && [ "$report" -nt "$launch_marker" ]; then
      cp "$report" "$diagnostics_dir/"
    fi
  done
}

print_failure_diagnostics ()
{
  echo "----- diagnostic summary -----"
  cat "$summary" 2>/dev/null || true
  echo "----- startup smoke output -----"
  cat "$output_log" 2>/dev/null || true
  if [ -s "$sample_command_log" ]; then
    echo "----- sample command output -----"
    cat "$sample_command_log"
  fi
}

echo "startup smoke: $executable ${*:2}"
"$@" >"$output_log" 2>&1 &
test_pid=$!
start_time=$SECONDS
timed_out=false

while kill -0 "$test_pid" 2>/dev/null; do
  if [ $((SECONDS - start_time)) -ge "$timeout_secs" ]; then
    timed_out=true
    break
  fi
  sleep 0.25
done

if [ "$timed_out" = true ]; then
  {
    echo "result=timeout"
    echo "pid=$test_pid"
    echo "timeout_seconds=$timeout_secs"
    echo "executable=$executable"
  } >"$summary"
  /usr/bin/sample "$test_pid" "$SAMPLE_DURATION_SECS" "$SAMPLE_INTERVAL_MSECS" \
    -mayDie -file "$sample_log" >"$sample_command_log" 2>&1 || true
  kill -TERM "$test_pid" 2>/dev/null || true
  terminate_attempts=0
  while [ "$terminate_attempts" -lt 8 ]; do
    if ! kill -0 "$test_pid" 2>/dev/null; then
      break
    fi
    sleep 0.25
    terminate_attempts=$((terminate_attempts + 1))
  done
  if kill -0 "$test_pid" 2>/dev/null; then
    kill -KILL "$test_pid" 2>/dev/null || true
  fi
  wait "$test_pid" 2>/dev/null || true
  copy_new_crash_reports
  print_failure_diagnostics
  exit 124
fi

wait "$test_pid"
status=$?
if [ "$status" -ne 0 ]; then
  sleep 2
  copy_new_crash_reports
  {
    echo "result=exit"
    echo "pid=$test_pid"
    echo "exit_status=$status"
    echo "executable=$executable"
  } >"$summary"
  print_failure_diagnostics
  exit "$status"
fi

cat "$output_log"
rm -f "$output_log" "$launch_marker"
exit 0
