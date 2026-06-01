#!/usr/bin/env bash
# Launch a Windows executable with the MinGW runtime directory removed from
# PATH and assert it does NOT fail with STATUS_DLL_NOT_FOUND (0xC0000135).
#
# This reproduces the failure class behind "map65.exe won't start": a missing
# runtime DLL (libgomp-1.dll, libportaudio-2.dll, libgfortran-5.dll, ...) makes
# the process exit at load time, before main(), with exit code 0xC0000135. A
# build that links and packages correctly survives a launch from a clean PATH;
# one that relies on the dev shell's /mingw64/bin does not.
#
# GUI apps (wsjtx.exe) never return on their own, so we launch with a bounded
# timeout: surviving past the timeout means every DLL resolved and the process
# reached its event loop — that is the success signal. We then terminate it.
# CLI apps that exit on their own are judged purely by exit code.
#
# Usage: smoke-launch-windows.sh <exe> [args...]
set -u

# 0xC0000135 as a bash exit status. Windows returns the 32-bit NTSTATUS; MSYS2
# bash surfaces it as 3221225781 (unsigned) for processes that exit on their
# own. The 128+signal form does not apply here — this is a real process exit.
readonly STATUS_DLL_NOT_FOUND=3221225781
readonly TIMEOUT_SECS=20

exe="${1:?usage: smoke-launch-windows.sh <exe> [args...]}"
shift || true

if [ ! -f "$exe" ]; then
  echo "::error::smoke-launch: executable not found: $exe"
  exit 1
fi

# Strip the MinGW runtime dir from PATH so only DLLs the build/packaging step
# placed beside the exe (or in still-present system dirs) can resolve.
cleaned_path=$(echo "$PATH" | tr ':' '\n' | grep -ivE '(^|/)mingw64/bin/?$' | paste -sd: -)
echo "smoke-launch: $exe $* (PATH cleaned of /mingw64/bin)"

# timeout returns 124 when it had to kill the process — for a GUI app that is
# exactly the "still alive, DLLs resolved" success case.
PATH="$cleaned_path" timeout "${TIMEOUT_SECS}s" "$exe" "$@" >launch.out 2>&1
code=$?

if [ "$code" -eq "$STATUS_DLL_NOT_FOUND" ]; then
  echo "::error::smoke-launch: $exe exited 0xC0000135 (STATUS_DLL_NOT_FOUND) — a runtime DLL is missing from the clean-PATH launch context"
  echo "----- output -----"; cat launch.out || true
  exit 1
fi

if [ "$code" -eq 124 ]; then
  echo "smoke-launch OK: $exe survived ${TIMEOUT_SECS}s (all DLLs resolved; GUI event loop reached), terminated by timeout"
  exit 0
fi

if [ "$code" -eq 0 ]; then
  echo "smoke-launch OK: $exe loaded and exited cleanly"
  exit 0
fi

# Any other non-zero code is a real fault (a missing DLL is the thing we guard
# against; a usage/help non-zero exit would also surface here, so print it).
echo "::error::smoke-launch: $exe exited with code $code"
echo "----- output -----"; cat launch.out || true
exit 1
