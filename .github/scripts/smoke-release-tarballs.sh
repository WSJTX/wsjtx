#!/usr/bin/env bash
# Run every program of the release tarballs from its own extracted tarball.
#
#   smoke-release-tarballs.sh --platform macos|linux|windows --version V \
#       --target T --groups FILE --dir DIR [--ldd]
#
# Each tarball DIR/wsjtx-V-T-<tarball>.tar.gz is extracted into a directory of
# its own, so a library only another tarball carries cannot hide a gap, and
# each program runs from a scratch working directory with the library search
# variables unset, for at most 120 s on Linux; Windows programs launch
# through smoke-launch-windows.sh, which allows 20 s and removes the MinGW
# runtime from PATH. --ldd (Linux) also requires
# every library a tarball bundles to resolve to the bundled copy, because a
# build container has the same libraries installed system-wide.
set -euo pipefail

PLATFORM=""; VERSION=""; TARGET=""; GROUPS_FILE=""; DIR=""; LDD=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --platform) PLATFORM="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --target) TARGET="$2"; shift 2 ;;
    --groups) GROUPS_FILE="$2"; shift 2 ;;
    --dir) DIR="$2"; shift 2 ;;
    --ldd) LDD=1; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
for option in PLATFORM VERSION TARGET GROUPS_FILE DIR; do
  if [ -z "${!option}" ]; then echo "missing --$(echo "${option%_FILE}" | tr '[:upper:]' '[:lower:]')" >&2; exit 2; fi
done

SCRIPTS="$(cd "$(dirname "$0")" && pwd)"
SAMPLE="${SCRIPTS}/../../samples/WSPR/150426_0918.wav"
DIR="$(cd "$DIR" && pwd)"

fail() { echo "::error::$*" >&2; exit 1; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

run_program() {
  local tree="$1" program="$2" scratch="${WORK}/run-${2}" path arguments=() limit=()
  mkdir -p "$scratch"
  case "$program" in
    jt9|wsjt-codec) arguments=(--help) ;;
    wsprd)
      [ -f "$SAMPLE" ] || fail "WSPR sample not found: $SAMPLE"
      arguments=(-a "$scratch" "$SAMPLE")
      ;;
    encode77) arguments=("CQ K1ABC FN42") ;;
    wsprcode|ft4sim|jt4sim|jt65sim|cwsim|sfoxsim) ;;
    *) fail "no smoke invocation for ${program}" ;;
  esac
  case "$PLATFORM" in
    macos) path="${tree}/${program}" ;;
    linux) path="${tree}/bin/${program}"; limit=(timeout 120) ;;
    windows) path="${tree}/${program}.exe" ;;
    *) echo "unknown platform: $PLATFORM" >&2; exit 2 ;;
  esac
  [ -f "$path" ] || fail "${program} is not in ${tree##*/}"
  if [ "$PLATFORM" = windows ]; then
    (cd "$scratch" && "${SCRIPTS}/smoke-launch-windows.sh" "$path" ${arguments[@]+"${arguments[@]}"}) ||
      fail "${program} from ${tree##*/} failed"
    return
  fi
  if ! (cd "$scratch" && env -u LD_LIBRARY_PATH -u LD_PRELOAD -u DYLD_LIBRARY_PATH \
      -u DYLD_FALLBACK_LIBRARY_PATH -u DYLD_FRAMEWORK_PATH -u DYLD_INSERT_LIBRARIES \
      ${limit[@]+"${limit[@]}"} "$path" ${arguments[@]+"${arguments[@]}"} > "${scratch}/output.txt" 2>&1); then
    cat "${scratch}/output.txt" >&2
    fail "${program} from ${tree##*/} failed"
  fi
  echo "smoke OK: ${tree##*/}/${path#"${tree}"/}"
}

check_ldd() {
  local tree="$1" file line name resolved directory libdir output bundled=() wrong=()
  libdir=$(cd "${tree}/lib" && pwd -P)
  while IFS= read -r file; do
    bundled+=("${file##*/}")
  done < <(find "${tree}/lib" -type f)
  while IFS= read -r file; do
    output=$(env -u LD_LIBRARY_PATH -u LD_PRELOAD ldd "$file" 2>&1) || fail "ldd failed on ${file#"${tree}"/}: ${output}"
    while IFS= read -r line; do
      case "$line" in
        *'=> not found'*) wrong+=("${line%% *}(${file#"${tree}"/}): not found"); continue ;;
        *' => '*) ;;
        *) continue ;;
      esac
      read -r name _ resolved _ <<< "$line"
      for library in ${bundled[@]+"${bundled[@]}"}; do
        [ "$library" = "$name" ] || continue
        directory=$(cd "$(dirname "$resolved")" 2>/dev/null && pwd -P) || directory=""
        if [ "${directory}/${resolved##*/}" != "${libdir}/${name}" ]; then
          wrong+=("${name}(${file#"${tree}"/}): ${resolved}")
        fi
      done
    done <<< "$output"
  done < <(find "${tree}/bin" "${tree}/lib" -type f)
  [ "${#wrong[@]}" -eq 0 ] || fail "libraries of ${tree##*/} that do not resolve to its bundled copies: ${wrong[*]}"
}

count=0
while IFS= read -r line || [ -n "$line" ]; do
  line="${line%$'\r'}"
  read -r -a fields <<< "$line" || true
  [ "${#fields[@]}" -gt 1 ] || continue
  case "${fields[0]}" in '#'*) continue ;; esac
  name="wsjtx-${VERSION}-${TARGET}-${fields[0]}"
  tarball="${DIR}/${name}.tar.gz"
  [ -f "$tarball" ] || fail "release tarball not found: $tarball"
  mkdir -p "${WORK}/${fields[0]}"
  tar -xzf "$tarball" -C "${WORK}/${fields[0]}"
  tree="${WORK}/${fields[0]}/${name}"
  [ -d "$tree" ] || fail "${name}.tar.gz does not extract to ${name}/"
  if [ -n "$LDD" ]; then check_ldd "$tree"; fi
  for program in "${fields[@]:1}"; do
    run_program "$tree" "$program"
    count=$((count + 1))
  done
done < "$GROUPS_FILE"
[ "$count" -gt 0 ] || fail "no programs named in ${GROUPS_FILE}"
echo "ran ${count} programs from their release tarballs"
