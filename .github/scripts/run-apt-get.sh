#!/bin/bash

set -euo pipefail

if [ "$#" -eq 0 ]; then
  echo "usage: $0 <apt-get arguments...>" >&2
  exit 2
fi

apt_options=(
  -o Acquire::Retries=3
  -o Acquire::http::Timeout=30
  -o Acquire::https::Timeout=30
  -o DPkg::Lock::Timeout=60
)

run_privileged() {
  if [ "$(id -u)" = "0" ]; then
    "$@"
  else
    sudo "$@"
  fi
}

run_with_timeout() {
  local timeout_limit="$1"
  shift
  run_privileged timeout --signal=TERM --kill-after=30s "$timeout_limit" "$@"
}

is_timeout_status() {
  [ "$1" -eq 124 ] || [ "$1" -eq 137 ]
}

case "$1" in
  update)
    timeout_limit="${APT_UPDATE_TIMEOUT:-15m}"
    status=0
    run_with_timeout "$timeout_limit" apt-get \
      "${apt_options[@]}" -o APT::Update::Error-Mode=any "$@" || status=$?
    if is_timeout_status "$status"; then
      echo "::error::apt-get update exceeded or was killed while enforcing its ${timeout_limit} wall-clock limit"
    fi
    exit "$status"
    ;;
  install)
    download_timeout="${APT_DOWNLOAD_TIMEOUT:-${APT_INSTALL_TIMEOUT:-20m}}"
    download_attempts="${APT_DOWNLOAD_ATTEMPTS:-2}"
    case "$download_attempts" in
      ''|*[!0-9]*|0)
        echo "::error::APT_DOWNLOAD_ATTEMPTS must be a positive integer" >&2
        exit 2
        ;;
    esac

    for ((attempt = 1; attempt <= download_attempts; ++attempt)); do
      echo "::group::APT package acquisition attempt ${attempt}/${download_attempts}"
      status=0
      run_with_timeout "$download_timeout" apt-get \
        "${apt_options[@]}" --download-only "$@" || status=$?
      echo "::endgroup::"

      if [ "$status" -eq 0 ]; then
        break
      fi
      if ! is_timeout_status "$status"; then
        echo "::error::apt-get install failed during package acquisition with status ${status}"
        exit "$status"
      fi
      if [ "$attempt" -eq "$download_attempts" ]; then
        echo "::error::apt-get install package acquisition exceeded or was killed while enforcing its ${download_timeout} wall-clock limit"
        exit "$status"
      fi
      echo "::warning::apt-get install package acquisition exceeded its ${download_timeout} wall-clock limit; retrying with cached partial downloads"
    done

    echo "::group::APT package installation from local cache"
    status=0
    run_privileged apt-get "${apt_options[@]}" --no-download "$@" || status=$?
    echo "::endgroup::"
    if [ "$status" -ne 0 ]; then
      echo "::error::apt-get install failed during package unpacking or configuration with status ${status}"
      run_privileged dpkg --audit || true
    fi
    exit "$status"
    ;;
  *)
    timeout_limit="${APT_COMMAND_TIMEOUT:-20m}"
    status=0
    run_with_timeout "$timeout_limit" apt-get "${apt_options[@]}" "$@" || status=$?
    if is_timeout_status "$status"; then
      echo "::error::apt-get $1 exceeded or was killed while enforcing its ${timeout_limit} wall-clock limit"
    fi
    exit "$status"
    ;;
esac
