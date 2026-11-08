#!/usr/bin/env bash

# shellcheck disable=SC2034
LINUX_BOOST_PREFIX=/opt/wsjtx/boost-static
_boost_config_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
# shellcheck source=.github/scripts/boost-version.sh
. "$_boost_config_dir/boost-version.sh"
unset _boost_config_dir

linux_boost_source_config() {
  if [ "$1" != "$BOOST_VERSION" ]; then
    echo "Unsupported Linux Boost version: $1 (expected $BOOST_VERSION)" >&2
    return 2
  fi
  LINUX_BOOST_VERSION=$BOOST_VERSION
  LINUX_BOOST_SOURCE_SHA256=$BOOST_SOURCE_SHA256
  LINUX_BOOST_SOURCE_URL=$BOOST_SOURCE_URL
}

linux_boost_recipe_hash() {
  local script_dir file
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
  for file in build-boost-linux.sh linux-boost-config.sh boost-version.sh patch-boost-log-queue.py; do
    sha256sum "$script_dir/$file" | awk '{print $1}'
  done | sha256sum | awk '{print $1}'
}
