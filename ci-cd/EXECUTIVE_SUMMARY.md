# CI/CD for WSJT-X — Executive Summary

**Audience:** Team-internal. Summary for team members and org leadership evaluating the proposed CI/CD machinery.

## What We Built

A fully automated build-and-release pipeline for WSJT-X that compiles the application on five platforms (macOS ARM64, macOS Intel x86_64, Linux x86_64, Linux aarch64, Windows x86_64) every time code is pushed, and publishes signed release binaries with a single `git tag`.

## Current State

WSJT-X `WSJTX/*` repos do not have build automation today. Builds on the team side are manual. The sandbox pipeline (in `KJ5HST-LABS/wsjtx-internal`) provides per-commit build verification across all five platforms and automates the release process; the team decides whether and when to replicate it.

## How It Works

**On every push to `develop`:**
Code is automatically built on all five platforms in parallel. Green check = compiles everywhere. Red X = something broke. Results appear directly on the commit or pull request in GitHub.

**On a version tag (`build/v3.0.1`):**
All five platforms build, a GitHub Release is created with signed, downloadable binaries, and the source is automatically synced to the public repo.

> *Sandbox triggers on the `build/v*` tag prefix (decision #1 in the accompanying email proposes changing this to bare `v*` on adoption). The examples in this document show the current sandbox convention.*

## Test Results

The pipeline has been proven end-to-end in the sandbox — a complete happy-path release cycle and a 5-platform CI green run.

| Platform | Build Time | Signed | Notes |
|----------|-----------|--------|-------|
| macOS ARM64 | ~8 min | Yes | Developer ID + Apple Notarization, Gatekeeper-ready |
| macOS Intel x86_64 | ~10 min | Yes | Developer ID + Apple Notarization, Gatekeeper-ready |
| Linux x86_64 | ~7 min | No | AppImage output; GPG signing can be added |
| Linux aarch64 | ~10 min | No | AppImage output; `ubuntu-24.04-arm` runner |
| Windows x86_64 | ~15 min | Sandbox: self-signed | Production: Authenticode (team cert — see decision 5) |

The release pipeline was validated: tag push triggered five-platform builds, created a GitHub Release with all artifacts, and synced source + tag to the public repo — all automatically.

### Code Signing in CI

The macOS signing path is fully wired to real Apple Developer credentials. The Windows signing path is **wired but uses a sandbox placeholder cert** pending decision 5 (production Authenticode adoption).

**macOS:** The build signs the application binary and dylibs with the Developer ID Application certificate, signs the `.pkg` installer with the Developer ID Installer certificate, and submits the package to Apple for notarization. The resulting `.pkg` passes Gatekeeper without warnings. Secrets-absent builds (Dependabot, external forks) short-circuit the signing/notarization steps via a `signing_enabled` guard so compile + ctest still run.

**Windows:** The sandbox currently signs the NSIS installer with a **per-run ephemeral self-signed cert** generated at build time (`osslsigncode`, no stored secret). The signature is structurally valid but does not chain to a trusted root — Windows SmartScreen will still show an *"Unknown Publisher"* warning, because only certificates chained to a Microsoft-trusted Authenticode root suppress that prompt. `osslsigncode verify` is `|| true`-guarded so CI stays green. This proves the signing step is wired into the pipeline. Production replaces the ephemeral cert with a team-owned Authenticode certificate in an encrypted secret (e.g. `WINDOWS_CODESIGN_PFX_BASE64`) and drops the `|| true` guard — see email decision 5.

**Linux:** Unsigned for now. Linux users don't encounter the same install-time warnings as macOS and Windows. GPG-signing release tarballs is straightforward to add if the team wants it — one additional secret (GPG private key) and a small step in the release workflow.

The Deployment Playbook covers how to export the macOS certificates as CI secrets. Windows cert provisioning is covered by email decision 5.

## What It Takes to Deploy

**Six workflow files** copied to `.github/workflows/` (`ci.yml`, `release.yml`, `build-macos.yml`, `build-linux.yml`, `build-windows.yml`, `hamlib-upstream-check.yml`) — a small number of edits across two files (the public-repo URL block in `release.yml`, and optionally the Hamlib branch pin; the version string is auto-derived from `CMakeLists.txt`).

**Eight repository secrets today** — Apple signing certificates (4), Apple notarization credentials (3), and a GitHub token for public-repo sync (1). The team's existing Apple signing credentials are used directly — they just need to be exported as base64-encoded secrets. Set once via `gh secret set` (`gh` is the [GitHub CLI](https://cli.github.com/)). **Production adds two more secrets** when the Windows Authenticode certificate lands (see email decision 5), bringing the total to ten.

The Apple Developer account is currently held by **John G4KLA**, who produces the team's existing signed/notarized macOS releases. Adopting this pipeline does not require transferring the account — John exports his existing Developer ID certificates as `.p12` files and they become CI secrets. See the [Deployment Playbook §5.2](DEPLOYMENT_PLAYBOOK.md#52-secrets-2-5-macos-code-signing-certificates) for the handoff workflow.

**One prerequisite** — GitHub Actions must be enabled at the WSJTX org level (an admin setting).

No changes to the build system are required. One optional CMake change (`OMNIRIG_TYPE_LIB` variable) makes the Windows CI cleaner and is backward-compatible with local builds.

## Documentation

| Document | What It Covers |
|----------|---------------|
| [Development Workflow](DEVELOPMENT_WORKFLOW.md) | How team members and external contributors work, how the two repos relate, how CI/CD integrates into daily development, the release process, branch strategy, code review |
| [CI/CD Deployment Playbook](DEPLOYMENT_PLAYBOOK.md) | Step-by-step instructions to deploy the pipeline to the official WSJTX org — enabling Actions, creating secrets, adapting files, testing, troubleshooting |
| Workflow files (included in PR) | Platform-specific build strategies, caching, Windows CI findings, patches applied |

## Hand-off artifacts (available for team evaluation)

1. **Executive summary** (`EXECUTIVE_SUMMARY.md` — this doc) — two-page overview.
2. **Development Workflow** (`DEVELOPMENT_WORKFLOW.md`) — how the two-repo model operates once CI/CD is in place.
3. **Deployment Playbook** (`DEPLOYMENT_PLAYBOOK.md`) — step-by-step the team can follow if/when it adopts this machinery.
4. **Adoption Notes** (`ADOPTION_NOTES.md`) — every difference between the sandbox prototype and production that needs reconciling, as of the current post-Win32 5-platform matrix.

Team-owned next steps (exports of team signing credentials, Windows Authenticode cert provisioning, Linux GPG-signing adoption, org-level Actions enablement on `WSJTX/*`) are the team's call.
