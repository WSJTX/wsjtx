#!/usr/bin/env bash
set -euo pipefail

# linuxdeploy's ldd probe crashes when an ARM loader is nested under user-mode
# QEMU. Resolve DT_NEEDED entries from the clean ARMHF runtime without executing
# the target; packaged runtime behavior is still proved by the QEMU smoke test.

if [ "$#" -ne 1 ]; then
  echo "Usage: armhf-static-ldd.sh ELF" >&2
  exit 2
fi

target=$1
runtime_prefix=${WSJT_ARMHF_RUNTIME_PREFIX:-/opt/wsjtx/armhf-runtime}
search_path=${ARMHF_STATIC_LDD_LIBRARY_PATH:-}

base_search_directories=("$runtime_prefix")
if [ -n "$search_path" ]; then
  IFS=: read -r -a configured_directories <<< "$search_path"
  base_search_directories+=("${configured_directories[@]}")
fi

status=0
declare -A resolved_libraries=()

resolve_dependencies() {
  local elf=$1 dynamic origin line directory library resolved metadata
  local -a needed=() encoded_directories=() search_directories=()

  dynamic="$(readelf -dW -- "$elf")" || exit
  origin="$(dirname -- "$elf")"
  while IFS= read -r line; do
    if [[ "$line" =~ \(NEEDED\).*Shared[[:space:]]library:[[:space:]]\[([^]]+)\] ]]; then
      needed+=("${BASH_REMATCH[1]}")
    elif [[ "$line" =~ \((RUNPATH|RPATH)\).*Library[[:space:]](runpath|rpath):[[:space:]]\[([^]]+)\] ]]; then
      IFS=: read -r -a encoded <<< "${BASH_REMATCH[3]}"
      for directory in "${encoded[@]}"; do
        # shellcheck disable=SC2016 # expand the loader's literal ORIGIN token
        directory=${directory//'${ORIGIN}'/$origin}
        # shellcheck disable=SC2016 # expand the loader's literal ORIGIN token
        directory=${directory//'$ORIGIN'/$origin}
        encoded_directories+=("$directory")
      done
    fi
  done <<< "$dynamic"
  search_directories=(
    "${base_search_directories[@]}"
    "${encoded_directories[@]}"
    /lib/arm-linux-gnueabihf
    /usr/lib/arm-linux-gnueabihf
    /lib
    /usr/lib
  )

  for library in "${needed[@]}"; do
    if [[ "$library" == libquadmath.so* ]]; then
      echo "$elf unexpectedly depends on unavailable ARMHF runtime $library" >&2
      exit 1
    fi
    if [ -n "${resolved_libraries[$library]:-}" ]; then
      continue
    fi

    resolved=
    if [[ "$library" == */* ]] && [ -f "$library" ]; then
      resolved=$library
    else
      for directory in "${search_directories[@]}"; do
        if [ -f "$directory/$library" ]; then
          resolved=$directory/$library
          break
        fi
      done
    fi

    if [ -z "$resolved" ]; then
      printf '%s => not found (0x00000000)\n' "$library"
      status=1
      continue
    fi

    metadata="$(file -L -- "$resolved")" || exit
    case "$metadata" in
      *"ELF 32-bit"*"ARM, EABI5"*) ;;
      *)
        echo "$elf resolves $library to a non-ARMHF ELF: $metadata" >&2
        exit 1
        ;;
    esac
    resolved_libraries[$library]=$resolved
    printf '%s => %s (0x00000000)\n' "$library" "$resolved"
    resolve_dependencies "$resolved"
  done
}

resolve_dependencies "$target"

exit "$status"
