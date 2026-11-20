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
# WSJT_RELEASE_CHANNEL, where empty selects release-state.txt, and
# WSJT_PRERELEASE_NUMBER, which is optional.
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

require_boolean() {
  case "$2" in
    true|false) ;;
    *) fail "Invalid $1 '$2': use true or false" ;;
  esac
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
      ''|DEVEL|BETA|RC|GA) ;;
      *)
        fail "Invalid release_channel '${WSJT_RELEASE_CHANNEL}': use DEVEL, BETA, RC, GA, or empty"
        ;;
    esac
  fi

  if [ -n "${WSJT_PRERELEASE_NUMBER+x}" ]; then
    case "$WSJT_PRERELEASE_NUMBER" in
      ''|*[!0-9]*)
        if [ -n "$WSJT_PRERELEASE_NUMBER" ]; then
          fail "Invalid prerelease_number '${WSJT_PRERELEASE_NUMBER}': use digits only"
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

  if [ -n "${VALIDATE_FORTRAN_FALLBACK+x}" ]; then
    require_boolean "validate_fortran_fallback" "$VALIDATE_FORTRAN_FALLBACK"
  fi

  if [ -n "${RUNTIME_TESTS+x}" ]; then
    case "$RUNTIME_TESTS" in
      include|exclude|only) ;;
      *) fail "Invalid runtime_tests '${RUNTIME_TESTS}': use include, exclude, or only" ;;
    esac
  fi

  # 0 keeps the repository's retention; upload-artifact allows at most 90 days.
  if [ -n "${ARTIFACT_RETENTION_DAYS+x}" ]; then
    case "$ARTIFACT_RETENTION_DAYS" in
      [0-9]|[1-8][0-9]|90) ;;
      *) fail "Invalid artifact_retention_days '${ARTIFACT_RETENTION_DAYS}': use a whole number of days from 0 to 90" ;;
    esac
  fi

  if [ -n "${IMAGE_TAG+x}" ]; then
    require_nonempty "image_tag" "$IMAGE_TAG"
    if [ "${#IMAGE_TAG}" -gt 128 ]; then
      fail "Invalid image_tag: value exceeds the 128-character container tag limit"
    fi
    case "$IMAGE_TAG" in
      *[!A-Za-z0-9._-]*)
        fail "Invalid image_tag '${IMAGE_TAG}': use a letter or number followed by letters, numbers, '.', '_', or '-'"
        ;;
    esac
    case "$IMAGE_TAG" in
      [A-Za-z0-9]*) ;;
      *) fail "Invalid image_tag '${IMAGE_TAG}': the first character must be a letter or number" ;;
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
}

run_isolated() {
  env -u WSJTX_VERSION -u HAMLIB_BRANCH -u WSJT_RELEASE_CHANNEL \
    -u WSJT_PRERELEASE_NUMBER -u ARCH -u IMAGE_TAG -u DEPLOYMENT_TARGET -u RUNNER \
    -u VALIDATE_FORTRAN_FALLBACK -u RUNTIME_TESTS -u ARTIFACT_RETENTION_DAYS \
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
        WSJT_RELEASE_CHANNEL="DEVEL" WSJT_PRERELEASE_NUMBER="" \
    >/dev/null
  run_isolated \
    env WSJTX_VERSION="3.3.0-beta1" \
        WSJT_RELEASE_CHANNEL="BETA" WSJT_PRERELEASE_NUMBER="1" \
    >/dev/null
  run_isolated env WSJT_RELEASE_CHANNEL="" WSJT_PRERELEASE_NUMBER="" >/dev/null
  run_isolated \
    env WSJTX_VERSION="3.0.1-rc1" HAMLIB_BRANCH="4.7.2" \
        WSJT_RELEASE_CHANNEL="RC" WSJT_PRERELEASE_NUMBER="1" \
        ARCH="x86_64" DEPLOYMENT_TARGET="10.13" RUNNER="macos-15-intel" \
        IMAGE_TAG="candidate-123-1" \
        VALIDATE_FORTRAN_FALLBACK="true" RUNTIME_TESTS="include" ARTIFACT_RETENTION_DAYS="0" \
    >/dev/null
  run_isolated env ARCH="arm64" DEPLOYMENT_TARGET="11.0" RUNNER="macos-15" >/dev/null
  run_isolated env HAMLIB_BRANCH="integration/4.7" VALIDATE_FORTRAN_FALLBACK="false" >/dev/null
  run_isolated env RUNTIME_TESTS="exclude" ARTIFACT_RETENTION_DAYS="7" >/dev/null
  run_isolated env RUNTIME_TESTS="only" ARTIFACT_RETENTION_DAYS="90" >/dev/null
  run_isolated env ARTIFACT_RETENTION_DAYS="1" >/dev/null
  run_isolated env ARTIFACT_RETENTION_DAYS="10" >/dev/null
  run_isolated env ARTIFACT_RETENTION_DAYS="89" >/dev/null

  expect_failure 'command substitution in version' WSJTX_VERSION='3.1.0$(id)'
  expect_failure 'backticks in version' WSJTX_VERSION='3.1.0`id`'
  expect_failure 'quote break in version' WSJTX_VERSION='3.1.0"; echo pwned #'
  expect_failure 'command substitution in hamlib_branch' HAMLIB_BRANCH='4.7.2$(touch /tmp/x)'
  expect_failure 'shell metacharacters in hamlib_branch' HAMLIB_BRANCH='4.7.2; id'
  expect_failure 'invalid channel' WSJT_RELEASE_CHANNEL='dev'
  expect_failure 'non-numeric prerelease number' WSJT_PRERELEASE_NUMBER='1a'
  expect_failure 'empty version' WSJTX_VERSION=''
  expect_failure 'invalid arch' ARCH='x86_64; id'
  expect_failure 'slash in image tag' IMAGE_TAG='candidate/unsafe'
  expect_failure 'leading punctuation in image tag' IMAGE_TAG='-candidate'
  expect_failure 'empty image tag' IMAGE_TAG=''
  expect_failure 'overlong image tag' IMAGE_TAG="$(printf 'a%.0s' {1..129})"
  expect_failure 'command substitution in deployment_target' DEPLOYMENT_TARGET='11.0$(id)'
  expect_failure 'empty fallback boolean' VALIDATE_FORTRAN_FALLBACK=''
  expect_failure 'uppercase fallback boolean' VALIDATE_FORTRAN_FALLBACK='TRUE'
  expect_failure 'numeric fallback boolean' VALIDATE_FORTRAN_FALLBACK='1'
  expect_failure 'shell payload in fallback boolean' VALIDATE_FORTRAN_FALLBACK='true$(id)'
  expect_failure 'empty runtime_tests' RUNTIME_TESTS=''
  expect_failure 'capitalized runtime_tests' RUNTIME_TESTS='Include'
  expect_failure 'unknown runtime_tests' RUNTIME_TESTS='all'
  expect_failure 'shell payload in runtime_tests' RUNTIME_TESTS='exclude; id'
  expect_failure 'empty retention' ARTIFACT_RETENTION_DAYS=''
  expect_failure 'retention above 90' ARTIFACT_RETENTION_DAYS='91'
  expect_failure 'three-digit retention' ARTIFACT_RETENTION_DAYS='100'
  expect_failure 'negative retention' ARTIFACT_RETENTION_DAYS='-1'
  expect_failure 'fractional retention' ARTIFACT_RETENTION_DAYS='7.5'
  expect_failure 'zero-padded retention' ARTIFACT_RETENTION_DAYS='07'
  expect_failure 'shell payload in retention' ARTIFACT_RETENTION_DAYS='7; id'

  echo "validate-ci-inputs.sh self-test passed"
  exit 0
fi

validate_inputs
