# Linux CI images

WSJT-X publishes five private, single-platform build environments to GHCR:

| Package | Platform | CI consumers |
| --- | --- | --- |
| `wsjtx-internal/linux-noble` | `linux/amd64` | Normal and ASan/UBSan |
| `wsjtx-internal/linux-tsan-noble` | `linux/amd64` | TSan |
| `wsjtx-internal/linux-arm64-bookworm` | `linux/arm64` | aarch64 |
| `wsjtx-internal/linux-armhf-cross-bookworm` | `linux/amd64` | Native armhf cross-build |
| `wsjtx-internal/linux-armv7-bookworm` | `linux/arm/v7` | armhf tests and packaging under QEMU |

The normal images contain the compiler, development and packaging closure, and
the slow-moving pFUnit and Hamlib prefixes. The TSan image instead contains the
pinned, instrumented Qt, Boost, and Hamlib tuple. The armhf cross-builder pairs
a native x86-64 GCC 13.4 cross-toolchain with a Debian Bookworm armhf sysroot;
its runtime partner contains only the target execution and packaging closure.
WSJT-X objects and binaries are never baked into these images.

The Ubuntu and GCC base tags intentionally follow their maintained upstream
security updates. Each published generation records the resolved package set,
compiler identity, dependency pins, and recipe fingerprint. Normal-image
promotion is gated by image construction, the image-level checks, and a full
WSJT-X build using the exact immutable candidate. That build also publishes the
candidate generation's ccache snapshot before `stable` moves. TSan publication
is an explicit image cohort and verifies its pinned closure during the image
build; the current code-level TSan suite is not part of image promotion.

## Publication

`publish-linux-ci-images.yml` pushes unique candidate and immutable `build-*`
tags for the selected image cohort. Each Docker build verifies the image
manifest, dependency metadata, and a representative compile/link smoke test.
Before a normal candidate is promoted, the standard x64 WSJT-X build and test
pipeline runs against its exact `build-*` tag and saves a generation-specific
ccache archive. This prevents a new image from becoming stable without a cache
that pull requests can restore. Successful generations also receive
`validated-build-*` tags. The secondary normal-image `stable` tags move first
and `linux-noble:stable` moves last within that cohort. Consumers resolve that
pointer to its immutable `build-*` tag and use the same generation for every
normal Linux leg.

The armhf image pair is published explicitly with `include_armhf: true`. It is
excluded from routine weekly/monthly refreshes because toolchain construction
is expensive. The pair is built, promoted, and rolled back as one generation.
Opt-in full-ci, release, and standalone armhf builds resolve the last promoted
runtime generation once and require the matching cross-builder tag; a missing
member fails explicitly. Manual full-ci can instead name an immutable candidate
generation for pre-promotion acceptance testing.

The TSan workflow resolves `linux-tsan-noble:stable` independently when its
optional label is used. A missing TSan generation fails explicitly instead of
silently using the normal image. A TSan-only publication builds, promotes, or
rolls back only that package; it does not rebuild or retag normal or ARM images.

Ordinary non-TSan PR and `develop` jobs may overlap a dependency refresh. They
pin the image generation resolved at job start and allow a recipe-fingerprint
mismatch with a warning, so the last known-good stable generation remains a
usable fallback while the refresh completes. Image publication, release, and
TSan jobs keep strict fingerprint checks; if no promoted generation exists, the
resolver still fails explicitly.

The dependency refresh workflow runs image publication monthly and when image
recipes change on `develop`. Changed paths are classified from the same profile
membership used to calculate the recipe fingerprints, so normal-only and
TSan-only inputs refresh only their affected cohort while shared inputs refresh
both. The monthly refresh includes TSan; weekly and `all` refreshes omit it.
The `linux-tsan` manual target selects only TSan, while `linux` and
`linux-images` select normal and ARM64 without armhf. Use the direct publisher
workflow when explicit armhf validation is needed. A monthly epoch invalidates the
package-install layer so repository security updates are not hidden by
BuildKit's cache. The direct publisher workflow also supports
retagging a retained `validated-build-*` generation as `stable` for rollback.
Promotion and rollback are restricted to `develop`; non-promoting candidate
builds may be run from another ref.

## ccache compatibility

The image recipe and ccache compatibility identities intentionally answer
different questions. The recipe and legacy `toolchain_id` describe the complete
image for verification and provenance. Actions-cache keys instead use the
architecture, sanitizer profile when applicable, a `gcc<major>-v2`
compatibility ID, and the immutable image generation.

Within that archive, ccache uses a verified content signature covering the C
and C++ drivers, GCC frontends and LTO executable, assembler, their resolved
runtime-library closure, the target, and GCC specs. Compiler changes therefore
miss safely inside the compatible archive.
Source, flags, generated sources, and included header contents retain ccache's
normal per-object invalidation. Changes to unrelated image packages do not
discard otherwise compatible objects.

A new generation first restores its own snapshot and then falls back to any
compatible older generation. The candidate build recompiles only genuine
misses and saves an augmented generation-specific snapshot before promotion.
Recipe-stale fallback builds may restore caches but are not allowed to save
them.

## Retention

The weekly retention job keeps `stable`, preserves unrecognized and untagged
versions, retains at least the newest three recognized package versions, and
deletes only recognized build, validated-build, and candidate versions older
than 60 days. Manual retention runs are dry-run only; scheduled runs apply the
printed plan.

The Buildx jobs deliberately disable provenance and SBOM attachment. Even a
single-platform attestation is represented through an image index with
untagged child manifests, which cannot be safely classified from GitHub's
package-version REST response alone.
