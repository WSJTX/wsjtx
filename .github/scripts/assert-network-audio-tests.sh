#!/usr/bin/env bash

# Assert that WSJT_ENABLE_NETWORK_AUDIO adds exactly the network-audio tests to
# the CTest registration, and removes nothing.
#
# Why this exists, and why it is an assertion on a SET rather than on a count:
# the job that runs it configures, builds and tests an enabled tree, and without
# this check that job can pass while proving nothing. If the option ever stops
# reaching tests/unit/audio/CMakeLists.txt -- a renamed option, a moved guard, a
# find_package that resolves to something without the symbols -- the tree still
# configures, still builds and still runs a full green suite, none of it the
# feature's. A count would also pass the day one feature test is dropped and an
# unrelated one is added. So the check names the tests.
#
# Usage:
#   assert-network-audio-tests.sh ENABLED_LISTING DISABLED_LISTING
#   assert-network-audio-tests.sh --self-test
#
# Each LISTING is the stdout of `ctest -N` from a configured build tree.

set -euo pipefail

# The tests tests/unit/audio/CMakeLists.txt registers inside its
# `if (WSJT_ENABLE_NETWORK_AUDIO)` block. Keep this list in step with that block:
# adding a network-audio test without adding it here fails this check, which is
# the intended prompt to update both.
EXPECTED_ADDED=(
  test_audio_source_router
  test_network_audio_input
  test_network_sound_output
)

# `ctest -N` prints one "  Test #N: name" line per registered test, plus a
# "Total Tests: N" trailer that must not be mistaken for one.
ctest_names () {
  sed -n 's/^[[:space:]]*Test[[:space:]]*#[0-9][0-9]*:[[:space:]]*//p' "$1" | sort
}

assert_network_audio_tests () {
  local enabled=$1 disabled=$2
  local expected added removed failed=0

  if [ ! -s "$enabled" ]; then
    echo "assert-network-audio-tests: enabled listing '$enabled' is missing or empty" >&2
    return 1
  fi
  if [ ! -s "$disabled" ]; then
    echo "assert-network-audio-tests: disabled listing '$disabled' is missing or empty" >&2
    return 1
  fi

  expected="$(printf '%s\n' "${EXPECTED_ADDED[@]}" | sort)"
  added="$(comm -23 <(ctest_names "$enabled") <(ctest_names "$disabled"))"
  removed="$(comm -13 <(ctest_names "$enabled") <(ctest_names "$disabled"))"

  if [ "$added" != "$expected" ]; then
    echo "assert-network-audio-tests: enabling WSJT_ENABLE_NETWORK_AUDIO did not add exactly the expected tests" >&2
    echo "  expected added: $(echo "$expected" | tr '\n' ' ')" >&2
    echo "  actually added: $(echo "$added" | tr '\n' ' ')" >&2
    failed=1
  fi

  if [ -n "$removed" ]; then
    echo "assert-network-audio-tests: enabling WSJT_ENABLE_NETWORK_AUDIO removed tests from the suite" >&2
    echo "  removed: $(echo "$removed" | tr '\n' ' ')" >&2
    failed=1
  fi

  if [ "$failed" -ne 0 ]; then
    return 1
  fi

  echo "Enabled configuration registers $(ctest_names "$enabled" | wc -l | tr -d ' ') tests," \
       "$(ctest_names "$disabled" | wc -l | tr -d ' ') with the option off;" \
       "the difference is exactly: $(echo "$expected" | tr '\n' ' ')"
}

# ---- self-test --------------------------------------------------------------
# Drives the assertion RED on each way it is meant to fail before the green case
# is trusted. An absence check that has never been seen to fail is not a check.

self_test () {
  local dir
  dir="$(mktemp -d)"
  # shellcheck disable=SC2064
  trap "rm -rf '$dir'" RETURN

  write_listing () {
    local path=$1
    shift
    local i=1 name
    : > "$path"
    for name in "$@"; do
      printf '    Test #%d: %s\n' "$i" "$name" >> "$path"
      i=$((i + 1))
    done
    printf '\nTotal Tests: %d\n' "$((i - 1))" >> "$path"
  }

  expect_pass () {
    if ! assert_network_audio_tests "$1" "$2" > /dev/null 2>&1; then
      echo "self-test failed: expected pass for $3" >&2
      exit 1
    fi
  }

  expect_fail () {
    if assert_network_audio_tests "$1" "$2" > /dev/null 2>&1; then
      echo "self-test failed: expected failure for $3" >&2
      exit 1
    fi
  }

  # The empty/missing-listing guard changes the DIAGNOSTIC, never the verdict:
  # the set comparison below rejects those inputs on its own. So the control for
  # it has to assert the message, or it is a control that cannot fail (it was
  # exactly that until a mutation run caught it).
  expect_fail_saying () {
    local message=$1 enabled=$2 disabled=$3 what=$4 output
    output="$(assert_network_audio_tests "$enabled" "$disabled" 2>&1 || true)"
    if assert_network_audio_tests "$enabled" "$disabled" > /dev/null 2>&1; then
      echo "self-test failed: expected failure for $what" >&2
      exit 1
    fi
    case "$output" in
      *"$message"*) ;;
      *)
        echo "self-test failed: $what did not report '$message'" >&2
        echo "  got: $output" >&2
        exit 1
        ;;
    esac
  }

  # The green case: the option adds exactly the expected tests.
  write_listing "$dir/on" test_bwf_file "${EXPECTED_ADDED[@]}"
  write_listing "$dir/off" test_bwf_file
  expect_pass "$dir/on" "$dir/off" "the option adding exactly the expected tests"

  # RED 1 -- the option took no effect. This is the vacuous-pass this check exists
  # for: the enabled tree is identical to the disabled one and everything is green.
  write_listing "$dir/on-inert" test_bwf_file
  expect_fail "$dir/on-inert" "$dir/off" "an option that added nothing"

  # RED 2 -- one feature test silently gone, the others present.
  write_listing "$dir/on-partial" test_bwf_file "${EXPECTED_ADDED[@]:1}"
  expect_fail "$dir/on-partial" "$dir/off" "all but one of the expected tests"

  # RED 3 -- an unexpected extra test rode in with the option.
  write_listing "$dir/on-extra" test_bwf_file "${EXPECTED_ADDED[@]}" test_surprise
  expect_fail "$dir/on-extra" "$dir/off" "an unexpected extra test"

  # RED 4 -- enabling the option removed an unrelated test.
  write_listing "$dir/off-wider" test_bwf_file test_unrelated
  expect_fail "$dir/on" "$dir/off-wider" "a test removed by enabling the option"

  # RED 5 -- an empty or missing listing must be reported AS SUCH, not left to
  # surface as a confusing set difference. A ctest -N step that failed while its
  # redirect succeeded is the case this names.
  : > "$dir/empty"
  expect_fail_saying "is missing or empty" "$dir/empty" "$dir/off" "an empty enabled listing"
  expect_fail_saying "is missing or empty" "$dir/on" "$dir/empty" "an empty disabled listing"
  expect_fail_saying "is missing or empty" "$dir/on" "$dir/absent" "a missing disabled listing"

  # No separate control for the "Total Tests" trailer: a ctest_names that swallows
  # it makes the green case above fail (both listings gain a bogus count name), so
  # a dedicated check here would be unreachable. Verified by mutation, not assumed.

  echo "assert-network-audio-tests self-test passed"
}

if [ "${1:-}" = "--self-test" ]; then
  self_test
  exit 0
fi

if [ "$#" -ne 2 ]; then
  echo "Usage: assert-network-audio-tests.sh ENABLED_LISTING DISABLED_LISTING" >&2
  echo "       assert-network-audio-tests.sh --self-test" >&2
  exit 2
fi

assert_network_audio_tests "$1" "$2"
