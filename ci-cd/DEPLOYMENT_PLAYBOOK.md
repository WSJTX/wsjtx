# CI/CD Deployment Playbook

Reference for configuring and operating the WSJT-X GitHub Actions CI/CD pipeline in the official `WSJTX` organization repositories.

**Audience:** Repository administrators and release managers responsible for CI/CD configuration and release credentials.

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Architecture Overview](#2-architecture-overview)
3. [Phase 1: Enable GitHub Actions on the Org](#3-phase-1-enable-github-actions-on-the-org)
4. [Phase 2: Install the Release Workflows](#4-phase-2-install-the-release-workflows)
5. [Phase 3: Create Repository Secrets](#5-phase-3-create-repository-secrets)
6. [Phase 4: Supporting Files](#6-phase-4-supporting-files)
7. [Phase 5: Submit the PR](#7-phase-5-submit-the-pr)
8. [Phase 6: Test the CI Pipeline](#8-phase-6-test-the-ci-pipeline)
9. [Phase 7: Test the Release Pipeline](#9-phase-7-test-the-release-pipeline)
10. [Scheduled Beta Releases](#10-scheduled-beta-releases)
11. [Ongoing Maintenance](#11-ongoing-maintenance)
12. [Troubleshooting](#12-troubleshooting)
13. [Reference: Complete File Inventory](#13-reference-complete-file-inventory)

---

## 1. Prerequisites

Before starting, confirm every item on this list. Missing any one of them will block deployment.

### Access & Permissions

| Requirement | How to Verify | Who Can Grant |
|-------------|---------------|---------------|
| **Org admin** or **repo admin** on `WSJTX/wsjtx-internal` | Go to repo → Settings. If you see "Actions" in the left sidebar, you have admin access. | Org owner (Joe K1JT) |
| **Write access** to `WSJTX/wsjtx` (public repo) | Try `gh api repos/WSJTX/wsjtx --jq .permissions.push` — should return `true` | Org owner |
| **GitHub Actions enabled** at the org level | Org Settings → Actions → General. Must not show "Actions permissions: Disabled" | Org owner |
| **Create and install** the release-bot GitHub App on `WSJTX/wsjtx-internal` | Org Settings → Developer settings → GitHub Apps | Org owner |

### Credentials You'll Need

| Credential | Where to Get It | Format |
|------------|-----------------|--------|
| Apple Developer ID Application certificate (.p12) | Export from the Apple Developer account holder's keychain | PKCS12 file + password |
| Apple Developer ID Installer certificate (.p12) | Export from the Apple Developer account holder's keychain | PKCS12 file + password |
| App Store Connect API key (for notarization) | A team-owned key created by the Apple Developer Account Holder | `.p8` private key, key ID, and issuer ID |
| Apple Team ID | Apple Developer portal → Membership Details | 10-char alphanumeric like `ABCDE12345` |
| GitHub fine-grained PAT | github.com → Settings → Developer settings → Fine-grained tokens | `github_pat_...` token string |
| Release-bot GitHub App private key | Generated in the App's settings (§5.6) | `.pem` file plus the App ID or client ID |
| SignPath API token | The SignPath CI user's API token (§5.4) | Token string |

### Tools

- `gh` ([GitHub CLI](https://cli.github.com/)) authenticated with an account that has admin access to the target repos
- `base64` command (macOS and Linux both have this)
- Git with push access to `WSJTX/wsjtx-internal`

### Required Repository Configuration

The release pipeline requires this repository configuration. Each row links to its procedure.

| Area | Requirement | Details |
|------|-------------|---------|
| Private branches | Rulesets on `develop` and `release/*` forbid force-push and deletion, so GitHub reports them as protected. Prepare Release Dependencies runs only from protected `develop` and requires the selected source branch to be protected. | [Branch, Tag, and Environment Protection](#branch-tag-and-environment-protection), [Prepared Linux release images](#prepared-linux-release-images) |
| Public refs | `v*` tags: no update or deletion, and only the source-promotion identity creates them. `master` and `release/*`: no force-push or deletion, and no other rule. | [Branch, Tag, and Environment Protection](#branch-tag-and-environment-protection) |
| Private environments | `candidate-tagging` and `source-promotion` admit only `release/*`; `beta-automation` admits only `develop`. None has required reviewers, which would stall the [beta scheduler](#10-scheduled-beta-releases). | [Navigate to Secrets Settings](#navigate-to-secrets-settings) |
| Secrets | `CROSS_REPO_TOKEN` only in `source-promotion`, with no repository-level copy. No `SIGNPATH_API_TOKEN` in `wsjtx-internal`. On `WSJTX/wsjtx`: `SIGNPATH_API_TOKEN` only in `windows-release-signing`; Apple credentials, when distribution signing is enabled, only in `apple-release-signing`. | [Verification: Credential Boundaries](#verification-credential-boundaries) |
| Publication environments | `public-release`: one required-reviewers rule (`release-approvers`), self-review allowed, `v*` tags; RC and GA. `beta-release`: no reviewer, one tag rule `v*-beta*`; betas. | [Public Final Publication Approval](#public-final-publication-approval), [Public Beta Publication](#public-beta-publication) |
| Public signing environments | `apple-release-signing` admits `v*` tags and holds the Apple identity variables; `windows-release-signing` admits `master` and `v*` tags and holds variables `WINDOWS_SIGNER_SUBJECT` and `WINDOWS_SIGNER_THUMBPRINTS`. Repository variable `MACOS_DISTRIBUTION_SIGNING_ENABLED` on `WSJTX/wsjtx` selects macOS distribution signing. | [5.2 macOS Code Signing Environment](#52-macos-code-signing-environment), [5.4 Windows Authenticode Signing via SignPath Foundation](#54-windows-authenticode-signing-via-signpath-foundation) |
| Release-bot App | Installed on `wsjtx-internal` with Contents, Workflows, and Actions write. `beta-automation` holds variable `RELEASE_BOT_APP_ID` (the App's numeric ID or client ID) and secret `RELEASE_BOT_PRIVATE_KEY`. | [5.6 Release-bot GitHub App](#56-release-bot-github-app) |
| Beta switch | Repository variable `BETA_AUTOMATION` on `WSJTX/wsjtx-internal` equals `enabled` while betas run. Never set on `WSJTX/wsjtx`. | [Pause and freeze](#pause-and-freeze) |
| Issues | Issues enabled on `WSJTX/wsjtx-internal` for the beta alert issue | [Failure alerts](#failure-alerts) |
| Prepared image packages | `ghcr.io/wsjtx/wsjtx/linux-noble`, `ghcr.io/wsjtx/wsjtx/linux-arm64-bookworm`, `ghcr.io/wsjtx/wsjtx/linux-armhf-cross-bookworm`, and `ghcr.io/wsjtx/wsjtx/linux-armv7-bookworm` are public, and `wsjtx-internal` has write access to them. | [Prepared Linux release images](#prepared-linux-release-images) |
| Linux image selection | Each `release/X.Y` has a committed `release-linux-images.json` before its first candidate. | [Prepared Linux release images](#prepared-linux-release-images) |

---

## 2. Architecture Overview

Understanding the architecture will help you debug issues during deployment.

> **Tag convention.** Internal `build/v*` tags identify immutable candidates. Only promotion creates the corresponding public `v*` tag: a release manager promotes RCs and GA releases, and the beta scheduler promotes betas. Public distribution builds start from that public tag. Public tags are `vX.Y.Z-betaN`, `vX.Y.Z-rcN`, or `vX.Y.Z`; [Public branch history and GA line policy](#public-branch-history-and-ga-line-policy) gives their order.

### Release channels

`release-state.txt` names the channel: `DEVEL`, `BETA`, `RC`, or `GA`. A release line `release/X.Y` has three phases:

1. **Beta phase.** Each Tuesday the beta scheduler cuts a beta from the newest green `develop` and carries it through candidate creation, promotion, and publication with no human step. It skips a week in which `develop` has no green commit that the line lacks. See [Scheduled Beta Releases](#10-scheduled-beta-releases).
2. **Freeze.** A reviewed commit that sets `RC 1` ends the beta phase ([Pause and freeze](#pause-and-freeze)).
3. **RC and GA.** A release manager promotes each candidate, and a member of the `release-approvers` team approves publication, as [Phase 7](#9-phase-7-test-the-release-pipeline) describes.

Every promotion, beta included, creates or fast-forwards public `release/X.Y`, so the beta line is a public branch. Betas are GitHub prereleases; they never become Latest and never move `master`.

`DEVEL`, `BETA`, and `RC` GUI builds show a prerelease notice and expire ([Release state contract](#release-state-contract)); `GA` builds and the command-line programs never expire.

**Fixes land on `develop` first, in every phase.** After the freeze, cherry-pick each fix from `develop` to `release/X.Y`. In the beta phase, push no fix to the line, not even a cherry-pick: the scheduler stops the line on any commit that `develop` does not contain, other than the App's merges and metadata-only commits ([state machine](#state-machine) step 4). After each GA, merge the GA commit into `develop`, as the history bridge in [Public branch history and GA line policy](#public-branch-history-and-ga-line-policy) describes.

### Workflow Structure

```
.github/workflows/
├── ci.yml                       ← Orchestrator. Triggers on develop and release/**.
│                                    Linux x86_64 is the default path; full-ci or a
│                                    manual run selects broader platform coverage.
│
├── build-macos.yml              ← Reusable workflow (workflow_call).
│                                    macOS build (arm64 or x86_64); Developer ID
│                                    signing and notarization require credentials.
│
├── build-linux.yml              ← Reusable workflow (workflow_call).
│                                    Linux build (x86_64, aarch64, or armhf),
│                                    unsigned.
│
├── build-windows.yml            ← Reusable workflow (workflow_call).
│                                    Windows x86_64 via MSYS2/MinGW64. Builds
│                                    the installer unsigned and verifies
│                                    NotSigned.
│
├── sign-windows-release.yml     ← Reusable workflow (workflow_call) called by
│                                    public-release.yml on WSJTX/wsjtx. Rebuilds the
│                                    installer from public source; SignPath signs it
│                                    unless the tagged state is unsigned.
│
├── signpath-smoke.yml           ← workflow_dispatch; public repo.
│                                    ~2-minute SignPath round-trip check
│                                    with a trivial PE — no WSJT source built.
│
├── hamlib-upstream-check.yml    ← Scheduled (`cron: '0 12 * * MON'`) + `workflow_dispatch`.
│                                    Weekly poll of Hamlib upstream tags; files a
│                                    GitHub issue when a newer 4.x release is available.
│                                    No platform builds; self-contained.
│
├── prepare-release-dependencies.yml ← Prepare Release Dependencies: workflow_dispatch
│                                    from develop. Copies the four Linux images to public
│                                    packages and produces release-linux-images.json.
│
├── release-tag-helper.yml       ← Prepare Release Candidate: workflow_dispatch.
│                                    Validates the release-branch tip and its CI, creates
│                                    the build/v* tag, then calls release.yml.
│
├── release.yml                  ← Internal `build/v*` candidate builds only.
│                                    Uploads validation artifacts; never publishes.
│
├── promote-release.yml          ← Promote Release Source: workflow_dispatch.
│                                    Validates a candidate and promotes its tag;
│                                    manual for RC and GA, dispatched for betas.
│
├── public-release.yml           ← Build and Publish Public Release: v* tag push on
│                                    WSJTX/wsjtx. Rebuilds from public source, verifies
│                                    the bundle, and publishes the GitHub Release.
│
├── beta-cut.yml                 ← Scheduled Beta Cut: weekly cron + workflow_dispatch.
│                                    Applies one state-machine step.
│
└── beta-continue.yml            ← Scheduled Beta Continue: workflow_run.
                                     Carries each App-started beta run onward.
```

### How It Flows

**On pushes and pull requests to private `develop` or `release/**`:**
```
Push or pull request
  └─→ ci.yml triggers
       ├─→ validate workflow policy and release-state.txt
       └─→ build/test Linux x86_64 by default
            └─→ full-ci or manual selection adds other platforms
```

**For an RC or GA release:**
```
release/X.Y metadata commit
  └─→ Prepare Release Candidate validates/creates build/vX.Y.Z[-rcN]
       └─→ release.yml builds the private candidate
            └─→ release manager validates/promotes that candidate
                 └─→ public vX.Y.Z[-rcN] builds and checks all distributions
                          └─→ assembled bundle summary and final `public-release` approval
                                └─→ verified GitHub Release publication
```

**For a scheduled beta:**
```
beta-cut.yml merges the newest green develop into release/X.Y as BETA N
  └─→ ci.yml push run on release/X.Y
       └─→ beta-continue.yml dispatches Prepare Release Candidate: build/vX.Y.Z-betaN
            └─→ release.yml builds the private candidate on every target
                 └─→ beta-continue.yml dispatches Promote Release Source
                      └─→ public vX.Y.Z-betaN builds and checks all distributions
                           └─→ publication through beta-release, no approval
                                └─→ GitHub prerelease; never Latest; master does not move
```

**For `beta1`, or a retry at a green tip:**
```
beta-cut.yml dispatches Prepare Release Candidate at the tip, with no merge
  └─→ release.yml builds the private candidate on every target
       └─→ continues as above
```

Source promotion makes the candidate source public. The public workflow builds and verifies the bundle. An RC or GA then waits for final publication approval in `public-release`; a beta publishes through `beta-release` without one. The [state machine](#state-machine) decides which path a cut takes. See [Public branch history and GA line policy](#public-branch-history-and-ga-line-policy) for branch and tag behavior.

### Build Strategy

Linux candidate and public builds compile WSJT-X from the selected source commit using prepared dependency images. macOS and Windows build native dependency prefixes and reuse them through Actions caches.

### Cache readiness

Native build summaries show dependency cache hits, misses, and rebuild results. Compiler-cache rows distinguish exact and fallback restores from a miss; a forced recompile is reported separately. The dependency warmer also summarizes each selected platform's result. A cache hit alone does not establish that the application build passed.

Use `cold_build=true` on a manually dispatched macOS or Windows build to bypass workflow-managed dependency and compiler-cache restores and saves. Windows also bypasses the MSYS2 package cache. This leaves the runner's preinstalled software in place. `recache` only forces application recompilation and does not make dependencies cold. The macOS build budget is 180 minutes; confirm a cold run completes dependency builds, tests, and packaging within that budget before relying on it for a release.

### Prepared Linux release images

Before creating a candidate, run Prepare Release Dependencies from protected private `develop`, selecting a protected `develop` or `release/X.Y` source branch and an explicit validated image generation. The workflow resolves the selected branch to an exact commit before validating its recipes. Refresh incompatible or missing images through the existing Linux image publisher first.

Preparation verifies all four images against the selected source recipes: x86_64, aarch64, and the ARMHF cross-builder/runtime pair. It copies them to `ghcr.io/wsjtx/wsjtx` without changing their digests and verifies anonymous access. Images contain dependency tooling, not WSJT-X application builds. Review changes to copied scripts and image metadata before public preparation; a digest-preserving copy retains the original OCI labels and build metadata.

The private workflow needs package read access to the internal images and write access to the destination packages through its `GITHUB_TOKEN`. Destination packages must be public, which only the web UI can set. An organization owner first allows public packages under Org Settings → Packages → Package creation. Then, on each destination package's Package settings, add `WSJTX/wsjtx-internal` with role Write under Manage Actions access, and choose Public under Change visibility. A public package cannot be made private again. The workflow changes neither visibility nor access. Prepared image retention tags must remain available for release rebuilds.

Download the successful `prepared-release-dependencies` artifact and review `release-linux-images.json`. Commit that file on the private release branch before candidate tagging. It records the validated generation, four public image digests, and their recipe fingerprints. Application-only changes may reuse it; recipe changes require a new preparation. Keep the detailed preparation record with the private workflow artifacts.

The selection belongs to its release branch and never lands on `develop`. In the beta phase the scheduler replaces it without review when `develop`'s recipes change ([state machine](#state-machine) step 5); RC and GA candidates keep the review above.

Candidate and public builds consume the committed selection and build WSJT-X afresh. They stop on missing images or incompatible recipes instead of resolving `stable` or refreshing images during release. Candidate provenance binds the selection's checksum and digests; source promotion verifies that binding. The public release manifest records the same builder digests.

### What the Release Produces

Each published public `v*` tag yields, on the public GitHub Release, one installer and the command-line program tarballs per target, a `.deb` and an `.rpm` per Linux architecture, a source tarball, `SHA256SUMS`, and `release-manifest.json`:

| Artifact | Produced by | Format |
|----------|-------------|--------|
| `wsjtx-<ver>-arm64-macOS.pkg` | `build-macos.yml` (arm64 leg) | GA: signed in distribution mode; signed by hand and replaced in manual mode. RC: the unsigned validation package in manual mode; signed in distribution mode. Beta: the unsigned validation package |
| `wsjtx-<ver>-x86_64-macOS.pkg` | `build-macos.yml` (x86_64 leg) | GA: signed in distribution mode; signed by hand and replaced in manual mode. RC: the unsigned validation package in manual mode; signed in distribution mode. Beta: the unsigned validation package |
| `wsjtx-<ver>-linux-x86_64.AppImage` | `build-linux.yml` (x86_64 leg) | Portable AppImage |
| `wsjtx-<ver>-linux-aarch64.AppImage` | `build-linux.yml` (aarch64 leg) | Portable AppImage |
| `wsjtx-<ver>-linux-armhf.AppImage` | `build-linux.yml` (armhf leg) | Portable AppImage |
| Linux x86_64 `.deb` and `.rpm` | `build-linux.yml` (x86_64 leg) | Distribution packages |
| Linux aarch64 `.deb` and `.rpm` | `build-linux.yml` (aarch64 leg) | Distribution packages |
| Linux armhf `.deb` and `.rpm` | `build-linux.yml` (armhf leg) | Distribution packages |
| `wsjtx-<ver>-win64.exe` | `build-windows.yml`; signed by `sign-windows-release.yml` | GA: SignPath Foundation Authenticode. Beta and RC: unsigned |
| `wsjtx-<ver>-arm64-macOS-<tarball>.tar.gz` | `build-macos.yml` | Programs with the dylibs they load in `lib/`; ad-hoc signed, not notarized |
| `wsjtx-<ver>-x86_64-macOS-<tarball>.tar.gz` | `build-macos.yml` | Programs with the dylibs they load in `lib/`; ad-hoc signed, not notarized |
| `wsjtx-<ver>-linux-<arch>-<tarball>.tar.gz` | `build-linux.yml` (x86_64, aarch64 and armhf legs) | Programs in `bin/` with the libraries they load in `lib/`, except the host libraries linuxdeploy leaves out; unsigned |
| `wsjtx-<ver>-windows-x86_64-<tarball>.tar.gz` | `build-windows.yml` | Programs with the DLLs they import; not Authenticode-signed |
| `wsjtx-<ver>-src.tar.gz` | Public release workflow | Source tarball from the public tag |
| `SHA256SUMS` | Public release workflow | SHA-256 of each immutable asset |
| `release-manifest.json` | Public release workflow | Tag, source commit, workflow run, builder digests, signing modes, and immutable asset hashes |

`CMake/Install.cmake` defines the command-line programs every installer carries, `wsjt_installed_cli_tools`: the install rules cover it, and the macOS package stages `/usr/local/wsjtx` from the build's `installed-cli-tools.txt`; unless `/usr/local/wsjtx` is a symbolic link, its postinstall removes programs there that the package does not ship, with their `/usr/local/bin` links, and leaves `lib/` alone. Staging fails if a listed program is missing.

`CMake/release-tarballs.txt` defines the release tarballs, one per line: `jt9`; `codec`, which holds `wsjt-codec`, the stream decoder for other front ends; `wsprd`; and `utilities`, which holds `wsprcode`, `encode77` and the signal simulators that no installer carries. `release-policy.py` reads the same file for the asset names, and `CMake/Install.cmake` adds its programs, whatever the build options, to the build's `cli-tools.txt`, from which the macOS build collects the programs it packages. `.github/scripts/package-cli-tools.sh` gives each tarball the libraries its programs load beyond the operating system's: on macOS the dylibs reached through `@rpath`, on Windows the import closure from the staged installation, and on Linux what linuxdeploy deploys outside its excludelist (`.github/scripts/linuxdeploy-excludelist.txt`), with RUNPATH `$ORIGIN/../lib` on programs and `$ORIGIN` on libraries, and the Debian copyright files of the libraries that come from Debian packages. Each tarball also holds `README.txt`, which gives the layout, the writable directories the programs need, the WSJT-X copyright notice from `doc/common/license.adoc` and, on Linux, the oldest glibc and the host libraries the programs need; `COPYING`; and `THIRD-PARTY.txt`, which names each bundled library's component and license from `.github/scripts/third-party-libraries.tsv`. Packaging fails if a listed program is missing, a dependency resolves neither inside the tarball nor to the operating system, a Linux RUNPATH entry is not relative to `$ORIGIN`, or a bundled library is missing from that table. Each build then runs every program from its own extracted tarball with the library search variables unset (`smoke-release-tarballs.sh`) before it uploads the tarballs.

The project-created source tarball is assembled from the public tag. GitHub also generates its own zip and tar.gz source archives for that tag; they contain the tagged tree but may have different compressed hashes. `SHA256SUMS` covers immutable payload assets uploaded by the workflow. For an RC or GA in manual macOS signing mode it excludes each macOS package, which the release manifest identifies as replaceable; a beta's macOS packages are not replaceable, so `SHA256SUMS` covers them. The manifest also records the tag, source commit, workflow run, builder provenance, and both signing modes. Checksums detect changed bytes; platform signatures establish signer identity and must be verified separately.

The release body is a Quick Downloads list of the installers and the source tarball, then the change list that `generate-change-list.py` builds from the promoted commit subjects. It lists features, fixes, and performance changes by their Conventional Commits type, each with its author, except those whose scopes are all among `release`, `ci`, `test`, and `tests`; it counts the other commits and links the full comparison. A beta or RC lists the changes since the newest earlier public tag on its `X.Y` line, or, for a line's first release, since the newest earlier public tag of any line. A GA lists the changes since the newest earlier GA. Commits that an earlier public release already contains are left out, and so are commits whose patch matches one in an earlier release of the same line or the line before it; for a GA, only earlier GAs count. A trailing pull request number in a subject is dropped, and any other `#` or `@` reference is shown as code: the numbers belong to the private repository, and an `@` name would notify that account. If the script fails, or prints nothing because no earlier public tag exists, the publish step warns and keeps GitHub's generated notes, which hold only a comparison link because release commits merge in the private repository. Only the run that creates a release writes its body.

### All-Platforms-Ready Gate

Before publishing, the release workflow requires each platform build to produce its expected installer and release tarball artifacts. This prevents a structurally successful build job from creating a partial release.

The gate checks for exactly one non-empty installer per platform and every release tarball:

| Platform | Expected artifact pattern |
|----------|---------------------------|
| macOS arm64 | `artifacts/wsjtx-<ver>-arm64-macOS.pkg/*.pkg` in distribution mode, or `artifacts/wsjtx-<ver>-arm64-macOS-unsigned.pkg/*.pkg` in manual mode and for betas |
| macOS x86_64 | `artifacts/wsjtx-<ver>-x86_64-macOS.pkg/*.pkg` in distribution mode, or `artifacts/wsjtx-<ver>-x86_64-macOS-unsigned.pkg/*.pkg` in manual mode and for betas |
| Linux x86_64 | `artifacts/wsjtx-<ver>-linux-x86_64-AppImage/*.AppImage` |
| Linux aarch64 | `artifacts/wsjtx-<ver>-linux-aarch64-AppImage/*.AppImage` |
| Linux armhf | `artifacts/wsjtx-<ver>-linux-armhf-AppImage/*.AppImage` |
| Linux packages | One non-empty `.deb` in `artifacts/wsjtx-<ver>-linux-<arch>-deb/` and one `.rpm` in `artifacts/wsjtx-<ver>-linux-<arch>-rpm/` for each architecture, checked when the manifest is written |
| Windows x86_64 | Signed mode: `artifacts/wsjtx-<ver>-windows-x86_64-installer-signed/*.exe`; unsigned mode: `artifacts/wsjtx-<ver>-windows-x86_64-installer/wsjtx-<ver>-win64.exe` and no `-installer-signed` artifact |
| Release tarballs | One non-empty `.tar.gz` in `artifacts/wsjtx-<ver>-<target>-<tarball>/` for each target (`arm64-macOS`, `x86_64-macOS`, `linux-x86_64`, `linux-aarch64`, `linux-armhf`, `windows-x86_64`) and each tarball in `CMake/release-tarballs.txt` |

The four tarball groups across six platforms require 24 release tarballs.

If any pattern matches no file or more than one, the release job stops before publishing.

Presence alone is insufficient. The publication gate also requires the expected production filenames and reports, and verifies that their hashes, public tag, and source SHA agree. In manual macOS signing mode the reports prove that the unsigned packages came from the expected validation builds; for a GA release, their final signatures are verified after manual replacement. A separate installed-runtime smoke test remains necessary because cryptographic verification does not establish application behavior.

---

## 3. Phase 1: Enable GitHub Actions on the Org

This is a **one-time setup** that requires org owner access.

### Step 1: Navigate to Org Actions Settings

```
https://github.com/organizations/WSJTX/settings/actions
```

Or: GitHub → WSJTX org → Settings (gear icon) → Actions → General

### Step 2: Set Actions Permissions

Under **Actions permissions**, select one of:
- **"Allow all actions and reusable workflows"** — simplest, allows everything
- **"Allow WSJTX, and select non-WSJTX, actions and reusable workflows"** — more restrictive

If you choose the restrictive option, enable "Allow actions created by GitHub", because several workflows pin `actions/*` by commit SHA and the beta scheduler uses `actions/create-github-app-token`. Then allow these third-party actions:
- `docker/build-push-action@v7`
- `docker/login-action@v4`
- `docker/setup-buildx-action@v4`
- `docker/setup-qemu-action@v4`
- `msys2/setup-msys2@v2`
- `signpath/github-action-submit-signing-request@v3`

**Important:** GitHub's allowlist must admit each workflow `uses:` entry: the GitHub-created option covers `actions/*`, and the list above covers the rest. Re-run the following after any Dependabot bump to stay aligned:

```bash
grep -rhoE 'uses: [^.][^ ]*' .github/workflows/ .github/actions/ | sort -u
```

### Step 3: Set Workflow Permissions

Under **Workflow permissions**, select:
- **"Read and write permissions"**

This allows the release workflow to create GitHub Releases and push tags. Without write permissions, the release job will fail with a 403 error.

### Step 4: Verify

```bash
gh api orgs/WSJTX --jq '.has_organization_projects'
# Just verifying API access to the org works

gh api repos/WSJTX/wsjtx-internal/actions/permissions --jq '.enabled'
# Should return: true
```

If `enabled` returns `false`, Actions is still disabled. Double-check the org settings.

---

## 4. Phase 2: Install the Release Workflows

The workflows in this repository are already configured for private trunk `develop`, release branches `release/**`, public branch `master`, and public repository `WSJTX/wsjtx`. Copying an older prototype or changing repository names inside `release.yml` would restore the obsolete behavior where an internal build published and synchronized source in one step.

The release-related files have distinct responsibilities:

| File | Responsibility |
|------|----------------|
| `release-state.txt` | Tracked numeric version, channel, prerelease number, optional Windows signing mode, and archive revision placeholder |
| `prepare-release-dependencies.yml` | Verify the four Linux release images against a branch's recipes, copy them to the public packages, and produce `release-linux-images.json` |
| `release-tag-helper.yml` | Validate the release-branch tip and CI, create an immutable private candidate tag, then call the candidate build |
| `release.yml` | Build private validation artifacts only; never publish or copy source |
| `promote-release.yml` | Validate the candidate run and atomically create its immutable tag and create or fast-forward its public release branch; advance public `master` only for newest-line GA |
| `public-release.yml` | Rebuild from public source, enforce the tagged signing policy, verify the bundle, publish (RC and GA after approval; betas through `beta-release`), and preserve Latest on older-line GA |
| `beta-cut.yml` | Weekly cron or a dispatch: apply one step of the [state machine](#state-machine) to the line in `BETA` state |
| `beta-continue.yml` | After each App-started beta or preparation run: dispatch the next stage, or alert on a failure ([Continuation](#continuation)) |

`release-state.txt` is the version source of truth. Keep `revision=$Format:%H$` literal in Git; Git expands it in exported archives. The workflows reject a tag whose version, channel, or prerelease number does not match the tracked state.

The Hamlib version remains pinned on reusable-workflow calls. To audit all pin sites before changing it:

```bash
rg -n 'hamlib_branch:' .github/workflows
```

### Release state contract

`release-state.txt` holds four or five `key=value` lines, each key once. Any other key, such as `rc`, is rejected.

| Key | Value |
|-----|-------|
| `version` | Numeric `X.Y.Z` |
| `channel` | `DEVEL`, `BETA`, `RC`, or `GA` |
| `prerelease` | A positive number `N` with no leading zero for `BETA` and `RC`; empty for `DEVEL` and `GA` |
| `revision` | The literal `$Format:%H$` in Git; the full commit ID in an exported archive |
| `windows_signing` | Optional: `signpath`, the default, or `unsigned`. `release-policy.py` rejects a `BETA` state without `unsigned`; RC metadata commits set `unsigned` by procedure, and nothing checks it ([Step 1](#step-1-prepare-release-metadata)) |

The channel and number set the version suffix: `-devel`, `-betaN`, `-rcN`, or none for `GA`. Candidate tags, public tags, and asset names carry the suffixed version, for example `build/v3.3.0-beta1`, `v3.3.0-beta1`, and `wsjtx-3.3.0-beta1-win64.exe`. A `DEVEL` state is never tagged.

Linux `.deb` and `.rpm` package versions write the suffix as `~betaN` or `~rcN`, so package managers order a beta before an RC and an RC before the GA release. A `DEVEL` package version is `X.Y.Z-devel` (`X.Y.Z_devel` in an `.rpm`) and sorts after the GA release of the same version. dpkg and rpm therefore treat that version's beta, RC, or GA package as older than an installed `DEVEL` package.

The Windows installer's `DisplayVersion` uses the hyphenated form, such as `3.3.0-beta1`, and its VersionInfo product and file version strings add a `v`, such as `v3.3.0-beta1`. Each installer version writes its own Add/Remove Programs entry, while every version has the same default install directory and so shares one uninstaller. Every entry has the same display name and runs the same `Uninstall.exe`, the last-installed version's, which removes only that version's entry; the other entries remain. The macOS package version is hyphenated, the app bundle's short version string is `v`-prefixed, such as `v3.3.0-beta1`, and the bundle version is the numeric `X.Y.Z`.

`DEVEL`, `BETA`, and `RC` builds of the WSJT-X GUI show a prerelease notice at every start. Once the 90th day (UTC) after the built commit's date has ended, they close after that notice. A build from a source archive, which has no Git metadata, counts from its configure date, and `SOURCE_DATE_EPOCH`, when set, replaces either date. `GA` builds never expire, and the command-line programs never expire in any channel.

---

## 5. Phase 3: Create Repository Secrets

The public `public-release` environment requires one approval from a member of the `release-approvers` team and allows self-review. For an RC or GA, a release manager's `operation=promote` dispatch approves public source, and only the final `publish` job waits for release approval. Betas publish through `beta-release`, which has no reviewer. `CROSS_REPO_TOKEN` is a secret of the private `source-promotion` environment.

### Public Final Publication Approval

Configure WSJTX/wsjtx Settings > Environments > public-release:

1. Turn on Required reviewers and add the `release-approvers` team as the only reviewer. The team needs read access to `WSJTX/wsjtx`. Any member may approve.
2. Turn off Prevent self-review; the first job rejects the environment while it is on.
3. Under Deployment branches and tags, choose "Selected branches and tags", add one tag rule with the pattern `v*`, then save.

The first job reads the environment with `Actions: read` and stops before platform builds if the rule is missing, malformed, unreadable, or blocks self-review. Only the final `publish` job should use this environment.

Keep release credentials in their environments, not in repository secrets. A job that references `secrets.NAME` can fall back to a same-named repository or organization secret, which defeats the environment's ref restriction. When moving a secret into an environment, verify the environment copy before deleting the repository-level one. The Apple and SignPath credentials work only as environment secrets, because `public-release.yml` calls the reusable signing workflows without passing secrets.

### Public Beta Publication

Configure WSJTX/wsjtx Settings > Environments > beta-release:

1. Leave Required reviewers off.
2. Under Deployment branches and tags, choose "Selected branches and tags".
3. Add exactly one rule: a tag rule with the pattern `v*-beta*`, then save.

The `publish` job uses `beta-release` for a beta and `public-release` for an RC or GA. For a beta, the first job checks `beta-release` instead of the `public-release` reviewer rule. It stops before platform builds unless `beta-release` has no reviewer rule, uses "Selected branches and tags", and has exactly the one `v*-beta*` tag rule. That pattern matches no RC or GA tag, so no RC or GA can deploy through `beta-release`.

### Navigate to Secrets Settings

```
https://github.com/WSJTX/wsjtx-internal/settings/environments
```

Or: Repo → Settings → Environments. Configure the private environments:

1. `candidate-tagging` and `source-promotion`: choose "Selected branches and tags" and add one branch rule, `release/*`.
2. `beta-automation`: choose "Selected branches and tags" and add one branch rule, `develop`.
3. Add no required reviewers to any of the three; the scheduler's dispatched runs would wait for approval.
4. Keep the promotion token in `source-promotion` (§5.1) and the release-bot App's credentials in `beta-automation` (§5.6).

### 5.1 Secret 1: `CROSS_REPO_TOKEN`

**Purpose:** Allows Promote Release Source to push the public release branch, the public tag, and, for a newest-line GA, `master` to the public repo (`WSJTX/wsjtx`).

**How to create the PAT:**

1. Go to https://github.com/settings/personal-access-tokens/new
2. Select **"Fine-grained personal access tokens"**
3. Configure:
   - **Token name:** `wsjtx-release-sync` (or similar)
   - **Expiration:** 1 year (maximum). Set a calendar reminder to rotate it before expiry.
   - **Resource owner:** Select the `WSJTX` organization
   - **Repository access:** Select "Only select repositories" → choose `WSJTX/wsjtx`
   - **Permissions:**
     - **Contents:** Read and write (to push code)
     - **Workflows:** Read and write (to push `.github/workflows/` files)
   - All other permissions: leave as "No access"
4. Click "Generate token"
5. **Copy the token immediately** — you cannot view it again.

**Important:** The token must be created by someone who has **admin access to the target repo** (`WSJTX/wsjtx`). A fine-grained token scoped to a repo you don't admin will fail silently.

**Set the secret:**
```bash
# Paste the token when prompted (it won't echo to the terminal):
gh secret set CROSS_REPO_TOKEN --repo WSJTX/wsjtx-internal --env source-promotion
```

**Why not a deploy key?** Deploy keys cannot push `.github/workflows/` files. This is a GitHub platform restriction. The error message ("refusing to allow an OAuth App to create or update workflow") is misleading — it applies to any non-PAT credential, including deploy keys over SSH.

### 5.2 macOS Code Signing Environment

Create the `apple-release-signing` environment on `WSJTX/wsjtx`, restrict it to release tags, and grant access only to release maintainers. These four secrets provide the distinct Developer ID identities used for application code and installer packages. Prefer fresh, CI-specific certificates rather than transferring a maintainer's long-lived personal export.

#### Credential responsibilities

The WSJT-X Apple Developer membership is currently held by **John G4KLA**, who is therefore the current Apple Developer Account Holder. The responsibilities below are described by role so that the procedure remains valid if the Account Holder changes.

The Apple Developer account holder exports the signing identities. A repository administrator stores the exported credentials as GitHub Actions secrets. The team must assign responsibility for credential rotation and release-artifact verification.

The workflow uses two identities with different responsibilities:

- **Developer ID Application** signs application executables, frameworks, plug-ins, and the command-line tools the package installs.
- **Developer ID Installer** signs the outer `.pkg` installer.

Notarization uses the App Store Connect API key in §5.3 rather than either certificate password. Rotate a certificate's `.p12` and password together. Rotate the notarization key on the team's schedule, when access changes, or after suspected exposure.

The command-line program tarballs are not Developer ID-signed or notarized in either signing mode: their programs and `lib/` dylibs carry the build's ad-hoc signatures, and `SHA256SUMS` and the release manifest bind the tarball bytes to the tag and source commit. Browsers mark downloads with the `com.apple.quarantine` attribute, and macOS blocks quarantined programs that are not notarized, so remove the attribute from a tarball before extracting it: `xattr -d com.apple.quarantine wsjtx-<ver>-arm64-macOS-codec.tar.gz`.

#### Preparing the .p12 files

You need two signing identities associated with the Apple Developer team:
- **Developer ID Application** — signs the app binary and dylibs
- **Developer ID Installer** — signs the `.pkg` installer

If you already have `.p12` files exported from Keychain Access, skip to the base64 step.

**To export from Keychain Access (on a Mac):**

1. Open Keychain Access
2. In the left sidebar, select "login" keychain
3. Click "My Certificates" tab
4. Find "Developer ID Application: [Team Name]"
5. Right-click → "Export..."
6. Choose format: Personal Information Exchange (.p12)
7. Save as `app.p12`
8. Enter a password when prompted — you'll need this for the secret
9. Repeat for "Developer ID Installer: [Team Name]" → save as `installer.p12`

#### Base64-encode the certificates

GitHub secrets are text, so binary `.p12` files must be base64-encoded:

```bash
# On macOS:
base64 -i app.p12 -o app.p12.b64
base64 -i installer.p12 -o installer.p12.b64

# On Linux:
base64 -w0 app.p12 > app.p12.b64
base64 -w0 installer.p12 > installer.p12.b64
```

#### Set the four secrets

```bash
# Application signing certificate (base64-encoded .p12):
gh secret set DEVELOPER_ID_CERTIFICATE_P12 --repo WSJTX/wsjtx --env apple-release-signing < app.p12.b64

# Password for the application certificate:
gh secret set DEVELOPER_ID_CERTIFICATE_PASSWORD --repo WSJTX/wsjtx --env apple-release-signing
# (paste the password you chose when exporting, press Enter)

# Installer signing certificate (base64-encoded .p12):
gh secret set DEVELOPER_ID_INSTALLER_P12 --repo WSJTX/wsjtx --env apple-release-signing < installer.p12.b64

# Password for the installer certificate:
gh secret set DEVELOPER_ID_INSTALLER_PASSWORD --repo WSJTX/wsjtx --env apple-release-signing
# (paste the password you chose when exporting, press Enter)
```

**After setting secrets, delete the local .p12 and .b64 files.** They contain your signing keys.

```bash
rm app.p12 installer.p12 app.p12.b64 installer.p12.b64
```

### 5.3 Apple Notarization API Key

Notarization submits the signed package to Apple's service for automated security checks. An accepted submission is then stapled to the package so the ticket can be validated without contacting Apple. Notarization is distinct from Developer ID signing and does not by itself verify Gatekeeper acceptance or application behavior.

Have the Account Holder create a team-owned App Store Connect API key scoped for notarization. Store its key ID as `APP_STORE_CONNECT_KEY_ID`, issuer ID as `APP_STORE_CONNECT_ISSUER_ID`, and base64-encoded `.p8` as `APP_STORE_CONNECT_PRIVATE_KEY_P8_BASE64`, all as `apple-release-signing` environment secrets.

Configure these non-secret variables in the same environment: `APPLE_TEAM_ID`, `APPLE_APPLICATION_CERTIFICATE_SHA1`, and `APPLE_INSTALLER_CERTIFICATE_SHA1`. The SHA-1 values are the full certificate fingerprints without relying on certificate-name matching. Keeping expected identities separate lets the job reject a valid but unintended certificate.

```bash
gh secret set APP_STORE_CONNECT_KEY_ID --repo WSJTX/wsjtx --env apple-release-signing
gh secret set APP_STORE_CONNECT_ISSUER_ID --repo WSJTX/wsjtx --env apple-release-signing
gh secret set APP_STORE_CONNECT_PRIVATE_KEY_P8_BASE64 --repo WSJTX/wsjtx --env apple-release-signing
gh variable set APPLE_TEAM_ID --repo WSJTX/wsjtx --env apple-release-signing --body '<team-id>'
gh variable set APPLE_APPLICATION_CERTIFICATE_SHA1 --repo WSJTX/wsjtx --env apple-release-signing --body '<40-hex-fingerprint>'
gh variable set APPLE_INSTALLER_CERTIFICATE_SHA1 --repo WSJTX/wsjtx --env apple-release-signing --body '<40-hex-fingerprint>'
```

Do not use a maintainer's Apple ID password or app-specific password. The API key is independently revocable and does not tie unattended releases to one person's login.

### Verification: Confirm All Secrets Are Set

```bash
gh secret list --repo WSJTX/wsjtx --env apple-release-signing
```

The environment inventory must contain both certificate identities and the API-key notarization credential set declared by `build-macos.yml`: `DEVELOPER_ID_CERTIFICATE_P12`, `DEVELOPER_ID_CERTIFICATE_PASSWORD`, `DEVELOPER_ID_INSTALLER_P12`, `DEVELOPER_ID_INSTALLER_PASSWORD`, `APP_STORE_CONNECT_KEY_ID`, `APP_STORE_CONNECT_ISSUER_ID`, and `APP_STORE_CONNECT_PRIVATE_KEY_P8_BASE64`. Separately confirm `CROSS_REPO_TOKEN` in the private `source-promotion` environment.

Also configure the public workflow's non-secret expected Apple Team ID and SHA-1 fingerprints for the Application and Installer certificates. These are identifiers, not private-key material; the distribution job uses them to reject a valid but unintended identity.

Set repository variable `MACOS_DISTRIBUTION_SIGNING_ENABLED=false` on `WSJTX/wsjtx` (`gh variable set MACOS_DISTRIBUTION_SIGNING_ENABLED --repo WSJTX/wsjtx --body false`) until the full environment is configured and tested. In that state, manual mode, the workflow publishes each validated unsigned package of an RC or GA under its stable release filename. The manifest identifies each such package as replaceable and excludes it from immutable hashes; workflow reruns preserve existing packages by name. Set the variable to `true` only after both architectures complete signing, notarization, stapling, identity, entitlement, and Gatekeeper verification. Distribution mode, `true`, signs every RC and GA package and fails closed if any credential is absent. Betas ignore the variable ([Beta signing](#beta-signing)).

### 5.4 Windows Authenticode Signing via SignPath Foundation

> **How it works.** SignPath Foundation signs OSS artifacts built from the public repository, so the signature attests public-source provenance as well as identity. A promoted tag whose state selects SignPath, as a GA tag `vX.Y.Z` does, has its unsigned installer submitted under the `release-signing` policy. The public build verifies the returned Authenticode signature and timestamp and makes that verified installer eligible for publication. A failed or rejected request blocks the release. The certificate's private key lives in SignPath's HSM; there is no `.pfx` to export or store in GitHub.

SignPath signs only the installer. The Windows command-line program tarballs are not submitted: their programs and DLLs carry no Authenticode signature, and `SHA256SUMS` and the release manifest bind the tarball bytes to the tag and source commit.

#### The one secret

Create public environment `windows-release-signing`, restrict it to protected `master` and `v*` release tags, and set the token there (not on `wsjtx-internal`). `master` access permits the explicit smoke-test workflow; production signing still accepts only a public RC or GA tag.

```bash
gh secret set SIGNPATH_API_TOKEN --repo WSJTX/wsjtx --env windows-release-signing
# (paste the SignPath CI user's API token, press Enter)
```

Set `WINDOWS_SIGNER_SUBJECT` and comma-separated `WINDOWS_SIGNER_THUMBPRINTS` as variables in that environment. The thumbprint list permits an explicit certificate rollover window; remove the old value after rollover.

```bash
gh variable set WINDOWS_SIGNER_SUBJECT --repo WSJTX/wsjtx --env windows-release-signing --body '<exact certificate subject>'
gh variable set WINDOWS_SIGNER_THUMBPRINTS --repo WSJTX/wsjtx --env windows-release-signing --body '<thumbprint>[,<rollover-thumbprint>]'
```

The SignPath CI user must be a **submitter** on the signing policies (`release-signing`, `test-signing`). The public workflow consumes its own signed result; the internal promotion token does not need access to SignPath credentials.

#### SignPath dashboard configuration

- **Artifact configuration** — GitHub Actions artifacts are always ZIP-wrapped, so the root element must be `zip-file`:

  ```xml
  <?xml version="1.0" encoding="utf-8" ?>
  <artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
    <zip-file>
      <pe-file path="*.exe">
        <authenticode-sign />
      </pe-file>
    </zip-file>
  </artifact-configuration>
  ```

- **Trusted build system** — the predefined *GitHub.com* trusted build system added to the SignPath org and linked to the `wsjtx` project; the [SignPath GitHub App](https://github.com/apps/signpath) installed with access to the public repo (origin verification).

#### Validation

`signpath-smoke.yml` (workflow_dispatch on the public repo) signs a trivial hello-world PE through the complete round trip in ~2 minutes — no WSJT source is built or exposed. Dispatch it against `test-signing` to validate plumbing without consuming a `release-signing` approval. Success: the workflow completes green — the returned exe carries a parseable Authenticode signature (hard-checked); chain status is printed for inspection only, since the test certificate never chains to a trusted root.

#### Signing mode by channel

A GA installer uses SignPath `release-signing`, because the GA metadata commit omits `windows_signing`. An RC installer is unsigned because its metadata commit sets `windows_signing=unsigned` ([Step 1](#step-1-prepare-release-metadata)); the public workflow records that mode in the manifest and verifies the installer accordingly. `release-policy.py` rejects a `BETA` state without `windows_signing=unsigned` ([Beta signing](#beta-signing)).

### 5.5 Linux Signing (Optional)

Linux binary signing is less critical — Linux users don't encounter SmartScreen-style warnings when downloading binaries. However, GPG-signing release tarballs is good practice if the team distributes `.tar.gz` or `.deb` packages. This would require one additional secret (`GPG_SIGNING_KEY`) and a small step in the release workflow.

### 5.6 Release-bot GitHub App

The beta scheduler pushes merge commits to `release/X.Y` and dispatches workflows as the release-bot App. Create a GitHub App under the WSJTX organization (Org Settings → Developer settings → GitHub Apps → New GitHub App). Make its webhook inactive, subscribe it to no events, choose "Only on this account", and give it these repository permissions:

- **Actions:** Read and write, to dispatch workflows and read their runs and artifacts
- **Contents:** Read and write, to push to `release/X.Y`
- **Workflows:** Read and write, because `develop` merges routinely change `.github/workflows/*`

Install the App on `WSJTX/wsjtx-internal` only ("Only select repositories"). Each scheduler workflow mints a token for the repository it runs in, so no other installation is used. Generate a private key for the App, then store its credentials in the private `beta-automation` environment:

```bash
# The App's numeric ID or its client ID, from the App's settings page:
gh variable set RELEASE_BOT_APP_ID --repo WSJTX/wsjtx-internal --env beta-automation --body '<app-id>'

# The .pem private key downloaded from the App's settings page:
gh secret set RELEASE_BOT_PRIVATE_KEY --repo WSJTX/wsjtx-internal --env beta-automation < release-bot.private-key.pem
```

Delete the downloaded `.pem` file after setting the secret.

The App has no ruleset bypass: its merge pushes to `release/X.Y` are fast-forwards. Failure alerts use each workflow's `GITHUB_TOKEN` with `issues: write`, so the App needs no issue permission.

### Verification: Credential Boundaries

Confirm that `CROSS_REPO_TOKEN` exists only in the private `source-promotion` environment, with no repository-level copy, and that `wsjtx-internal` holds no `SIGNPATH_API_TOKEN`. `RELEASE_BOT_APP_ID` and `RELEASE_BOT_PRIVATE_KEY` belong only in private environment `beta-automation`, and `BETA_AUTOMATION` is a repository variable of `wsjtx-internal` alone. Apple material belongs only in public environment `apple-release-signing`, and `SIGNPATH_API_TOKEN` belongs only in public environment `windows-release-signing`. Only the final public `publish` job uses the `public-release` or `beta-release` environment.

```bash
# Repository level: no CROSS_REPO_TOKEN or SIGNPATH_API_TOKEN; BETA_AUTOMATION is a variable here:
gh secret list --repo WSJTX/wsjtx-internal
gh variable list --repo WSJTX/wsjtx-internal

# Environment level:
gh secret list --repo WSJTX/wsjtx-internal --env source-promotion
gh secret list --repo WSJTX/wsjtx-internal --env beta-automation
gh variable list --repo WSJTX/wsjtx-internal --env beta-automation

# Public repository level: no signing secrets and no BETA_AUTOMATION:
gh secret list --repo WSJTX/wsjtx
gh variable list --repo WSJTX/wsjtx
```

---

## 6. Phase 4: Supporting Files

These files are referenced by the workflows and must exist in the repo.

### `entitlements.plist` (repo root)

This file is the source of truth for executable entitlements. The **Code sign binaries** step applies the complete plist to every executable under `wsjtx.app/Contents/MacOS`, re-applies it when signing the app bundle and main executable, and applies it to staged standalone executables. Frameworks, plug-in libraries, and other bundled libraries do not receive this plist. The completed bundle must pass deep signature verification.

The current plist enables:

| Entitlement | Current requirement |
|-------------|---------------------|
| `com.apple.security.cs.disable-library-validation` | Allows loading bundled third-party code that does not satisfy library validation, including ad-hoc CI artifacts. |
| `com.apple.security.device.audio-input` | Allows WSJT-X to capture audio under hardened runtime. `NSMicrophoneUsageDescription` in the app's `Info.plist` supplies the separate TCC permission prompt. |

Both entitlements are currently applied as one set, including to executables that may not exercise every capability. Narrowing that scope is a security-sensitive behavior change and is outside this playbook.

Bundle signing re-signs the main executable, so it must receive the entitlement file again after nested code is signed. `codesign --verify` validates signature integrity but does not prove that required entitlements are present. Inspect the effective entitlements and exercise audio capture and a decoder cycle when validating a release.

### macOS decoder shared memory

The decoder uses native POSIX shared memory. The installer does not ship a
System V sysctl launch daemon or change system shared-memory limits, and CI
runs decoder and GUI tests without raising those limits. Existing settings
from older installations are left in place for applications that may still
need them.

### OmniRig type-library input

Windows CI passes `-DOMNIRIG_TYPE_LIB=<path>` to CMake because the workflow
already knows the location of the installed OmniRig executable. The build
supports this input directly. Local Windows builds can omit it and allow
`dumpcpp` to query the registered OmniRig type library instead.

MSYS2 installs the tool as `dumpcpp-qt5`; the build discovers both that name
and the unversioned `dumpcpp` name. Do not create an alias in the workflow.

---

## 7. Phase 5: Submit the PR

### Option A: PR from a Branch

If you have write access to `WSJTX/wsjtx-internal`, create a feature branch from `develop`, commit the workflow and supporting-file changes together, push it, and open a PR. Do not copy older prototype workflows over the current files: the candidate, promotion, signing, and publication boundaries must be reviewed as one system.

### Option B: PR from a Fork

If you don't have write access:

```bash
# Fork the repo on GitHub first, then:
git clone git@github.com:YOUR_USERNAME/wsjtx-internal.git
cd wsjtx-internal
git remote add upstream git@github.com:WSJTX/wsjtx-internal.git

# Then follow the same steps as Option A, but push to your fork:
git push -u origin ci/github-actions

# Create PR from fork to upstream:
gh pr create \
  --repo WSJTX/wsjtx-internal \
  --title "Add GitHub Actions CI/CD" \
  --body "Six-platform CI (macOS arm64, macOS x86_64, Linux x86_64, Linux aarch64, Linux armhf, Windows x86_64) with tag-triggered releases (\`build/v*\`)."
```

**Important note about forks:** Workflow files in PRs from forks don't run automatically — this is a GitHub security feature. The PR must be merged before the workflows will trigger. This means you can't test the CI from a fork PR. If you need to test before merging, use a branch on the official repo (Option A).

---

## 8. Phase 6: Test the CI Pipeline

After the PR is merged (or if you pushed directly to a test branch), verify CI works.

### Step 1: Trigger a CI Run

Push any small change to the target branch:

```bash
# If testing on a branch:
git checkout develop  # or master
echo "# CI test" >> README.md
git add README.md
git commit -m "test: trigger CI pipeline"
git push
```

### Step 2: Monitor the Run

```bash
# Watch the run in real-time:
gh run watch --repo WSJTX/wsjtx-internal

# Or list recent runs:
gh run list --repo WSJTX/wsjtx-internal --limit 5
```

### Step 3: Check the Selected Platforms

An ordinary push runs the default Linux x86_64 check. Before a candidate, apply the `full-ci` label to a PR or manually dispatch full CI and confirm all six target builds. Expected times (first run, no cache):

| Platform | First Run | Cached Run |
|----------|-----------|------------|
| macOS arm64 | ~12-15 min | ~8 min |
| macOS x86_64 (Intel) | ~15-20 min | ~10 min |
| Linux x86_64 | ~10-12 min | ~7 min |
| Linux aarch64 | ~10-12 min | ~7 min |
| Windows x86_64 | ~40-45 min | ~15 min |

Windows is the slowest because MSYS2 package installation is slow on first run.

### Step 4: Inspect Failures

If a job fails:

```bash
# View the failed run's logs:
gh run view <RUN_ID> --repo WSJTX/wsjtx-internal --log-failed
```

Common first-run failures:

| Symptom | Cause | Fix |
|---------|-------|-----|
| "Resource not accessible by integration" | Workflow permissions too restrictive | Org Settings → Actions → Workflow permissions → "Read and write" |
| macOS signing fails with empty identity | Application P12 is present, but the credential set is incomplete or invalid | Verify the application P12, its password, and the imported identity |
| macOS notarization fails | Missing, revoked, or mismatched App Store Connect API credential | Verify the API key, key ID, issuer ID, and expected Team ID in `apple-release-signing` |
| Windows build timeout (>60 min) | MSYS2 cache miss + slow package install | Re-run — the cache will be populated for next time |
| "refusing to allow an OAuth App to create or update workflow" | This error can appear at PR merge time if the branch contains workflow files and was pushed with a deploy key | Push the branch using a PAT or via the GitHub web UI instead |

### Step 5: Revert the Test Commit

```bash
git revert HEAD
git push
```

---

## 9. Phase 7: Test the Release Pipeline

Only do this after CI is green on all six targets.

### Procedures by channel

Steps 1 to 6 are the RC and GA procedure. A beta runs the same workflows with no human step, as [Scheduled Beta Releases](#10-scheduled-beta-releases) describes. The channels differ as follows:

| Aspect | Beta | RC | GA |
|-------|------|----|----|
| Metadata | `channel=BETA`, `prerelease=N`, `windows_signing=unsigned`, set by the [line-start commit](#starting-a-release-line) or a scheduler merge commit | A reviewed commit: `channel=RC`, `prerelease=N`, `windows_signing=unsigned` | A reviewed metadata-only commit: `channel=GA`, with no `windows_signing` |
| Candidate | `operation=create`, dispatched by `beta-continue.yml` after the cut's push CI succeeds, or by `beta-cut.yml` for `beta1` and a retry at a green tip | A release manager runs `operation=validate`, then `operation=create` | As for RC |
| Promotion | `operation=promote`, dispatched by `beta-continue.yml` after the candidate succeeds; its `validate` job runs first | A release manager runs `operation=validate`, then `operation=promote` | As for RC |
| Publication | Through `beta-release`, with no approval | After approval in `public-release` | After approval in `public-release` |
| GitHub Release | Prerelease; never Latest | Prerelease | Release; Latest only for the newest GA version on the newest line |
| Windows installer | Unsigned ([Beta signing](#beta-signing)) | Unsigned | SignPath Foundation Authenticode |
| macOS package | The unsigned validation package, published as-is | The unsigned validation package in manual mode; distribution mode signs it as for GA | Signed in distribution mode; signed by hand and replaced in manual mode |

### Step 1: Prepare Release Metadata

On `release/X.Y`, commit the numeric version and matching `RC N` or `GA` state in `release-state.txt`. A line's first `BETA` state comes from its [line-start commit](#starting-a-release-line), and the scheduler writes the rest. Set `windows_signing=unsigned`, the source-pinned unsigned mode, in every RC metadata commit, including the freeze commit that sets `RC 1`. For `3.3.0-rc1`, use `version=3.3.0`, `channel=RC`, `prerelease=1`, and `windows_signing=unsigned`. The GA metadata commit omits `windows_signing`, which selects SignPath. Leave the `$Format:%H$` revision placeholder intact so Git substitutes the source SHA when exporting an archive. Wait for branch CI before tagging. This metadata commit is required even when GA application source is otherwise identical to the last RC, because it makes builds from GitHub's source archives identify themselves correctly.

### Step 2: Build the Private Candidate

Commit the reviewed [Linux image selection](#prepared-linux-release-images) before creating the candidate. A line frozen from its beta phase carries the scheduler's selection, which the freeze commit's review covers ([Pause and freeze](#pause-and-freeze)); prepare a new one only when Prepare Release Candidate reports a stale recipe.

Run Prepare Release Candidate on `release/X.Y` at the expected SHA. Use `operation=validate` first and review the summary, then use `operation=create`. This creates the immutable `build/v...` tag and private validation artifacts. It does not publish or copy source.

### Step 3: Approve Public Source Promotion

Record the candidate run ID. From the same branch and SHA, run Promote Release Source with `operation=validate`; review the summary, then run it with `operation=promote`. Promotion atomically creates the immutable public tag and creates or fast-forwards `release/X.Y` at the candidate SHA, following the [public branch policy](#public-branch-history-and-ga-line-policy). Conflicting tags and non-fast-forward updates fail without changing refs. If public refs change after validation, validate again.

### Step 4: Review and Approve Final Publication

After the builds finish, review the Verified public release bundle summary before approving. It lists the tag, source SHA, run link, signing modes, assets, and checksums. In manual macOS signing mode, the summary identifies the manually replaceable macOS packages, which are excluded from `SHA256SUMS`. For a GA release, a SignPath approver must approve the `release-signing` request that the public build submits. The `sign` job waits up to 60 minutes, then fails; [Step 6](#step-6-recover-safely) covers the re-run. A GA's Windows signing mode is `signpath`, and a macOS package signed in distribution mode must be Developer ID-signed, notarized, stapled, and pass Gatekeeper. An RC shows Windows signing as source-pinned unsigned and needs no SignPath.

Once the bundle is ready, approve the final `publish` job in `public-release`. One approval from a member of the `release-approvers` team is enough. With Prevent self-review off, the person who promoted the source can approve the same run. The job rechecks the bundle and tag before publishing. Betas and RCs are prereleases. Only the newest GA version on the newest line gets Latest; other GA releases use `--latest=false`.

### Public branch history and GA line policy

Promotion publishes only the validated candidate SHA. Public tags are immutable; release branches contain only promoted commits and must advance by ancestry.

- Betas and RCs advance `release/X.Y` and leave `master` unchanged.
- A GA on the newest line also fast-forwards `master`.
- An older-line GA leaves `master` and GitHub Latest unchanged.

Determine the newest GA line from the highest numeric `X.Y` in public GA tags, ignoring betas, RCs, and release dates. Reject a GA below the highest GA tag on its line. Only the newest GA version on the newest line can become Latest. Stop if GA tags are missing or refs are malformed.

Promotion enforces strict ordering on each line: a new public tag must sort above the latest public tag on its `X.Y` line. Versions compare by `X.Y.Z`, then by channel, `BETA` < `RC` < `GA`, then by `N` as a number, so `v3.3.0-beta2` < `v3.3.0-beta10` < `v3.3.0-rc1` < `v3.3.0` < `v3.3.1-beta1`. Promoting the line's latest candidate again is idempotent: its tag already resolves to the candidate SHA, so promotion creates no tag.

The promotion workflow creates a missing `release/X.Y` branch. If that line already has public GA, RC, or beta tags, the candidate must descend from its latest tag. Do not seed public release branches manually. Subsequent candidates must descend from the existing branch.

Before a newest-line GA, verify that public `master` is an ancestor of the candidate. If it is not, stop and review the divergent history and tree differences. Reconcile the ancestry on the private release branch through a reviewed manual merge, verify that the resulting tree matches the intended release source, and rerun private CI and candidate validation. Promotion never creates this merge or force-rewrites public history.

After each GA, merge the GA commit into private `develop`. This history bridge lets the next line's GA fast-forward public `master`. The internal repository holds the GA commit as `build/vX.Y.Z` and on private `release/X.Y`; the public `vX.Y.Z` tag exists only on `WSJTX/wsjtx`.

1. On a branch from private `develop`, merge `build/vX.Y.Z` without committing.
2. Keep `develop`'s `release-state.txt`, and remove `release-linux-images.json` from the merge.
3. Resolve any other conflict to `develop`'s side.
4. Run `git diff --cached HEAD --stat`. When the release branch holds nothing that `develop` lacks, the merged tree equals `develop`'s and the command prints nothing. Each file it lists holds something the merge took from the release branch alone. It can be a block Git duplicated without a conflict marker (such as a Python test method), a change that `develop` later reverted, or a fix made only on the release branch. Restore `develop`'s version with `git restore --source=HEAD --staged --worktree -- <path>`, which also removes a file `develop` does not have, unless it is a release-only fix that `develop` also needs.
5. Commit, and land the merge on `develop` as a merge commit. A squash or rebase merge leaves the GA commit out of `develop`'s history.

```bash
# Steps 1 and 2:
git fetch origin develop "refs/tags/build/vX.Y.Z:refs/tags/build/vX.Y.Z"
git switch --no-track -c history-bridge-X.Y.Z origin/develop
git merge --no-ff --no-commit build/vX.Y.Z
git checkout HEAD -- release-state.txt
git rm -q -f --ignore-unmatch release-linux-images.json

# Step 4, after resolving conflicts:
git diff --cached HEAD --stat
```

After promotion, verify that the public release branch and tag resolve to the candidate SHA. For a newest-line GA, check `master` too. For an older-line GA, confirm that `master` and GitHub Latest did not change. Public release-branch pushes skip CI; private release-branch pushes and pull requests run the default Linux x86_64 check.

If `MACOS_DISTRIBUTION_SIGNING_ENABLED` is false, after a GA publication download both `.pkg` assets, sign and notarize them outside GitHub, verify their signatures and installed behavior, then replace the assets without changing their filenames (`gh release upload vX.Y.Z wsjtx-X.Y.Z-arm64-macOS.pkg wsjtx-X.Y.Z-x86_64-macOS.pkg --clobber --repo WSJTX/wsjtx`). They are intentionally absent from `SHA256SUMS`; the manifest and assemble summary identify them as manually replaceable. Enable the variable after the `apple-release-signing` environment is fully configured.

### Step 5: Verify the Artifacts

Download the release artifacts to a new directory:

```bash
RELEASE_DIR="$(mktemp -d)"
gh release download "v$VERSION" --repo WSJTX/wsjtx --dir "$RELEASE_DIR"
```

Verify both architectures. For each signed package, check its installer signature, stapled notarization ticket, and Gatekeeper policy independently; skip these three checks for unsigned validation packages:

```bash
for ARCH in arm64 x86_64; do
  PKG="$RELEASE_DIR/wsjtx-${VERSION}-${ARCH}-macOS.pkg"
  pkgutil --check-signature "$PKG"
  xcrun stapler validate "$PKG"
  spctl --assess --type install --verbose=2 "$PKG"
done
```

For a signed package, success requires a valid Developer ID Installer chain, a valid staple, and an `accepted` Gatekeeper assessment. These checks do not validate nested application signatures, entitlements, or runtime behavior.

Inspect the packaged application rather than the staged build tree:

```bash
for ARCH in arm64 x86_64; do
  PKG="$RELEASE_DIR/wsjtx-${VERSION}-${ARCH}-macOS.pkg"
  EXPANDED="$RELEASE_DIR/expanded-$ARCH"
  pkgutil --expand-full "$PKG" "$EXPANDED"
  APP="$EXPANDED/wsjtx-component.pkg/Payload/Applications/wsjtx.app"

  codesign --verify --deep --strict --verbose=2 "$APP"
  codesign -d --entitlements :- "$APP/Contents/MacOS/wsjtx"
  codesign -d --entitlements :- "$APP/Contents/MacOS/jt9"
done
```

Signature verification must succeed, and both inspected executables must contain the two keys in `entitlements.plist`. The effective-entitlement check detects an outer bundle re-sign that preserved a valid signature but removed the executable entitlements.

Finally, install each architecture's package (an unsigned package as [Beta signing](#beta-signing) describes) on a matching disposable or release-test macOS system and launch the installed app through Finder. Confirm that macOS grants audio input after the usage prompt, the receive level responds to live input, and `jt9` completes a decode cycle without a hardened-runtime or dynamic-loader failure. A successful signing or notarization check does not establish these runtime properties.

### Step 6: Recover Safely

Rerun jobs against the immutable tag for transient signing, notarization, or service failures. If source or metadata changes, make a new commit and cut the next RC. For a beta, the scheduler retries or advances the number itself, and [Recovery](#recovery) lists what a person does. Do not clean up a failed attempt by moving or recreating either tag; retaining the original identity preserves the audit trail and prevents an already downloaded release name from silently changing meaning.

---

## 10. Scheduled Beta Releases

Betas are unattended weekly prereleases of `develop`, published from the single `release/X.Y` in `BETA` state. The beta scheduler is two workflows. `beta-cut.yml` (Scheduled Beta Cut) applies the [state machine](#state-machine); each of its runs is a cut. `beta-continue.yml` (Scheduled Beta Continue) is the continuation, which chains the runs that follow. A beta's GitHub Release body is the one [What the Release Produces](#what-the-release-produces) describes. The channel model is in [Release channels](#release-channels).

> **Unattended publication.** No person reviews a beta before it is public. Each beta carries the newest green `develop`, including its workflow changes, which the release-bot App merges onto the release line.

After each beta publishes ([Schedule](#schedule)), a release manager emails the mailing list with its GitHub Release link, a short summary of what is ready for testing and feedback, and how to open its unsigned packages ([Beta signing](#beta-signing)).

### Schedule

`beta-cut.yml` runs every Tuesday at 20:00 UTC (cron `0 20 * * 2`), which is 16:00 US Eastern daylight time or 15:00 Eastern standard time. The Tuesday cut leaves Monday for fixes to land on `develop`. CI, the candidate, and the public builds then take about two to three hours, so a beta publishes on Tuesday evening, US Eastern time.

GitHub starts scheduled runs late under load, often by 10 to 20 minutes at the start of an hour, and can drop a run. A dropped run means no cut and no alert that week; dispatch Scheduled Beta Cut to cut the beta.

### Workflows

`beta-cut.yml` runs on its cron and on `workflow_dispatch`, which takes no inputs. Dispatch it on `develop`, the only branch the `beta-automation` environment admits (`gh workflow run beta-cut.yml --repo WSJTX/wsjtx-internal --ref develop`). `beta-continue.yml` runs on `workflow_run` when a CI, Prepare Release Candidate, Prepare Release Dependencies, or Promote Release Source run completes on `develop` or a release branch. Both workflows need the settings in [Required Repository Configuration](#required-repository-configuration).

Each workflow has a `gate` job, which checks the [pause switch](#pause-and-freeze), and one acting job in the private `beta-automation` environment. The acting job mints a release-bot App token from the `RELEASE_BOT_APP_ID` variable and the `RELEASE_BOT_PRIVATE_KEY` secret in `beta-automation`.

The two acting jobs share the concurrency group `beta-scheduler` without cancellation, so only one acts at a time. GitHub keeps at most one waiting job per group, and a newer arrival replaces it. The next cut resumes a continuation lost that way, or reports it when a promotion was never dispatched; a cut lost that way does nothing that week, as with a dropped schedule.

### State machine

Each cut re-derives its state from refs, run history, the public release, image package tags, and workflow artifacts, and applies one step. A cut acts by pushing a merge commit or dispatching a workflow, skips with a note in the job summary, or stops and [alerts](#failure-alerts). Two rules apply throughout:

- **Newest green `develop`.** The newest `develop` commit whose push CI succeeded; the cut does not wait for a `develop` push CI run in progress. Push CI on `develop` and on `release/X.Y` builds Linux x86_64 only; the candidate builds every target.
- **Runs in progress.** A CI, candidate, promotion, or preparation run that the cut depends on may still be in progress. If the App started it, the cut skips, because the continuation resumes when it finishes. If a person started it, the cut stops and asks for Scheduled Beta Cut to be dispatched after that run finishes. A public build in progress is always a skip.

The steps:

1. The cut finds the single `release/X.Y` in `BETA` state. With none, as between a freeze and the next line's start, it skips. It stops and alerts when two or more lines are in `BETA` state, or when a line's `BETA` state does not validate or names another line's version.
2. When `build/vX.Y.Z-betaN` for the line's `BETA N` does not exist, N is unspent, because a number is spent only once its build tag exists. After the check in step 4, the cut reads the tip's CI:
   - It passed: the cut dispatches Prepare Release Candidate at the tip with no merge. This is how `beta1` is cut, and how a candidate that failed before pushing its tag is retried.
   - It did not succeed: the cut merges the newest green `develop` under the same N, through steps 5 and 6. If `develop` has no green commit newer than the tip, the cut stops and alerts.
   - It never ran: the cut stops and alerts.
3. When the build tag exists, the cut checks public `vX.Y.Z-betaN`:
   - Published: the cut merges the newest green `develop` as N+1, through steps 4 to 6. If that commit is already on `release/X.Y`, the cut skips with no alert, even when newer `develop` commits exist whose push CI failed or is still running. If no `develop` commit has a successful push CI run, it stops and alerts.
   - Not published, with a CI, candidate, or promotion run at the tag or the public build in progress: the cut skips or stops, as the rules above describe.
   - The candidate failed after pushing its tag, and no promotion succeeded: N is spent, because build tags never move. The cut merges a newer green `develop` as N+1, or stops and alerts when there is none ([Recovery](#recovery)).
   - No candidate run, a successful candidate with no promotion run, or a failed promotion: the cut stops and alerts, naming the stage.
   - A public build that never started, failed, or succeeded without a published release: the cut stops and alerts at the publication stage.
4. Before it dispatches a candidate at the tip or merges `develop`, the cut checks that fixes landed on `develop` first. It lists the commits on `release/X.Y` that `develop` does not contain. The App's commits, identified by their author, are exempt. So are commits that change only `release-state.txt` or `release-linux-images.json`, such as the line-start commit. Any other commit stops the line and alerts. A cherry-pick or a person's commit on the beta line therefore stops it until that commit is reachable from `develop` ([Recovery](#recovery)). For an urgent fix in the beta phase, land it on `develop` and, once its push CI passes, dispatch Scheduled Beta Cut, which cuts the next beta at once when the current one is published.
5. Before a merge, the cut compares the Linux image recipe fingerprints of the chosen `develop` commit with the line's `release-linux-images.json`; its alerts call this selection the lock:
   - They match: the cut keeps the selection unchanged.
   - They differ, or the selection is missing: the cut picks the validated image generation built for those recipes. It reuses a successful Prepare Release Dependencies run for that generation, or dispatches one on `develop` and skips. When that App-started preparation succeeds, the continuation dispatches the cut again, and that cut commits the prepared selection.
   - No generation matches, including while matching images are still being refreshed: the cut stops and alerts. It also stops when the newest preparation failed and none is usable, or when a preparation is needed but the `develop` tip's recipes differ from the chosen commit's.
6. The cut merges the chosen `develop` commit into the tip with `git merge --no-ff`. It writes `release-state.txt` with the line's own version, `channel=BETA`, the chosen number, and `windows_signing=unsigned`, and writes the kept or prepared selection. Conflicts in those two files are resolved by overwriting them; a conflict in any other file stops and alerts. The merged tree's selection must pass `release-linux-images.py validate --check-remote`, or the cut stops and alerts. The App authors and commits the merge and pushes it to `release/X.Y` as a fast-forward, never with force; a rejected push stops and alerts. That push starts the CI run that the [continuation](#continuation) follows.

### Continuation

The continuation acts only on completed runs the App started: CI, candidate, and promotion runs on the line's `release/X.Y` at its current tip, and Prepare Release Dependencies runs on `develop`. A person's re-run of such a run counts. The continuation skips every other run.

| Completed Run | Action |
|---------------|--------|
| Push CI succeeded | Dispatch Prepare Release Candidate with `operation=create` at the tip, unless the build tag exists or a candidate is running |
| Candidate succeeded, its tag at the tip | Dispatch Promote Release Source with `operation=promote` and that candidate run's ID |
| Promotion succeeded | None; the public repository's release workflow publishes the beta |
| Preparation succeeded | Dispatch Scheduled Beta Cut on `develop` |

The continuation alerts at once when a push CI, candidate, promotion, or preparation run concludes other than success, and when a candidate's tag is not at the tip.

### Beta signing

Beta installers and packages are unsigned. A `BETA` state must carry `windows_signing=unsigned`, because SignPath Foundation requires a manual approval for every signed release; `release-policy.py` rejects any other mode. The SignPath signing job therefore never runs for a beta. A beta's macOS package is the unsigned validation package whatever `MACOS_DISTRIBUTION_SIGNING_ENABLED` says. Each unsigned macOS package is published as-is and is not replaceable, so `SHA256SUMS` covers it, and the release manifest records `macos_signing.mode` as `unsigned`. [Procedures by channel](#procedures-by-channel) gives RC and GA signing. Linux packages are unsigned on every channel.

macOS Gatekeeper rejects an unsigned package that a browser downloaded. To install an unsigned beta or RC package, remove the quarantine attribute first (`xattr -d com.apple.quarantine wsjtx-<ver>-arm64-macOS.pkg`), or open it once and then choose Open Anyway in System Settings > Privacy & Security. Windows SmartScreen can warn on an unsigned installer; choose More info, then Run anyway.

### Failure alerts

Every stop and unexpected failure of a cut or the continuation goes to one tracking issue in `WSJTX/wsjtx-internal`, titled "Scheduled beta alerts". `github-actions[bot]` opens the issue, or comments on the open one, with the stage, the reason, and the run link. Closing the issue makes the next alert open another. Skips appear only in the job summary.

The continuation alerts at once in the cases [Continuation](#continuation) lists and on the line errors in [state machine](#state-machine) step 1. Every other stop in the state machine is reported only when a cut runs: on the Tuesday cron, on a dispatch, or after a preparation. A public build that failed or never started is therefore reported only by the next cut, up to a week later. To check sooner, read the public run with `gh run list --repo WSJTX/wsjtx --workflow public-release.yml`. A dispatch of Scheduled Beta Cut also reports it, but cuts N+1 at once when N is published and `develop` has a newer green commit.

### Pause and freeze

`BETA_AUTOMATION` is a repository variable on `WSJTX/wsjtx-internal`; the gate jobs that read it have no environment. Both workflows act only when it is exactly `enabled`. With any other value, or unset, they do nothing and say so in the job summary, with no alert. Set it with `gh variable set BETA_AUTOMATION --repo WSJTX/wsjtx-internal --body enabled` once the release-bot App's credentials are in `beta-automation`. Each acting job mints the App token first, so a missing credential fails the job and alerts. Pause with any other value, for example `--body paused`. Never set it on `WSJTX/wsjtx`: promoted source carries the beta workflows there.

Pausing also stops the continuation, so a beta in progress halts at its current stage. A beta already promoted still publishes, because the public build has no pause switch. Once `BETA_AUTOMATION` is `enabled` again, the next cut resumes from a push CI that passed or from a prepared selection; dispatch Scheduled Beta Cut to resume at once instead of on Tuesday. A candidate that succeeded with no promotion stops the cut, which asks for a dispatch of Promote Release Source; dispatch it on `release/X.Y` with the version, `operation=promote`, and the candidate run ID that the alert names. That promotion downloads the candidate run's artifact, which is kept 30 days.

To freeze a line, land a reviewed commit on `release/X.Y` that sets `RC 1` and keeps `windows_signing=unsigned`, as in [Step 1](#step-1-prepare-release-metadata). Its review also covers the line's `release-linux-images.json`, which the scheduler maintained without review. The scheduler acts only on `BETA` state, so it stops. After the freeze, bump `develop` to `X.(Y+1).0` in a one-line PR, and publish `samples/contents_X.(Y+1).json` to `feat-web-pages` as the [sample catalog procedure](DEVELOPMENT_WORKFLOW.md#3-promote-the-exact-source) describes. A beta not yet promoted is abandoned: the continuation finds no `BETA` line and skips, and a promotion that validates after the freeze fails because the tip moved; one already promoted still publishes. The line continues through RC and GA as in [Phase 7](#9-phase-7-test-the-release-pipeline).

A patch release on an existing line never uses `BETA` state. It uses `RC N` metadata commits and cherry-picked fixes, because a `BETA` state would make the scheduler merge the newest green `develop` into the line.

### Starting a release line

Cut `release/X.Y` from a green `develop`. Then land the line-start commit, which changes only `release-state.txt` and `release-linux-images.json`. The state is `BETA 1` for the line's version (`channel=BETA`, `prerelease=1`, `windows_signing=unsigned`), and the selection is the one prepared as [Prepared Linux release images](#prepared-linux-release-images) describes. For that preparation, choose a `validated-build-YYYYMMDD-RUN-ATTEMPT` generation that all four `ghcr.io/wsjtx/wsjtx-internal/<package>` images carry, from a Refresh Build Caches or Publish Linux CI Images run on `develop` built for the line's recipes. List a package's tags with a token that has the `read:packages` scope:

```bash
# Tags of one internal image package, here linux-noble:
gh api orgs/WSJTX/packages/container/wsjtx-internal%2Flinux-noble/versions --jq '.[].metadata.container.tags[]'
```

The line's sample catalog, `samples/contents_X.Y.json`, is already on Pages: it was published when `develop` took version `X.Y.0`.

The next cut finds `build/vX.Y.Z-beta1` missing and cuts `beta1` at the tip ([state machine](#state-machine) step 2). If the line-start commit's CI is still running at the cut, the cut stops and asks for a dispatch; dispatch Scheduled Beta Cut once that CI passes. From then on the scheduler cuts every beta. Push to the line only commits that change nothing but `release-state.txt` and `release-linux-images.json`; any other commit stops the line ([state machine](#state-machine) step 4). Such a push while a beta is in flight moves the tip: the continuation then ignores that beta's runs, and the beta waits for the next cut.

### Recovery

Most stop messages name the action to take. Re-running a failed run that the App started resumes the chain through the continuation. For a CI, candidate, or promotion run the `release/X.Y` tip must be unmoved: the continuation ignores runs at an older commit, and candidate and promotion runs also check the tip. For other stops, fix the cause, then dispatch Scheduled Beta Cut on `develop`. Never move or recreate a tag; see [Step 6](#step-6-recover-safely).

When a candidate fails after pushing its tag, land the fix on `develop` before the next cut, and confirm it on the failing target with `full-ci` or a dispatched CI run. A newer green `develop` commit without the fix spends N+1 too.

A fixes stop ([state machine](#state-machine) step 4) clears only when the listed commits are reachable from `develop`. Landing the same fix on `develop` or reverting the commit on the line does not clear it, and the rulesets forbid removing the commit. To clear it, merge the `release/X.Y` tip (`origin/release/X.Y`) into `develop` with the [history-bridge](#public-branch-history-and-ga-line-policy) steps: keep `develop`'s `release-state.txt`, drop `release-linux-images.json`, run the bridge's tree check, and land the reviewed PR as a merge commit. A squash or rebase merge leaves the commit unreachable from `develop`, and the stop stays. When the next cut merges `develop`, it rewrites both files on the line.

For a failed public build, re-run the failed jobs of Build and Publish Public Release in `WSJTX/wsjtx`:

```bash
# Re-run only the failed jobs of the public build:
gh run rerun <RUN_ID> --failed --repo WSJTX/wsjtx
```

A successful re-run publishes the beta, and the next cut moves on to N+1 when `develop` has a newer green commit. Once the release exists, re-run only the failed jobs: the publish job refuses any asset whose bytes differ from the published one. Nothing starts a new public build for an existing tag, because the public build runs only on a tag push and tags never move. A public build that never started, or that fails at its tag for a reason in the tagged source, therefore stops the line at publication every week. The stop lasts until `vX.Y.Z-betaN` has a published GitHub Release or the line's state moves past `BETA N`. GitHub allows a re-run only within 30 days of a run's first attempt; after that a failed public build stops the line like one that never started.

### Actions minutes

The estimate for each beta with Apple silicon as the only macOS build is 125 to 135 private-repository minutes, about $1.40 at the rates in [where CI runs](DEVELOPMENT_WORKFLOW.md#where-ci-runs), or about 500 to 680 included minutes for four or five betas a month. These estimates need remeasurement with both macOS architectures. A cold macOS dependency build can run to the 180-minute macOS budget in [Cache readiness](#cache-readiness). The public builds run in `WSJTX/wsjtx`, where standard GitHub-hosted runners are free.

---

## 11. Ongoing Maintenance

### Secret Rotation

| Secret | Rotation Schedule | How to Rotate |
|--------|-------------------|---------------|
| `CROSS_REPO_TOKEN` | Before expiry (check token settings at github.com) | Generate new PAT → update the secret in private `source-promotion`; a new owner must first be the identity allowed to create public `v*` tags. `operation=validate` does not use the token |
| Release-bot App private key | On team schedule, personnel change, or suspected exposure | Generate a new key in the App's settings → update `RELEASE_BOT_PRIVATE_KEY` in `beta-automation` → delete the old key |
| App Store Connect API key | On team schedule, personnel change, or suspected exposure | Revoke the old key, create a team-owned replacement, and update `apple-release-signing` |
| macOS signing certificates (.p12) | When certificate expires (typically 5 years) | Export new cert from Keychain → base64-encode → update both P12 and PASSWORD secrets |
| SignPath API token | On SignPath schedule, submitter change, or suspected exposure | Replace the public-repo token; the signing key remains in SignPath's HSM |

### Version Bumps

Before creating an RC or GA candidate, commit its release identity in `release-state.txt` as [Step 1](#step-1-prepare-release-metadata) describes; beta states come from the [line-start commit](#starting-a-release-line) and the scheduler's merge commits. Workflows derive their build inputs from that tracked state and reject a mismatched tag; do not maintain duplicate per-platform version literals.

```bash
# Review the tracked release identity before running Prepare Release Candidate:
git diff HEAD^ -- release-state.txt
```

### Hamlib Updates

If the team moves to a new Hamlib version, update `hamlib_branch` in both `ci.yml` and `release.yml`. The Hamlib cache will automatically invalidate because the cache key includes the branch name.

### Cache Management

If builds behave strangely after dependency changes, clear the Actions cache:

```bash
# List caches:
gh api repos/WSJTX/wsjtx-internal/actions/caches --jq '.actions_caches[] | "\(.id) \(.key)"'

# Delete a specific cache:
gh api -X DELETE repos/WSJTX/wsjtx-internal/actions/caches/<CACHE_ID>
```

Or go to: Repo → Actions → Caches (left sidebar)

### Workflow File Updates

When modifying workflow files, keep in mind:
- Changes to `build-*.yml` invalidate the Hamlib cache for that platform (cache key includes the workflow file hash).
- Changes to `ci.yml` or `release.yml` do **not** invalidate caches (they're just orchestrators).
- Reusable workflows (`workflow_call`) cannot be tested from fork PRs — they must be on the same repo.

### Branch, Tag, and Environment Protection

Protect `develop` with a ruleset that forbids force-push and deletion, and apply the team's normal CI and review policy to it. Protect `release/*` with rulesets that forbid force-push and deletion. Do not add a pull-request requirement: the release-bot App has no bypass, and its merge pushes would fail. Run the two helper workflows from the current `release/X.Y` tip. Promote Release Source rejects any workflow ref or SHA other than that tip. Prepare Release Candidate rejects any other SHA, and the `candidate-tagging` environment's `release/*` policy rejects other refs.

On WSJTX/wsjtx, protect `v*` tags from updates and deletion, and limit creation to the source-promotion identity. Protect `master` and `release/*` from force-pushes and deletion only; promotion pushes them directly, so a pull-request or status-check requirement would block it.

Restrict `candidate-tagging` and `source-promotion` to private `release/*` branches, `apple-release-signing` to public `v*` tags, and `windows-release-signing` to public `master` plus `v*` tags. Restrict `beta-automation` to private `develop`. Configure `public-release` and `beta-release` as [Public Final Publication Approval](#public-final-publication-approval) and [Public Beta Publication](#public-beta-publication) describe.

### Dependabot & Auto-merge Policy

**Repo feature — enable everywhere this machinery lives:**

```bash
gh api -X PATCH /repos/<org>/<repo> -f allow_auto_merge=true
```

Enabling the feature only makes auto-merge *available* per-PR; it does not auto-merge anything on its own.

**Usage policy:**

| Posture | Dependabot security updates | Patch bumps (x.y.Z) | Minor/major bumps (x.Y.z / X.y.z) |
|---------|-----------------------------|---------------------|-----------------------------------|
| Sandbox | Auto-merge | Auto-merge | Auto-merge OK — breakage teaches here |
| Production | Auto-merge | Auto-merge | **Human merge click required** |

Rationale: CI cannot catch runtime-behavior regressions in major action-version bumps (output-format, permission-model, default-input changes). A human click is the cheapest way to force a moment of review proportional to the blast radius of a production action upgrade.

**Arming auto-merge on a specific PR:**

```bash
gh pr merge <N> --repo <org>/<repo> --auto --squash --delete-branch
```

Dependabot auto-rebases its PRs when the base branch moves, CI re-runs, and GitHub merges once the branch-protection checks (including "up-to-date with base") pass.

---

## 12. Troubleshooting

### Problem: "refusing to allow an OAuth App to create or update workflow"

**Context:** This appears when the release workflow tries to push to the public repo.

**Root cause:** The credential being used (deploy key, OAuth token, or any non-PAT) cannot modify `.github/workflows/` files. This is a GitHub platform-level restriction.

**Fix:** Ensure `CROSS_REPO_TOKEN` is a **fine-grained PAT** with **Contents: Read and write** AND **Workflows: Read and write** permissions. Classic PATs need the `workflow` scope.

### Problem: macOS build fails with "no identity found"

**Context:** The signing step can't find a Developer ID certificate in the keychain.

**Root cause:** Distribution mode was selected, but the imported file, password, or certificate contents do not yield the expected Developer ID Application identity.

**Diagnosis:**
```bash
# Check that the secret exists:
gh secret list --repo WSJTX/wsjtx --env apple-release-signing | rg DEVELOPER_ID

# Re-encode and re-set:
base64 -i app.p12 -o app.p12.b64
gh secret set DEVELOPER_ID_CERTIFICATE_P12 --repo WSJTX/wsjtx --env apple-release-signing < app.p12.b64
```

### Problem: Notarization fails with "Invalid" status

**Context:** The notarytool submission comes back as "Invalid" instead of "Accepted."

**Diagnosis:** The workflow automatically fetches the notarization log on failure. Look for the log output in the GitHub Actions run. Common issues:

| Log message | Meaning | Fix |
|-------------|---------|-----|
| "The signature of the binary is invalid" | Code signing used wrong identity or missed a binary | Check that all executables and dylibs are signed |
| "The binary uses an SDK older than the 10.9 SDK" | Deployment target too old | Check `CMAKE_OSX_DEPLOYMENT_TARGET` (11.0 for arm64, 10.13 for x86_64 Intel, through the `deployment_target` input of `build-macos.yml`) |
| "The signature does not include a secure timestamp" | Missing `--timestamp` in codesign | Verify the codesign commands include `--timestamp` |

### Problem: Windows build fails at OmniRig install

**Context:** The PowerShell step that downloads OmniRig fails.

**Root cause:** The download URL (`https://www.dxatlas.com/OmniRig/Files/OmniRig.zip`) may be temporarily unavailable.

**Fix:** Re-run the job. If persistent, download OmniRig manually, add the `.exe` to the repo as a build dependency, and update the workflow to use the local copy.

### Problem: Public source promotion reports a missing token

**Context:** The `promote` job of Promote Release Source stops before it pushes anything to the public repository.

**Root cause:** No `CROSS_REPO_TOKEN` value reaches the job: the secret is absent from the private `source-promotion` environment and from the repository and organization secrets, or it is empty. A repository or organization copy would satisfy the job, which is why none should exist ([Verification: Credential Boundaries](#verification-credential-boundaries)).

**Fix:**
```bash
# Verify the secret exists:
gh secret list --repo WSJTX/wsjtx-internal --env source-promotion | rg CROSS_REPO

# Re-set it:
gh secret set CROSS_REPO_TOKEN --repo WSJTX/wsjtx-internal --env source-promotion
# Paste the token value
```

### Problem: Cache not being used

**Context:** Hamlib builds from source every run even though nothing changed.

**Diagnosis:** Check the cache step output in the workflow run. Look for "Cache not found" vs "Cache restored."

**Common causes:**
- Cache key changed (workflow file was modified)
- Cache was evicted (GitHub evicts caches after 7 days of no access, or when the repo exceeds 10 GB of cache storage)
- The `actions/cache` action version changed behavior

### Problem: Build succeeds but binary crashes

**Context:** The built binary segfaults or behaves differently than a local build.

**Diagnosis:**
- Check compiler versions: `gcc --version` / `gfortran --version` in the workflow output
- Check linked libraries: the macOS build has a verification step that checks for remaining Homebrew paths
- Compare CMake configure output between CI and a local build

---

## 13. Reference: Complete File Inventory

### Files to Include in the PR

| File | Purpose | Changes Needed |
|------|---------|----------------|
| `.github/workflows/ci.yml` | CI orchestrator for `develop` and `release/**` | None |
| `.github/workflows/release.yml` | Reusable private candidate build | None |
| `.github/workflows/prepare-release-dependencies.yml` | Prepare Release Dependencies: verify and copy the four Linux images and produce `release-linux-images.json` | None |
| `.github/workflows/release-tag-helper.yml` | Validate/create immutable internal candidates | None |
| `.github/workflows/promote-release.yml` | Validate/promote exact source to the public repo | None |
| `.github/workflows/public-release.yml` | Public policy-checked build, bundle verification, and GitHub Release | None |
| `.github/workflows/beta-cut.yml` | Scheduled Beta Cut: weekly or dispatched cut of the next beta on the line in `BETA` state | None |
| `.github/workflows/beta-continue.yml` | Scheduled Beta Continue: carry each App-started beta run to its next stage | None |
| `.github/workflows/sign-windows-release.yml` | Public SignPath build/sign/verification | SignPath project and policy identifiers |
| `.github/workflows/build-macos.yml` | macOS build (parameterized arm64/x86_64) | None |
| `.github/workflows/build-linux.yml` | Linux build (parameterized x86_64/aarch64/armhf) | None |
| `.github/workflows/build-windows.yml` | Windows x86_64 build | None |
| `.github/workflows/hamlib-upstream-check.yml` | Scheduled (weekly cron + `workflow_dispatch`) poll of Hamlib upstream tags; files a tracking issue when a newer 4.x release is available. No platform builds; self-contained. | None |
| `entitlements.plist` | macOS app entitlements | None (if not already in repo) |
| `release-state.txt` | Tracked version, channel, prerelease number, optional Windows signing mode, and archival revision | Set before each RC or GA candidate; the line-start commit and the scheduler set beta states |
| `.github/scripts/release-policy.py` | Release identity, asset, archive, signing-report, manifest, and public ref promotion policy | None |
| `.github/scripts/beta-scheduler.py` | Beta scheduler state machine, dispatches, and failure alerts | None |
| `.github/scripts/release-linux-images.py` | Linux image selection preparation, validation, and provenance | None |
| `.github/scripts/generate-quick-downloads.py` | Quick Downloads list in the GitHub Release body | None |
| `.github/scripts/generate-change-list.py` | Change list in the GitHub Release body | None |

### Secrets Required on `wsjtx-internal`

Use the canonical inventory and setup procedure in [Phase 3](#5-phase-3-create-repository-secrets). `CROSS_REPO_TOKEN` is a secret of the private `source-promotion` environment. The private `beta-automation` environment holds the release-bot App's variable `RELEASE_BOT_APP_ID` and secret `RELEASE_BOT_PRIVATE_KEY`, and repository variable `BETA_AUTOMATION` switches betas on. Apple material belongs in public environment `apple-release-signing`, and `SIGNPATH_API_TOKEN` belongs in public environment `windows-release-signing`. No SignPath private key is stored in either repository.

### External Dependencies (Downloaded at Build Time)

| Dependency | URL | Used By |
|------------|-----|---------|
| Hamlib 4.7.2 | `https://github.com/Hamlib/Hamlib.git` | All supported platforms |
| OmniRig | `https://www.dxatlas.com/OmniRig/Files/OmniRig.zip` | Windows only |

### Build-Time Source Patches

The current workflows do not patch the WSJT-X source tree during a build.
Windows support for MAP65, the MSYS2 FFTW threads library, and the
`dumpcpp-qt5` executable is implemented in the repository and exercised by CI.
