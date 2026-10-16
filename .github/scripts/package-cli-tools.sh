#!/usr/bin/env bash
# Package the command-line programs into the release tarballs published beside
# the installers: one tarball per line of CMake/release-tarballs.txt.
#
#   package-cli-tools.sh --platform macos|linux|windows --version V --arch A \
#       --groups FILE --source DIR --out DIR \
#       [--dll-dir DIR --system-dir DIR] [--linuxdeploy COMMAND]
#
# wsjtx-V-<target>-<tarball>.tar.gz holds one directory of the same name: the
# tarball's programs, every library they load beyond the operating system's,
# README.txt, COPYING and THIRD-PARTY.txt. A missing program, an unresolved
# library or a library third-party-libraries.tsv does not name fails the run,
# and then no tarball is written.
#
# macos:   --source is the CLI assets directory: the signed programs and the
#          deployed lib/ of dylibs they reference as @rpath/<name>. Each
#          tarball gets its programs at the top and, in lib/, the dylibs they
#          reach through their LC_RPATH entries. A reference outside /usr/lib
#          and /System/Library that does not resolve there, or an absolute
#          LC_RPATH, fails the run.
# linux:   --source is the CMake build directory. --linuxdeploy is a command
#          that runs the pinned linuxdeploy AppImage. It deploys each tarball's
#          programs to bin/ and the libraries they load to lib/, except those
#          on its excludelist (linuxdeploy-excludelist.txt), with RUNPATH
#          $ORIGIN/../lib and $ORIGIN, and the libraries' Debian copyright
#          files to share/doc/. A library that resolves neither inside the
#          tarball nor to the excludelist, or a RUNPATH entry not relative to
#          $ORIGIN, fails the run.
# windows: --source is the CMake build directory, --dll-dir the staged bin/
#          after DLL fixup and --system-dir the Windows system directory. Each
#          tarball gets its .exe files and the transitive closure of the DLLs
#          they import from --dll-dir; an import found in neither directory,
#          other than the Universal C Runtime's API sets, fails the run.
set -euo pipefail

PLATFORM=""; VERSION=""; ARCH=""; GROUPS_FILE=""; SOURCE=""; OUT=""; DLL_DIR=""; SYSTEM_DIR=""; LINUXDEPLOY=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --platform) PLATFORM="$2"; shift 2 ;;
    --version) VERSION="$2"; shift 2 ;;
    --arch) ARCH="$2"; shift 2 ;;
    --groups) GROUPS_FILE="$2"; shift 2 ;;
    --source) SOURCE="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    --dll-dir) DLL_DIR="$2"; shift 2 ;;
    --system-dir) SYSTEM_DIR="$2"; shift 2 ;;
    --linuxdeploy) LINUXDEPLOY="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
for option in PLATFORM VERSION ARCH GROUPS_FILE SOURCE OUT; do
  if [ -z "${!option}" ]; then echo "missing --$(echo "${option%_FILE}" | tr '[:upper:]' '[:lower:]')" >&2; exit 2; fi
done

SCRIPTS="$(cd "$(dirname "$0")" && pwd)"
COPYING="${SCRIPTS}/../../COPYING"
NOTICES_TABLE="${SCRIPTS}/third-party-libraries.tsv"
EXCLUDELIST="${SCRIPTS}/linuxdeploy-excludelist.txt"
LICENSE_PAGE="${SCRIPTS}/../../doc/common/license.adoc"

fail() { echo "::error::$*" >&2; exit 1; }

case "$PLATFORM" in
  macos) TARGET="${ARCH}-macOS"; EXT="" ;;
  linux) TARGET="linux-${ARCH}"; EXT=""
    [ -n "$LINUXDEPLOY" ] || { echo "::error::--linuxdeploy is required for linux" >&2; exit 2; } ;;
  windows) TARGET="windows-${ARCH}"; EXT=".exe"
    [ -n "$DLL_DIR" ] && [ -d "$DLL_DIR" ] || { echo "::error::--dll-dir is required for windows" >&2; exit 2; }
    [ -n "$SYSTEM_DIR" ] && [ -d "$SYSTEM_DIR" ] || { echo "::error::--system-dir is required for windows" >&2; exit 2; }
    ;;
  *) echo "unknown platform: $PLATFORM" >&2; exit 2 ;;
esac
[ -d "$SOURCE" ] || fail "source directory not found: $SOURCE"
[ -s "$GROUPS_FILE" ] || fail "release tarball list not found or empty: $GROUPS_FILE"
[ -f "$COPYING" ] || fail "COPYING not found: $COPYING"
[ -f "$NOTICES_TABLE" ] || fail "third-party library table not found: $NOTICES_TABLE"
[ -f "$LICENSE_PAGE" ] || fail "license page not found: $LICENSE_PAGE"
[ "$PLATFORM" != linux ] || [ -f "$EXCLUDELIST" ] || fail "linuxdeploy excludelist not found: $EXCLUDELIST"

# Plain indexed arrays and lookup files keep this runnable on bash 3.2.
GROUP_NAMES=(); GROUP_PROGRAMS=()
while IFS= read -r line || [ -n "$line" ]; do
  line="${line%$'\r'}"   # a Windows checkout may have CRLF endings
  read -r -a fields <<< "$line" || true
  [ "${#fields[@]}" -gt 0 ] || continue
  case "${fields[0]}" in '#'*) continue ;; esac
  [ "${#fields[@]}" -ge 2 ] || fail "release tarball ${fields[0]} names no programs in ${GROUPS_FILE}"
  for name in ${GROUP_NAMES[@]+"${GROUP_NAMES[@]}"}; do
    [ "$name" != "${fields[0]}" ] || fail "release tarball ${name} is named twice in ${GROUPS_FILE}"
  done
  GROUP_NAMES+=("${fields[0]}")
  GROUP_PROGRAMS+=("${fields[*]:1}")
done < "$GROUPS_FILE"
[ "${#GROUP_NAMES[@]}" -gt 0 ] || fail "no release tarballs named in ${GROUPS_FILE}"

missing=()
for programs in "${GROUP_PROGRAMS[@]}"; do
  for program in $programs; do
    [ -f "${SOURCE}/${program}${EXT}" ] || missing+=("${program}${EXT}")
  done
done
[ "${#missing[@]}" -eq 0 ] || fail "programs listed in ${GROUPS_FILE} are missing under ${SOURCE}: ${missing[*]}"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "${WORK}/tarballs"

# --- macOS -----------------------------------------------------------------

macho_rpaths() {
  otool -l "$1" | awk '$1 == "cmd" && $2 == "LC_RPATH" { getline; getline; print $2 }'
}

# Copies into $DEST/lib every dylib program $1 reaches. dyld resolves the
# @rpath references of the program, and of each dylib it loads, through that
# file's own LC_RPATH entries and then the program's; @executable_path is the
# program's directory, the top of $SOURCE.
macos_closure() {
  local program="$1" rpath entry file id ref candidate directory found
  local program_rpaths=() rpaths=() queue=("$1") unresolved=() i=0 visited=$'\n'"$1"$'\n'
  while IFS= read -r rpath; do
    case "$rpath" in
      @loader_path|@executable_path) entry="" ;;
      @loader_path/*) entry="${rpath#@loader_path/}" ;;
      @executable_path/*) entry="${rpath#@executable_path/}" ;;
      /*) fail "${program} has an absolute LC_RPATH: ${rpath}" ;;
      *) fail "${program} has an LC_RPATH dyld cannot resolve: ${rpath}" ;;
    esac
    case "/${entry}/" in */../*) fail "${program} has an LC_RPATH outside its directory: ${rpath}" ;; esac
    program_rpaths+=("$entry")
  done < <(macho_rpaths "${SOURCE}/${program}")
  while [ "$i" -lt "${#queue[@]}" ]; do
    file="${queue[$i]}"; i=$((i + 1))
    directory=$(dirname "$file")
    [ "$directory" != . ] || directory=""
    rpaths=()
    if [ "$file" != "$program" ]; then
      while IFS= read -r rpath; do
        case "$rpath" in
          @loader_path) entry="$directory" ;;
          @loader_path/*) entry="${directory:+${directory}/}${rpath#@loader_path/}" ;;
          @executable_path) entry="" ;;
          @executable_path/*) entry="${rpath#@executable_path/}" ;;
          /*) fail "${file} has an absolute LC_RPATH: ${rpath}" ;;
          *) fail "${file} has an LC_RPATH dyld cannot resolve: ${rpath}" ;;
        esac
        # QtCore searches ../Frameworks, outside the tarball; nothing may load from there.
        case "/${entry}/" in */../*) continue ;; esac
        rpaths+=("$entry")
      done < <(macho_rpaths "${SOURCE}/${file}")
    fi
    rpaths+=(${program_rpaths[@]+"${program_rpaths[@]}"})
    id=$(otool -D "${SOURCE}/${file}" | awk 'NR == 2 { print; exit }')
    while IFS= read -r ref; do
      [ "$ref" != "$id" ] || continue
      found=""
      case "$ref" in
        /usr/lib/*|/System/Library/*) continue ;;
        @rpath/*)
          for entry in ${rpaths[@]+"${rpaths[@]}"}; do
            candidate="${entry:+${entry}/}${ref#@rpath/}"
            if [ -f "${SOURCE}/${candidate}" ]; then found="$candidate"; break; fi
          done
          ;;
        @loader_path/*)
          candidate="${directory:+${directory}/}${ref#@loader_path/}"
          if [ -f "${SOURCE}/${candidate}" ]; then found="$candidate"; fi
          ;;
        @executable_path/*)
          candidate="${ref#@executable_path/}"
          if [ -f "${SOURCE}/${candidate}" ]; then found="$candidate"; fi
          ;;
      esac
      case "/${found}/" in */../*) found="" ;; esac
      case "$found" in lib/*) ;; *) found="" ;; esac
      if [ -z "$found" ]; then
        unresolved+=("${ref}(${file})")
        continue
      fi
      case "$visited" in *$'\n'"$found"$'\n'*) continue ;; esac
      visited="${visited}${found}"$'\n'
      queue+=("$found")
      if [ ! -e "${DEST}/${found}" ]; then
        mkdir -p "$(dirname "${DEST}/${found}")"
        cp -p "${SOURCE}/${found}" "${DEST}/${found}"
      fi
    done < <(otool -L "${SOURCE}/${file}" | awk 'NR > 1 { print $1 }')
  done
  [ "${#unresolved[@]}" -eq 0 ] || fail "references that resolve neither to lib/ under ${SOURCE} nor to the system: ${unresolved[*]}"
}

macos_minimum() {
  local file
  find "$DEST" -type f ! -name '*.txt' ! -name COPYING | while IFS= read -r file; do
    otool -l "$file" | awk '
      $1 == "Load" && $2 == "command" { command = "" }
      $1 == "cmd" { command = $2 }
      command == "LC_BUILD_VERSION" && $1 == "minos" { print $2 }
      command == "LC_VERSION_MIN_MACOSX" && $1 == "version" { print $2 }
    '
  done | sort -t . -k 1,1n -k 2,2n -k 3,3n | tail -n 1
}

# --- Windows ---------------------------------------------------------------

windows_setup() {
  have="${WORK}/have.tsv"
  : > "$have"
  for dll in "$DLL_DIR"/*.dll "$DLL_DIR"/*.DLL; do
    [ -e "$dll" ] || continue
    printf '%s\t%s\n' "$(basename "$dll" | tr '[:upper:]' '[:lower:]')" "$dll" >> "$have"
  done
  system="${WORK}/system.txt"
  for file in "$SYSTEM_DIR"/*; do
    printf '%s\n' "${file##*/}"
  done | tr '[:upper:]' '[:lower:]' > "$system"
}

windows_closure() {
  local queue=("${DEST}"/*.exe) unresolved=() i=0 bin imports dep lower src target
  while [ "$i" -lt "${#queue[@]}" ]; do
    bin="${queue[$i]}"; i=$((i + 1))
    imports=$(objdump -p "$bin") || fail "objdump failed on ${bin##*/}"
    while read -r dep; do
      lower=$(printf '%s' "$dep" | tr '[:upper:]' '[:lower:]')
      # The Universal C Runtime's API sets are names the Windows 10 loader
      # maps; no file by that name need exist in the system directory.
      case "$lower" in api-ms-win-crt-*) continue ;; esac
      src=$(awk -F '\t' -v k="$lower" '$1 == k { print $2; exit }' "$have")
      if [ -z "$src" ]; then
        grep -qxF "$lower" "$system" || unresolved+=("${dep}(${bin##*/})")
        continue
      fi
      target="${DEST}/$(basename "$src")"
      [ ! -e "$target" ] || continue
      cp "$src" "$target"
      queue+=("$target")
    done < <(awk '/DLL Name:/ { print $3 }' <<< "$imports")
  done
  [ "${#unresolved[@]}" -eq 0 ] || fail "DLL imports found in neither ${DLL_DIR} nor ${SYSTEM_DIR}: ${unresolved[*]}"
}

# --- Linux -----------------------------------------------------------------

in_excludelist() {
  local library="$1" pattern
  case "$library" in ld-linux*|linux-vdso*) return 0 ;; esac
  while IFS= read -r pattern; do
    pattern="${pattern%%#*}"
    pattern="${pattern//[[:space:]]/}"
    [ -n "$pattern" ] || continue
    # shellcheck disable=SC2254 # entries are glob patterns
    case "$library" in $pattern) return 0 ;; esac
  done < "$EXCLUDELIST"
  return 1
}

linux_deploy() {
  local appdir="${WORK}/appdir-$1" program arguments=()
  shift
  for program in "$@"; do
    arguments+=(--executable "${SOURCE}/${program}")
  done
  env -u GITHUB_TOKEN APPIMAGE_EXTRACT_AND_RUN=1 \
    "$LINUXDEPLOY" --appimage-extract-and-run --appdir "$appdir" "${arguments[@]}" ||
    fail "linuxdeploy failed for ${NAME}"
  mkdir -p "${DEST}/bin" "${DEST}/lib"
  for program in "$@"; do
    [ -f "${appdir}/usr/bin/${program}" ] || fail "linuxdeploy did not deploy ${program}"
    cp -p "${appdir}/usr/bin/${program}" "${DEST}/bin/"
  done
  if [ -d "${appdir}/usr/lib" ]; then
    find "${appdir}/usr/lib" -maxdepth 1 -type f -exec cp -p {} "${DEST}/lib/" \;
  fi
  if [ -d "${appdir}/usr/share/doc" ]; then
    mkdir -p "${DEST}/share"
    cp -R "${appdir}/usr/share/doc" "${DEST}/share/doc"
  fi
}

# Every library a file needs must resolve inside the tarball through that
# file's own RUNPATH (it is not inherited) or be on the excludelist. Records
# what the host must provide.
linux_check() {
  local file relative directory dynamic line entry library found unresolved=() searched
  # shellcheck disable=SC2016 # the loader's literal ORIGIN token
  local origin='$ORIGIN' braced='${ORIGIN}'
  : > "${WORK}/host-libraries.txt"
  : > "${WORK}/host-versions.txt"
  while IFS= read -r file; do
    relative="${file#"${DEST}"/}"
    dynamic=$(readelf -dW "$file") || fail "readelf failed on ${relative}"
    directory=$(dirname "$file")
    searched=()
    while IFS= read -r line; do
      [[ "$line" =~ \((RUNPATH|RPATH)\).*Library[[:space:]](runpath|rpath):[[:space:]]\[([^]]+)\] ]] || continue
      # The loader reads an empty entry as the current directory.
      case "${BASH_REMATCH[3]}" in
        :*|*:|*::*) fail "${relative} has an empty RUNPATH entry: ${BASH_REMATCH[3]}" ;;
      esac
      IFS=: read -r -a entries <<< "${BASH_REMATCH[3]}"
      for entry in "${entries[@]}"; do
        case "$entry" in
          "$origin"|"$braced") searched+=("$directory") ;;
          "$origin"/*) searched+=("${directory}/${entry#"$origin"/}") ;;
          "$braced"/*) searched+=("${directory}/${entry#"$braced"/}") ;;
          *) fail "${relative} has a RUNPATH entry not relative to ${origin}: ${entry}" ;;
        esac
      done
    done <<< "$dynamic"
    while IFS= read -r line; do
      [[ "$line" =~ \(NEEDED\).*Shared[[:space:]]library:[[:space:]]\[([^]]+)\] ]] || continue
      library="${BASH_REMATCH[1]}"
      if in_excludelist "$library"; then
        printf '%s\n' "$library" >> "${WORK}/host-libraries.txt"
        continue
      fi
      found=""
      for entry in ${searched[@]+"${searched[@]}"}; do
        if [ -f "${entry}/${library}" ]; then found=1; break; fi
      done
      [ -n "$found" ] || unresolved+=("${library}(${relative})")
    done <<< "$dynamic"
    readelf -VW "$file" | awk '
      /^Version needs section/ { needs = 1; next }
      /^Version (definition|symbols) section/ { needs = 0 }
      needs && match($0, /Name: (GLIBC|GLIBCXX)_[0-9.]+/) { print substr($0, RSTART + 6, RLENGTH - 6) }
    ' >> "${WORK}/host-versions.txt" || fail "readelf failed on ${relative}"
  done < <(find "${DEST}/bin" "${DEST}/lib" -type f)
  [ "${#unresolved[@]}" -eq 0 ] || fail "libraries that resolve neither inside ${NAME} nor to linuxdeploy's excludelist: ${unresolved[*]}"
}

highest_version() {
  awk -v prefix="$1_" 'index($0, prefix) == 1 { print substr($0, length(prefix) + 1) }' "${WORK}/host-versions.txt" |
    sort -t . -k 1,1n -k 2,2n -k 3,3n | tail -n 1
}

# --- Notices and README ----------------------------------------------------

bundled_libraries() {
  case "$PLATFORM" in
    macos) if [ -d "${DEST}/lib" ]; then find "${DEST}/lib" -type f; fi ;;
    linux) find "${DEST}/lib" -type f ;;
    windows) find "$DEST" -maxdepth 1 -type f -iname '*.dll' ;;
  esac
}

write_notices() {
  local file name pattern component license url matched unknown=()
  : > "${WORK}/notices.tsv"
  shopt -s nocasematch   # Windows DLL names vary in case
  while IFS= read -r file; do
    name="${file##*/}"
    matched=""
    while IFS=$'\t' read -r pattern component license url; do
      case "$pattern" in ''|'#'*) continue ;; esac
      # shellcheck disable=SC2254 # table entries are glob patterns
      case "$name" in
        $pattern) printf '%s\t%s\t%s\t%s\n' "$component" "$license" "$url" "$name" >> "${WORK}/notices.tsv"; matched=1; break ;;
      esac
    done < "$NOTICES_TABLE"
    [ -n "$matched" ] || unknown+=("$name")
  done < <(bundled_libraries)
  shopt -u nocasematch
  while IFS=$'\t' read -r pattern component license url; do
    [ "$pattern" != "@${PLATFORM}" ] || printf '%s\t%s\t%s\t(static)\n' "$component" "$license" "$url" >> "${WORK}/notices.tsv"
  done < "$NOTICES_TABLE"
  [ "${#unknown[@]}" -eq 0 ] || fail "bundled libraries missing from ${NOTICES_TABLE##*/}: ${unknown[*]}"
  {
    echo "Third-party libraries in ${NAME}"
    echo
    if [ -s "${WORK}/notices.tsv" ]; then
      echo "These libraries are not part of WSJT-X. Each is distributed under its own license, published at the address given."
      echo
      LC_ALL=C sort -t $'\t' -k 1,1 -k 4,4 "${WORK}/notices.tsv" | awk -F '\t' '
        function flush() {
          if (files != "") print "  Files:" files
          if (static) print "  Linked statically into the programs"
        }
        $1 != last { if (last != "") { flush(); print "" } printf "%s\n  License: %s\n  %s\n", $1, $2, $3; last = $1; files = ""; static = 0 }
        $4 == "(static)" { static = 1; next }
        { files = files " " $4 }
        END { if (last != "") flush() }
      '
    else
      echo "This tarball bundles no third-party libraries."
    fi
    if [ -d "${DEST}/share/doc" ]; then
      echo
      echo "share/doc/ holds the copyright files of the Debian packages that bundled libraries come from; a library built outside a Debian package has none there."
    fi
  } > "${DEST}/THIRD-PARTY.txt"
}

describe() {
  local writes="writes working files to its data directory (-a) and its temporary directory (-t), both the current directory by default; run it where it can write, or point -a and -t at writable directories."
  case "$1" in
    jt9)
      echo "jt9 is the decoder the WSJT-X application drives through shared memory (-s). It also decodes WAV files. wsjt-codec, in its own tarball, is the same decoder without the shared-memory worker, and decodes framed PCM audio on standard input (--stream)."
      echo "jt9 ${writes}"
      ;;
    codec)
      echo "wsjt-codec is the decoder of jt9 without the shared-memory worker: it decodes WAV files, and framed PCM audio on standard input (--stream)."
      echo "wsjt-codec ${writes}"
      ;;
    wsprd)
      echo "wsprd decodes WSPR from WAV files, and from framed PCM audio on standard input (-0)."
      echo "wsprd writes working files to its data directory (-a, default the current directory) and stops if it cannot write there."
      ;;
    utilities)
      echo "Utilities for testing decoders: wsprcode and encode77 encode messages; ft4sim, jt4sim, jt65sim, cwsim and sfoxsim write simulated signals to WAV files in the current directory, so run them where they can write."
      ;;
    *) fail "no README description for release tarball $1" ;;
  esac
}

# The notice doc/common/license.adoc asks anyone using WSJT-X under the GPL to display.
copyright_notice() {
  tr -d '\r' < "$LICENSE_PAGE" |
    awk '/^\*The algorithms/ { on = 1 } on { printf "%s ", $0 } on && /Development Group\.\*$/ { exit }' |
    sed -e 's/_{prog}_/WSJT-X/g' -e 's/^\*//' -e 's/\* *$//' -e 's/  */ /g' -e 's/ *$//'
}

write_readme() {
  local group="$1" programs="$2" floor glibc glibcxx host notice
  notice=$(copyright_notice)
  [ -n "$notice" ] || fail "no copyright notice found in ${LICENSE_PAGE}"
  {
    echo "WSJT-X ${VERSION}: ${group} for ${TARGET}"
    echo
    echo "Programs: ${programs}"
    describe "$group"
    echo
    case "$PLATFORM" in
      macos)
        floor=$(macos_minimum)
        [ -n "$floor" ] || fail "no minimum macOS version recorded in ${NAME}"
        echo "Layout: the programs are at the top of this directory and the libraries they load are in lib/. Keep lib/ beside the programs. They require macOS ${floor} or later."
        echo
        echo "The programs and libraries carry ad-hoc signatures and are not notarized. macOS blocks quarantined programs that are not notarized, so remove the quarantine attribute from the tarball before extracting it (xattr -d com.apple.quarantine <tarball>), or from an extracted directory (xattr -dr com.apple.quarantine <directory>). Changing one of these files, for example with install_name_tool, invalidates its signature; sign it again with codesign --force --sign - <file>."
        ;;
      linux)
        glibc=$(highest_version GLIBC)
        glibcxx=$(highest_version GLIBCXX)
        host=$(LC_ALL=C sort -u "${WORK}/host-libraries.txt" | paste -sd ' ' -)
        echo "Layout: the programs are in bin/ and the libraries they load are in lib/. Keep both directories together."
        echo
        echo "The system must provide glibc ${glibc:-of any version}${glibc:+ or later}${glibcxx:+, libstdc++ with GLIBCXX_${glibcxx} or later}, and these libraries: ${host}."
        echo "LD_LIBRARY_PATH takes precedence over the bundled lib/, so start the programs without it."
        ;;
      windows)
        echo "Layout: the programs and the DLLs they load are in this directory. Keep them together."
        echo
        echo "The programs use the Universal C Runtime, part of Windows 10 and later."
        echo
        echo "The programs and DLLs carry no Authenticode signature."
        ;;
    esac
    echo
    echo "WSJT-X is free software under the GNU General Public License version 3 (COPYING). Its source code is wsjtx-${VERSION}-src.tar.gz on the same release page. THIRD-PARTY.txt lists the bundled libraries and their licenses."
    echo
    echo "$notice"
  } > "${DEST}/README.txt"
}

# --- Package ---------------------------------------------------------------

[ "$PLATFORM" != windows ] || windows_setup
packaged=()
for index in "${!GROUP_NAMES[@]}"; do
  group="${GROUP_NAMES[$index]}"
  programs="${GROUP_PROGRAMS[$index]}"
  NAME="wsjtx-${VERSION}-${TARGET}-${group}"
  DEST="${WORK}/${NAME}"
  mkdir -p "$DEST"
  # shellcheck disable=SC2086 # program names are single words
  case "$PLATFORM" in
    macos)
      for program in $programs; do
        cp -p "${SOURCE}/${program}" "${DEST}/"
        macos_closure "$program"
      done
      ;;
    linux)
      linux_deploy "$group" $programs
      linux_check
      ;;
    windows)
      for program in $programs; do cp "${SOURCE}/${program}.exe" "${DEST}/"; done
      windows_closure
      ;;
  esac
  cp "$COPYING" "${DEST}/COPYING"
  write_notices
  write_readme "$group" "$programs"
  COPYFILE_DISABLE=1 tar -czf "${WORK}/tarballs/${NAME}.tar.gz" -C "$WORK" "$NAME"
  packaged+=("${NAME}.tar.gz")
done

mkdir -p "$OUT"
for tarball in "${packaged[@]}"; do
  mv "${WORK}/tarballs/${tarball}" "${OUT}/${tarball}"
  echo "packaged ${OUT}/${tarball}"
done
