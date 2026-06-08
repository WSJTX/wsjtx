#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  cat <<'USAGE'
Usage: deploy-macos-app.sh --app PATH --workspace-prefix PATH --qt-prefix PATH --boost-prefix PATH --fftw-prefix PATH --libusb-prefix PATH --portaudio-prefix PATH --fortran-prefix PATH --gcc-lib PATH [--fortran-original-prefix PATH] [--homebrew-prefix PATH]
USAGE
}

APP=""
WORKSPACE_PREFIX="${GITHUB_WORKSPACE:-}"
QT_PREFIX=""
BOOST_PREFIX=""
FFTW_PREFIX=""
LIBUSB_PREFIX=""
PORTAUDIO_PREFIX=""
FORTRAN_PREFIX=""
FORTRAN_ORIGINAL_PREFIX=""
GCC_LIB=""
HOMEBREW_PREFIX="${HOMEBREW_PREFIX:-}"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --app) APP="$2"; shift 2 ;;
    --workspace-prefix) WORKSPACE_PREFIX="$2"; shift 2 ;;
    --qt-prefix) QT_PREFIX="$2"; shift 2 ;;
    --boost-prefix) BOOST_PREFIX="$2"; shift 2 ;;
    --fftw-prefix) FFTW_PREFIX="$2"; shift 2 ;;
    --libusb-prefix) LIBUSB_PREFIX="$2"; shift 2 ;;
    --portaudio-prefix) PORTAUDIO_PREFIX="$2"; shift 2 ;;
    --fortran-prefix) FORTRAN_PREFIX="$2"; shift 2 ;;
    --fortran-original-prefix) FORTRAN_ORIGINAL_PREFIX="$2"; shift 2 ;;
    --gcc-lib) GCC_LIB="$2"; shift 2 ;;
    --homebrew-prefix) HOMEBREW_PREFIX="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

for value_name in APP WORKSPACE_PREFIX QT_PREFIX BOOST_PREFIX FFTW_PREFIX LIBUSB_PREFIX PORTAUDIO_PREFIX FORTRAN_PREFIX GCC_LIB HOMEBREW_PREFIX; do
  if [ -z "${!value_name}" ]; then
    echo "Missing required argument or environment: ${value_name}" >&2
    usage >&2
    exit 2
  fi
done

FRAMEWORKS="${APP}/Contents/Frameworks"
mkdir -p "${FRAMEWORKS}"

dump_bundle_context() {
  local status="$1"
  local line="$2"
  local command="$3"
  echo "::error::deploy-macos-app failed at line ${line} with status ${status}: ${command}"
  echo "APP=${APP}"
  echo "FRAMEWORKS=${FRAMEWORKS}"
  echo "FORTRAN_PREFIX=${FORTRAN_PREFIX}"
  echo "FORTRAN_ORIGINAL_PREFIX=${FORTRAN_ORIGINAL_PREFIX}"
  echo "GCC_LIB=${GCC_LIB}"
  echo "HOMEBREW_PREFIX=${HOMEBREW_PREFIX}"
  echo "App executable contents:"
  ls -la "${APP}/Contents/MacOS" || true
  echo "Framework contents:"
  ls -la "${FRAMEWORKS}" || true
  echo "External references in app executables:"
  find "${APP}/Contents/MacOS" -type f -perm +111 -print -exec otool -L {} \; 2>/dev/null || true
  if [ -f macdeployqt.log ]; then
    echo "macdeployqt.log:"
    cat macdeployqt.log
  fi
}
trap 'status=$?; dump_bundle_context "$status" "$LINENO" "$BASH_COMMAND"; exit "$status"' ERR

run_logged() {
  echo "+ $*"
  "$@"
}

echo "Running macdeployqt..."
set +e
"${QT_PREFIX}/bin/macdeployqt" "${APP}" -verbose=2 > macdeployqt.log 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
  echo "::error::macdeployqt failed with status ${status}"
  dump_bundle_context "$status" "$LINENO" "macdeployqt"
  exit "$status"
fi
cat macdeployqt.log

resolve_lib() {
  local lib="$1"
  if [ -n "$FORTRAN_ORIGINAL_PREFIX" ] && [[ "$lib" == "$FORTRAN_ORIGINAL_PREFIX"/* ]]; then
    local original_prefix="${FORTRAN_ORIGINAL_PREFIX}/"
    local relocated="${FORTRAN_PREFIX}/${lib:${#original_prefix}}"
    [ -f "$relocated" ] && echo "$relocated" && return 0
  fi
  if [[ "$lib" == $HOMEBREW_PREFIX/* ]]; then
    echo "$lib"
    return 0
  elif [[ "$lib" == "$WORKSPACE_PREFIX"/* ]]; then
    echo "$lib"
    return 0
  elif [[ "$lib" == @rpath/* ]]; then
    local name="${lib#@rpath/}"
    for dir in \
      "$FRAMEWORKS" \
      "${BOOST_PREFIX}/lib" \
      "${FFTW_PREFIX}/lib" \
      "${LIBUSB_PREFIX}/lib" \
      "${PORTAUDIO_PREFIX}/lib" \
      "${FORTRAN_PREFIX}/lib" \
      "$GCC_LIB" \
      "${HOMEBREW_PREFIX}/lib"
    do
      [ -f "$dir/$name" ] && echo "$dir/$name" && return
    done
  fi
  return 0
}

should_bundle_ref() {
  local ref="$1"
  if [ -n "$FORTRAN_ORIGINAL_PREFIX" ] && [[ "$ref" == "$FORTRAN_ORIGINAL_PREFIX"/* ]]; then
    return 0
  fi
  case "$ref" in
    *Qt*.framework/*)
      return 1
      ;;
    "$HOMEBREW_PREFIX"/*|"$WORKSPACE_PREFIX"/*|@rpath/*)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

bundle_dylib() {
  local binary="$1"
  local lib
  while IFS= read -r lib; do
    should_bundle_ref "$lib" || continue
    local resolved
    resolved=$(resolve_lib "$lib")
    if [ -z "$resolved" ]; then
      echo "::error::Could not resolve ${lib} required by ${binary}"
      exit 1
    fi
    local libname
    libname=$(basename "$resolved")
    case "$libname" in
      libgomp*.dylib)
        echo "::error::Refusing to bundle ${resolved}; libgomp must be linked statically for deployable macOS artifacts."
        exit 1
        ;;
    esac
    if [ ! -f "${FRAMEWORKS}/${libname}" ]; then
      echo "  Bundling: $resolved -> ${FRAMEWORKS}/${libname}"
      run_logged cp "$resolved" "${FRAMEWORKS}/${libname}"
      run_logged chmod 644 "${FRAMEWORKS}/${libname}"
      run_logged install_name_tool -id "@rpath/${libname}" "${FRAMEWORKS}/${libname}"
      bundle_dylib "${FRAMEWORKS}/${libname}"
    fi
    if [ "$lib" != "@rpath/${libname}" ]; then
      run_logged install_name_tool -change "$lib" "@rpath/${libname}" "$binary"
    fi
  done < <(otool -L "$binary" | awk 'NR > 1 {print $1}' || true)
}

ensure_app_rpath() {
  local binary="$1"
  local rpath

  while IFS= read -r rpath; do
    [ -n "$rpath" ] || continue
    case "$rpath" in
      "$WORKSPACE_PREFIX"/*|"$HOMEBREW_PREFIX"/*)
        run_logged install_name_tool -delete_rpath "$rpath" "$binary"
        ;;
    esac
  done < <(otool -l "$binary" | awk '/LC_RPATH/{getline; getline; print $2}' || true)

  if ! otool -l "$binary" | awk '/LC_RPATH/{getline; getline; print $2}' | grep -Fxq "@executable_path/../Frameworks"; then
    run_logged install_name_tool -add_rpath "@executable_path/../Frameworks" "$binary"
  fi
}

for exe in "${APP}/Contents/MacOS/"*; do
  [ -f "$exe" ] && [ -x "$exe" ] && {
    ensure_app_rpath "$exe"
    bundle_dylib "$exe"
    for fw in $(otool -L "$exe" | awk '{print $1}' | grep '\.framework/' || true); do
      fwname=$(basename "$fw")
      bundled=$(find "${FRAMEWORKS}" -name "${fwname}" -path "*.framework/*" 2>/dev/null | head -1)
      if [ -n "$bundled" ]; then
        app_contents="${APP}/Contents/"
        fwrel="${bundled:${#app_contents}}"
        if [ "$fw" != "@executable_path/../${fwrel}" ]; then
          run_logged install_name_tool -change "$fw" "@executable_path/../${fwrel}" "$exe"
        fi
      fi
    done
  }
done

for fw in "${FRAMEWORKS}"/*.dylib; do
  [ -f "$fw" ] && bundle_dylib "$fw"
done

for exe in "${APP}/Contents/MacOS/"*; do
  [ -f "$exe" ] && [ -x "$exe" ] && bundle_dylib "$exe"
done

echo "Checking for remaining external references..."
remaining_refs="${RUNNER_TEMP:-/tmp}/wsjtx-app-remaining-refs.txt"
: > "$remaining_refs"
while IFS= read -r binary; do
  while IFS= read -r ref; do
    case "$ref" in
      "$HOMEBREW_PREFIX"/*|"$WORKSPACE_PREFIX"/*)
        echo "${binary}: ${ref}" >> "$remaining_refs"
        ;;
    esac
    if [ -n "$FORTRAN_ORIGINAL_PREFIX" ] && [[ "$ref" == "$FORTRAN_ORIGINAL_PREFIX"/* ]]; then
      echo "${binary}: ${ref}" >> "$remaining_refs"
    fi
  done < <(otool -L "$binary" | awk 'NR > 1 {print $1}' || true)
done < <(find "${APP}/Contents/MacOS" -type f -perm +111 -print 2>/dev/null)
if [ -s "$remaining_refs" ]; then
  echo "::error::External build-time paths still present:"
  cat "$remaining_refs"
  exit 1
fi

echo "All dylibs bundled successfully"
