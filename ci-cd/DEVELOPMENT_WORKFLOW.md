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

- **wsjtx** is the public face of the project. It receives an exact release commit and tag only after a release manager approves promotion of a successful internal candidate. External contributors fork this repo.

### How they stay in sync

The repos are **not** GitHub forks of each other — they are independent Git repos that share history. Synchronization happens in one direction only:

```
wsjtx-internal  ──→  wsjtx
   (private)     tag    (public)
                 sync
```

For a version such as `3.2.0-rc1`:
1. `build/v3.2.0-rc1` identifies an immutable internal candidate and builds validation artifacts
2. A release manager inspects that run and manually promotes its exact commit as public tag `v3.2.0-rc1`
3. The public repository builds and signs the distribution artifacts from that tag
4. A final `public-release` approval publishes the GitHub prerelease or release

An RC promotion publishes only its tag, so public `master` remains the latest GA source. GA promotion also advances public `master` to the same commit. This keeps public source, signed binaries, and the release page bound to one reviewed revision.

The public repo never pushes back to internal. Changes from external contributors are manually cherry-picked or merged by a team member (see [Section 4](#4-contributing-from-outside-the-team)).

---

## 2. Roles and Access

### Team Members

Team members have **write access** to both repos. They can:
- Push branches directly to wsjtx-internal
- Open and merge pull requests
- Create and manage issues
- Trigger workflow runs manually
- Create tags (which trigger releases)

### Org Admins

Org admins can additionally:
- Manage org settings (Actions permissions, member roles)
- Configure repository secrets
- Add or remove team members
- Set branch protection rules

### External Contributors

External contributors have **read access** to the public repo (wsjtx). They can:
- Fork wsjtx to their own GitHub account
- Open pull requests from their fork to wsjtx
- File issues on wsjtx

They **cannot** directly access wsjtx-internal, push branches to either org repo, or trigger protected release operations. Candidate CI runs internally; promoted RC and GA source tags and their distribution builds are public. External PRs are triaged by a team member, who brings accepted changes into wsjtx-internal where the pipeline builds and tests them.

### Access Summary

| Action | Team Member | Org Admin | External Contributor |
|--------|:-----------:|:---------:|:--------------------:|
| Read wsjtx-internal | Yes | Yes | No |
| Push to wsjtx-internal | Yes | Yes | No |
| Read wsjtx (public) | Yes | Yes | Yes |
| Push to wsjtx (public) | Yes | Yes | No (fork + PR) |
| Open PRs on wsjtx-internal | Yes | Yes | No |
| Open PRs on wsjtx (public) | Yes | Yes | Yes (from fork) |
| Create tags/releases | Yes | Yes | No |
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

- **macOS ARM64** — builds; Developer ID signing and notarization require credentials
- **macOS Intel x86_64** — builds; Developer ID signing and notarization require credentials
- **Linux x86_64** — builds
- **Linux aarch64** — builds (ARM Linux, via `ubuntu-24.04-arm`)
- **Linux armhf** — builds in the armhf container
- **Windows x86_64** — builds and signs via MSYS2/MinGW

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
- On macOS, ordinary CI uses ad-hoc signing for validation. Public RC and GA packages require Developer ID signing, notarization, and stapling.
- On Windows, public RC and GA installers are Authenticode-signed through SignPath. Ordinary CI may use a per-run ephemeral self-signed certificate.
- Build artifacts are uploaded for inspection
- **Tests pass on every platform** (Qt helpers, decoder smoke tests, pFUnit Fortran unit tests — registered via ctest). See [Test Failure Policy](#test-failure-policy) below.

**What CI does NOT check (yet):**
- Code style or linting
- Documentation generation

**How to read CI results:**
- On a PR, scroll to the bottom to see the status checks
- Green check = all selected jobs completed successfully
- Red X = at least one selected job failed. Click it to read the logs.
- Yellow circle = builds still running

### Release: Build and Publish

The release pipeline is separate from CI. An internal candidate tag builds without publishing; manual source promotion starts public distribution builds, and a second approval publishes them. See [Section 6](#6-the-release-process).

### Where CI runs

CI runs on GitHub-hosted runners:

| Platform | Runner | Architecture | Cost |
|----------|--------|-------------|------|
| macOS ARM64 | `macos-15` | ARM64 (Apple Silicon) | 10x multiplier on Actions minutes |
| macOS Intel | `macos-15-intel` | x86_64 | 10x multiplier on Actions minutes |
| Linux | `ubuntu-24.04` | x86_64 | 1x (baseline) |
| Linux aarch64 | `ubuntu-24.04-arm` | aarch64 | 1x (baseline) |
| Linux armhf | `ubuntu-24.04` + container | armhf | 1x (baseline) |
| Windows | `windows-latest` + MSYS2 | x86_64 | 2x multiplier |

**Free tier:** GitHub provides 2,000 free Actions minutes/month for private repos (with multipliers applied). macOS jobs dominate billed time because both use the 10x multiplier; Linux targets run at the baseline rate.

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

Releases are tag-defined but approval-driven. The internal `build/v...` tag fixes the candidate revision; it does not publish source or binaries. A release manager separately approves copying that exact revision to the public `v...` tag, and the public release waits for one final approval after its signed artifacts are available for inspection.

### Overview

```
release/3.2 metadata commit
  └─→ internal build/v3.2.0-rc1 candidate and validation artifacts
        └─→ manual source promotion
              └─→ public v3.2.0-rc1 tag
                    └─→ public all-platform builds and signing
                          └─→ public-release approval
                                └─→ public GitHub prerelease
```

### Step by step

#### 1. Prepare tracked release metadata

Cut `release/3.2` from a green `develop`, then make a small metadata commit before every RC or GA candidate. `release-state.txt` records the numeric version, `DEVEL`, `RC` plus its positive RC number, or `GA`; its export-substituted revision also preserves the source SHA outside Git. This makes a build from GitHub's automatic source archive identify itself correctly even though that archive has no `.git` directory.

Do not put an RC or GA tag on a `DEVEL` commit. The helper rejects disagreement among the requested version, tag, and tracked release state; CMake derives its version from that same state.

#### 2. Create the internal candidate

Run **Prepare Release Candidate** from `release/3.2` at the intended SHA. First use `operation=validate`, then `operation=create`, supplying the version (`3.2.0-rc1`, for example) and the full SHA. Creation makes immutable tag `build/v3.2.0-rc1`; the same workflow run calls `release.yml` to build all internal validation artifacts but publishes nothing.

Inspect the candidate run and installable validation artifacts. Record its run ID for promotion.

#### 3. Promote the exact source

From the same release branch and SHA, run **Promote Release Source** with the same version, the candidate run ID, and `operation=validate`. After reviewing its summary, rerun with `operation=promote`. The workflow verifies the immutable tag, commit, release metadata, current release-branch tip, candidate run, and artifacts before creating public tag `v3.2.0-rc1` at the same commit.

The public tag exposes the corresponding source required for public distribution and triggers fresh public distribution builds. RC promotion does not move public `master`; GA promotion advances `master` to the same commit with a guarded update.

#### 4. Review signed builds and approve publication

The public workflow builds all supported targets. Both RC and GA Windows installers use SignPath production signing. Both RC and GA macOS installers must be Developer ID-signed, notarized, stapled, and verified.

While Apple credentials are being provisioned, `MACOS_DISTRIBUTION_SIGNING_ENABLED=false` permits clearly named unsigned validation artifacts but blocks publication. It never converts an unsigned validation package into an official release asset. After `apple-release-signing` is configured, enable the variable and rerun the immutable public tag workflow.

Download and review `release-bundle-<version>`, including its checksums, manifest, and signing reports. Approve the waiting `public-release` environment only when they all correspond to the public tag and expected SHA. This final approval publishes an RC as a GitHub prerelease or GA as the latest release.

#### 5. Recover without moving tags

Rerun a failed workflow against the existing immutable tag after fixing transient credentials or service configuration. If source or tracked release metadata must change, commit the fix and cut the next RC. Never delete, recreate, or force-move a candidate or public release tag; immutable tags keep reviews, source, signatures, and downloaded artifacts attributable to one commit.

### Release candidates

Before a final release, cut one or more RCs and let the team exercise the same public, signed distribution path. An RC uses a SemVer suffix such as `3.2.0-rc1` and is published as a GitHub prerelease, so it does not replace the latest GA release.

#### When to cut an RC

Cut an RC whenever a release contains more than a trivial change — any feature work, non-obvious bug fixes, or changes to the build, signing, or notarization path. A pure doc or CI-config release does not need an RC.

#### Tagging an RC

RCs and GA are prepared on the same `release/X.Y` branch, never directly on `develop`. Make and merge the matching `RC n` metadata commit before asking Prepare Release Candidate to create the candidate tag.

#### Testing an RC

Before promoting an RC to GA, confirm:

- All six target jobs in the public release workflow ran green
- The macOS `.pkg` passes the signing, staple, Gatekeeper, entitlement, and installed-runtime checks in the Deployment Playbook
- At least one volunteer on each supported platform (macOS ARM64, macOS Intel x86_64, Linux x86_64, Linux aarch64, Windows x86_64) has installed the RC and exercised the workflow they care about
- No critical issue has been filed against the RC for a reasonable soak period (typically 48 hours after the platform volunteers confirm)

If an RC fails testing, push a fix to the release branch, update the metadata to the next RC number, and create `-rc2`, `-rc3`, etc. Each RC remains an independent public prerelease for reference.

#### Promoting an RC to GA

Change the tracked state from `RC n` to `GA` in a metadata-only commit, wait for CI, and create a new `3.2.0` candidate through the same validate/create/promote/approve sequence. Even when application source is unchanged, the GA commit is intentionally distinct so ordinary builds from its GitHub source archive report GA rather than RC.

### What the release produces

| Artifact | Platform | Signed | Notes |
|----------|----------|--------|-------|
| `wsjtx-3.2.0-rc1-arm64-macOS.pkg` | macOS ARM64 | Yes | Developer ID signed, notarized, and stapled; verify per the Deployment Playbook |
| `wsjtx-3.2.0-rc1-x86_64-macOS.pkg` | macOS Intel x86_64 | Yes | Developer ID signed, notarized, and stapled; verify per the Deployment Playbook |
| `wsjtx-3.2.0-rc1-linux-x86_64.AppImage` | Linux x86_64 | No | Published with matching `.deb` and `.rpm` packages |
| `wsjtx-3.2.0-rc1-linux-aarch64.AppImage` | Linux aarch64 | No | Published with matching `.deb` and `.rpm` packages |
| `wsjtx-3.2.0-rc1-linux-armhf.AppImage` | Linux armhf | No | Published with matching `.deb` and `.rpm` packages |
| `wsjtx-3.2.0-rc1-win64.exe` | Windows x86_64 | Yes | SignPath Foundation Authenticode for RC and GA |
| `wsjtx-3.2.0-rc1-src.tar.gz` | Source | N/A | Project-created archive of the public tagged commit |
| `SHA256SUMS` and release manifest | All uploaded assets | N/A | Bind uploaded bytes to their public tag, commit, and build provenance |

GitHub also adds automatic **Source code (zip)** and **Source code (tar.gz)** links from the public tag. They represent the same tagged source but are generated and compressed by GitHub, so their archive hashes need not equal the project-created `.tar.gz`. `SHA256SUMS` covers the assets the project uploads; a checksum detects changed bytes but is not a substitute for the platform signatures or the tag-to-commit checks.

### Who can trigger a release?

Team members can run candidate validation. Creating the candidate, promoting its source, and approving the `public-release` environment are explicit release-manager actions; repository protection and environment access determine who can perform each one.

---

## 7. Branch Strategy

### Branch types

| Prefix | Purpose | Base branch | Merges to | Lifetime |
|--------|---------|-------------|-----------|----------|
| `feat-*` | New feature | `develop` | `develop` via PR | Until merged |
| `fix-*` | Bug fix | `develop` | `develop` via PR | Until merged |
| `<issue#>-*` | Issue-linked work | `develop` | `develop` via PR | Until merged |
| `release/X.Y` | RC and GA stabilization | `develop` | Public release tags; `master` only at GA | Maintained for patch releases |
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

CI runs. All six targets build green.

### 5. Review and merge

Another team member reviews the Fortran change, confirms the logic, and approves. The PR is merged to `develop`. Issue #20 is automatically closed by the "Fixes #20" reference.

### 6. Eventually released

When the team decides to release the next version, this fix is included automatically — it's already on `develop`. The tag triggers the release pipeline, and the fix reaches the public repo and the downloadable binaries.

---

## 11. End-to-End Example: A New Release

Here's the complete flow for releasing `3.2.0-rc1`.

### 1. Release decision

The team cuts `release/3.2` from a green `develop`. Later `3.2.x` patches use that existing release branch so unrelated work on `develop` is not included.

### 2. Version preparation

Update the numeric version and tracked release state, then commit them before creating any tag:

```bash
git switch release/3.2
git pull origin release/3.2
# Set release-state.txt to version 3.2.0, channel RC, and rc 1.
git commit -m "chore(release): prepare 3.2.0-rc1"
git push origin release/3.2
```

Wait for CI to go green on this commit.

### 3. Validate and create the candidate

From `release/3.2` at the metadata commit, run **Prepare Release Candidate** twice with version `3.2.0-rc1` and the full SHA: first `operation=validate`, then `operation=create`. Do not create or move the tag locally. The helper creates immutable `build/v3.2.0-rc1` only after its checks pass.

### 4. Automated pipeline runs

The Prepare Release Candidate run calls internal `release.yml` to build validation artifacts from the candidate tag, but does not publish a release or copy source. Inspect that run and retain its run ID.

```
  build/v3.2.0-rc1
    └─→ private validation builds
          └─→ Promote Release Source validate, then promote
                └─→ public v3.2.0-rc1
                      └─→ public Linux, macOS, and Windows builds
                            └─→ signing and provenance checks
                                  └─→ public-release approval
                                        └─→ public prerelease
```

### 5. Promote, verify, and approve

```bash
# After running Promote Release Source with validate and then promote:
gh api repos/WSJTX/wsjtx/git/ref/tags/v3.2.0-rc1 --jq .object.sha

# After reviewing the public signing reports and approving public-release:
gh release view v3.2.0-rc1 --repo WSJTX/wsjtx
```

### 6. Distribute

- Post the public GitHub Release link to the mailing list
- Upload artifacts to SourceForge (if still used as distribution channel)
- Update the website (wsjtx.github.io/wsjtx) if applicable

### 7. External contributors sync

For an RC, external contributors can fetch the public tag while `master` remains at the latest GA:

```bash
git fetch upstream
git switch --detach v3.2.0-rc1
```

After GA promotion, public `master` advances to the GA commit and normal fork synchronization resumes.

---

## 12. Quick Reference

### For Team Members

| I want to... | Do this |
|--------------|---------|
| Start new work | `git checkout develop && git pull && git checkout -b feat-my-feature` |
| Submit my changes | `git push -u origin feat-my-feature` then `gh pr create --base develop` |
| Check CI status | Look at the PR's status checks, or `gh run list` |
| Trigger a release | Commit matching metadata on `release/X.Y`; run Prepare Release Candidate validate/create, inspect the candidate, then run Promote Release Source validate/promote. See [§6](#6-the-release-process). |
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
| Issues | `https://github.com/WSJTX/wsjtx-internal/issues` |
| Release artifacts | `https://github.com/WSJTX/wsjtx-internal/releases` |
