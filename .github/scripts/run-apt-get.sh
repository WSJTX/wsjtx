#!/bin/bash

set -euo pipefail

if [ "$#" -eq 0 ]; then
  echo "usage: $0 <apt-get arguments...>" >&2
  exit 2
fi

case "$1" in
  update)
    timeout_limit="${APT_UPDATE_TIMEOUT:-8m}"
    ;;
  install)
    timeout_limit="${APT_INSTALL_TIMEOUT:-20m}"
    ;;
  *)
    timeout_limit="${APT_COMMAND_TIMEOUT:-20m}"
    ;;
esac

apt_options=(
  -o Acquire::Retries=3
  -o Acquire::http::Timeout=30
  -o Acquire::https::Timeout=30
  -o DPkg::Lock::Timeout=60
)
timeout_command=(timeout --signal=TERM --kill-after=30s "$timeout_limit")

if [ "$(id -u)" != "0" ]; then
  timeout_command=(sudo "${timeout_command[@]}")
fi

status=0
"${timeout_command[@]}" apt-get "${apt_options[@]}" "$@" || status=$?
if [ "$status" -eq 124 ] || [ "$status" -eq 137 ]; then
  echo "::error::apt-get $1 exceeded or was killed while enforcing its ${timeout_limit} wall-clock limit"
fi
exit "$status"
