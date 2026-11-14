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
#
# wsjt-codec then decodes samples/FT8/210703_133430.wav through the Python example client (doc/codec/examples/python/codec_client.py), with its extracted tarball as the client's data directory; the tarball must carry the source's ALLCALL7.TXT, every message in test/fixtures/ft8_210703_133430_mtd.expected.txt must decode, and the period must finish. The client needs Python 3.8 or later as python3; a missing python3, client, sample or list fails the run.
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
CLIENT="${SCRIPTS}/../../doc/codec/examples/python/codec_client.py"
FT8_SAMPLE="${SCRIPTS}/../../samples/FT8/210703_133430.wav"
FT8_EXPECTED="${SCRIPTS}/../../test/fixtures/ft8_210703_133430_mtd.expected.txt"
ALLCALL7="${SCRIPTS}/../../ALLCALL7.TXT"
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
  elif ! (cd "$scratch" && env -u LD_LIBRARY_PATH -u LD_PRELOAD -u DYLD_LIBRARY_PATH \
      -u DYLD_FALLBACK_LIBRARY_PATH -u DYLD_FRAMEWORK_PATH -u DYLD_INSERT_LIBRARIES \
      ${limit[@]+"${limit[@]}"} "$path" ${arguments[@]+"${arguments[@]}"} > "${scratch}/output.txt" 2>&1); then
    cat "${scratch}/output.txt" >&2
    fail "${program} from ${tree##*/} failed"
  else
    echo "smoke OK: ${tree##*/}/${path#"${tree}"/}"
  fi
  if [ "$program" = wsjt-codec ]; then decode_ft8_sample "$tree" "$path"; fi
}

decode_ft8_sample() {
  local tree="$1" path="$2" scratch="${WORK}/decode-${1##*/}" file python version line label run=()
  local fields=() decoded="|" total=0 missing="" missed=0 status=0 finished=0
  for file in "$CLIENT" "$FT8_SAMPLE" "$FT8_EXPECTED" "$ALLCALL7"; do
    [ -f "$file" ] || fail "the codec decode needs ${file#"${SCRIPTS}/../../"}, which this tree lacks"
  done
  python=$(command -v python3) || fail "the codec decode needs Python 3.8 or later as python3; there is no python3"
  version=$("$python" -c 'import sys; print("%d.%d.%d" % sys.version_info[:3]); sys.exit(sys.version_info < (3, 8))') ||
    fail "the codec decode needs Python 3.8 or later as python3; ${python} is ${version:-unusable}"
  version=${version%$'\r'}  # a native Windows Python ends its line in CR LF
  "$python" -c 'import sys; sys.exit(open(sys.argv[1], "rb").read() != open(sys.argv[2], "rb").read())' \
    "${tree}/ALLCALL7.TXT" "$ALLCALL7" 2> /dev/null || fail "${tree##*/} does not carry the source's ALLCALL7.TXT"
  case "$PLATFORM" in
    # The clean-PATH launch above proved the codec's DLLs; the decode keeps the shell's PATH, because the MSYS2 python3 lives in the runtime directory that launch removes.
    windows) run=(timeout 120) ;;
    linux) run=(env -u LD_LIBRARY_PATH -u LD_PRELOAD timeout 120) ;;
    macos) run=(env -u DYLD_LIBRARY_PATH -u DYLD_FALLBACK_LIBRARY_PATH -u DYLD_FRAMEWORK_PATH -u DYLD_INSERT_LIBRARIES) ;;
  esac
  mkdir -p "$scratch"
  (cd "$scratch" && "${run[@]}" "$python" "$CLIENT" decode --codec "$path" --data "$tree" "$FT8_SAMPLE" \
    < /dev/null > "${scratch}/stdout.txt" 2> "${scratch}/stderr.txt") || status=$?
  label="${FT8_SAMPLE##*_}"
  label="${label%.wav}"
  while IFS= read -r line || [ -n "$line" ]; do
    read -r -a fields <<< "${line%$'\r'}" || true
    if [ "${#fields[@]}" -gt 5 ] && [ "${fields[0]} ${fields[1]}" = "decode ${label}" ]; then
      decoded="${decoded}${fields[*]:5}|"
    elif [ "${#fields[@]}" -eq 2 ] && [ "${fields[0]} ${fields[1]}" = "finished ${label}" ]; then
      finished=1
    fi
  done < "${scratch}/stdout.txt"
  # Each line of the list is a jt9 decode: time, snr, dt, freq, a sync marker, then the message.
  while IFS= read -r line || [ -n "$line" ]; do
    read -r -a fields <<< "${line%$'\r'}" || true
    [ "${#fields[@]}" -gt 5 ] || continue
    case "${fields[0]}" in '#'*|'<'*) continue ;; esac
    total=$((total + 1))
    case "$decoded" in
      *"|${fields[*]:5}|"*) ;;
      *) missed=$((missed + 1)); missing="${missing}${missing:+; }${fields[*]:5}" ;;
    esac
  done < "$FT8_EXPECTED"
  if [ "$status" -ne 0 ] || [ "$total" -eq 0 ] || [ "$missed" -gt 0 ] || [ "$finished" -eq 0 ]; then
    cat "${scratch}/stdout.txt" "${scratch}/stderr.txt" >&2
    [ "$status" -eq 0 ] || fail "the decode by wsjt-codec from ${tree##*/} failed (exit status ${status})"
    [ "$total" -gt 0 ] || fail "${FT8_EXPECTED##*/} lists no messages"
    [ "$missed" -eq 0 ] || fail "wsjt-codec from ${tree##*/} decoded $((total - missed)) of the ${total} messages of ${FT8_SAMPLE##*/}; missing: ${missing}"
    fail "wsjt-codec from ${tree##*/} sent no decode_finished for ${label}"
  fi
  echo "smoke OK: ${tree##*/}/${path#"${tree}"/} decodes the ${total} messages of ${FT8_SAMPLE##*/} (Python ${version})"
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
