#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: linux-ccache-compiler-signature.sh C_COMPILER" >&2
  exit 2
fi

c_compiler=$1
case "$(basename "$c_compiler")" in
  gcc)
    cxx_compiler=g++
    ;;
  gcc-*)
    cxx_compiler="g++-${c_compiler##*-}"
    ;;
  *-gcc)
    cxx_compiler="${c_compiler%-gcc}-g++"
    ;;
  *)
    echo "Unsupported C compiler for Linux ccache identity: $c_compiler" >&2
    exit 2
    ;;
esac

resolve_executable() {
  local executable=$1 resolved
  if [[ "$executable" == */* ]]; then
    resolved=$executable
  else
    resolved="$(command -v "$executable" || true)"
  fi
  if [ -z "$resolved" ] || [ ! -f "$resolved" ]; then
    echo "Compiler component not found: $executable" >&2
    exit 1
  fi
  readlink -f "$resolved"
}

hash_file() {
  sha256sum "$1" | awk '{print $1}'
}

c_path="$(resolve_executable "$(command -v "$c_compiler")")"
cxx_path="$(resolve_executable "$(command -v "$cxx_compiler")")"
compiler_version="$($c_compiler -dumpfullversion -dumpversion)"
cxx_version="$($cxx_compiler -dumpfullversion -dumpversion)"
compiler_target="$($c_compiler -dumpmachine)"
cxx_target="$($cxx_compiler -dumpmachine)"

if [ "$cxx_version" != "$compiler_version" ]; then
  echo "C and C++ compiler version mismatch: $compiler_version != $cxx_version" >&2
  exit 1
fi
if [ "$cxx_target" != "$compiler_target" ]; then
  echo "C and C++ compiler target mismatch: $compiler_target != $cxx_target" >&2
  exit 1
fi
if [[ ! "$compiler_version" =~ ^[0-9]+([.][0-9]+)*$ ]]; then
  echo "Invalid GCC version: $compiler_version" >&2
  exit 1
fi
if [[ ! "$compiler_target" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
  echo "Invalid GCC target: $compiler_target" >&2
  exit 1
fi

cc1_path="$(resolve_executable "$($c_compiler -print-prog-name=cc1)")"
cc1plus_path="$(resolve_executable "$($cxx_compiler -print-prog-name=cc1plus)")"
lto1_path="$(resolve_executable "$($c_compiler -print-prog-name=lto1)")"
assembler_path="$(resolve_executable "$($c_compiler -print-prog-name=as)")"
dumpspecs_sha256="$($c_compiler -dumpspecs | sha256sum | awk '{print $1}')"

if ! command -v ldd >/dev/null 2>&1; then
  echo "ldd is required to identify compiler runtime dependencies" >&2
  exit 1
fi

runtime_dependency_records() {
  local role=$1 executable=$2 output dependencies name dependency

  if ! output="$(LC_ALL=C ldd "$executable" 2>&1)"; then
    case "$output" in
      *"not a dynamic executable"*|*"statically linked"*) return 0 ;;
      *)
        echo "Could not inspect runtime dependencies for $executable: $output" >&2
        return 1
        ;;
    esac
  fi
  if [[ "$output" == *"=> not found"* ]]; then
    echo "Unresolved runtime dependency for $executable: $output" >&2
    return 1
  fi

  dependencies="$(printf '%s\n' "$output" | awk '
    /=> \/[^ ]+/ { print $1 "\t" $(NF - 1) }
    $1 ~ /^\// {
      name = $1
      sub(/^.*\//, "", name)
      print name "\t" $1
    }
  ')"
  if [ -z "$dependencies" ]; then
    case "$output" in
      *"statically linked"*|*"not a dynamic executable"*) return 0 ;;
      *)
        echo "No runtime dependencies could be parsed for $executable: $output" >&2
        return 1
        ;;
    esac
  fi

  while IFS=$'\t' read -r name dependency; do
    dependency="$(readlink -f "$dependency")"
    if [ ! -f "$dependency" ]; then
      echo "Runtime dependency not found for $executable: $dependency" >&2
      return 1
    fi
    printf '%s:%s:%s\n' "$role" "$name" "$(hash_file "$dependency")"
  done <<< "$dependencies"
}

runtime_dependencies="$({
  runtime_dependency_records c_driver "$c_path"
  runtime_dependency_records cxx_driver "$cxx_path"
  runtime_dependency_records cc1 "$cc1_path"
  runtime_dependency_records cc1plus "$cc1plus_path"
  runtime_dependency_records lto1 "$lto1_path"
  runtime_dependency_records assembler "$assembler_path"
} | LC_ALL=C sort)"

signature_record="$({
  printf 'format=wsjtx-linux-ccache-compiler-signature-v2\n'
  printf 'c_driver_sha256=%s\n' "$(hash_file "$c_path")"
  printf 'cxx_driver_sha256=%s\n' "$(hash_file "$cxx_path")"
  printf 'cc1_sha256=%s\n' "$(hash_file "$cc1_path")"
  printf 'cc1plus_sha256=%s\n' "$(hash_file "$cc1plus_path")"
  printf 'lto1_sha256=%s\n' "$(hash_file "$lto1_path")"
  printf 'assembler_sha256=%s\n' "$(hash_file "$assembler_path")"
  printf 'target=%s\n' "$compiler_target"
  printf 'dumpspecs_sha256=%s\n' "$dumpspecs_sha256"
  while IFS= read -r dependency_record; do
    if [ -n "$dependency_record" ]; then
      printf 'runtime_dependency=%s\n' "$dependency_record"
    fi
  done <<< "$runtime_dependencies"
})"
compiler_signature_sha256="$(printf '%s\n' "$signature_record" | sha256sum | awk '{print $1}')"
compiler_major=${compiler_version%%.*}

printf '%s\t%s\t%s\t%s\t%s\n' \
  "$compiler_version" \
  "$compiler_target" \
  "$(hash_file "$c_path")" \
  "gcc${compiler_major}-v2" \
  "$compiler_signature_sha256"
