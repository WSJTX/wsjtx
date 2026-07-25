# WSJT-X Development Workflow

How the WSJT-X project uses its two-repo model, how team members and external contributors participate, how CI/CD automates quality checks and releases, and how all the pieces fit together.

**Audience:** Public-facing. Current team members, prospective contributors, and anyone evaluating the project's development practices.

*This document describes the post-adoption target state — how development on `WSJTX/*` would operate once the team adopts the proposed CI/CD machinery. The CI/CD segments described run today in the `KJ5HST-LABS/wsjtx-internal` sandbox, not yet on `WSJTX/*`. Read this as reference material for the shape of the workflow, not as a current-state description of `WSJTX/*`.*

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

- **wsjtx** is the public face of the project. It receives code only at release time — when a version is tagged in wsjtx-internal, the release pipeline automatically syncs the source and tag to the public repo. External contributors fork this repo.

### How they stay in sync

The repos are **not** GitHub forks of each other — they are independent Git repos that share history. Synchronization happens in one direction only:

```
wsjtx-internal  ──→  wsjtx
   (private)     tag    (public)
                 sync
```

When a version tag (e.g., `build/v3.0.1`) is pushed to wsjtx-internal:
1. The release workflow builds all five platforms
2. It creates a GitHub Release on wsjtx-internal with downloadable binaries
3. It pushes the current source to `master` on wsjtx
4. It pushes the version tag to wsjtx

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

They **cannot** directly access wsjtx-internal, push branches to either org repo, or trigger workflows. CI/CD runs entirely on wsjtx-internal — the public repo receives only source + tags + GitHub Release artifacts at release time. External PRs are triaged by a team member, who brings accepted changes into wsjtx-internal where the pipeline builds and tests them.

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

When the PR is opened (and on every subsequent push to the PR branch), CI builds the code on all five platforms:

- **macOS ARM64** — builds, signs, and notarizes
- **macOS Intel x86_64** — builds, signs, and notarizes
- **Linux x86_64** — builds
- **Linux aarch64** — builds (ARM Linux, via `ubuntu-24.04-arm`)
- **Windows x86_64** — builds and signs via MSYS2/MinGW

Green checks mean it compiles everywhere. A red X means something broke — click the check to see which platform failed and view the logs.

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
4. CI validates the change on all five platforms
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
- **Build instructions** are in the CONTRIBUTING.md file. Build locally before submitting.
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
  or open a PR  ──> │  ┌────────┐ ┌────────┐ ┌───────┐ ┌─────────┐ ┌────────┐ │
                    │  │ macOS  │ │ macOS  │ │ Linux │ │  Linux  │ │  Win   │ │
                    │  │ ARM64  │ │ Intel  │ │ x86   │ │ aarch64 │ │  x86   │ │
                    │  └───┬────┘ └───┬────┘ └───┬───┘ └────┬────┘ └───┬────┘ │
                    │      │          │          │          │          │      │
                    │      v          v          v          v          v      │
                    │   Green ✓    Green ✓    Green ✓    Green ✓    Green ✓   │
                    └─────────────────────────────────────────────────────────┘
```

**What triggers CI:**
- Every push to `develop`
- Every pull request targeting `develop`
- Manual trigger via the Actions UI (workflow_dispatch)

**What CI checks:**
- The code compiles on all five platforms (macOS ARM64, macOS Intel x86_64, Linux x86_64, Linux aarch64, Windows x86_64)
- On macOS: the binary is correctly signed and notarized
- On Windows: the NSIS installer is signed — GA releases via SignPath Foundation (rebuilt and signed from the public repo by `sign-windows-release.yml`), CI/DEVEL/RC builds via a per-run ephemeral self-signed cert with `osslsigncode` (no stored secret).
- Build artifacts are uploaded for inspection
- **Tests pass on every platform** (Qt helpers, decoder smoke tests, pFUnit Fortran unit tests — registered via ctest). See [Test Failure Policy](#test-failure-policy) below.

**What CI does NOT check (yet):**
- Code style or linting
- Documentation generation

**How to read CI results:**
- On a PR, scroll to the bottom to see the status checks
- Green check = all platforms built successfully
- Red X = at least one platform failed. Click to see which one and read the logs.
- Yellow circle = builds still running

### Release: Build and Publish

The release pipeline is separate from CI. It only runs when a version tag is pushed. See [Section 6](#6-the-release-process).

### Where CI runs

CI runs on GitHub-hosted runners:

| Platform | Runner | Architecture | Cost |
|----------|--------|-------------|------|
| macOS ARM64 | `macos-15` | ARM64 (Apple Silicon) | 10x multiplier on Actions minutes |
| macOS Intel | `macos-15-intel` | x86_64 | 10x multiplier on Actions minutes |
| Linux | `ubuntu-24.04` | x86_64 | 1x (baseline) |
| Linux aarch64 | `ubuntu-24.04-arm` | aarch64 | 1x (baseline) |
| Windows | `windows-latest` + MSYS2 | x86_64 | 2x multiplier |

**Free tier:** GitHub provides 2,000 free Actions minutes/month for private repos (with multipliers applied). A single CI run across all five platforms uses roughly 45 minutes of real time but ~120 minutes of billed time due to the macOS 10x multiplier (applied to both macOS jobs); the added Linux aarch64 job contributes ~10 minutes at the 1x rate.

**Caching:** Hamlib builds and MSYS2 packages are cached to reduce build times. First-run builds are slower; subsequent builds use the cache.

### Test Failure Policy

Tests run via ctest at the end of each platform's build job, after compilation succeeds. The policy is **hard-fail everywhere**:

- Any test failure fails the platform's build job
- A failed build job fails the entire CI run (red X on the PR or push)
- A failed build job also blocks `release.yml` — the release job in `release.yml` declares `needs: [prepare, macos, macos-intel, linux, linux-arm, windows]`, so a tag-triggered release cannot publish unless every platform's tests pass

This is the simplest possible policy for v1. If a flaky test emerges, the team can add `continue-on-error: true` to the offending test's platform as a targeted soft-warn, file an issue to triage the flake, and remove the exception once fixed. No blanket soft-warn policy on `develop`.

**Where to see results:**

- **GitHub Actions step summary** — each build job posts a `## Test Results — <platform> — PASS/FAIL` table with per-test status and timing. Viewable on the job summary page without downloading logs.
- **Job log** — `ctest --output-on-failure` prints the full decoder/test output for any failing test directly into the job log.
- **Uploaded artifact** — each build job uploads `ctest-results-<platform>.xml` (JUnit-format XML) as a run artifact. Download via `gh run download <RUN_ID> --name ctest-results-<platform>` or via the Actions UI. Useful for programmatic parsing or post-mortem.

---

## 6. The Release Process

Releases are tag-driven. The entire process from tag to published release is automated.

> *Sandbox triggers on the `build/v*` tag prefix (release.yml:5). The examples in this section use the current sandbox convention. Upstream adoption may switch to bare `v*` — that change is tracked separately (decision #1 in the adoption email).*

### Overview

```
Team decides to release v3.0.1
         │
         v
  Tag build/v3.0.1 on v3.0.0_test (wsjtx-internal)
         │
         v
  ┌───────────────────────────────────────────────┐
  │              release.yml                      │
  │                                               │
  │  1. Build macOS ARM64 (signed + notarized)     │
  │  2. Build macOS Intel x86_64 (signed + notar) │
  │  3. Build Linux x86_64                        │
  │  4. Build Linux aarch64                       │
  │  5. Build Windows x86_64 (signed — sandbox:   │
  │     self-signed, production: Authenticode)    │
  │                                               │
  │  6. Create GitHub Release on wsjtx-internal   │
  │     with all platform binaries attached       │
  │                                               │
  │  7. Sync source to WSJTX/wsjtx (public)       │
  │     - Push code to master                     │
  │     - Push tag build/v3.0.1                   │
  └───────────────────────────────────────────────┘
         │
         v
  Public repo updated. External contributors
  can now see the new code.
```

### Step by step

#### 1. Prepare the release

Ensure `develop` is in a releasable state:
- All planned changes are merged
- CI is green on `develop`
- Version string in `CMakeLists.txt` is correct (the single source of truth; `release.yml`'s prepare job enforces tag↔CMakeLists parity before any platform build runs)
- Release notes or changelog are updated

#### 2. Tag the release

Tag the release on the appropriate **release branch** (`v*_test`), **not on `develop`**. The `develop` branch is the integration trunk and may contain work in progress — for example, **until the team decides whether `v3.0.0_test` merges to `develop` (decision #2 in the adoption email), v3.0.1 is cut from `v3.0.0_test`** because `develop` carries JTTY work that should not ship in a v3.0.1 patch release. If decision #2 flips (i.e., `v3.0.0_test` is merged to `develop`), v3.0.1 would instead be cut from the merged trunk.

```bash
# Check out the release branch the tag should live on.
# For v3.0.1, that's the v3.0.0_test branch (patches of the 3.0 line).
git checkout v3.0.0_test
git pull origin v3.0.0_test
git tag build/v3.0.1
git push origin build/v3.0.1
```

The `build/v*` tag pattern triggers `release.yml` (release.yml:5).

See [Branch Strategy](#7-branch-strategy) for the `v*_test` release-branch convention.

#### 3. Monitor the release

```bash
gh run watch --repo WSJTX/wsjtx-internal
```

The release workflow takes roughly 20-45 minutes depending on cache state. It builds all five platforms in parallel, then runs the release job sequentially.

#### 4. Verify

After the workflow completes:

```bash
# Check the release was created:
gh release view build/v3.0.1 --repo WSJTX/wsjtx-internal

# Check the public repo received the sync:
gh api repos/WSJTX/wsjtx/tags --jq '.[0].name'
# Should show: build/v3.0.1
```

#### 5. Post-release

- Announce the release through normal channels (email, website)
- Upload artifacts to SourceForge if that's still the primary distribution channel
- External contributors can now `git pull upstream master` to get the new code

### Release candidates

Before cutting a final release, cut one or more release candidates (RCs) and let the team exercise them. The workflow is identical to a final release, with two exceptions:

1. **The tag uses a SemVer pre-release suffix** — `v3.0.1-rc1`, `v3.0.1-rc2`, etc. (The hyphen is the distinguishing feature.)
2. **`release.yml` marks the resulting GitHub Release as a pre-release.** Any tag containing a hyphen is passed through with `gh release create --prerelease`, so the RC does not appear as "latest" on the releases page.

#### When to cut an RC

Cut an RC whenever a release contains more than a trivial change — any feature work, non-obvious bug fixes, or changes to the build, signing, or notarization path. A pure doc or CI-config release does not need an RC.

#### Tagging an RC

RCs are tagged on the **same release branch** that the final release will be tagged on — never on `develop`. For a v3.0.1 patch release that, for example, cuts from `v3.0.0_test`:

```bash
git checkout v3.0.0_test
git pull origin v3.0.0_test
git tag build/v3.0.1-rc1
git push origin build/v3.0.1-rc1
```

This triggers a full pipeline run. Because the tag contains a hyphen, the resulting GitHub Release on wsjtx-internal is flagged `prerelease: true`.

#### Testing an RC

Before promoting an RC to GA, confirm:

- All five platform jobs in the `Release` workflow ran green
- The macOS `.pkg` installs without Gatekeeper warnings (notarization is live)
- At least one volunteer on each supported platform (macOS ARM64, macOS Intel x86_64, Linux x86_64, Linux aarch64, Windows x86_64) has installed the RC and exercised the workflow they care about
- No critical issue has been filed against the RC for a reasonable soak period (typically 48 hours after the platform volunteers confirm)

If an RC fails testing, push a fix to the release branch and tag `-rc2`, `-rc3`, etc. Each RC is an independent pipeline run and an independent GitHub Release — the earlier RCs remain in the release history as pre-releases for reference.

#### Promoting an RC to GA

Once an RC has been exercised and is ready to ship, tag the final release on the same release branch:

```bash
git checkout v3.0.0_test
git pull origin v3.0.0_test
git tag build/v3.0.1
git push origin build/v3.0.1
```

There is no separate "promote the RC" command — the `build/v3.0.1` tag triggers a new full pipeline run, which re-builds from the same source (the same commit the last RC built from, assuming no changes landed on the release branch after the last RC). The new GitHub Release is created without the pre-release flag, so it becomes the "latest" release. Earlier RCs remain in the release history.

If new changes landed on the release branch between the last RC and the GA tag, consider cutting one more RC first — the GA build should be bit-for-bit the same as an RC that the team has already exercised.

### What the release produces

| Artifact | Platform | Signed | Notes |
|----------|----------|--------|-------|
| `wsjtx-3.0.1-arm64-macOS.pkg` | macOS ARM64 | Yes (Developer ID + Apple Notarization) | Gatekeeper-ready, no user warnings |
| `wsjtx-3.0.1-x86_64-macOS.pkg` | macOS Intel x86_64 | Yes (Developer ID + Apple Notarization) | Gatekeeper-ready, no user warnings |
| `wsjtx-3.0.1-linux-x86_64.AppImage` | Linux x86_64 | No | GPG signing can be added |
| `wsjtx-3.0.1-linux-aarch64.AppImage` | Linux aarch64 | No | GPG signing can be added |
| `wsjtx-3.0.1-win64.exe` | Windows x86_64 | Yes (GA: SignPath Foundation Authenticode; RC/DEVEL: ephemeral self-signed via `osslsigncode`) | NSIS installer |
| Individual binary `.tar.gz` archives | macOS ARM64, macOS Intel | Yes | Signed and notarized |
| `wsjtx-3.0.1-src.tar.gz` | Source | N/A | `git archive` of the tagged commit; top-level repo only (no submodules) |

### Who can trigger a release?

Anyone with push access to wsjtx-internal can create a tag and trigger a release. In practice, releases should be coordinated with the team — don't tag a release unilaterally.

---

## 7. Branch Strategy

### Branch types

| Prefix | Purpose | Base branch | Merges to | Lifetime |
|--------|---------|-------------|-----------|----------|
| `feat-*` | New feature | `develop` | `develop` via PR | Until merged |
| `fix-*` | Bug fix | `develop` | `develop` via PR | Until merged |
| `<issue#>-*` | Issue-linked work | `develop` | `develop` via PR | Until merged |
| `v*_test` | Release candidate | `develop` | `master` (public) at release | Until release ships |
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

CI runs. All five platforms build green.

### 5. Review and merge

Another team member reviews the Fortran change, confirms the logic, and approves. The PR is merged to `develop`. Issue #20 is automatically closed by the "Fixes #20" reference.

### 6. Eventually released

When the team decides to release the next version, this fix is included automatically — it's already on `develop`. The tag triggers the release pipeline, and the fix reaches the public repo and the downloadable binaries.

---

## 11. End-to-End Example: A New Release

Here's the complete flow for releasing version 3.0.1.

### 1. Release decision

The team agrees (via email) that the `v3.0.0_test` release branch is ready for a point release — all backported fixes are in, and CI is green on the branch. (For a patch release like v3.0.1, the base is the existing release branch — **not `develop`**, which may contain later features, e.g., JTTY, that aren't part of this release.)

### 2. Version preparation

If version strings need updating:

```bash
git checkout v3.0.0_test
git pull origin v3.0.0_test
# Update version in CMakeLists.txt (the VERSION field on the project() call).
git commit -m "chore: bump version to 3.0.1"
git push origin v3.0.0_test
```

Wait for CI to go green on this commit.

### 3. Tag and push

```bash
# Still on v3.0.0_test after the version bump.
git tag build/v3.0.1
git push origin build/v3.0.1
```

### 4. Automated pipeline runs

The `release.yml` workflow triggers automatically:

```
  build/v3.0.1 tag pushed
    │
    ├─→ macOS ARM64 build (8 min, cached)
    │     └─→ Signed .pkg + notarized binaries
    │
    ├─→ macOS Intel x86_64 build (10 min, cached)
    │     └─→ Signed .pkg + notarized binaries
    │
    ├─→ Linux x86_64 build (7 min, cached)
    │     └─→ AppImage + individual binaries
    │
    ├─→ Linux aarch64 build (7 min, cached)
    │     └─→ AppImage + individual binaries
    │
    ├─→ Windows x86_64 build (15 min, cached)
    │     └─→ NSIS installer (GA: unsigned here, SignPath signs the
    │         public-repo rebuild; RC/DEVEL: ephemeral self-signed)
    │
    └─→ Release job (after all builds complete)
          ├─→ Pushes source to WSJTX/wsjtx master
          ├─→ Pushes tag v3.0.1 to WSJTX/wsjtx
          │     └─→ triggers sign-windows-release.yml there:
          │         rebuild from public source → SignPath signs →
          │         signtool verifies chain
          ├─→ Waits for the signed installer, swaps it in
          └─→ Creates GitHub Releases (internal + public) with
                all installer-grade artifacts, Windows exe signed
```

### 5. Verify

```bash
# Release on internal repo:
gh release view build/v3.0.1 --repo WSJTX/wsjtx-internal

# Tag on public repo:
gh api repos/WSJTX/wsjtx/tags --jq '.[0].name'

# Download and spot-check an artifact:
gh release download build/v3.0.1 --repo WSJTX/wsjtx-internal --pattern '*.pkg' --dir /tmp
pkgutil --check-signature /tmp/wsjtx-3.0.1-arm64-macOS.pkg
```

### 6. Distribute

- Post the GitHub Release link to the mailing list
- Upload artifacts to SourceForge (if still used as distribution channel)
- Update the website (wsjtx.github.io/wsjtx) if applicable

### 7. External contributors sync

External contributors update their forks:

```bash
git fetch upstream
git merge upstream/master
```

They now have the v3.0.1 code and can branch from it for future contributions.

---

## 12. Quick Reference

### For Team Members

| I want to... | Do this |
|--------------|---------|
| Start new work | `git checkout develop && git pull && git checkout -b feat-my-feature` |
| Submit my changes | `git push -u origin feat-my-feature` then `gh pr create --base develop` |
| Check CI status | Look at the PR's status checks, or `gh run list` |
| Trigger a release | On the `v*_test` release branch (not `develop`): `git tag build/v3.0.1 && git push origin build/v3.0.1`. See [§6](#6-the-release-process). |
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
| Build from source | See the CONTRIBUTING.md file for platform-specific instructions |

### Key URLs

| Resource | URL |
|----------|-----|
| Internal repo | `https://github.com/WSJTX/wsjtx-internal` (team only) |
| Public repo | `https://github.com/WSJTX/wsjtx` |
| CI runs | `https://github.com/WSJTX/wsjtx-internal/actions` |
| Issues | `https://github.com/WSJTX/wsjtx-internal/issues` |
| Release artifacts | `https://github.com/WSJTX/wsjtx-internal/releases` |
