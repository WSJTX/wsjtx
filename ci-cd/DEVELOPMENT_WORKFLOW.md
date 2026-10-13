# WSJT-X Development Workflow

How the WSJT-X project uses its two-repo model, how team members and external contributors participate, how CI/CD automates quality checks and releases, and how all the pieces fit together.

**Audience:** Public-facing. Current team members, prospective contributors, and anyone evaluating the project's development practices.

This document describes the WSJTX GitHub development and release workflow. Repository settings and signing credentials must be configured as described in the Deployment Playbook before the protected release paths can run.

---

## Table of Contents

1. [The Two-Repo Model](#1-the-two-repo-model)
2. [Roles and Access](#2-roles-and-access)
3. [Day-to-Day Development (Team Members)](#3-day-to-day-development-team-members)
4. [Contributing from Outside the Team](#4-contributing-from-outside-the-team)
5. [How CI/CD Fits In](#5-how-cicd-fits-in)
6. [The Release Process](#6-the-release-process)
7. [Branch Strategy](#7-branch-strategy)
8. [Issue and PR Conventions](#8-issue-and-pr-conventions)
9. [Code Review](#9-code-review)
10. [End-to-End Example: A Bug Fix](#10-end-to-end-example-a-bug-fix)
11. [End-to-End Example: A New Release](#11-end-to-end-example-a-new-release)
12. [Quick Reference](#12-quick-reference)

---

## 1. The Two-Repo Model

WSJT-X uses two separate repositories in the WSJTX GitHub organization:

```
WSJTX/wsjtx-internal  (private)     WSJTX/wsjtx  (public)
┌──────────────────────────┐         ┌──────────────────────────┐
│  Default branch: develop │         │  Default branch: master  │
│                          │         │                          │
│  Active development      │  ────>  │  Tagged releases only    │
│  Feature branches        │  sync   │                          │
│  Issues & PRs            │         │  External contributors   │
│  CI/CD workflows         │         │  fork from here          │
└──────────────────────────┘         └──────────────────────────┘
```

### Why two repos?

- **wsjtx-internal** is where all development happens. It's private so the team can work without external pressure during development cycles. This is the repo team members push to, open PRs against, and file issues in.

- **wsjtx** is the public face of the project. It receives the exact commit of each promoted beta, RC, and GA release on `release/X.Y`, with its immutable version tag. Newest-line GA commits also advance public `master`. External contributors fork this repo.

### How they stay in sync

The repos are **not** GitHub forks of each other — they are independent Git repos that share history. Synchronization happens in one direction only:

```
wsjtx-internal ──→ wsjtx
   (private)    (public)
              release/X.Y + immutable v*
```

For a version such as `3.3.0-rc1`:
1. Prepare Release Candidate creates the immutable internal candidate tag `build/v3.3.0-rc1` and builds validation artifacts from it
2. A release manager inspects that run and manually promotes its exact commit as public branch `release/3.3` and tag `v3.3.0-rc1`
3. The public repository builds the distribution artifacts from that tag
4. The public workflow verifies and summarizes the exact bundle
5. A member of the `release-approvers` team approves the final `publish` job, which creates the GitHub prerelease or release

A beta takes the same steps, except that the [beta scheduler](DEPLOYMENT_PLAYBOOK.md#10-scheduled-beta-releases) creates the candidate and promotes it, and the `publish` job runs without approval.

See the [public branch policy](DEPLOYMENT_PLAYBOOK.md#public-branch-history-and-ga-line-policy) for ancestry requirements and how GA releases update `master` and GitHub Latest.

The public repo never pushes back to internal. Changes from external contributors are manually cherry-picked or merged by a team member (see [Section 4](#4-contributing-from-outside-the-team)).

---

## 2. Roles and Access

### Team Members

Team members have **write access** to both repos. They can:
- Push branches directly to wsjtx-internal
- Open and merge pull requests
- Create and manage issues
- Trigger workflow runs manually
- Run Prepare Release Candidate and Promote Release Source, which create every release tag; never push a `build/v*` or `v*` tag by hand

### Org Admins

Org admins can additionally:
- Manage org settings (Actions permissions, member roles)
- Configure environments, secrets, and variables
- Add or remove team members
- Set branch protection rules

### External Contributors

External contributors have **read access** to the public repo (wsjtx). They can:
- Fork wsjtx to their own GitHub account
- Open pull requests from their fork to wsjtx
- File issues on wsjtx

They **cannot** directly access wsjtx-internal, push branches to either org repo, or trigger protected release operations. Candidate CI runs internally; promoted beta, RC, and GA source tags and their distribution builds are public. External PRs are triaged by a team member, who brings accepted changes into wsjtx-internal where the pipeline builds and tests them.

### Access Summary

| Action | Team Member | Org Admin | External Contributor |
|--------|:-----------:|:---------:|:--------------------:|
| Read wsjtx-internal | Yes | Yes | No |
| Push to wsjtx-internal | Yes | Yes | No |
| Read wsjtx (public) | Yes | Yes | Yes |
| Push to wsjtx (public) | Yes | Yes | No (fork + PR) |
| Open PRs on wsjtx-internal | Yes | Yes | No |
| Open PRs on wsjtx (public) | Yes | Yes | Yes (from fork) |
| Create release tags (through the release workflows) | Yes | Yes | No |
| Manage secrets/settings | No | Yes | No |

---

## 3. Day-to-Day Development (Team Members)

### The basic loop

```
1. Pull latest develop
2. Create a feature/fix branch
3. Make changes, test locally
4. Push branch to wsjtx-internal
5. Open a PR against develop
6. CI runs automatically on the PR
7. Team reviews, discusses
8. Merge when ready
```

### Step by step

#### 1. Start from develop

```bash
cd wsjtx-internal
git checkout develop
git pull origin develop
```

#### 2. Create a branch

Use a descriptive name with a prefix:

```bash
# Feature:
git checkout -b feat-improved-decoder

# Bug fix:
git checkout -b fix-audio-dropout-on-macos

# Tied to an issue:
git checkout -b 15-fix-shared-memory-leak
```

See [Section 7](#7-branch-strategy) for naming conventions.

#### 3. Make changes and test

Build and test locally on your platform. At minimum, verify:
- The application compiles without errors
- The application launches and basic functions work
- If you changed decoding code, test with known `.wav` files

#### 4. Push your branch

```bash
git push -u origin feat-improved-decoder
```

#### 5. Open a pull request

Use `gh` ([GitHub CLI](https://cli.github.com/)) to create the PR:

```bash
gh pr create --base develop --title "feat: improve FT8 decoder sensitivity" \
  --body "Description of what changed and why. Tested on macOS ARM64."
```

Or use the GitHub web UI: go to the repo, click "Compare & pull request" on the banner that appears after pushing.

#### 6. CI runs automatically

When the PR is opened (and on every subsequent push), the default CI path runs the Linux x86_64 build. Apply the `full-ci` label or dispatch CI manually when all supported targets should run:

- **macOS ARM64** — builds; the app is ad-hoc signed and the package is unsigned
- **macOS Intel x86_64** — builds; the app is ad-hoc signed and the package is unsigned
- **Linux x86_64** — builds
- **Linux aarch64** — builds (ARM Linux, via `ubuntu-24.04-arm`)
- **Linux armhf** — cross-builds on `ubuntu-24.04`, then tests and packages in the armhf container on `ubuntu-24.04-arm`
- **Windows x86_64** — builds via MSYS2/MinGW; the installer is unsigned

Green checks cover the jobs selected for that run. A red X means something broke — click the check to see which platform failed and view the logs. The private release candidate always runs the complete six-target matrix before source can be promoted.

#### 7. Review and merge

The team reviews the PR via GitHub. When approved, merge to `develop`. Prefer **"Create a merge commit"** to preserve history (not squash or rebase).

### What if I want to push directly to develop?

For very small, obvious changes (typo fixes, comment updates), pushing directly to `develop` is acceptable. CI will still run on the push. But for anything substantive — new features, bug fixes, refactoring — use a PR so the team can see what's happening and CI validates it before it lands on `develop`.

---

## 4. Contributing from Outside the Team

External contributors work through the public repo (wsjtx). The process has more steps because external changes must cross the public-to-internal boundary.

### The external contributor loop

```
1. Fork WSJTX/wsjtx on GitHub
2. Clone your fork locally
3. Create a feature/fix branch
4. Make changes, test locally
5. Push to your fork
6. Open a PR from your fork to WSJTX/wsjtx (master branch)
7. A team member reviews the PR
8. If accepted, a team member ports the change to wsjtx-internal
```

### Step by step

#### 1. Fork the public repo

On GitHub, go to https://github.com/WSJTX/wsjtx and click **Fork**.

#### 2. Clone your fork

```bash
git clone git@github.com:YOUR_USERNAME/wsjtx.git
cd wsjtx
git remote add upstream https://github.com/WSJTX/wsjtx.git
```

#### 3. Create a branch and make changes

```bash
git checkout -b fix-audio-dropout
# ... make changes ...
git add -A
git commit -m "fix: resolve audio dropout on macOS Sequoia"
```

Commit subjects typed `feat`, `fix`, or `perf` become lines in the public release notes with the author's name, unless their scopes are all among `release`, `ci`, `test`, and `tests`; other subjects are only counted ([What the Release Produces](DEPLOYMENT_PLAYBOOK.md#what-the-release-produces)).

#### 4. Push and open a PR

```bash
git push -u origin fix-audio-dropout
gh pr create --repo WSJTX/wsjtx --base master \
  --title "fix: resolve audio dropout on macOS Sequoia" \
  --body "Description of the fix. Tested on macOS 15.3, ARM64."
```

#### 5. What happens next

A team member reviews the PR on the public repo. If the change is accepted:

1. The team member checks out the PR locally or cherry-picks the commits
2. They apply the change to a branch on wsjtx-internal
3. They open an internal PR against `develop`
4. Default CI validates the change on Linux x86_64; `full-ci`, manual CI, and release candidates provide all-target coverage
5. The change merges to `develop`
6. At the next release, the change flows back to the public repo automatically

**Why this indirection?** The public repo only receives code at release time. Development happens on `develop` in wsjtx-internal. Merging an external PR directly to `master` on the public repo would put it out of sync with internal development.

#### 6. Keeping your fork up to date

After a release (when the public repo's `master` is updated):

```bash
git checkout master
git fetch upstream
git merge upstream/master
git push origin master
```

### What external contributors should know

- **Response time varies.** The core developers are volunteers with day jobs and other commitments. PRs may take days or weeks to review.
- **Build instructions** are in the
  [source-build guide](../doc/user_guide/en/install-from-source.adoc). Build
  locally before submitting.
- **One logical change per PR.** Don't bundle unrelated fixes.
- **Test on your platform.** Mention which OS and architecture you tested on.
- **License.** All contributions must be GPL-3.0 compatible. By submitting a PR, you agree to license your code under GPL-3.0.

---

## 5. How CI/CD Fits In

CI/CD serves two purposes: **quality gates** (does it compile?) and **release automation** (build and publish binaries).

### CI: Quality Gates

```
                    ┌─────────────────────────────────────────────────────────┐
  Push to develop   │                       ci.yml                            │
  or open a PR  ──> │ Linux x86_64 by default                               │
                    │                                                        │
  full-ci/manual ──>│ macOS arm64 + Intel, Linux x86_64 + aarch64 + armhf,  │
                    │ and Windows x86_64                                     │
                    │                                                        │
                    │ Selected jobs must all be green                        │
                    └─────────────────────────────────────────────────────────┘
```

**What triggers CI:**
- Every push to `develop` or `release/**`
- Every pull request targeting `develop` or `release/**`
- Manual trigger via the Actions UI (workflow_dispatch)

**What CI checks:**
- Default PR and branch CI compiles and tests Linux x86_64; `full-ci`, manual CI, and release candidates provide broader coverage
- CI, candidate, and beta installers and packages are unsigned; on macOS the app inside carries an ad-hoc signature for validation ([beta signing](DEPLOYMENT_PLAYBOOK.md#beta-signing)).
- Only a GA installer is signed, through SignPath; a GA release's macOS packages are Developer ID-signed, and so are an RC's while hosted Developer ID signing is enabled ([procedures by channel](DEPLOYMENT_PLAYBOOK.md#procedures-by-channel)).
- Build artifacts are uploaded for inspection
- **Tests pass on every platform** (Qt helpers, decoder smoke tests, pFUnit Fortran unit tests — registered via ctest). See [Test Failure Policy](#test-failure-policy) below.

**What CI does not check:**
- Code style or linting
- Documentation generation

**How to read CI results:**
- On a PR, scroll to the bottom to see the status checks
- Green check = all selected jobs completed successfully
- Red X = at least one selected job failed. Click it to read the logs.
- Yellow circle = builds still running

### Release: Build and Publish

The release pipeline is separate from CI. An internal candidate tag builds without publishing. Source promotion starts the public distribution builds and their automatic publication checks. See [Section 6](#6-the-release-process).

### Where CI runs

CI runs on GitHub-hosted runners:

| Platform | Runner | Architecture | Cost |
|----------|--------|-------------|------|
| macOS ARM64 | `macos-15` | ARM64 (Apple Silicon) | $0.062 per minute |
| macOS Intel x86_64 | `macos-15-intel` | x86_64 | $0.062 per minute |
| Linux x86_64 | `ubuntu-24.04` | x86_64 | $0.006 per minute |
| Linux aarch64 | `ubuntu-24.04-arm` | aarch64 | $0.005 per minute |
| Linux armhf | `ubuntu-24.04` (cross-build), then `ubuntu-24.04-arm` + container (tests and packaging) | armhf | $0.006 per minute (cross-build), then $0.005 per minute |
| Windows | `windows-latest` + MSYS2 | x86_64 | $0.010 per minute |
| Orchestration (scheduler, tagging, promotion, gates) | `ubuntu-latest` | x86_64 | $0.006 per minute |

**Included minutes:** The organization's GitHub Team plan includes 3,000 Actions minutes a month for private repositories. Usage draws on them at each runner's per-minute list price, and usage beyond them is billed at the same rates. Standard GitHub-hosted runners are free in public repositories.

**Caching:** Hamlib builds and MSYS2 packages are cached to reduce build times. First-run builds are slower; subsequent builds use the cache.

### Test Failure Policy

Tests run via ctest at the end of each platform's build job, after compilation succeeds. The policy is **hard-fail everywhere**:

- Any test failure fails the platform's build job
- A failed build job fails the entire CI run (red X on the PR or push)
- A failed candidate build blocks `release.yml`'s `candidate-ready` job, which requires macOS arm64 and Intel, Linux x86_64, aarch64, and armhf, plus Windows x86_64. Candidate builds do not publish.

This is the simplest possible policy for v1. If a flaky test emerges, the team can add `continue-on-error: true` to the offending test's platform as a targeted soft-warn, file an issue to triage the flake, and remove the exception once fixed. No blanket soft-warn policy on `develop`.

**Where to see results:**

- **GitHub Actions step summary** — each build job posts a `## Test Results — <platform> — PASS/FAIL` table with per-test status and timing. Viewable on the job summary page without downloading logs.
- **Job log** — `ctest --output-on-failure` prints the full decoder/test output for any failing test directly into the job log.
- **Uploaded artifact** — each build job uploads `ctest-results-<platform>.xml` (JUnit-format XML) as a run artifact. Download via `gh run download <RUN_ID> --name ctest-results-<platform>` or via the Actions UI. Useful for programmatic parsing or post-mortem.

---

## 6. The Release Process

For an RC or GA, after candidate creation, two approvals remain. First, review the candidate and run Promote Release Source with `operation=promote`; this makes the exact source public. After builds and bundle checks finish, approve the final `publish` job in `public-release`. The same maintainer can do both. Weekly betas run the same pipeline with no human step.

### Release channels

Every build identifies its channel through `release-state.txt`:

| Channel | Version | Built from | Published |
|---------|---------|------------|-----------|
| `DEVEL` | `X.Y.Z-devel` | `develop` and other development builds | Never |
| `BETA` | `X.Y.Z-betaN` | `release/X.Y` before the freeze, with the newest green `develop` merged in weekly | Automatically, as an unsigned GitHub prerelease that never becomes Latest |
| `RC` | `X.Y.Z-rcN` | `release/X.Y` after the freeze | After release-manager promotion and `release-approvers` approval, as an unsigned GitHub prerelease while hosted Developer ID signing is disabled |
| `GA` | `X.Y.Z` | `release/X.Y` after the freeze | After release-manager promotion and `release-approvers` approval; signed; Latest only for the newest GA on the newest line |

`DEVEL`, `BETA`, and `RC` builds of the WSJT-X GUI show a prerelease notice at every start and stop working at the end of the 90th day (UTC) after the built commit's date; `GA` builds never expire ([release state contract](DEPLOYMENT_PLAYBOOK.md#release-state-contract)).

A line publishes betas from its start ([starting a release line](DEPLOYMENT_PLAYBOOK.md#starting-a-release-line)) until a commit on `release/X.Y` sets `RC 1`, which freezes it ([pause and freeze](DEPLOYMENT_PLAYBOOK.md#pause-and-freeze)). RCs and GA then follow the steps below. After the freeze, bump `develop` to `X.(Y+1).0` in a one-line PR, and publish `samples/contents_X.(Y+1).json` to `feat-web-pages` as the [sample catalog procedure](#3-promote-the-exact-source) describes.

**Fixes land on `develop` first, in every phase.** The weekly beta carries them to the line; after the freeze, cherry-pick each fix from `develop` to `release/X.Y`. In the beta phase, push no fix to `release/X.Y`, not even a cherry-pick from `develop`. The scheduler stops the line on such a commit until that exact commit is reachable from `develop` ([state machine](DEPLOYMENT_PLAYBOOK.md#state-machine)). For an urgent fix, land it on `develop` and, once its push CI passes, dispatch Scheduled Beta Cut, which cuts the next beta at once when the current one is published.

### Overview

```
release/3.3 metadata commit
  └─→ internal build/v3.3.0-rc1 candidate and validation artifacts
        └─→ manual source promotion
              └─→ public v3.3.0-rc1 tag
                    └─→ public all-platform builds and policy checks
                          └─→ bundle summary and final publication approval
                                └─→ public GitHub prerelease
```

### Step by step

#### 1. Prepare tracked release metadata

Make a small metadata commit on `release/3.3` before every RC or GA candidate; a line in its beta phase gets its metadata from its [line-start commit](DEPLOYMENT_PLAYBOOK.md#starting-a-release-line) and the beta scheduler's merge commits instead. `release-state.txt` holds the numeric `version`, the `channel` (`DEVEL`, `BETA`, `RC`, or `GA`), and the `prerelease` number that `BETA` and `RC` require. Set `windows_signing=unsigned` in every RC metadata commit and omit it from the GA metadata commit, which selects SignPath ([release state contract](DEPLOYMENT_PLAYBOOK.md#release-state-contract)). The export-substituted `revision` preserves the source SHA outside Git, so a build from GitHub's automatic source archive identifies itself correctly even though that archive has no `.git` directory.

Do not put an RC or GA tag on a `DEVEL` commit. The helper rejects disagreement among the requested version, tag, and tracked release state; CMake derives its version from that same state.

#### 2. Create the internal candidate

Prepare and commit the [Linux image selection](DEPLOYMENT_PLAYBOOK.md#prepared-linux-release-images) first. A line frozen from its beta phase carries the scheduler's selection, which the freeze commit's review covers ([pause and freeze](DEPLOYMENT_PLAYBOOK.md#pause-and-freeze)); prepare a new one only when Prepare Release Candidate reports a stale recipe. Candidate and public builds use those same dependency digests. Native builds report [cache readiness](DEPLOYMENT_PLAYBOOK.md#cache-readiness).

Run Prepare Release Candidate from `release/3.3` at the intended SHA. Validate the version and full SHA, then use `operation=create`. This creates `build/v3.3.0-rc1` and the internal validation artifacts; it does not publish them.

Inspect the candidate run and installable validation artifacts. Record its run ID for promotion.

#### 3. Promote the exact source

From the same branch and SHA, run Promote Release Source with the version and candidate run ID. Validate first and review the summary, then rerun with `operation=promote`. The workflow checks the candidate, creates its immutable public tag, and creates or fast-forwards the public release branch at that exact commit.

Betas and RCs leave `master` unchanged. A newest-line GA also advances `master`; an older-line GA leaves both `master` and GitHub Latest unchanged. See the [playbook](DEPLOYMENT_PLAYBOOK.md#public-branch-history-and-ga-line-policy) for line selection, first branch creation, ancestry checks, and handling divergent history.

The sample downloader reads `samples/contents_X.Y.json` and the files it lists from GitHub Pages (`https://wsjtx.github.io/wsjtx/`), which serves the public `feat-web-pages` branch. Binaries released before 3.3 read the same paths from SourceForge; leave that copy in place. Each X.Y line needs its own catalog on Pages, published when `develop` takes that version. To publish a catalog, or after adding a sample, configure a build of that version's source with `WSJT_WEB_PAGES_DIR` set to a `feat-web-pages` checkout, build the `stage-samples` target, commit the staged `samples/` changes to `feat-web-pages` for review, and verify that the published catalog lists every file. The WSJT-X Help menu's Release Notes and Online User Guide are served from the public `feat-web-pages` branch; publish `Release_Notes.txt` and a guide generated from the tagged source there. Also update `https://wsjt.sourceforge.io/Release_Notes.txt`; MAP65's Help > Release Notes and already released WSJT-X binaries open that copy. These remote files are separate from the GitHub Release assets and are not updated by the public release workflow.

#### 4. Verify publication

After public builds finish, review the bundle summary for its tag, source SHA, run link, signing modes, assets, and checksums. Then approve the final `publish` job in `public-release`; the same maintainer can approve their own run. RCs publish as prereleases.

The playbook describes the required [reviewer settings](DEPLOYMENT_PLAYBOOK.md#public-final-publication-approval), [bundle checks and signing modes](DEPLOYMENT_PLAYBOOK.md#step-4-review-and-approve-final-publication), and manual replacement of a GA's macOS packages when hosted signing is disabled.

#### 5. Recover without moving tags

Rerun a failed workflow against the existing immutable tag after fixing transient credentials or service configuration. If source or tracked release metadata must change, commit the fix (source fixes land on `develop` first) and cut the next RC. Never delete, recreate, or force-move a candidate or public release tag; immutable tags keep reviews, source, signatures, and downloaded artifacts attributable to one commit.

### Release candidates

Before a final release, cut one or more RCs and let the team exercise the same public distribution path. An RC uses a SemVer suffix such as `3.3.0-rc1` and is published as a GitHub prerelease, so it does not replace the latest GA release.

#### When to cut an RC

Cut an RC whenever a release contains more than a trivial change — any feature work, non-obvious bug fixes, or changes to the build or packaging path. An RC's Windows installer is unsigned, and its macOS packages are unsigned while hosted Developer ID signing is disabled, so an RC does not exercise those signing paths. A pure doc or CI-config release does not need an RC.

#### Tagging an RC

RCs and GA are prepared on the same `release/X.Y` branch, never directly on `develop`. Make and merge the matching `RC N` metadata commit before asking Prepare Release Candidate to create the candidate tag.

#### Testing an RC

Before promoting an RC to GA, confirm:

- All six target jobs in the public release workflow ran green
- Both macOS `.pkg` assets pass the [artifact checks](DEPLOYMENT_PLAYBOOK.md#step-5-verify-the-artifacts) in the Deployment Playbook; an unsigned RC package skips the package-signature, staple, and Gatekeeper checks
- At least one volunteer on each supported platform (macOS ARM64, macOS Intel x86_64, Linux x86_64, Linux aarch64, Linux armhf, Windows x86_64) has installed the RC and exercised the workflow they care about; [beta signing](DEPLOYMENT_PLAYBOOK.md#beta-signing) says how to open the unsigned packages
- No critical issue has been filed against the RC for a reasonable soak period (typically 48 hours after the platform volunteers confirm)

If an RC fails testing, land the fix on `develop`, cherry-pick it to the release branch, update the metadata to the next RC number, and create `-rc2`, `-rc3`, etc. Each RC remains an independent public prerelease for reference.

#### Promoting an RC to GA

Change the tracked state from `RC N` to `GA` in a metadata-only commit (`channel=GA`, an empty `prerelease=`, and no `windows_signing` line), wait for CI, and create a new `3.3.0` candidate through the same validate/create/promote sequence. Even when application source is unchanged, the GA commit is intentionally distinct so ordinary builds from its GitHub source archive report GA rather than RC.

### What the release produces

| Artifact | Platform | Signed | Notes |
|----------|----------|--------|-------|
| `wsjtx-<ver>-arm64-macOS.pkg` | macOS ARM64 | GA: yes. Beta: no. RC: no while hosted Developer ID signing is disabled | GA: Developer ID signed, notarized, and stapled; verify per the Deployment Playbook. Beta and unsigned RC: remove the quarantine attribute before opening (`xattr -d com.apple.quarantine`) |
| `wsjtx-<ver>-x86_64-macOS.pkg` | macOS Intel x86_64 | GA: yes. Beta: no. RC: no while hosted Developer ID signing is disabled | GA: Developer ID signed, notarized, and stapled; verify per the Deployment Playbook. Beta and unsigned RC: remove the quarantine attribute before opening (`xattr -d com.apple.quarantine`) |
| `wsjtx-<ver>-linux-x86_64.AppImage` | Linux x86_64 | No | Published with matching `.deb` and `.rpm` packages |
| `wsjtx-<ver>-linux-aarch64.AppImage` | Linux aarch64 | No | Published with matching `.deb` and `.rpm` packages |
| `wsjtx-<ver>-linux-armhf.AppImage` | Linux armhf | No | Published with matching `.deb` and `.rpm` packages |
| `wsjtx-<ver>-win64.exe` | Windows x86_64 | GA: yes. Beta and RC: no | GA: SignPath Foundation Authenticode |
| `wsjtx-<ver>-arm64-macOS-<tarball>.tar.gz` | macOS ARM64 | No (ad-hoc) | Programs with their dylibs; remove the quarantine attribute before extracting (`xattr -d com.apple.quarantine`) |
| `wsjtx-<ver>-x86_64-macOS-<tarball>.tar.gz` | macOS Intel x86_64 | No (ad-hoc) | Programs with their dylibs; remove the quarantine attribute before extracting (`xattr -d com.apple.quarantine`) |
| `wsjtx-<ver>-linux-<arch>-<tarball>.tar.gz` | Linux x86_64, aarch64, armhf | No | Programs with the libraries they load; `README.txt` names the oldest glibc and the host libraries they need |
| `wsjtx-<ver>-windows-x86_64-<tarball>.tar.gz` | Windows x86_64 | No | Programs with the DLLs they import |
| `wsjtx-<ver>-src.tar.gz` | Source | N/A | Project-created archive of the public tagged commit |
| `SHA256SUMS` and release manifest | CI-managed assets | N/A | Bind immutable uploaded bytes to their public tag, commit, and build provenance |

`<ver>` is the release's suffixed version: `X.Y.Z-betaN`, `X.Y.Z-rcN`, or `X.Y.Z`, and `<tarball>` is each release tarball `CMake/release-tarballs.txt` names. Betas, RCs, and GA releases publish the same set of assets.

GitHub also adds automatic **Source code (zip)** and **Source code (tar.gz)** links from the public tag. They represent the same tagged source but are generated and compressed by GitHub, so their archive hashes need not equal the project-created `.tar.gz`. `SHA256SUMS` covers immutable assets uploaded by the project. For an RC or GA in manual macOS signing mode it excludes the two replaceable `.pkg` files, which the manifest identifies separately. A beta's macOS packages are not replaceable, so `SHA256SUMS` covers them. A checksum detects changed bytes but is not a substitute for platform signatures or tag-to-commit checks.

### Who can trigger a release?

Team members can run candidate validation. Creating the candidate and promoting its source are explicit release-manager actions for an RC or GA; repository permissions determine who can perform them. For a beta, the beta scheduler creates the candidate and promotes it.

---

## 7. Branch Strategy

### Branch types

| Prefix | Purpose | Base branch | Merges to | Lifetime |
|--------|---------|-------------|-----------|----------|
| `feat-*` | New feature | `develop` | `develop` via PR | Until merged |
| `fix-*` | Bug fix | `develop` | `develop` via PR | Until merged |
| `<issue#>-*` | Issue-linked work | `develop` | `develop` via PR | Until merged |
| `release/X.Y` | Weekly betas, then RCs and GA after the freeze | `develop` | Promoted to public `release/X.Y` with its tags; `master` only at newest-line GA; each GA commit (`build/vX.Y.Z`) merged back into `develop` | Maintained for patch releases, which skip the beta phase |
| `develop` | Main development trunk | — | — | Permanent |
| `master` | Public releases (on wsjtx) | — | — | Permanent |

### Naming conventions

```
feat-jtty                    # New JTTY mode
feat-improved-decoder        # Decoder improvement
fix-audio-dropout-on-macos   # Bug fix with context
15-fix-shared-memory-leak    # Issue #15
```

Use lowercase, hyphens between words. Keep names short but descriptive.

### What NOT to do

- **Don't push directly to `master`** on the public repo. It receives code only via the release sync.
- **Don't create long-lived feature branches** that diverge far from `develop`. Merge frequently to avoid painful conflicts.
- **Don't rewrite history** on shared branches (`develop`, release branches). Use merge commits, not force-push.

---

## 8. Issue and PR Conventions

### Issues

Issues are tracked on **wsjtx-internal** (not the public repo) because that's where development happens. External contributors without `wsjtx-internal` access should file issues on the public repo (see `CONTRIBUTING.md`); a team member will move the issue to `wsjtx-internal` if triage reveals development work to be done.

**When to file an issue:**
- Bug reports (use the bug report template)
- Feature requests
- Refactoring proposals
- Technical debt items (known-suboptimal code, deferred cleanup, latent bugs uncovered during other work — anything worth tracking but not urgent enough to fix immediately)

**Issue structure:**
- Clear title describing the problem or feature
- Steps to reproduce (for bugs)
- Expected vs. actual behavior
- Platform and version information
- Relevant log output or screenshots

### Pull Requests

**PR title format:**
```
feat: add JTTY decoder
fix: resolve audio dropout on macOS Sequoia
refactor: extract message parser from mainwindow.cpp
docs: update build instructions for Windows
```

**PR description should include:**
- What the change does and why
- Which platforms you tested on
- Related issue numbers (e.g., "Fixes #15")
- Any known limitations or follow-up work needed

**PR etiquette:**
- Keep PRs focused — one logical change per PR
- Respond to review feedback promptly
- Don't force-push after review has started (it loses review context)
- Update the PR description if scope changes during review

---

## 9. Code Review

### Who reviews?

Any team member can review any PR. For changes that affect specific areas:

*Callsigns below reflect the team as of 2026-04-21; the team should confirm primary reviewers in `CODEOWNERS` before this table is treated as authoritative.*

| Area | Primary Reviewer(s) | Why |
|------|---------------------|-----|
| Signal processing / codecs | K1JT, K9AN | Algorithm expertise |
| Qt GUI / application code | G4KLA, N9ADG | Application architecture |
| Build system / CMake | N9ADG, KJ5HST | Build infrastructure |
| Fortran code | K1JT, K9AN, W3SZ | Numerical methods |
| User guide / docs | DL3WDG | Documentation ownership |
| CI/CD workflows | KJ5HST | Pipeline expertise |

### Review expectations

- **Correctness:** Does the change do what it claims?
- **Platform impact:** Will this break other platforms?
- **Dependencies:** Does this introduce new external dependencies?
- **Backward compatibility:** Does this change behavior that users depend on?
- **Build system:** Does this require changes to CMakeLists.txt or new build dependencies?

### Approval and merge

- At least one team member should review before merging
- For significant changes (new features, architectural changes), wait for input from the relevant domain expert
- The PR author should not merge their own PR without at least one approval
- Use **"Create a merge commit"** (not squash or rebase) to preserve branch history

---

## 10. End-to-End Example: A Bug Fix

Here's what it looks like when a team member fixes a bug, from discovery to release.

### 1. Bug is reported

Someone files issue #20 on wsjtx-internal: "FT8 decoder misses callsigns with /P suffix on Windows."

### 2. Team member picks it up

```bash
git checkout develop
git pull
git checkout -b 20-fix-portable-suffix-decode
```

### 3. Fix and test

The developer finds the bug in `lib/ft8/decode.f90`, fixes it, and tests locally with a known `.wav` file that contains a /P callsign.

```bash
git add lib/ft8/decode.f90
git commit -m "fix: handle /P suffix in FT8 decoder (fixes #20)"
git push -u origin 20-fix-portable-suffix-decode
```

### 4. PR and CI

```bash
gh pr create --base develop --title "fix: handle /P suffix in FT8 decoder" \
  --body "Fixes #20. The suffix parsing was skipping the portable indicator.
Tested with the WA6BEV.wav reference file on macOS ARM64."
```

CI runs the default Linux x86_64 build and tests, and they pass.

### 5. Review and merge

Another team member reviews the Fortran change, confirms the logic, and approves. The PR is merged to `develop`. Issue #20 is automatically closed by the "Fixes #20" reference.

### 6. Eventually released

If a line is in its beta phase, the fix ships in the first weekly beta cut after a `develop` commit containing it passes push CI. With no line in its beta phase, it ships in the next line's betas. A frozen line gets the fix only once it is cherry-picked to that line's `release/X.Y`.

---

## 11. End-to-End Example: A New Release

Here's the complete flow for releasing `3.3.0-rc1`.

### 1. Release decision

The team decides to freeze `release/3.3`, which a maintainer cut from a green `develop` to start its weekly betas ([starting a release line](DEPLOYMENT_PLAYBOOK.md#starting-a-release-line)). Later `3.3.x` patches use that existing release branch with `RC N` metadata commits and cherry-picked fixes, never a `BETA` state, so unrelated work on `develop` is not included.

### 2. Version preparation

Set the tracked release state to `RC 1` in the freeze commit, and merge it through a reviewed PR, which also reviews the line's `release-linux-images.json`, before creating any tag:

```bash
git fetch origin
git switch -c freeze-3.3.0-rc1 origin/release/3.3
# Set release-state.txt to version 3.3.0, channel RC, and prerelease 1.
# Keep windows_signing=unsigned; only the GA metadata commit omits it.
git add release-state.txt
git commit -m "chore(release): prepare 3.3.0-rc1"
git push -u origin freeze-3.3.0-rc1
gh pr create --base release/3.3 --title "chore(release): prepare 3.3.0-rc1"
```

After review, merge the PR and wait for CI on `release/3.3`. Then bump `develop` to `3.4.0` in a one-line PR, and publish `samples/contents_3.4.json` to `feat-web-pages` as the [sample catalog procedure](#3-promote-the-exact-source) describes.

### 3. Validate and create the candidate

From the `release/3.3` tip, run **Prepare Release Candidate** twice with version `3.3.0-rc1` and the tip's full SHA: first `operation=validate`, then `operation=create`. Do not create or move the tag locally. The helper creates immutable `build/v3.3.0-rc1` only after its checks pass.

### 4. Automated pipeline runs

The Prepare Release Candidate run calls internal `release.yml` to build validation artifacts from the candidate tag, but does not publish a release or copy source. Inspect that run and retain its run ID.

```
  build/v3.3.0-rc1
    └─→ private validation builds
          └─→ Promote Release Source validate, then promote
                └─→ public v3.3.0-rc1
                      └─→ public Linux, macOS, and Windows builds
                            └─→ signing-mode and provenance checks
                                  └─→ final `public-release` approval
                                        └─→ public prerelease
```

### 5. Promote and verify

```bash
# After running Promote Release Source with validate and then promote:
gh api repos/WSJTX/wsjtx/git/ref/heads/release/3.3 --jq .object.sha
gh api repos/WSJTX/wsjtx/git/ref/tags/v3.3.0-rc1 --jq .object.sha

# After the public workflow verifies artifacts and publishes:
gh release view v3.3.0-rc1 --repo WSJTX/wsjtx --json isPrerelease,url

# Check which release is Latest:
gh release view --repo WSJTX/wsjtx --json tagName,url
```

### 6. Distribute

- Post the public GitHub Release link to the mailing list
- Publish `Release_Notes.txt` and the user guide generated from the tagged source on the public `feat-web-pages` branch, which GitHub Pages serves at `https://wsjtx.github.io/wsjtx/` (see [Promote the exact source](#3-promote-the-exact-source))
- Update `https://wsjt.sourceforge.io/Release_Notes.txt` to match

### 7. External contributors sync

For an RC, external contributors can fetch the public tag while `master` remains at the latest GA:

```bash
git fetch upstream
git switch --detach v3.3.0-rc1
```

After a newest-line GA promotion, public `master` advances to the GA commit and normal fork synchronization resumes. An older-line patch leaves it at the newer GA source; run `gh release view --repo WSJTX/wsjtx --json tagName,url` and confirm it still names the newer GA.

---

## 12. Quick Reference

### For Team Members

| I want to... | Do this |
|--------------|---------|
| Start new work | `git checkout develop && git pull && git checkout -b feat-my-feature` |
| Submit my changes | `git push -u origin feat-my-feature` then `gh pr create --base develop` |
| Check CI status | Look at the PR's status checks, or `gh run list` |
| Trigger an RC or GA release | Commit matching metadata on `release/X.Y`; run Prepare Release Candidate validate/create, inspect the candidate, then run Promote Release Source validate/promote. See [§6](#6-the-release-process). |
| See build logs | `gh run view <RUN_ID> --log` |
| Re-run a failed build | `gh run rerun <RUN_ID>` |
| Manually trigger CI | `gh workflow run ci.yml --ref develop` |

### For External Contributors

| I want to... | Do this |
|--------------|---------|
| Get the source | Fork `WSJTX/wsjtx` on GitHub, then `git clone` your fork |
| Submit a fix | Create a branch, push to your fork, open PR to `WSJTX/wsjtx` `master` |
| Update my fork | `git fetch upstream && git merge upstream/master` |
| Report a bug | Open an issue on `WSJTX/wsjtx` using the bug report template |
| Build from source | Follow the [source-build guide](../doc/user_guide/en/install-from-source.adoc) |

### Key URLs

| Resource | URL |
|----------|-----|
| Internal repo | `https://github.com/WSJTX/wsjtx-internal` (team only) |
| Public repo | `https://github.com/WSJTX/wsjtx` |
| CI runs | `https://github.com/WSJTX/wsjtx-internal/actions` |
| Public release builds | `https://github.com/WSJTX/wsjtx/actions` |
| Issues | `https://github.com/WSJTX/wsjtx-internal/issues` |
| Releases | `https://github.com/WSJTX/wsjtx/releases` |
