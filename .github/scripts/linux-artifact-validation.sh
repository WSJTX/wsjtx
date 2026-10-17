#!/bin/bash
# Shared acceptance checks for Linux build products.

linux_validation_error() {
  echo "::error::$*" >&2
  return 1
}

validate_elf_metadata() {
  local arch="$1"
  local path="$2"
  local file_output="$3"
  local header_output="$4"
  local program_output="$5"

  case "$arch" in
    armhf)
      [[ "$file_output" == *"ELF 32-bit"* ]] || {
        linux_validation_error "$path is not ELF32"
        return 1
      }
      [[ "$file_output" == *"ARM, EABI5"* ]] || {
        linux_validation_error "$path is not ARM EABI5"
        return 1
      }
      [[ "$header_output" == *"Class:"*"ELF32"* ]] || {
        linux_validation_error "$path has the wrong ELF class for armhf"
        return 1
      }
      [[ "$header_output" == *"Machine:"*"ARM"* ]] || {
        linux_validation_error "$path has the wrong ELF machine for armhf"
        return 1
      }
      [[ "$header_output" == *"Version5 EABI"* ]] || {
        linux_validation_error "$path does not use EABI5"
        return 1
      }
      [[ "$header_output" == *"hard-float ABI"* ]] || {
        linux_validation_error "$path does not use the ARM hard-float ABI"
        return 1
      }
      if [[ "$file_output" == *"executable"*"dynamically linked"* ]] &&
        [[ "$program_output" != *"/lib/ld-linux-armhf.so.3"* ]]; then
        linux_validation_error "$path does not request /lib/ld-linux-armhf.so.3"
        return 1
      fi
      ;;
    x86_64)
      [[ "$file_output" == *"ELF 64-bit"* && "$file_output" == *"x86-64"* ]] || {
        linux_validation_error "$path is not a 64-bit x86-64 ELF"
        return 1
      }
      ;;
    aarch64)
      [[ "$file_output" == *"ELF 64-bit"* && "$file_output" == *"ARM aarch64"* ]] || {
        linux_validation_error "$path is not a 64-bit AArch64 ELF"
        return 1
      }
      ;;
    *)
      linux_validation_error "Unsupported Linux architecture: $arch"
      return 2
      ;;
  esac
}

validate_elf_executable() {
  local arch="$1"
  local path="$2"
  local file_output
  local header_output
  local program_output
  local dynamic_output

  if [ ! -x "$path" ]; then
    linux_validation_error "Required executable is missing: $path"
    return 1
  fi

  file_output=$(file -- "$path") || return
  header_output=$(readelf -hW -- "$path") || return
  program_output=$(readelf -lW -- "$path") || return
  echo "$file_output"
  validate_elf_metadata "$arch" "$path" "$file_output" "$header_output" "$program_output" || return
  if [ "${path##*/}" = jt9 ]; then
    dynamic_output=$(readelf -dW -- "$path") || return
    if printf '%s\n' "$dynamic_output" | awk '/\(NEEDED\)/ && /\[libQt/ { found=1 } END { exit found ? 0 : 1 }'; then
      linux_validation_error "$path must not depend on Qt"
      return 1
    fi
  fi
}

validate_armhf_elf_tree() {
  local root="$1" distro="$2" ceiling path metadata header program dynamic versions version
  local readelf_command="${READELF:-readelf}" elf_count=0
  case "$distro" in
    bookworm) ceiling=2.36 ;;
    trixie) ceiling=2.41 ;;
    *) linux_validation_error "Unknown ARMHF distro: $distro"; return 2 ;;
  esac
  while IFS= read -r -d '' path; do
    metadata=$(file -- "$path") || return
    [[ "$metadata" == *ELF* ]] || continue
    [[ "$metadata" != *relocatable* ]] || continue
    header=$("$readelf_command" -hW -- "$path") || return
    program=$("$readelf_command" -lW -- "$path") || return
    validate_elf_metadata armhf "$path" "$metadata" "$header" "$program" || return
    dynamic=$("$readelf_command" -dW -- "$path") || return
    if [[ "$dynamic" == *"Shared library: [libquadmath.so"* ]]; then
      linux_validation_error "$path unexpectedly depends on libquadmath"
      return 1
    fi
    versions=$("$readelf_command" -VW -- "$path" | awk '
      /^Version needs section/ { needs = 1; next }
      /^Version (definition|symbols) section/ { needs = 0 }
      needs { print }
    ') || return
    if [[ "$versions" == *GLIBC_PRIVATE* ]]; then
      linux_validation_error "$path requires a private glibc symbol"
      return 1
    fi
    while [[ "$versions" =~ GLIBC_([0-9]+\.[0-9]+(\.[0-9]+)?) ]]; do
      version=${BASH_REMATCH[1]}
      versions=${versions#*"${BASH_REMATCH[0]}"}
      if dpkg --compare-versions "$version" gt "$ceiling"; then
        linux_validation_error "$path requires GLIBC_$version, above $distro ceiling $ceiling"
        return 1
      fi
    done
    elf_count=$((elf_count + 1))
  done < <(find "$root" -type f -print0)
  if [ "$elf_count" -eq 0 ]; then
    linux_validation_error "No ELF executables or shared libraries found below $root"
    return 1
  fi
}

validate_linux_build_executables() {
  local build_dir="$1"
  local arch="$2"
  local executable

  for executable in wsjtx jt9 qmap/qmap map65/map65 ft8code wsprd; do
    validate_elf_executable "$arch" "$build_dir/$executable" || return
  done
}

linux_manifest_has_path() {
  local manifest="$1"
  local path="$2"
  case $'\n'"$manifest"$'\n' in
    *"$path"$'\n'*) return 0 ;;
    *) return 1 ;;
  esac
}

validate_linux_package_manifest() {
  local manifest="$1"
  local label="$2"
  local path

  for path in \
    /usr/bin/wsjtx \
    /usr/bin/jt9 \
    /usr/bin/qmap \
    /usr/bin/map65 \
    /usr/share/wsjtx/ALLCALL7.TXT \
    /usr/share/wsjtx/CALL3.TXT \
    /usr/share/wsjtx/sounds/Message.wav \
    /usr/share/wsjtx/sounds/Testing123.wav; do
    if ! linux_manifest_has_path "$manifest" "$path"; then
      linux_validation_error "$label is missing expected package path: $path"
      return 1
    fi
  done

  case $'\n'"$manifest"$'\n' in
    *"/usr/bin/sounds"$'\n'* | *"/usr/bin/sounds/"*)
      linux_validation_error "$label contains unexpected package path: /usr/bin/sounds"
      return 1
      ;;
  esac
}

validate_deb_package() {
  local package="$1"
  local manifest

  manifest=$(dpkg-deb --fsys-tarfile "$package" | tar -tf -) || return
  validate_linux_package_manifest "$manifest" "DEB $package"
}

validate_rpm_package() {
  local package="$1"
  local manifest

  manifest=$(rpm -qpl "$package") || return
  printf '%s\n' "$manifest" | sed -n '1,40p'
  validate_linux_package_manifest "$manifest" "RPM $package"
}

linux_require_executable() {
  local path="$1"
  if [ ! -x "$path" ]; then
    linux_validation_error "Required executable is missing: $path"
    return 1
  fi
}

linux_require_file() {
  local path="$1"
  if [ ! -f "$path" ]; then
    linux_validation_error "Required file is missing: $path"
    return 1
  fi
}

validate_linux_application_layout() {
  local root="$1"
  local kind="$2"
  local path

  for path in wsjtx jt9 qmap map65 ft8code; do
    linux_require_executable "$root/usr/bin/$path" || return
  done
  if [ "$kind" = appimage ]; then
    linux_require_executable "$root/usr/bin/wsprd" || return
  elif [ "$kind" != appdir ]; then
    linux_validation_error "Unknown application tree kind: $kind"
    return 2
  fi

  for path in \
    usr/share/wsjtx/ALLCALL7.TXT \
    usr/share/wsjtx/CALL3.TXT \
    usr/share/wsjtx/sounds/Message.wav \
    usr/share/wsjtx/sounds/Testing123.wav; do
    linux_require_file "$root/$path" || return
  done
  if [ -e "$root/usr/bin/sounds" ] || [ -L "$root/usr/bin/sounds" ]; then
    linux_validation_error "Unexpected executable-adjacent sounds path is present: $root/usr/bin/sounds"
    return 1
  fi

  if [ "$kind" = appdir ]; then
    linux_require_file "$root/usr/share/applications/wsjtx.desktop" || return
    linux_require_file "$root/usr/share/pixmaps/wsjtx_icon.png" || return
    return 0
  fi

  for path in \
    usr/bin/qt.conf \
    usr/plugins/platforms/libqxcb.so \
    usr/plugins/sqldrivers/libqsqlite.so; do
    linux_require_file "$root/$path" || return
  done
  if ! grep -Eq '^[[:space:]]*Plugins[[:space:]]*=[[:space:]]*plugins[[:space:]]*$' \
    "$root/usr/bin/qt.conf"; then
    linux_validation_error "AppImage qt.conf must set Plugins = plugins"
    echo "AppImage qt.conf contents:"
    cat "$root/usr/bin/qt.conf"
    return 1
  fi

  for path in libQt5Core.so libQt5Widgets.so libQt5Multimedia.so; do
    if [ -z "$(find "$root/usr/lib" -maxdepth 1 -name "$path*" -print -quit 2>/dev/null)" ]; then
      linux_validation_error "Required AppImage Qt library is missing: $path"
      return 1
    fi
  done

  if [ -z "$(find "$root" -path '*/plugins/audio/*.so' -print -quit 2>/dev/null)" ]; then
    linux_validation_error "No Qt audio backend plugin is bundled in the AppImage"
    echo "AppImage plugin tree:"
    find "$root" -path '*/plugins/*' -name '*.so' -print 2>/dev/null | sed -n '1,20p'
    return 1
  fi
}

validate_linux_application_tree() {
  local root="$1"
  local kind="$2"
  local arch="$3"
  local executable

  validate_linux_application_layout "$root" "$kind" || return
  for executable in wsjtx jt9 qmap map65 ft8code; do
    validate_elf_executable "$arch" "$root/usr/bin/$executable" || return
  done
  if [ "$kind" = appimage ]; then
    validate_elf_executable "$arch" "$root/usr/bin/wsprd" || return
    echo "Qt audio plugins bundled:"
    find "$root" -path '*/plugins/audio/*.so' -print 2>/dev/null
  fi
}

run_packaged_appimage_startup_smoke() {
  local appimage="$1"
  local log="$2"
  local status
  local runner=()
  if [ -n "${LINUX_APPIMAGE_RUNNER:-}" ]; then
    runner=("$LINUX_APPIMAGE_RUNNER")
  fi

  mkdir -p "$(dirname "$log")"
  if env \
    -u LD_LIBRARY_PATH \
    -u LD_PRELOAD \
    -u LD_AUDIT \
    -u QT_PLUGIN_PATH \
    -u QT_QPA_PLATFORM_PLUGIN_PATH \
    -u QML_IMPORT_PATH \
    -u QML2_IMPORT_PATH \
    APPIMAGE_EXTRACT_AND_RUN=1 \
    QT_QPA_PLATFORM=xcb \
    timeout --kill-after=5s 45s \
      xvfb-run -a -s "-screen 0 1280x1024x24" \
      "${runner[@]}" \
      "$appimage" \
      --startup-smoke-test \
      --rig-name CI-APPIMAGE-STARTUP \
      >"$log" 2>&1; then
    status=0
  else
    status=$?
  fi

  cat "$log"
  if [ "$status" -eq 124 ]; then
    linux_validation_error "AppImage startup smoke test timed out after 45 seconds"
    return 1
  fi
  if [ "$status" -ne 0 ]; then
    linux_validation_error "AppImage startup smoke test exited with status $status"
    return 1
  fi
  if ! grep -Fq "WSJT-X startup smoke test passed" "$log"; then
    linux_validation_error "AppImage exited successfully without the startup-smoke success marker"
    return 1
  fi
}
