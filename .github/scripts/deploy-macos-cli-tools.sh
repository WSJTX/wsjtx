#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  cat <<'USAGE'
Usage: deploy-macos-cli-tools.sh --assets PATH --stage PATH --workspace-prefix PATH --boost-prefix PATH --fftw-prefix PATH --libusb-prefix PATH --portaudio-prefix PATH --fortran-prefix PATH --gcc-lib PATH [--fortran-original-prefix PATH] [--homebrew-prefix PATH]
USAGE
}

ASSETS=""
STAGE=""
WORKSPACE_PREFIX="${GITHUB_WORKSPACE:-}"
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
    --assets) ASSETS="$2"; shift 2 ;;
    --stage) STAGE="$2"; shift 2 ;;
    --workspace-prefix) WORKSPACE_PREFIX="$2"; shift 2 ;;
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

for value_name in ASSETS STAGE WORKSPACE_PREFIX BOOST_PREFIX FFTW_PREFIX LIBUSB_PREFIX PORTAUDIO_PREFIX FORTRAN_PREFIX GCC_LIB HOMEBREW_PREFIX; do
  if [ -z "${!value_name}" ]; then
    echo "Missing required argument or environment: ${value_name}" >&2
    usage >&2
    exit 2
  fi
done

LIBDIR="${ASSETS}/lib"
mkdir -p "${LIBDIR}"

dump_cli_context() {
  local status="$1"
  local line="$2"
  local command="$3"
  echo "::error::deploy-macos-cli-tools failed at line ${line} with status ${status}: ${command}"
  echo "ASSETS=${ASSETS}"
  echo "LIBDIR=${LIBDIR}"
  echo "FORTRAN_PREFIX=${FORTRAN_PREFIX}"
  echo "FORTRAN_ORIGINAL_PREFIX=${FORTRAN_ORIGINAL_PREFIX}"
  echo "GCC_LIB=${GCC_LIB}"
  echo "HOMEBREW_PREFIX=${HOMEBREW_PREFIX}"
  echo "Asset contents:"
  ls -la "${ASSETS}" || true
  echo "Library contents:"
  ls -la "${LIBDIR}" || true
  echo "External references in CLI assets:"
  for bin in "${ASSETS}"/*; do
    [ -f "$bin" ] && [ -x "$bin" ] && [ "$(basename "$bin")" != "lib" ] || continue
    echo "== $bin =="
    otool -L "$bin" || true
  done
}
trap 'status=$?; dump_cli_context "$status" "$LINENO" "$BASH_COMMAND"; exit "$status"' ERR

run_logged() {
  echo "+ $*"
  "$@"
}

resolve_cli_ref() {
  local ref="$1"
  local name="" suffix=""

  if [ -n "$FORTRAN_ORIGINAL_PREFIX" ] && [[ "$ref" == "$FORTRAN_ORIGINAL_PREFIX"/* ]]; then
    local original_prefix="${FORTRAN_ORIGINAL_PREFIX}/"
    local relocated="${FORTRAN_PREFIX}/${ref:${#original_prefix}}"
    [ -f "$relocated" ] && echo "$relocated"
    return 0
  fi

  case "$ref" in
    /usr/lib/*|/System/Library/*)
      return 0
      ;;
    @loader_path/lib/*)
      name="${ref#@loader_path/lib/}"
      [ -f "${LIBDIR}/${name}" ] && echo "${LIBDIR}/${name}"
      return 0
      ;;
    @loader_path/*)
      name="${ref#@loader_path/}"
      [ -f "${LIBDIR}/${name}" ] && echo "${LIBDIR}/${name}"
      return 0
      ;;
    "$WORKSPACE_PREFIX"/*|"$HOMEBREW_PREFIX"/*)
      echo "$ref"
      return 0
      ;;
  esac

  if [[ "$ref" == @rpath/* ]]; then
    suffix="${ref#@rpath/}"
    if [[ "$suffix" == *.framework/* ]]; then
      for dir in \
        "${STAGE}/wsjtx.app/Contents/Frameworks" \
        "${WORKSPACE_PREFIX}/qt-prefix/lib"
      do
        [ -f "${dir}/${suffix}" ] && echo "${dir}/${suffix}" && return 0
      done
    else
      name="${suffix}"
      for dir in \
        "${LIBDIR}" \
        "${STAGE}/wsjtx.app/Contents/Frameworks" \
        "${BOOST_PREFIX}/lib" \
        "${FFTW_PREFIX}/lib" \
        "${LIBUSB_PREFIX}/lib" \
        "${PORTAUDIO_PREFIX}/lib" \
        "${FORTRAN_PREFIX}/lib" \
        "${GCC_LIB}" \
        "${HOMEBREW_PREFIX}/lib"
      do
        [ -f "${dir}/${name}" ] && echo "${dir}/${name}" && return 0
      done
    fi
  fi

  return 0
}

bundle_cli_deps() {
  local binary="$1"
  local change_prefix="$2"
  local ref resolved out name target

  while IFS= read -r ref; do
    resolved=$(resolve_cli_ref "$ref")
    if [ -z "$resolved" ]; then
      case "$ref" in
        @rpath/*|@loader_path/*|"$WORKSPACE_PREFIX"/*|"$HOMEBREW_PREFIX"/*)
          echo "::error::Could not resolve ${ref} required by ${binary}"
          exit 1
          ;;
        *)
          continue
          ;;
      esac
    fi

    name="$(basename "$resolved")"
    case "$name" in
      libgomp*.dylib)
        echo "::error::Refusing to bundle ${resolved}; libgomp must be linked statically for deployable macOS artifacts."
        exit 1
        ;;
    esac

    out="${LIBDIR}/${name}"
    if [ ! -f "$out" ]; then
      echo "  Bundling: ${resolved} -> ${out}"
      run_logged cp "$resolved" "$out"
      run_logged chmod 644 "$out"
      run_logged install_name_tool -id "@rpath/${name}" "$out"
      bundle_cli_deps "$out" "@rpath"
    fi

    target="${change_prefix}/${name}"
    if [ "$ref" != "$target" ]; then
      run_logged install_name_tool -change "$ref" "$target" "$binary"
    fi
  done < <(otool -L "$binary" | awk 'NR > 1 {print $1}' | grep -E "($WORKSPACE_PREFIX|$HOMEBREW_PREFIX|@rpath|@loader_path)" || true)
}

ensure_cli_rpath() {
  local binary="$1"
  local rpath
  local bad_rpaths=()
  local unique_bad_rpaths=()

  while IFS= read -r rpath; do
    [ -n "$rpath" ] || continue
    case "$rpath" in
      "$WORKSPACE_PREFIX"/*|"$HOMEBREW_PREFIX"/*)
        bad_rpaths+=("$rpath")
        ;;
    esac
  done < <(otool -l "$binary" | awk '/LC_RPATH/{getline; getline; print $2}' || true)

  if [ "${#bad_rpaths[@]}" -gt 0 ]; then
    for rpath in "${bad_rpaths[@]}"; do
      local found=0
      local u
      if [ "${#unique_bad_rpaths[@]}" -gt 0 ]; then
        for u in "${unique_bad_rpaths[@]}"; do
          [ "$u" = "$rpath" ] && found=1 && break
        done
      fi
      [ "$found" -eq 0 ] && unique_bad_rpaths+=("$rpath")
    done
  fi

  if [ "${#unique_bad_rpaths[@]}" -gt 0 ]; then
    for rpath in "${unique_bad_rpaths[@]}"; do
      run_logged install_name_tool -delete_rpath "$rpath" "$binary"
    done
  fi

  if ! otool -l "$binary" | awk '/LC_RPATH/{getline; getline; print $2}' | grep -Fxq "@loader_path/lib"; then
    run_logged install_name_tool -add_rpath "@loader_path/lib" "$binary"
  fi
}

for bin in "${ASSETS}"/*; do
  [ -f "$bin" ] && [ -x "$bin" ] && [ "$(basename "$bin")" != "lib" ] || continue
  ensure_cli_rpath "$bin"
  bundle_cli_deps "$bin" "@rpath"
  run_logged codesign --force --sign - "$bin"
done

for lib in "${LIBDIR}"/*; do
  [ -f "$lib" ] || continue
  bundle_cli_deps "$lib" "@rpath"
  run_logged codesign --force --sign - "$lib"
done

echo "Verifying CLI tools..."
for bin in "${ASSETS}"/*; do
  [ -f "$bin" ] && [ -x "$bin" ] && [ "$(basename "$bin")" != "lib" ] && {
    BAD=$(otool -L "$bin" | grep "$HOMEBREW_PREFIX" || true)
    if [ -n "$BAD" ]; then
      echo "ERROR: $(basename "$bin") still has Homebrew refs:"
      echo "$BAD"
      exit 1
    fi
  }
done
echo "All CLI tools are self-contained"

cp -R "${LIBDIR}" "${STAGE}/lib"
for bin in "${ASSETS}"/*; do
  [ -f "$bin" ] && [ -x "$bin" ] && [ "$(basename "$bin")" != "lib" ] && {
    cp "$bin" "${STAGE}/$(basename "$bin")"
  }
done
