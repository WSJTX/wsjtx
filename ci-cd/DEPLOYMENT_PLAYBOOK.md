# CI/CD Deployment Playbook

Reference for configuring and operating the WSJT-X GitHub Actions CI/CD pipeline in the official `WSJTX` organization repositories.

**Audience:** Repository administrators and release managers responsible for CI/CD configuration and release credentials.

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Architecture Overview](#2-architecture-overview)
3. [Phase 1: Enable GitHub Actions on the Org](#3-phase-1-enable-github-actions-on-the-org)
4. [Phase 2: Adapt Workflow Files](#4-phase-2-adapt-workflow-files)
5. [Phase 3: Create Repository Secrets](#5-phase-3-create-repository-secrets)
6. [Phase 4: Supporting Files](#6-phase-4-supporting-files)
7. [Phase 5: Submit the PR](#7-phase-5-submit-the-pr)
8. [Phase 6: Test the CI Pipeline](#8-phase-6-test-the-ci-pipeline)
9. [Phase 7: Test the Release Pipeline](#9-phase-7-test-the-release-pipeline)
10. [Ongoing Maintenance](#10-ongoing-maintenance)
11. [Troubleshooting](#11-troubleshooting)
12. [Reference: Complete File Inventory](#12-reference-complete-file-inventory)

---

## 1. Prerequisites

Before starting, confirm every item on this list. Missing any one of them will block deployment.

### Access & Permissions

| Requirement | How to Verify | Who Can Grant |
|-------------|---------------|---------------|
| **Org admin** or **repo admin** on `WSJTX/wsjtx-internal` | Go to repo → Settings. If you see "Actions" in the left sidebar, you have admin access. | Org owner (Joe K1JT) |
| **Write access** to `WSJTX/wsjtx` (public repo) | Try `gh api repos/WSJTX/wsjtx --jq .permissions.push` — should return `true` | Org owner |
| **GitHub Actions enabled** at the org level | Org Settings → Actions → General. Must not show "Actions permissions: Disabled" | Org owner |

### Credentials You'll Need

| Credential | Where to Get It | Format |
|------------|-----------------|--------|
| Apple Developer ID Application certificate (.p12) | Export from the Apple Developer account holder's keychain | PKCS12 file + password |
| Apple Developer ID Installer certificate (.p12) | Export from the Apple Developer account holder's keychain | PKCS12 file + password |
| Apple ID email (for notarization) | The email address of the Apple Developer account | Plain text |
| App-specific password | appleid.apple.com → Sign-In and Security → App-Specific Passwords | 16-char token like `xxxx-xxxx-xxxx-xxxx` |
| Apple Team ID | Apple Developer portal → Membership Details | 10-char alphanumeric like `ABCDE12345` |
| GitHub fine-grained PAT | github.com → Settings → Developer settings → Fine-grained tokens | `github_pat_...` token string |

### Tools

- `gh` ([GitHub CLI](https://cli.github.com/)) authenticated with an account that has admin access to the target repos
- `base64` command (macOS and Linux both have this)
- Git with push access to `WSJTX/wsjtx-internal`

---

## 2. Architecture Overview

Understanding the architecture will help you debug issues during deployment.

> **Trigger-tag convention.** The sandbox release pipeline triggers on the `build/v*` tag prefix (`release.yml:5`). All tag examples in this document use that convention. If the team adopts a bare `v*` trigger during replication (decision #1 in the adoption email), `release.yml:5` and the examples in this playbook will need to be updated in lockstep.

### Workflow Structure

```
.github/workflows/
├── ci.yml                       ← Orchestrator. Triggers on push/PR to develop.
│                                    Calls the three reusable build workflows below
│                                    (via `workflow_call`), producing five platform
│                                    jobs in total (matrix parameters).
│
├── build-macos.yml              ← Reusable workflow (workflow_call).
│                                    macOS build (arm64 or x86_64); Developer ID
│                                    signing and notarization require credentials.
│
├── build-linux.yml              ← Reusable workflow (workflow_call).
│                                    Linux build (x86_64 or aarch64, parameterized),
│                                    unsigned.
│
├── build-windows.yml            ← Reusable workflow (workflow_call).
│                                    Windows x86_64 via MSYS2/MinGW64. Installer
│                                    signing per sign_mode input: ephemeral
│                                    self-signed osslsigncode (CI/DEVEL/RC) or
│                                    none (GA — SignPath signs downstream, §5.4).
│
├── sign-windows-release.yml     ← Public repo (WSJTX/wsjtx) only; triggered by
│                                    the v* tag release.yml's mirror step pushes.
│                                    Rebuilds the installer from public source,
│                                    SignPath authenticode-signs it, verifies the
│                                    chain with signtool /pa (hard-fail on GA).
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
└── release.yml                  ← Triggers on `build/v*` tags.
                                     Calls all five platform builds (5-platform matrix
                                     post-S74 +Linux aarch64), creates a GitHub Release
                                     with artifacts, syncs source to the public repo.
```

### How It Flows

**On every push to `develop`:**
```
Push to develop
  └─→ ci.yml triggers
       ├─→ build-macos.yml [arm64]    (parallel)
       ├─→ build-macos.yml [x86_64]   (parallel)
       ├─→ build-linux.yml  [x86_64]  (parallel)
       ├─→ build-linux.yml  [aarch64] (parallel)
       └─→ build-windows.yml          (parallel)
            └─→ All five upload artifacts
```

**On a `build/v*` tag:**
```
Push tag build/v3.0.1
  └─→ release.yml triggers
       ├─→ prepare  (derives version from tag; tag ↔ CMakeLists.txt parity check)
       ├─→ build-macos.yml [arm64]    (parallel)
       ├─→ build-macos.yml [x86_64]   (parallel)
       ├─→ build-linux.yml  [x86_64]  (parallel)
       ├─→ build-linux.yml  [aarch64] (parallel)
       └─→ build-windows.yml          (parallel)
       └─→ release job (after all builds)
            ├─→ Download all artifacts
            ├─→ all-platforms-ready gate: enforce 5 installer-grade artifacts
            ├─→ Create GitHub Release with artifacts attached
            └─→ Push source + tag to public repo (WSJTX/wsjtx)
```

### Build Strategy

Each platform build does the same two-stage process:
1. **Build Hamlib 4.7.2** from source (cached after first run)
2. **Build WSJT-X** against the Hamlib install prefix

This matches what developers do locally but doesn't use the superbuild. The superbuild's ExternalProject approach doesn't map well to CI caching. Building Hamlib directly and caching its install prefix gives better cache hits and faster builds.

### What Gets Cached

| Cache | Key | Saves |
|-------|-----|-------|
| Hamlib install (per platform × arch) | `hamlib-{os}-{arch}-{branch}-{workflow-hash}` on macOS and Linux (separate caches for macOS arm64 vs. x86_64 and Linux x86_64 vs. aarch64); `hamlib-windows-{branch}-{workflow-hash}` on Windows (single arch) | 5-10 min per platform |
| MSYS2 packages | Built-in `cache: true` parameter | 3-5 min on Windows |

Caches invalidate when the Hamlib branch changes or the workflow file changes. This is intentional — if you change build flags, the cache rebuilds.

### What the Release Produces

Each successful `build/v*` tag yields one installer per platform plus a source tarball, all attached to the GitHub Release and (post-release-job) mirrored to the public repo:

| Artifact | Produced by | Format |
|----------|-------------|--------|
| `wsjtx-<ver>-arm64-macOS.pkg` | `build-macos.yml` (arm64 leg) | `.pkg`; Developer ID signed, notarized, and stapled when credentials are available |
| `wsjtx-<ver>-x86_64-macOS.pkg` | `build-macos.yml` (x86_64 leg) | `.pkg`; Developer ID signed, notarized, and stapled when credentials are available |
| `wsjtx-<ver>-linux-x86_64.AppImage` | `build-linux.yml` (x86_64 leg) | Portable AppImage |
| `wsjtx-<ver>-linux-aarch64.AppImage` | `build-linux.yml` (aarch64 leg) | Portable AppImage |
| `wsjtx-<ver>-win64.exe` | `build-windows.yml` | NSIS installer (GA: SignPath Foundation Authenticode via `sign-windows-release.yml` on the public repo; RC/DEVEL: per-run ephemeral self-signed osslsigncode — see §5.4) |
| `wsjtx-<ver>-src.tar.gz` | `release.yml:113-126` (`git archive`) | Source tarball |

The source tarball is assembled from the pushed `build/v*` tag with `git archive --format=tar.gz --prefix="wsjtx-<ver>/"` and is published with every release — no per-release step or decision. This repo has no git submodules, so `git archive`'s default single-tree output captures the full source; if submodules are ever added, the step must be revisited (`git archive` does not recurse into submodules on its own).

### All-Platforms-Ready Gate

Before publishing, the release workflow requires each platform build to produce its expected installer artifact. This prevents a structurally successful build job from creating a partial release.

The gate checks for one installer per platform:

| Platform | Expected artifact pattern |
|----------|---------------------------|
| macOS arm64 | `artifacts/wsjtx-<ver>-arm64-macOS.pkg/*.pkg` |
| macOS x86_64 | `artifacts/wsjtx-<ver>-x86_64-macOS.pkg/*.pkg` |
| Linux x86_64 | `artifacts/wsjtx-<ver>-linux-x86_64-AppImage/*.AppImage` |
| Linux aarch64 | `artifacts/wsjtx-<ver>-linux-aarch64-AppImage/*.AppImage` |
| Windows x86_64 | `artifacts/wsjtx-<ver>-windows-x86_64-installer/*.exe` |

If any pattern matches zero files, the release job stops before publishing.

The gate verifies artifact presence only. It does not establish that a macOS package is Developer ID signed, notarized, stapled, accepted by Gatekeeper, or correct at runtime. Release verification must check those properties separately.

Release policy currently requires all five platform installers. If that policy changes, update the all-platforms-ready gate in `release.yml`.

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

If you choose the restrictive option, you must explicitly allow these third-party actions at the exact major versions the sandbox workflows pin:
- `actions/checkout@v6`
- `actions/cache@v5`
- `actions/upload-artifact@v7`
- `actions/download-artifact@v8`
- `msys2/setup-msys2@v2`

**Important:** pinning the allowlist to an older major (e.g., `@v4`) will block the sandbox workflows from running — GitHub's allowlist matches the exact major version string used in the workflow `uses:` line. Re-run the following after any Dependabot bump to stay aligned:

```bash
grep -rE 'uses: (actions/|msys2/)' .github/workflows/ | sort -u
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

## 4. Phase 2: Adapt Workflow Files

Copy all six workflow files from the prototype (`ci.yml`, `release.yml`, `build-macos.yml`, `build-linux.yml`, `build-windows.yml`, `hamlib-upstream-check.yml`) and make the changes below. Every change is listed with the exact file, line, and what to change. The three `build-*.yml` workflows and `hamlib-upstream-check.yml` have no org-specific references — they're copied as-is.

### 4a. Changes to `ci.yml`

**Branch name** (lines 4-7): If the official repo uses `master` instead of `develop`:

```yaml
# BEFORE (prototype):
on:
  push:
    branches: [develop]
  pull_request:
    branches: [develop]

# AFTER (if official repo uses master):
on:
  push:
    branches: [master]
  pull_request:
    branches: [master]
```

If the official repo already uses `develop`, no change needed.

**Version string:** no change needed. The release version is derived from the pushed `build/v*` tag (release.yml:20-21) and from `CMakeLists.txt` VERSION (release.yml:32-45 parity check, Issue #35). There is no hardcoded version string in `ci.yml` or `release.yml` to update per-release. `ci.yml:23` reads the version from `CMakeLists.txt` as a single source of truth.

**Hamlib branch**: The Hamlib version is pinned per-job. If the team moves to a different Hamlib version, update `hamlib_branch: "4.7.2"` at:

- `ci.yml` lines 36, 47, 58, 67, 76 (5 call sites — macOS arm64, macOS x86_64, Linux x86_64, Linux aarch64, Windows x86_64)
- `release.yml` lines 53, 64, 75, 84, 93 (5 call sites — same five)

```bash
# List all Hamlib pin sites:
grep -n 'hamlib_branch:' .github/workflows/ci.yml .github/workflows/release.yml
```

The scheduled `hamlib-upstream-check.yml` reads the pinned version from `ci.yml` (its single source of truth) and files a tracking issue when a newer 4.x release is available; no per-release edit is needed there.

### 4b. Changes to `release.yml`

**Public repo URL** (the `git remote add public` line — around `release.yml:249` as of this writing): This is the most critical change. The sandbox uses `KJ5HST-LABS/wsjtx.git`; production must point at `WSJTX/wsjtx`:

```yaml
# Replace the sandbox URL with the official public repo:
git remote add public "https://x-access-token:${TOKEN}@github.com/WSJTX/wsjtx.git" || true
```

**Public repo branch** (the `git push public HEAD:...` line — around `release.yml:266` as of this writing): If the public repo's default branch is `master`:

```yaml
# BEFORE (prototype pushes to main):
git push public HEAD:main --force

# AFTER (if public repo uses master):
git push public HEAD:master --force
```

The subsequent `git push public "$GITHUB_REF_NAME"` line (around `release.yml:267`) pushes the tag itself and does not need a branch-name change.

```bash
# Find the current line numbers in your checkout (they drift with workflow edits):
grep -n 'remote add public\|git push public' .github/workflows/release.yml
```

**Hamlib branch:** same 5 call sites as described in §4a above (`release.yml:53, 64, 75, 84, 93`). No hardcoded version string to update — version is derived from the pushed tag.

### 4c. Changes to `build-macos.yml`

The workflow receives the version, Hamlib branch, architecture, runner, and deployment target as inputs. It discovers signing identities from temporary keychains populated by the secrets in Phase 3.

Keep these repository inputs available:

- `entitlements.plist`, used by the **Code sign binaries** step;
- `Darwin/com.wsjtx.sysctl.plist`, copied by the **Prepare installer package** step.

### 4d. Changes to `build-linux.yml`

**No changes required.** The Linux workflow has no org-specific references.

### 4e. Changes to `build-windows.yml`

**No changes required.** The Windows workflow has no org-specific references.

### Summary of All Changes

| File | Change | Why |
|------|--------|-----|
| `ci.yml` branch-name block (`lines 5, 7` as of this writing) | `develop` → `master` if the official repo uses `master` | Match official branch name |
| `release.yml` public-remote block (`grep -n 'remote add public\|git push public' .github/workflows/release.yml` — currently `release.yml:249, 266, 267`) | (a) change remote URL to `https://...@github.com/WSJTX/wsjtx.git`; (b) change `git push public HEAD:main` → `HEAD:master` if the public repo uses `master`; the tag-push line (`release.yml:267`) does not need a branch-name change | Point sync at the official public repo on the right default branch |
| `ci.yml` + `release.yml` `hamlib_branch:` (10 call sites — `grep -n 'hamlib_branch:' .github/workflows/ci.yml .github/workflows/release.yml`) | Update only if the team is pinning a different Hamlib version — otherwise no edit per release | Hamlib version pin (single source: the `hamlib_branch:` input on each reusable-workflow call) |

No version-string row. The release version is derived from the pushed `build/v*` tag (`release.yml:20-21`) and cross-checked against `CMakeLists.txt` VERSION (`release.yml:32-45`, Issue #35); `ci.yml:23` reads the same source. There is no hardcoded version string to bump per release.

That's it — a small number of adaptations, all confined to `ci.yml` and `release.yml` (public repo URL, branch name, and Hamlib branch if the team is using a different Hamlib pin). The three `build-*.yml` workflows and `hamlib-upstream-check.yml` have no org-specific references. (The line-number references in the summary table above are as of this writing and will drift with workflow edits — `grep -n` against your checkout before editing.)

---

## 5. Phase 3: Create Repository Secrets

Store secrets at the **repository** level on `wsjtx-internal`. GitHub masks registered secret values in logs, but workflows must still avoid printing credentials or derived sensitive values.

### Navigate to Secrets Settings

```
https://github.com/WSJTX/wsjtx-internal/settings/secrets/actions
```

Or: Repo → Settings → Secrets and variables → Actions → "New repository secret"

### 5.1 Secret 1: `CROSS_REPO_TOKEN`

**Purpose:** Allows the release workflow to push source code and tags to the public repo (`WSJTX/wsjtx`).

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
gh secret set CROSS_REPO_TOKEN --repo WSJTX/wsjtx-internal
```

**Why not a deploy key?** Deploy keys cannot push `.github/workflows/` files. This is a GitHub platform restriction. The error message ("refusing to allow an OAuth App to create or update workflow") is misleading — it applies to any non-PAT credential, including deploy keys over SSH.

### 5.2 Secrets 2-5: macOS Code Signing Certificates

These four secrets provide the distinct Developer ID identities used for application code and installer packages.

#### Credential responsibilities

The WSJT-X Apple Developer membership is currently held by **John G4KLA**, who is therefore the current Apple Developer Account Holder. The responsibilities below are described by role so that the procedure remains valid if the Account Holder changes.

The Apple Developer account holder exports the signing identities. A repository administrator stores the exported credentials as GitHub Actions secrets. The team must assign responsibility for credential rotation and release-artifact verification.

The workflow uses two identities with different responsibilities:

- **Developer ID Application** signs application executables, frameworks, plug-ins, and command-line tools.
- **Developer ID Installer** signs the outer `.pkg` installer.

Notarization uses the Apple account credentials in §5.3 rather than either certificate password. Rotate a certificate's `.p12` and password together. Rotate notarization secrets when the Apple account, app-specific password, or team changes.

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
gh secret set DEVELOPER_ID_CERTIFICATE_P12 --repo WSJTX/wsjtx-internal < app.p12.b64

# Password for the application certificate:
gh secret set DEVELOPER_ID_CERTIFICATE_PASSWORD --repo WSJTX/wsjtx-internal
# (paste the password you chose when exporting, press Enter)

# Installer signing certificate (base64-encoded .p12):
gh secret set DEVELOPER_ID_INSTALLER_P12 --repo WSJTX/wsjtx-internal < installer.p12.b64

# Password for the installer certificate:
gh secret set DEVELOPER_ID_INSTALLER_PASSWORD --repo WSJTX/wsjtx-internal
# (paste the password you chose when exporting, press Enter)
```

**After setting secrets, delete the local .p12 and .b64 files.** They contain your signing keys.

```bash
rm app.p12 installer.p12 app.p12.b64 installer.p12.b64
```

### 5.3 Secrets 6-8: Apple Notarization

Notarization submits the signed package to Apple's service for automated security checks. An accepted submission is then stapled to the package so the ticket can be validated without contacting Apple. Notarization is distinct from Developer ID signing and does not by itself verify Gatekeeper acceptance or application behavior.

#### `APPLE_ID`

The email address used by the Apple Developer account responsible for notarization.

```bash
gh secret set APPLE_ID --repo WSJTX/wsjtx-internal
# Paste: developer@example.com
```

#### `APPLE_APP_SPECIFIC_PASSWORD`

Apple requires an app-specific password for automated notarization (not your regular Apple ID password).

**To generate one:**

1. Go to https://appleid.apple.com
2. Sign in with the Apple Developer account
3. Go to **Sign-In and Security** → **App-Specific Passwords**
4. Click **"Generate an app-specific password"**
5. Label it `wsjtx-ci-notarize` (or similar)
6. Copy the generated password (format: `xxxx-xxxx-xxxx-xxxx`)

```bash
gh secret set APPLE_APP_SPECIFIC_PASSWORD --repo WSJTX/wsjtx-internal
# Paste: xxxx-xxxx-xxxx-xxxx
```

#### `APPLE_TEAM_ID`

Your Apple Developer team identifier:

1. Go to https://developer.apple.com/account
2. Scroll down to **Membership Details**
3. Copy the **Team ID** (10-character alphanumeric string)

```bash
gh secret set APPLE_TEAM_ID --repo WSJTX/wsjtx-internal
# Paste: ABCDE12345
```

### Verification: Confirm All Secrets Are Set

```bash
gh secret list --repo WSJTX/wsjtx-internal
```

The inventory must contain the cross-repository token and the complete seven-secret Apple credential set:

```
APPLE_APP_SPECIFIC_PASSWORD
APPLE_ID
APPLE_TEAM_ID
CROSS_REPO_TOKEN
DEVELOPER_ID_CERTIFICATE_P12
DEVELOPER_ID_CERTIFICATE_PASSWORD
DEVELOPER_ID_INSTALLER_P12
DEVELOPER_ID_INSTALLER_PASSWORD
```

The workflow selects Developer ID signing from the presence of `DEVELOPER_ID_CERTIFICATE_P12`. If it is absent, the workflow ad-hoc signs application code, creates an unsigned package, skips notarization, and still uploads build artifacts. If it is present, all remaining Apple secrets must also be configured correctly or a later signing or notarization step will fail.

> **About Windows signing.** GA installers are Authenticode-signed by SignPath Foundation on the **public** repo — see §5.4. No Windows signing secrets exist on `wsjtx-internal`; the only signing-related secret is `SIGNPATH_API_TOKEN` on `WSJTX/wsjtx`, and the certificate's private key never leaves SignPath's HSM. CI/DEVEL/RC builds use a per-run ephemeral self-signed osslsigncode certificate (no stored secret).

### 5.4 Windows Authenticode Signing via SignPath Foundation

> **How it works.** SignPath Foundation signs OSS artifacts **built from the public repository only** — the signature attests provenance, not just identity. `release.yml` therefore mirrors source + tag to `WSJTX/wsjtx` *before* publishing anything; the tag push triggers `sign-windows-release.yml` on the public repo, which rebuilds the installer (`build-windows.yml` with `sign_mode=none`), submits it via `signpath/github-action-submit-signing-request@v2` (org `4c211821-e011-48a2-8a84-2cc29a76a8bf`, project `wsjtx`, policy `release-signing` for GA-shaped tags), verifies the chain with `signtool verify /pa`, and uploads a `…-installer-signed` artifact. The internal release job waits for that run, swaps the signed exe in, and only then publishes both releases. A failed or rejected signing run fails the release — no unsigned GA ships. The certificate's private key lives in SignPath's HSM; there is no `.pfx` to export, store, or protect.

#### The one secret

Set on the **public** repo (not `wsjtx-internal`):

```bash
gh secret set SIGNPATH_API_TOKEN --repo WSJTX/wsjtx
# (paste the SignPath CI user's API token, press Enter)
```

The SignPath CI user must be a **submitter** on the signing policies (`release-signing`, `test-signing`). Additionally, `CROSS_REPO_TOKEN` needs **Actions:read** on `WSJTX/wsjtx` (on top of its baseline Contents:write) so the internal release job can poll the sign run and download the signed artifact.

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

#### RC/DEVEL builds

CI, DEVEL, and RC builds use a per-run ephemeral self-signed certificate (the `osslsigncode` step in `build-windows.yml`, skipped when the caller passes `sign_mode=none`). GA release builds pass `sign_mode=none` on both repos — the internal installer is an unsigned placeholder until the signing gate swaps in the SignPath-signed exe. RC source stays internal by policy, and Foundation cannot sign non-public builds, so RC installers use the ephemeral cert and its `|| true`-guarded verify.

### 5.5 Linux Signing (Optional)

Linux binary signing is less critical — Linux users don't encounter SmartScreen-style warnings when downloading binaries. However, GPG-signing release tarballs is good practice if the team distributes `.tar.gz` or `.deb` packages. This would require one additional secret (`GPG_SIGNING_KEY`) and a small step in the release workflow.

### Verification: All Secrets

`wsjtx-internal` stays at the 8 baseline secrets; Windows signing adds exactly one secret, on the public repo:

```bash
gh secret list --repo WSJTX/wsjtx-internal
```

```
APPLE_APP_SPECIFIC_PASSWORD       Updated 2026-...
APPLE_ID                          Updated 2026-...
APPLE_TEAM_ID                     Updated 2026-...
CROSS_REPO_TOKEN                  Updated 2026-...
DEVELOPER_ID_CERTIFICATE_P12      Updated 2026-...
DEVELOPER_ID_CERTIFICATE_PASSWORD Updated 2026-...
DEVELOPER_ID_INSTALLER_P12        Updated 2026-...
DEVELOPER_ID_INSTALLER_PASSWORD   Updated 2026-...
```

```bash
gh secret list --repo WSJTX/wsjtx
```

```
SIGNPATH_API_TOKEN                Updated 2026-...
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
| `com.apple.security.cs.allow-unsigned-executable-memory` | Allows the executable trampolines used by `jt9`'s gfortran internal-procedure callbacks. The trampoline implementation varies by compiler and architecture. |

All three entitlements are currently applied as one set, including to executables that may not exercise every capability. Narrowing that scope is a security-sensitive behavior change and is outside this playbook.

Bundle signing re-signs the main executable, so it must receive the entitlement file again after nested code is signed. `codesign --verify` validates signature integrity but does not prove that required entitlements are present. Inspect the effective entitlements and exercise audio capture and a decoder cycle when validating a release.

### `Darwin/com.wsjtx.sysctl.plist`

The macOS installer package copies this into `/Library/LaunchDaemons/` to configure shared memory limits (required for WSJT-X interprocess communication). Check if it exists:

```bash
ls -la Darwin/com.wsjtx.sysctl.plist
```

### `CMakeLists.txt` OmniRig Change (Optional)

The Windows CI passes `-DOMNIRIG_TYPE_LIB=<path>` to CMake. This requires a small change to `CMakeLists.txt` (around line 940) that adds an `if (OMNIRIG_TYPE_LIB)` branch. The change is backward-compatible — existing local builds without `-DOMNIRIG_TYPE_LIB` work exactly as before.

If the official repo doesn't have this change, include it in the PR. The relevant section:

```cmake
if (WIN32)
  find_program (DUMPCPP dumpcpp)
  if (DUMPCPP-NOTFOUND)
    message (FATAL_ERROR "dumpcpp tool not found")
  endif (DUMPCPP-NOTFOUND)

  if (OMNIRIG_TYPE_LIB)
    # CI/headless: type library path provided directly
    file (TO_CMAKE_PATH "${OMNIRIG_TYPE_LIB}" AXSERVERSRCS)
    message (STATUS "Using OmniRig type library: ${AXSERVERSRCS}")
  else ()
    # Normal build: query COM registry for type library location
    execute_process (
      COMMAND ${DUMPCPP} -getfile {4FE359C5-A58F-459D-BE95-CA559FB4F270}
      OUTPUT_VARIABLE AXSERVER
      OUTPUT_STRIP_TRAILING_WHITESPACE
      )
    # ... existing registry-query code ...
  endif ()
```

---

## 7. Phase 5: Submit the PR

### Option A: PR from a Branch

If you have write access to `WSJTX/wsjtx-internal`:

```bash
# Clone the official repo (if you haven't already):
git clone git@github.com:WSJTX/wsjtx-internal.git
cd wsjtx-internal

# Create a feature branch:
git checkout -b ci/github-actions

# Copy workflow files from the prototype:
# (adjust paths to wherever you have the prototype checked out)
cp /path/to/prototype/.github/workflows/*.yml .github/workflows/

# Make the changes from Phase 2 (branch names, repo URL, versions)
# ... edit ci.yml and release.yml ...

# Copy supporting files if needed:
cp /path/to/prototype/entitlements.plist .
cp -r /path/to/prototype/Darwin .

# Commit:
git add .github/workflows/ entitlements.plist Darwin/
git commit -m "feat: add GitHub Actions CI/CD for five-platform builds"

# Push and create PR:
git push -u origin ci/github-actions
gh pr create \
  --title "Add GitHub Actions CI/CD" \
  --body "Five-platform CI (macOS arm64, macOS x86_64, Linux x86_64, Linux aarch64, Windows x86_64) with tag-triggered releases (\`build/v*\`)."
```

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
  --body "Five-platform CI (macOS arm64, macOS x86_64, Linux x86_64, Linux aarch64, Windows x86_64) with tag-triggered releases (\`build/v*\`)."
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

### Step 3: Check Each Platform

All five builds should complete. Expected times (first run, no cache):

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
| macOS notarization fails | Missing or wrong `APPLE_ID` / password / team ID | Verify all three notarization secrets |
| Windows build timeout (>60 min) | MSYS2 cache miss + slow package install | Re-run — the cache will be populated for next time |
| "refusing to allow an OAuth App to create or update workflow" | This error can appear at PR merge time if the branch contains workflow files and was pushed with a deploy key | Push the branch using a PAT or via the GitHub web UI instead |

### Step 5: Revert the Test Commit

```bash
git revert HEAD
git push
```

---

## 9. Phase 7: Test the Release Pipeline

Only do this after CI is green on all five platforms.

### Step 1: Choose a Test Version

Pick a version number that's clearly a test. Convention: append a patch number to the current version. The tag **must** start with `build/v` — the sandbox release pipeline triggers on `build/v*` only (`release.yml:5`). A bare `v3.0.0.1-test` tag will not trigger the release workflow.

```bash
# If current version is 3.0.0:
TEST_TAG="build/v3.0.0.1-test"
```

### Step 2: Create and Push the Tag

```bash
git tag "$TEST_TAG"
git push origin "$TEST_TAG"
```

### Step 3: Monitor the Release Run

```bash
gh run watch --repo WSJTX/wsjtx-internal
```

The release workflow will:
1. Build all five platforms (same as CI)
2. Create a GitHub Release with downloadable artifacts
3. Push source and the tag to the public repo

### Step 4: Verify the Release

```bash
# Check that the release was created:
gh release view "$TEST_TAG" --repo WSJTX/wsjtx-internal

# Check that the public repo received the code:
gh api repos/WSJTX/wsjtx/tags --jq '.[].name' | head -5

# Check that the public repo received the tag:
gh api repos/WSJTX/wsjtx/git/refs/tags/"$TEST_TAG" --jq '.ref'
```

### Step 5: Verify the Artifacts

Download the release artifacts to a new directory:

```bash
RELEASE_DIR="$(mktemp -d)"
gh release download "$TEST_TAG" --repo WSJTX/wsjtx-internal --dir "$RELEASE_DIR"
VERSION="${TEST_TAG#build/v}"
PKG="$RELEASE_DIR/wsjtx-${VERSION}-arm64-macOS.pkg"
```

Verify the installer signature, stapled notarization ticket, and Gatekeeper policy independently:

```bash
pkgutil --check-signature "$PKG"
xcrun stapler validate "$PKG"
spctl --assess --type install --verbose=2 "$PKG"
```

Success requires a valid Developer ID Installer chain, a valid staple, and an `accepted` Gatekeeper assessment. These checks do not validate nested application signatures, entitlements, or runtime behavior.

Inspect the packaged application rather than the staged build tree:

```bash
EXPANDED="$RELEASE_DIR/expanded"
pkgutil --expand-full "$PKG" "$EXPANDED"
APP="$EXPANDED/wsjtx-component.pkg/Payload/Applications/wsjtx.app"

codesign --verify --deep --strict --verbose=2 "$APP"
codesign -d --entitlements :- "$APP/Contents/MacOS/wsjtx"
codesign -d --entitlements :- "$APP/Contents/MacOS/jt9"
```

Signature verification must succeed, and both inspected executables must contain the three keys in `entitlements.plist`. The effective-entitlement check detects an outer bundle re-sign that preserved a valid signature but removed the executable entitlements.

Finally, install the package on a disposable or release-test macOS system and launch the installed app through Finder. Confirm that macOS grants audio input after the usage prompt, the receive level responds to live input, and `jt9` completes a decode cycle without a hardened-runtime or dynamic-loader failure. A successful signing or notarization check does not establish these runtime properties.

### Step 6: Clean Up the Test Release

```bash
# Delete the release:
gh release delete "$TEST_TAG" --repo WSJTX/wsjtx-internal --yes

# Delete the tag from wsjtx-internal:
gh api -X DELETE repos/WSJTX/wsjtx-internal/git/refs/tags/"$TEST_TAG"

# Delete the tag from wsjtx (public):
gh api -X DELETE repos/WSJTX/wsjtx/git/refs/tags/"$TEST_TAG"

# Delete local tag:
git tag -d "$TEST_TAG"
```

---

## 10. Ongoing Maintenance

### Secret Rotation

| Secret | Rotation Schedule | How to Rotate |
|--------|-------------------|---------------|
| `CROSS_REPO_TOKEN` | Before expiry (check token settings at github.com) | Generate new PAT → `gh secret set CROSS_REPO_TOKEN --repo WSJTX/wsjtx-internal` |
| `APPLE_APP_SPECIFIC_PASSWORD` | When Apple revokes it or account password changes | Generate new app-specific password → update secret |
| macOS signing certificates (.p12) | When certificate expires (typically 5 years) | Export new cert from Keychain → base64-encode → update both P12 and PASSWORD secrets |
| Windows signing certificate (.pfx) | When certificate expires (typically 1-3 years for OV) | Obtain renewed cert from CA → base64-encode → update PFX and PASSWORD secrets |

### Version Bumps

When releasing a new version, update the `version` input in both `ci.yml` and `release.yml`. Each file has three places where the version appears (one per platform call):

```bash
# Find all version references:
grep -n 'version:' .github/workflows/ci.yml .github/workflows/release.yml
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

### Branch Protection and Admin Bypass (A6 — sandbox vs. production)

The sandbox repo (`KJ5HST-LABS/wsjtx-internal`) and the production repo
(`WSJTX/wsjtx-internal`) have deliberately different branch-protection
postures. The difference is documented here so that replication doesn't
silently carry sandbox laxness into production.

**Current sandbox state** on `KJ5HST-LABS/wsjtx-internal`, branch `develop`:

| Setting | Sandbox value | Verify |
|---------|---------------|--------|
| `required_status_checks` | `macos / build`, `macos-intel / build`, `linux / build`, `linux-arm / build`, `windows / build` — all five must pass; strict (branch must be up-to-date with base). (**Note:** the sandbox list currently also carries a stale `windows-x86 / build` context left over from the removed Win32 path — scheduled for removal; does not block CI because no job publishes that context.) | `gh api repos/KJ5HST-LABS/wsjtx-internal/branches/develop/protection --jq '.required_status_checks.contexts'` |
| `required_pull_request_reviews` | **none** | `gh api .../branches/develop/protection --jq '.required_pull_request_reviews'` returns `null` |
| `enforce_admins` | **`false`** — admins can bypass required checks and direct-push | `gh api .../branches/develop/protection --jq '.enforce_admins.enabled'` returns `false` |
| `allow_force_pushes` | `false` | same endpoint, `.allow_force_pushes.enabled` |
| `allow_deletions` | `false` | same endpoint, `.allow_deletions.enabled` |
| `restrictions` (push allowlist) | `null` (everyone with write can push when other checks pass) | `.restrictions` |

**Why the sandbox keeps admin bypass on:**

1. The sandbox has a **single admin** (the project lead during bring-up). If
   `enforce_admins: true` were set, every routine SESSION_NOTES commit and
   every emergency hotfix would need to round-trip through a PR — but there
   is nobody else to review the PR. The protection becomes self-blocking.
2. During bring-up, workflow iteration frequently requires direct pushes to
   `develop` to unstick a half-working workflow that can only be validated
   on the real runner. Forcing every iteration through a PR adds 5-20
   minutes of cycle time per attempt with no added safety (the reviewer
   would be the same person pushing).
3. The sandbox does not ship to external users as a primary distribution
   channel — its purpose is (a) to prove the machinery works, and (b) to
   feed the arm64 macOS `.pkg` to the one downstream consumer. Blast
   radius from a bad direct-push is small.

**Production requirement** (MUST change before replicating to
`WSJTX/wsjtx-internal`):

1. Set `enforce_admins: true` so that **no one** — including org owners —
   can bypass the required status checks. Command:
   ```bash
   gh api -X POST repos/WSJTX/wsjtx-internal/branches/develop/protection/enforce_admins
   ```
2. Add `required_pull_request_reviews` with at least one required reviewer
   and `dismiss_stale_reviews: true`. Team size on the production repo is
   large enough that self-review is avoidable.
3. Consider adding `required_signatures: true` if the team's contributors
   all use signed commits, or leaving it off if signing adoption is not
   universal (the required checks still block unreviewed code).
4. Remove `--force` from the release-time public-mirror sync (around
   `release.yml:249`) before setting `enforce_admins: true` — the
   `--force` step currently relies on admin bypass during sandbox
   bring-up and must be retired in lockstep with enforcing admin
   protections.

**Verification of the production state** — after replication, this query
should return `true` on production and `false` on sandbox:

```bash
gh api repos/WSJTX/wsjtx-internal/branches/develop/protection/enforce_admins --jq '.enabled'
# → true (production)

gh api repos/KJ5HST-LABS/wsjtx-internal/branches/develop/protection/enforce_admins --jq '.enabled'
# → false (sandbox; intentional)
```

If the sandbox ever changes (e.g., a second maintainer joins the project
and PR-based review becomes viable), update this section with the new
state and open a tracking issue to tighten `enforce_admins` on the sandbox
as well.
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

## 11. Troubleshooting

### Problem: "refusing to allow an OAuth App to create or update workflow"

**Context:** This appears when the release workflow tries to push to the public repo.

**Root cause:** The credential being used (deploy key, OAuth token, or any non-PAT) cannot modify `.github/workflows/` files. This is a GitHub platform-level restriction.

**Fix:** Ensure `CROSS_REPO_TOKEN` is a **fine-grained PAT** with **Contents: Read and write** AND **Workflows: Read and write** permissions. Classic PATs need the `workflow` scope.

### Problem: macOS build fails with "no identity found"

**Context:** The signing step can't find a Developer ID certificate in the keychain.

**Root cause:** Developer ID mode was selected because `DEVELOPER_ID_CERTIFICATE_P12` is present, but the imported file, password, or certificate contents do not yield a Developer ID Application identity.

**Diagnosis:**
```bash
# Check that the secret exists:
gh secret list --repo WSJTX/wsjtx-internal | grep DEVELOPER_ID

# Re-encode and re-set:
base64 -i app.p12 -o app.p12.b64
gh secret set DEVELOPER_ID_CERTIFICATE_P12 --repo WSJTX/wsjtx-internal < app.p12.b64
```

### Problem: Notarization fails with "Invalid" status

**Context:** The notarytool submission comes back as "Invalid" instead of "Accepted."

**Diagnosis:** The workflow automatically fetches the notarization log on failure. Look for the log output in the GitHub Actions run. Common issues:

| Log message | Meaning | Fix |
|-------------|---------|-----|
| "The signature of the binary is invalid" | Code signing used wrong identity or missed a binary | Check that all executables and dylibs are signed |
| "The binary uses an SDK older than the 10.9 SDK" | Deployment target too old | Check `CMAKE_OSX_DEPLOYMENT_TARGET` (11.0 for arm64 per `ci.yml:39`, 10.13 for x86_64 Intel per `ci.yml:50`) |
| "The signature does not include a secure timestamp" | Missing `--timestamp` in codesign | Verify the codesign commands include `--timestamp` |

### Problem: Windows build fails at OmniRig install

**Context:** The PowerShell step that downloads OmniRig fails.

**Root cause:** The download URL (`https://www.dxatlas.com/OmniRig/Files/OmniRig.zip`) may be temporarily unavailable.

**Fix:** Re-run the job. If persistent, download OmniRig manually, add the `.exe` to the repo as a build dependency, and update the workflow to use the local copy.

### Problem: CROSS_REPO_TOKEN sync silently skips

**Context:** The release job succeeds but the public repo doesn't get updated. No error in logs — just a warning: "CROSS_REPO_TOKEN not set — skipping public repo sync."

**Root cause:** The secret is not set, or it's set on the wrong repo, or it's empty.

**Fix:**
```bash
# Verify the secret exists:
gh secret list --repo WSJTX/wsjtx-internal | grep CROSS_REPO

# Re-set it:
gh secret set CROSS_REPO_TOKEN --repo WSJTX/wsjtx-internal
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

## 12. Reference: Complete File Inventory

### Files to Include in the PR

| File | Purpose | Changes Needed |
|------|---------|----------------|
| `.github/workflows/ci.yml` | CI orchestrator | Branch name |
| `.github/workflows/release.yml` | Release pipeline | Public repo URL, branch name |
| `.github/workflows/build-macos.yml` | macOS build (parameterized arm64/x86_64) | None |
| `.github/workflows/build-linux.yml` | Linux build (parameterized x86_64/aarch64) | None |
| `.github/workflows/build-windows.yml` | Windows x86_64 build | None |
| `.github/workflows/hamlib-upstream-check.yml` | Scheduled (weekly cron + `workflow_dispatch`) poll of Hamlib upstream tags; files a tracking issue when a newer 4.x release is available. No platform builds; self-contained. | None |
| `entitlements.plist` | macOS app entitlements | None (if not already in repo) |
| `Darwin/com.wsjtx.sysctl.plist` | macOS shared memory config | None (if not already in repo) |
| `CMakeLists.txt` | OmniRig type library variable | Add `OMNIRIG_TYPE_LIB` conditional (optional, can be separate PR) |

### Secrets Required on `wsjtx-internal`

Use the canonical inventory and setup procedure in [Phase 3](#5-phase-3-create-repository-secrets). `CROSS_REPO_TOKEN` additionally needs Actions:read on `WSJTX/wsjtx` for release signing-run polling. The public repository's Windows SignPath workflows use `SIGNPATH_API_TOKEN`; no SignPath private key is stored in either repository.

### External Dependencies (Downloaded at Build Time)

| Dependency | URL | Used By |
|------------|-----|---------|
| Hamlib 4.7.2 | `https://github.com/Hamlib/Hamlib.git` | All five platforms |
| OmniRig | `https://www.dxatlas.com/OmniRig/Files/OmniRig.zip` | Windows only |

### Build-Time Patches Applied in CI

These are `sed` patches applied during the build, not committed to the repo. They work around upstream issues:

| Patch | File | Platform | Why |
|-------|------|----------|-----|
| Comment out MAP65 | `CMakeLists.txt` | Windows | GCC 15 rejects legacy Fortran in `decode0.f90` |
| Fix FFTW3 threads | `CMake/Modules/FindFFTW3.cmake` | Windows | MSYS2 splits FFTW threads into separate lib |
| Symlink dumpcpp | `/mingw64/bin/` | Windows | MSYS2 ships `dumpcpp-qt5`, CMake expects `dumpcpp` |

These patches are candidates for future upstream PRs but are not required for the CI deployment itself.
