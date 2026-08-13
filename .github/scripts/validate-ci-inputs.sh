#!/usr/bin/env bash
# Validate CI string inputs passed through environment variables.
#
# GitHub Actions expands ${{ }} expressions before the shell parses a
# `run:` script. Quoting in YAML does not stop command substitution or
# quote-breaking payloads in those expansions. Callers must pass
# workflow inputs through `env:` (which is not shell-parsed) and invoke
# this script before any later step interpolates the same values.
#
# Only variables that are set are checked, so each workflow can pass
# the subset it uses. Empty values are rejected except for
# WSJT_RC_NUMBER, which is optional.
#
# Usage:
#   WSJTX_VERSION=... HAMLIB_BRANCH=... .github/scripts/validate-ci-inputs.sh
#   .github/scripts/validate-ci-inputs.sh --self-test

set -euo pipefail

fail() {
  echo "::error::$*" >&2
  exit 1
}

# POSIX case patterns keep this working on bash 3.2 (macOS) and MSYS2.
require_nonempty() {
  if [ -z "$2" ]; then
    fail "Invalid $1: value must not be empty"
  fi
}

validate_inputs() {
  if [ -n "${WSJTX_VERSION+x}" ]; then
    require_nonempty "version" "$WSJTX_VERSION"
    case "$WSJTX_VERSION" in
      *[!A-Za-z0-9._+-]*)
        fail "Invalid version '${WSJTX_VERSION}': use only letters, numbers, '.', '_', '+', and '-'"
        ;;
    esac
  fi

  if [ -n "${HAMLIB_BRANCH+x}" ]; then
    require_nonempty "hamlib_branch" "$HAMLIB_BRANCH"
    case "$HAMLIB_BRANCH" in
      *[!A-Za-z0-9._/-]*)
        fail "Invalid hamlib_branch '${HAMLIB_BRANCH}': use only letters, numbers, '.', '_', '/', and '-'"
        ;;
    esac
  fi

  if [ -n "${WSJT_RELEASE_CHANNEL+x}" ]; then
    case "$WSJT_RELEASE_CHANNEL" in
      DEVEL|RC|GA) ;;
      *)
        fail "Invalid release_channel '${WSJT_RELEASE_CHANNEL}': use DEVEL, RC, or GA"
        ;;
    esac
  fi

  if [ -n "${WSJT_RC_NUMBER+x}" ]; then
    case "$WSJT_RC_NUMBER" in
      ''|*[!0-9]*)
        if [ -n "$WSJT_RC_NUMBER" ]; then
          fail "Invalid rc_number '${WSJT_RC_NUMBER}': use digits only"
        fi
        ;;
    esac
  fi

  if [ -n "${ARCH+x}" ]; then
    case "$ARCH" in
      x86_64|aarch64|armhf|arm64) ;;
      *)
        fail "Invalid arch '${ARCH}': use x86_64, aarch64, armhf, or arm64"
        ;;
    esac
  fi

  if [ -n "${DEPLOYMENT_TARGET+x}" ]; then
    case "$DEPLOYMENT_TARGET" in
      ''|*[!0-9.]*)
        fail "Invalid deployment_target '${DEPLOYMENT_TARGET}': use a dotted numeric version such as 10.13 or 11.0"
        ;;
    esac
  fi

  if [ -n "${RUNNER+x}" ]; then
    require_nonempty "runner" "$RUNNER"
    case "$RUNNER" in
      *[!A-Za-z0-9._-]*)
        fail "Invalid runner '${RUNNER}': use only letters, numbers, '.', '_', and '-'"
        ;;
    esac
  fi

  if [ -n "${SIGN_MODE+x}" ]; then
    case "$SIGN_MODE" in
      ephemeral|none) ;;
      *)
        fail "Invalid sign_mode '${SIGN_MODE}': use ephemeral or none"
        ;;
    esac
  fi
}

run_isolated() {
  env -u WSJTX_VERSION -u HAMLIB_BRANCH -u WSJT_RELEASE_CHANNEL \
    -u WSJT_RC_NUMBER -u ARCH -u DEPLOYMENT_TARGET -u RUNNER -u SIGN_MODE \
    "$@" "$0"
}

expect_failure() {
  local label="$1"
  shift
  if run_isolated env "$@" >/dev/null 2>&1; then
    echo "self-test failed: expected rejection of ${label}" >&2
    exit 1
  fi
}

if [ "${1:-}" = "--self-test" ]; then
  run_isolated \
    env WSJTX_VERSION="3.2.0-devel" HAMLIB_BRANCH="4.7.2" \
        WSJT_RELEASE_CHANNEL="DEVEL" WSJT_RC_NUMBER="" \
    >/dev/null
  run_isolated \
    env WSJTX_VERSION="3.0.1-rc1" HAMLIB_BRANCH="4.7.2" \
        WSJT_RELEASE_CHANNEL="RC" WSJT_RC_NUMBER="1" \
        ARCH="x86_64" DEPLOYMENT_TARGET="10.13" RUNNER="macos-15-intel" \
        SIGN_MODE="ephemeral" \
    >/dev/null
  run_isolated env HAMLIB_BRANCH="integration/4.7" >/dev/null

  expect_failure 'command substitution in version' WSJTX_VERSION='3.1.0$(id)'
  expect_failure 'backticks in version' WSJTX_VERSION='3.1.0`id`'
  expect_failure 'quote break in version' WSJTX_VERSION='3.1.0"; echo pwned #'
  expect_failure 'command substitution in hamlib_branch' HAMLIB_BRANCH='4.7.2$(touch /tmp/x)'
  expect_failure 'shell metacharacters in hamlib_branch' HAMLIB_BRANCH='4.7.2; id'
  expect_failure 'invalid channel' WSJT_RELEASE_CHANNEL='dev'
  expect_failure 'non-numeric rc' WSJT_RC_NUMBER='1a'
  expect_failure 'empty version' WSJTX_VERSION=''
  expect_failure 'invalid arch' ARCH='x86_64; id'
  expect_failure 'command substitution in deployment_target' DEPLOYMENT_TARGET='11.0$(id)'

  echo "validate-ci-inputs.sh self-test passed"
  exit 0
fi

validate_inputs
