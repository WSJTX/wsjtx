# Replication Delta — Sandbox → `WSJTX/wsjtx-internal`

**Status:** DRAFT — reflects the current 5-platform matrix (macOS ARM64, macOS Intel x86_64, Linux x86_64, Linux aarch64, Windows x86_64).
**Purpose:** Inventory every difference between the sandbox's proven CI/CD surface and `WSJTX/wsjtx-internal` that must be reconciled before the sandbox machinery can run there.
**Audience:** WSJT-X maintainers evaluating replication.
**Scope:** Inventory only. This document does not open PRs against `WSJTX/*`; workflow replication is a team-owned decision and team-owned execution. Timing and ordering of adoption are for the team.

---

## 1. Proof of fitness

The sandbox's pipeline is exercised end-to-end on a 5-platform matrix (macOS arm64, macOS Intel x86_64, Linux x86_64, Linux aarch64, Windows x86_64). Each row below is a reconciliation item; the runs cited in §1 establish that once the reconciliations are made, the pipeline is operationally complete.

| Evidence | What it proves | Run |
|---|---|---|
| Forced-failure — `needs:` refusal path | Release job is SKIPPED when any platform build fails | `24641436818` |
| Forced-failure — gate step refusal path | All-platforms-ready gate exits 1 on missing installer; Create/Verify/Push steps skip | `24644904444` |
| Happy-path — full publish | Gate passes; GitHub Release created `--prerelease`; public mirror force-pushed | `24673796962` |
| 5-platform CI green (post-Win32 removal) | 5-platform matrix builds successfully after Win32 removal (+Linux aarch64) | `24726660336` |

---

## 2. Sandbox workflow surface

Six workflow files live in `.github/workflows/`:

| File | Trigger | Purpose | Calls |
|---|---|---|---|
| `ci.yml` | `push`/`pull_request` to `develop`, `workflow_dispatch` | PR/develop CI matrix (5 platforms, build + ctest) | `build-{macos,linux,windows}.yml` |
| `release.yml` | `push` of tag `build/v*` | Build + gate + publish + public-mirror sync | `build-{macos,linux,windows}.yml` |
| `build-macos.yml` | `workflow_call` | macOS build (parameterised `arch`, `runner`, `deployment_target`) | — |
| `build-linux.yml` | `workflow_call` | Linux AppImage (parameterised `arch` — x86_64 or aarch64; runner selected accordingly) | — |
| `build-windows.yml` | `workflow_call` | Windows x86_64 NSIS installer + self-signed signature | — |
| `hamlib-upstream-check.yml` | `schedule` (Mon 12:00 UTC) + `workflow_dispatch` | File a tracking issue when upstream Hamlib 4.x advances beyond the pin | — |

`ci.yml:15-29` and `release.yml:14-45` share the `prepare` pattern — version is derived once (from `CMakeLists.txt` on PR/develop; from tag name on release) and passed to every downstream job. `release.yml:32-45` additionally enforces tag ↔ `CMakeLists.txt:55` numeric-base parity (Issue #35 resolution).

---

## 3. Secrets

Every `${{ secrets.X }}` reference, its files, and its reconciliation requirement:

| Secret | Files (`.github/workflows/`) | Purpose | Sandbox provenance | Reconciliation |
|---|---|---|---|---|
| `DEVELOPER_ID_CERTIFICATE_P12` | `build-macos.yml:37, 322` | macOS Developer ID Application cert (base64 p12) | Personal (sandbox owner) | Team's "Developer ID Application" cert |
| `DEVELOPER_ID_CERTIFICATE_PASSWORD` | `build-macos.yml:323` | P12 password | — | Paired with above |
| `DEVELOPER_ID_INSTALLER_P12` | `build-macos.yml:324` | macOS Developer ID Installer cert | Personal (sandbox owner) | Team's "Developer ID Installer" cert |
| `DEVELOPER_ID_INSTALLER_PASSWORD` | `build-macos.yml:325` | P12 password | — | Paired with above |
| `APPLE_ID` | `build-macos.yml:437, 467` | Apple ID for `notarytool` | Personal (sandbox owner) | Team's notarization identity |
| `APPLE_APP_SPECIFIC_PASSWORD` | `build-macos.yml:438, 468` | App-specific password for notarization | — | Paired with above |
| `APPLE_TEAM_ID` | `build-macos.yml:439, 469` | Apple Developer team ID | Sandbox organization | Team's Apple team ID |
| `CROSS_REPO_TOKEN` | `release.yml:235` | PAT (`repo`+`workflow` scopes) for force-pushing to the public mirror | Personal PAT (sandbox owner) | Decision tied to §8 mirror policy — may be unneeded if team drops mirror |

**Scope to request:** 8 secrets. All are consumed at repo level in the sandbox; upstream may elect environment- or org-level scoping — orthogonal to the replication mechanics.

**Graceful-degradation posture (build-macos.yml:34-44):** when the macOS secrets are absent (e.g. Dependabot PR or external fork), the workflow sets `signing_enabled=false` and the `Import signing certificates` / `Code sign binaries` / `Build installer pkg` / `Notarize pkg` / `Notarize CLI tools` / `Upload pkg` steps all short-circuit via `if: steps.check_secrets.outputs.signing_enabled == 'true'`. Compile, ctest, and dylib-bundling still run. This preserves PR coverage on unprivileged triggers. Upstream may retain this posture or enforce secrets-required on protected branches.

**Windows signing uses no secrets (`build-windows.yml:208-239`):** an ephemeral self-signed cert is generated per run, satisfies osslsigncode structurally but does not chain to a trusted root. `osslsigncode verify` is `|| true`-guarded (line 235) because the self-signed CA will never pass CA-trust. Per decision #5 in the adoption email, production replaces this with a real cert in an encrypted secret (e.g. `WINDOWS_CODESIGN_PFX_BASE64`) and removes the `|| true`. Flagged here because the Windows replication is a drop-in copy minus this signing block, which is cert-gated.

---

## 4. Runners

Every `runs-on:` label:

| Label | File:line | Job | Plan tier needed |
|---|---|---|---|
| `ubuntu-latest` | `ci.yml:16`, `release.yml:15`, `release.yml:99`, `hamlib-upstream-check.yml:15` | `prepare`, `release`, `check` | Standard |
| `macos-15` | set via `runner:` input at `ci.yml:38`, `release.yml:55` | `macos` (arm64) | Standard (Apple Silicon) |
| `macos-15-intel` | set via `runner:` input at `ci.yml:49`, `release.yml:66` | `macos-intel` | Standard (x86_64 VM on Apple Silicon host) |
| `ubuntu-24.04` | `build-linux.yml:23` (conditional `arch != 'aarch64'` branch) | `linux` (x86_64) | Standard |
| `ubuntu-24.04-arm` | `build-linux.yml:23` (conditional `arch == 'aarch64'` branch) | `linux-arm` (aarch64) | Standard (Ubuntu arm64 runner) |
| `windows-latest` | `build-windows.yml:18` | `windows` (x86_64) | Standard |

The macOS entries are parameterised in the caller (`ci.yml` and `release.yml`) and consumed by `build-macos.yml:30` as `runs-on: ${{ inputs.runner }}`. Re-parameterising for different runner labels is one-line.

The Linux runner is chosen inline at `build-linux.yml:23` via the expression `${{ inputs.arch == 'aarch64' && 'ubuntu-24.04-arm' || 'ubuntu-24.04' }}`. Both jobs (`linux` x86_64 and `linux-arm` aarch64) share the same workflow file; the runner label differs by `arch` input.

**Reconciliation items:**
- `macos-15-intel` is a relatively new runner label. The team plan must have it available; confirm at replication time.
- `ubuntu-24.04-arm` is GitHub's native Ubuntu aarch64 runner; confirm the team's plan tier includes it.

---

## 5. Permissions and tokens

Explicit `permissions:` blocks:

| Location | Grant | Reason |
|---|---|---|
| `release.yml:100-101` (job `release`) | `contents: write` | `gh release create` + `git push public HEAD:main --force` + tag push |
| `hamlib-upstream-check.yml:9-11` (workflow) | `contents: read`, `issues: write` | `gh issue create` for upstream tracking |

All other jobs use the default `GITHUB_TOKEN` permissions.

`GH_TOKEN` / `github.token` references:

| Location | Use |
|---|---|
| `release.yml:194` | `gh release create` |
| `hamlib-upstream-check.yml:23` | `gh api /repos/Hamlib/Hamlib/tags --paginate` + `gh issue create` + `gh issue list --search` |

`persist-credentials: false` is set on `actions/checkout@v6` in `release.yml:103-106` and `hamlib-upstream-check.yml:17-19` to prevent the auto-configured credential from being used in subsequent `git push`es (release.yml explicitly uses `CROSS_REPO_TOKEN`-authenticated remote instead).

**Reconciliation items:**
- Team's org/repo default token permissions may be more restrictive; confirm `contents: write` is grantable at job level on `release.yml`.
- If the team uses a GitHub App for writes instead of `GITHUB_TOKEN`, `release.yml:194` and `hamlib-upstream-check.yml:23` change from `github.token` to the app's installation-token expression.

---

## 6. External dependencies

| Dependency | Pinned at | Pin type | Replication notes |
|---|---|---|---|
| Hamlib 4.7.1 | `ci.yml:36,47,58,67,76` + `release.yml:53,64,75,84,93` | Branch (`hamlib_branch: "4.7.1"`) | 10 call sites (5 per caller); `hamlib-upstream-check.yml` tracks eligibility for bumps |
| pFUnit v4.9.0 | `build-macos.yml:76`, `build-linux.yml:49`, `build-windows.yml:62` | Tag (`--branch v4.9.0 --recursive`) via `git clone` | 3 sites; cached per-platform; per-arch cache key on Linux |
| linuxdeploy `1-alpha-20251107-1` | `build-linux.yml:156` (tag) + `build-linux.yml:159-160` (SHA256 per arch) | Dated tag + SHA256 (x86_64: `c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d`; aarch64: `620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff`) | Verified at download via `sha256sum -c -` |
| linuxdeploy-plugin-qt `continuous` | `build-linux.yml:166` | Rolling | Plan doc A9b — blocked on upstream; documented at `build-linux.yml:148-155` |
| OmniRig (dxatlas.com) | `build-windows.yml:122` | Live URL — no pin | `Invoke-WebRequest https://www.dxatlas.com/OmniRig/Files/OmniRig.zip`; provides COM type library for `dumpcpp` |
| MSYS2 MINGW64 packages | `build-windows.yml:26-48` | Rolling via `msys2/setup-msys2@v2 update: true` | ~18 packages; `cache: true` |

External action pins (GitHub Marketplace):

| Action | Version | Locations |
|---|---|---|
| `actions/checkout` | `@v6` | 7× (one per workflow; `release.yml` calls it twice — `release.yml:19` in `prepare`, `release.yml:103` in `release`) |
| `actions/cache` | `@v5` | 6× (pFUnit + Hamlib per build-*.yml) |
| `actions/upload-artifact` | `@v7` | 9× (3 per build-*.yml: test results + binaries + installers) |
| `actions/download-artifact` | `@v8` | 1× (`release.yml:109`) |
| `msys2/setup-msys2` | `@v2` | 1× (`build-windows.yml:25`) |

Dependabot (`.github/dependabot.yml`): weekly, Monday, GitHub Actions ecosystem, open-pull-requests-limit 5, `ci(deps)` commit prefix. Already merged in sandbox: `actions/cache v4→v5` (#33), `actions/checkout v4→v6` (#32). Auto-merge policy: `DEPLOYMENT_PLAYBOOK.md §10`.

**Reconciliation items:**
- Team may prefer SHA-pinned actions over major-version tags; that's a one-pass substitution on the 24 `uses:` lines in Appendix A.
- OmniRig download is not pinned; if the team requires pinning, cache the zip in a repo-hosted location (not the sandbox's remit) or accept the dxatlas.com URL as a trust boundary.
- pFUnit v4.9.0 is pinned by tag only (no SHA). Upgrading would require the same cache-key drop pattern already present (`hashFiles('.github/workflows/build-*.yml')` invalidates on workflow edits).

---

## 7. Branch structure & tag convention

Sandbox:
- Default branch: `develop`.
- Release-tag convention: `build/v<MAJOR>.<MINOR>.<PATCH>[-<pre>]` (e.g. `build/v3.0.1-rc1`, `build/v3.0.1`). SemVer pre-release detected by hyphen at `release.yml:210`; pre-release tags flag the GitHub Release as `--prerelease`.
- `release.yml` triggers on `tags: ["build/v*"]` (`release.yml:5`).
- `CMakeLists.txt:55` `VERSION` must match the tag numeric base; enforced at `release.yml:32-45`. CMake's `VERSION` is numeric-only, so `-rc1` / `-beta1` pre-release suffixes live on the tag, not in `CMakeLists.txt`.
- `build-windows.yml:200-201` overrides `CPACK_PACKAGE_VERSION` and `CPACK_PACKAGE_FILE_NAME` with `${{ inputs.version }}` so the NSIS installer carries the pre-release suffix (Issue #35 resolution; run `24673796962` confirmed `wsjtx-3.0.1-rc1-win64.exe`).

**Reconciliation items (team-owned):**
- Is `WSJTX/wsjtx-internal` default branch `develop`? Last observed 2026-04-02; confirm unchanged at replication time.
- Does `WSJTX/wsjtx-internal` already use a release-tag pattern? If the existing pattern is `v<ver>`, `release/v<ver>`, or similar, the `tags:` trigger at `release.yml:5` and the parity check at `release.yml:32-45` both change. Not a structural rewrite — same derivation, different prefix.

---

## 8. Public-mirror policy

`release.yml:233-267` force-pushes `WSJTX/wsjtx-internal` HEAD to `KJ5HST-LABS/wsjtx:main` (via `CROSS_REPO_TOKEN` over HTTPS) and pushes the release tag. This is the sandbox modelling the upstream `WSJTX/wsjtx-internal` → `WSJTX/wsjtx` one-way sync pattern.

Elements the team would decide, not inherited automatically:
- Whether to preserve the `wsjtx-internal` → `wsjtx` mirror pattern.
- Whether to retain `--force` or move to fast-forward-only.
- Whether to gate the release job on a manual-approval GitHub Environment (`environment: public-release`).
- Whether `WSJTX/wsjtx` remains the public release surface or is deprecated in favour of publishing directly from `WSJTX/wsjtx-internal`.

These are out-of-scope for this inventory. This document only inventories what the sandbox does; the sandbox's choices here are not prescriptions.

---

## 9. Replication ordering (blast-radius ascending)

One possible ordering, presented for the team's planning convenience. The team decides actual ordering and timing.

| Chunk | Files | Blast radius | Gating reconciliation |
|---|---|---|---|
| **CI matrix** | `ci.yml` + `build-{macos,linux,windows}.yml` | Lowest — PR status only, no public surface | Runners (§4); macOS secrets (§3) — graceful-degraded, so replication works with secrets absent initially |
| **Upstream watch** | `hamlib-upstream-check.yml` | Low — files a tracking issue once per week | `issues: write` permission (§5) |
| **Release** | `release.yml` | Highest — touches GitHub Releases + (optionally) public mirror | All of §3, §7 tag convention, §8 mirror decision, production cert delivery (out of scope here — decision #5 in the adoption email) |

The CI chunk does not depend on the release chunk; the release chunk does not start producing real releases until the team chooses to tag `build/v*` on the team repo. Replication can land the CI chunk and run it for weeks before the team authorises the first real release tag.

---

## 10. Completion criteria for this inventory

- [x] Inventory document (this document).
- [x] Grep-based evidence (`grep -rE 'secrets\.|runs-on:|permissions:' .github/workflows/`) in the appendix.
- [ ] Team sign-off on reconciliation choices, per item, dated.

The third bullet is a team action: adoption of proven machinery — when and how the team replicates — is team-owned. This inventory is complete when the first two bullets land.

---

## 11. What this inventory does NOT cover

- **Production code-signing certs** (team-owned; tied to decision #5 in the adoption email).
- **When** the team replicates, **in what order**, or **by which PRs** — team-owned.
- **Branch-protection settings** on `WSJTX/*` — applied by the team at its own cadence.
- **Governance files** (`SECURITY.md`, `CONTRIBUTING.md`, `CODEOWNERS`) — already in the sandbox; team applies same shape if/when adopting.
- **`WSJTX/wsjtx` visibility or release-surface redesign** — team-owned.
- **PR opening on `WSJTX/wsjtx-internal`** — team-owned. Opening this machinery on `WSJTX/wsjtx-internal:develop` is a team execution action.

---

## Appendix A — Grep evidence

*Grep evidence reflects the current 5-platform matrix (+Linux aarch64, −Win32).*

### A.1 `grep -rnE 'secrets\.|runs-on:|permissions:' .github/workflows/`

```
.github/workflows/ci.yml:16:    runs-on: ubuntu-latest
.github/workflows/build-macos.yml:30:    runs-on: ${{ inputs.runner }}
.github/workflows/build-macos.yml:37:          APP_P12: ${{ secrets.DEVELOPER_ID_CERTIFICATE_P12 }}
.github/workflows/build-macos.yml:320:        if: steps.check_secrets.outputs.signing_enabled == 'true'
.github/workflows/build-macos.yml:322:          APP_P12: ${{ secrets.DEVELOPER_ID_CERTIFICATE_P12 }}
.github/workflows/build-macos.yml:323:          APP_P12_PW: ${{ secrets.DEVELOPER_ID_CERTIFICATE_PASSWORD }}
.github/workflows/build-macos.yml:324:          INST_P12: ${{ secrets.DEVELOPER_ID_INSTALLER_P12 }}
.github/workflows/build-macos.yml:325:          INST_P12_PW: ${{ secrets.DEVELOPER_ID_INSTALLER_PASSWORD }}
.github/workflows/build-macos.yml:352:        if: steps.check_secrets.outputs.signing_enabled == 'true'
.github/workflows/build-macos.yml:384:        if: steps.check_secrets.outputs.signing_enabled == 'true'
.github/workflows/build-macos.yml:435:        if: steps.check_secrets.outputs.signing_enabled == 'true'
.github/workflows/build-macos.yml:437:          APPLE_ID: ${{ secrets.APPLE_ID }}
.github/workflows/build-macos.yml:438:          APPLE_ID_PASSWORD: ${{ secrets.APPLE_APP_SPECIFIC_PASSWORD }}
.github/workflows/build-macos.yml:439:          APPLE_TEAM_ID: ${{ secrets.APPLE_TEAM_ID }}
.github/workflows/build-macos.yml:465:        if: steps.check_secrets.outputs.signing_enabled == 'true'
.github/workflows/build-macos.yml:467:          APPLE_ID: ${{ secrets.APPLE_ID }}
.github/workflows/build-macos.yml:468:          APPLE_ID_PASSWORD: ${{ secrets.APPLE_APP_SPECIFIC_PASSWORD }}
.github/workflows/build-macos.yml:469:          APPLE_TEAM_ID: ${{ secrets.APPLE_TEAM_ID }}
.github/workflows/build-macos.yml:490:        if: always() && steps.check_secrets.outputs.signing_enabled == 'true'
.github/workflows/build-macos.yml:494:        if: steps.check_secrets.outputs.signing_enabled == 'true'
.github/workflows/build-windows.yml:18:    runs-on: windows-latest
.github/workflows/hamlib-upstream-check.yml:9:permissions:
.github/workflows/hamlib-upstream-check.yml:15:    runs-on: ubuntu-latest
.github/workflows/build-linux.yml:23:    runs-on: ${{ inputs.arch == 'aarch64' && 'ubuntu-24.04-arm' || 'ubuntu-24.04' }}
.github/workflows/release.yml:15:    runs-on: ubuntu-latest
.github/workflows/release.yml:99:    runs-on: ubuntu-latest
.github/workflows/release.yml:100:    permissions:
.github/workflows/release.yml:235:          TOKEN: ${{ secrets.CROSS_REPO_TOKEN }}
```

### A.2 `grep -rnE 'uses: [a-z0-9-]+/[a-z0-9-]+@' .github/workflows/`

```
.github/workflows/ci.yml:20:      - uses: actions/checkout@v6
.github/workflows/build-macos.yml:32:      - uses: actions/checkout@v6
.github/workflows/build-macos.yml:68:        uses: actions/cache@v5
.github/workflows/build-macos.yml:102:        uses: actions/cache@v5
.github/workflows/build-macos.yml:149:        uses: actions/upload-artifact@v7
.github/workflows/build-macos.yml:495:        uses: actions/upload-artifact@v7
.github/workflows/build-macos.yml:522:        uses: actions/upload-artifact@v7
.github/workflows/build-windows.yml:23:      - uses: actions/checkout@v6
.github/workflows/build-windows.yml:25:      - uses: msys2/setup-msys2@v2
.github/workflows/build-windows.yml:52:        uses: actions/cache@v5
.github/workflows/build-windows.yml:93:        uses: actions/cache@v5
.github/workflows/build-windows.yml:171:        uses: actions/upload-artifact@v7
.github/workflows/build-windows.yml:242:        uses: actions/upload-artifact@v7
.github/workflows/build-windows.yml:248:        uses: actions/upload-artifact@v7
.github/workflows/hamlib-upstream-check.yml:17:      - uses: actions/checkout@v6
.github/workflows/build-linux.yml:25:      - uses: actions/checkout@v6
.github/workflows/build-linux.yml:41:        uses: actions/cache@v5
.github/workflows/build-linux.yml:74:        uses: actions/cache@v5
.github/workflows/build-linux.yml:123:        uses: actions/upload-artifact@v7
.github/workflows/build-linux.yml:200:        uses: actions/upload-artifact@v7
.github/workflows/build-linux.yml:207:        uses: actions/upload-artifact@v7
.github/workflows/release.yml:19:      - uses: actions/checkout@v6
.github/workflows/release.yml:103:      - uses: actions/checkout@v6
.github/workflows/release.yml:109:        uses: actions/download-artifact@v8
```

### A.3 `grep -rnE 'GH_TOKEN|github\.token|CROSS_REPO_TOKEN' .github/workflows/`

```
.github/workflows/hamlib-upstream-check.yml:23:          GH_TOKEN: ${{ github.token }}
.github/workflows/release.yml:194:          GH_TOKEN: ${{ github.token }}
.github/workflows/release.yml:235:          TOKEN: ${{ secrets.CROSS_REPO_TOKEN }}
.github/workflows/release.yml:241:            echo "::error::CROSS_REPO_TOKEN not set — refusing to publish without public-mirror sync"
.github/workflows/release.yml:245:          # restriction).  Use CROSS_REPO_TOKEN (PAT with repo+workflow
```

### A.4 `grep -rnE 'git clone|curl .* https|Invoke-WebRequest' .github/workflows/`

```
.github/workflows/build-macos.yml:76:          git clone --depth 1 --branch v4.9.0 --recursive \
.github/workflows/build-macos.yml:110:          git clone --depth 1 --branch "${{ inputs.hamlib_branch }}" \
.github/workflows/build-windows.yml:62:          git clone --depth 1 --branch v4.9.0 --recursive \
.github/workflows/build-windows.yml:104:          git clone --depth 1 --branch "${{ inputs.hamlib_branch }}" \
.github/workflows/build-windows.yml:122:          Invoke-WebRequest -Uri "https://www.dxatlas.com/OmniRig/Files/OmniRig.zip" -OutFile OmniRig.zip
.github/workflows/build-linux.yml:49:          git clone --depth 1 --branch v4.9.0 --recursive \
.github/workflows/build-linux.yml:82:          git clone --depth 1 --branch "${{ inputs.hamlib_branch }}" \
```

---

## Appendix B — Pin inventory summary

| Class | Pinned? | How | Where |
|---|---|---|---|
| GitHub Actions | Major-version tag | `@vN` | 24 `uses:` lines |
| MSYS2 setup-msys2 | Major-version tag | `@v2` | `build-windows.yml:25` |
| Hamlib | Branch | `hamlib_branch: "4.7.1"` | 10 call sites in `ci.yml` + `release.yml` (5 per caller) |
| pFUnit | Tag | `--branch v4.9.0` | 3 `git clone` sites |
| linuxdeploy core | Tag + SHA256 | `LINUXDEPLOY_TAG` + `sha256sum -c -` | `build-linux.yml:156` (tag), `:159-160` (SHA256 per arch), `:163-164` (download), `:167` (verify) |
| linuxdeploy-plugin-qt | Rolling | `continuous` | `build-linux.yml:166` (download); rationale at `build-linux.yml:148-155` (A9b — blocked on upstream) |
| OmniRig | Unpinned | Direct URL | `build-windows.yml:122` |
| MSYS2 packages | Rolling | `update: true` | `build-windows.yml:26-48` |
| Dependabot policy | Weekly GitHub Actions | `.github/dependabot.yml` | — |

---

**This inventory should be refreshed whenever workflow files, secrets, or external pins change in the sandbox.**
