#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Select ci.yml's scope: which opt-in legs run and whether the Linux x86_64
# leg runs the runtime-labelled tests.
#
# Outputs: full_ci, asan, tsan (true|false) and runtime_tests (include|exclude).
#
# On a PR into or out of develop, full-ci counts only on the event that asks
# for it: applying the label, or opening or reopening a PR that carries it. A
# push to a PR that still carries full-ci gets the routine run. PRs into or out
# of release/* keep label-presence semantics and always run the runtime tests.
# Develop pushes leave the runtime tests to nightly-runtime-tests.yml, and a
# beta cut takes only a develop commit that passed both.
#
# Labels and the release/ prefix match without regard to case, as contains(),
# == and startsWith() do in ci.yml.

set -euo pipefail

# The labels whose `labeled` event starts a run. ci.yml repeats this list
# verbatim in prepare.if; test_ci_scope_policy.py holds the two copies equal.
# Only full-ci's `labeled` event joins the PR's concurrency group.
SCOPE_LABELS='["full-ci","sanitizers","thread-sanitizer"]'

lower() {
  printf '%s' "$1" | tr '[:upper:]' '[:lower:]'
}

# Exact match of one lower-case label against a comma-joined label list.
has_label() {
  case ",$(lower "$1")," in
    *",$2,"*) return 0 ;;
  esac
  return 1
}

is_scope_label() {
  [ -n "$1" ] || return 1
  case "$SCOPE_LABELS" in
    *"\"$(lower "$1")\""*) return 0 ;;
  esac
  return 1
}

is_release_branch() {
  case "$(lower "$1")" in
    release/*) return 0 ;;
  esac
  return 1
}

is_release_pr() {
  is_release_branch "$1" || is_release_branch "$2"
}

# Sets full_ci, asan, tsan and runtime_tests.
select_scope() {
  local event_name=$1
  local action=$2
  local label
  label=$(lower "$3")
  local labels=$4
  local base_ref=$5
  local head_ref=$6
  local ref=$7

  full_ci=false
  asan=false
  tsan=false
  runtime_tests=include

  case "$event_name" in
    pull_request)
      if is_release_pr "$base_ref" "$head_ref"; then
        if has_label "$labels" full-ci; then
          full_ci=true
        fi
      else
        case "$action" in
          opened|reopened)
            if has_label "$labels" full-ci; then
              full_ci=true
            fi
            ;;
          labeled)
            if [ "$label" = full-ci ]; then
              full_ci=true
            fi
            ;;
        esac
        if [ "$full_ci" != true ]; then
          runtime_tests=exclude
        fi
      fi
      if [ "$full_ci" = true ] || has_label "$labels" sanitizers; then
        asan=true
      fi
      if has_label "$labels" thread-sanitizer; then
        tsan=true
      fi
      ;;
    push)
      if [ "$ref" = refs/heads/develop ]; then
        runtime_tests=exclude
      fi
      ;;
    workflow_dispatch)
      # Manual runs build every platform leg; the sanitizers stay PR opt-ins.
      full_ci=true
      ;;
  esac
}

# expect_scope "<full_ci> <asan> <tsan> <runtime_tests>" EVENT ACTION LABEL LABELS BASE_REF HEAD_REF GITHUB_REF
expect_scope() {
  local expected=$1
  shift
  select_scope "$@"
  local actual="$full_ci $asan $tsan $runtime_tests"
  if [ "$actual" != "$expected" ]; then
    echo "self-test failed: expected '$expected' for [$*], got '$actual'" >&2
    exit 1
  fi
}

if [ "${1:-}" = --self-test ]; then
  pr=refs/pull/852/merge
  # Routine PR events.
  expect_scope "false false false exclude" pull_request opened "" "" develop topic "$pr"
  expect_scope "false false false exclude" pull_request synchronize "" "" develop topic "$pr"
  expect_scope "false false false exclude" pull_request reopened "" "documentation" develop topic "$pr"
  # full-ci counts when applied, or when a PR carrying it opens or reopens.
  expect_scope "true true false include" pull_request labeled full-ci "full-ci" develop topic "$pr"
  expect_scope "true true true include" pull_request labeled full-ci "documentation,full-ci,thread-sanitizer" develop topic "$pr"
  expect_scope "true true false include" pull_request opened "" "full-ci" develop topic "$pr"
  expect_scope "true true false include" pull_request reopened "" "bug,full-ci" develop topic "$pr"
  # A push to a PR that still carries full-ci stays routine.
  expect_scope "false false false exclude" pull_request synchronize "" "full-ci" develop topic "$pr"
  expect_scope "false true false exclude" pull_request labeled sanitizers "full-ci,sanitizers" develop topic "$pr"
  expect_scope "false false false exclude" pull_request labeled documentation "documentation,full-ci" develop topic "$pr"
  # The sanitizer labels count on any PR event.
  expect_scope "false true false exclude" pull_request synchronize "" "sanitizers" develop topic "$pr"
  expect_scope "false true false exclude" pull_request labeled sanitizers "sanitizers" develop topic "$pr"
  expect_scope "false false true exclude" pull_request synchronize "" "thread-sanitizer" develop topic "$pr"
  expect_scope "false false true exclude" pull_request labeled thread-sanitizer "thread-sanitizer" develop topic "$pr"
  # Labels match exactly.
  expect_scope "false false false exclude" pull_request labeled not-full-ci "not-full-ci" develop topic "$pr"
  expect_scope "false false false exclude" pull_request opened "" "not-full-ci,full-ci-later" develop topic "$pr"
  expect_scope "false false false exclude" pull_request synchronize "" "no-sanitizers,thread-sanitizer-off" develop topic "$pr"
  # Labels match without regard to case, as they do in ci.yml's expressions.
  expect_scope "true true false include" pull_request labeled Full-CI "Full-CI" develop topic "$pr"
  expect_scope "true true false include" pull_request opened "" "bug,FULL-CI" develop topic "$pr"
  expect_scope "false true true exclude" pull_request synchronize "" "Sanitizers,Thread-Sanitizer" develop topic "$pr"
  expect_scope "true true false include" pull_request synchronize "" "Full-CI" release/3.2 topic "$pr"
  # PRs into or out of release/* keep label-presence semantics.
  expect_scope "true true false include" pull_request synchronize "" "full-ci" release/3.2 topic "$pr"
  expect_scope "true true false include" pull_request labeled documentation "documentation,full-ci" release/3.2 topic "$pr"
  expect_scope "true true false include" pull_request synchronize "" "full-ci" develop release/3.2 "$pr"
  expect_scope "false false false include" pull_request synchronize "" "" release/3.2 topic "$pr"
  expect_scope "false true false include" pull_request synchronize "" "sanitizers" release/3.2 topic "$pr"
  expect_scope "false false true include" pull_request opened "" "thread-sanitizer" release/3.2 topic "$pr"
  expect_scope "false false false exclude" pull_request synchronize "" "full-ci" develop release-notes "$pr"
  # startsWith() ignores case, so Release/ counts as release/.
  expect_scope "true true false include" pull_request synchronize "" "full-ci" Release/3.3 topic "$pr"
  expect_scope "false false false include" pull_request synchronize "" "" develop RELEASE/3.3 "$pr"
  # Branch pushes and manual runs.
  expect_scope "false false false exclude" push "" "" "" "" "" refs/heads/develop
  expect_scope "false false false include" push "" "" "" "" "" refs/heads/release/3.2
  expect_scope "false false false include" push "" "" "" "" "" refs/heads/developer
  expect_scope "true false false include" workflow_dispatch "" "" "" "" "" refs/heads/develop
  expect_scope "true false false include" workflow_dispatch "" "" "" "" "" refs/heads/topic
  expect_scope "false false false include" schedule "" "" "" "" "" refs/heads/develop
  # The scope list is exactly the three labels.
  for label in full-ci sanitizers thread-sanitizer Full-CI THREAD-SANITIZER; do
    is_scope_label "$label" || { echo "self-test failed: $label is not a scope label" >&2; exit 1; }
  done
  for label in "" full sanitizer not-full-ci documentation; do
    if is_scope_label "$label"; then
      echo "self-test failed: '$label' was accepted as a scope label" >&2
      exit 1
    fi
  done

  echo "select-ci-scope.sh self-test passed"
  exit 0
fi

select_scope \
  "${EVENT_NAME:-}" \
  "${EVENT_ACTION:-}" \
  "${EVENT_LABEL:-}" \
  "${PR_LABELS:-}" \
  "${BASE_REF:-}" \
  "${HEAD_REF:-}" \
  "${GITHUB_REF:-}"

echo "CI scope: full_ci=$full_ci asan=$asan tsan=$tsan runtime_tests=$runtime_tests"
echo "Scope inputs: event=${EVENT_NAME:-unknown}, action=${EVENT_ACTION:-none}, label=${EVENT_LABEL:-none}, labels=${PR_LABELS:-none}, base=${BASE_REF:-none}, head=${HEAD_REF:-none}, ref=${GITHUB_REF:-unknown}"
# prepare.if skips a labeled event for any other label on a non-release PR.
if [ "${EVENT_NAME:-}" = pull_request ] && [ "${EVENT_ACTION:-}" = labeled ] \
    && ! is_scope_label "${EVENT_LABEL:-}" \
    && ! is_release_pr "${BASE_REF:-}" "${HEAD_REF:-}"; then
  echo "::warning::This labeled event does not change the CI scope; ci.yml prepare.if and select-ci-scope.sh disagree on the label list."
fi
if [ -n "${GITHUB_OUTPUT:-}" ]; then
  {
    echo "full_ci=$full_ci"
    echo "asan=$asan"
    echo "tsan=$tsan"
    echo "runtime_tests=$runtime_tests"
  } >> "$GITHUB_OUTPUT"
fi
