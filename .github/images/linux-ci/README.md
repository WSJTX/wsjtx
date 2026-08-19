# Linux CI images

WSJT-X publishes four private, single-platform build environments to GHCR:

| Package | Platform | CI consumers |
| --- | --- | --- |
| `wsjtx-internal/linux-noble` | `linux/amd64` | Normal and ASan/UBSan |
| `wsjtx-internal/linux-tsan-noble` | `linux/amd64` | TSan |
| `wsjtx-internal/linux-arm64-bookworm` | `linux/arm64` | aarch64 |
| `wsjtx-internal/linux-armv7-bookworm` | `linux/arm/v7` | armhf under QEMU |

The images contain the compiler, development and packaging closure, and the
slow-moving pFUnit and Hamlib prefixes. The TSan image instead contains the
pinned, instrumented Qt, Boost, and Hamlib tuple. WSJT-X objects and binaries
are never baked into these images.

The Ubuntu and GCC base tags intentionally follow their maintained upstream
security updates. Each published generation records the resolved package set,
compiler identity, dependency pins, and recipe fingerprint. Promotion is gated
by image construction and lightweight manifest, toolchain, metadata, plugin,
and compile/link checks. Full application pipelines remain useful consumer
smoke tests but do not block promotion. The TSan dependency image is an
explicit `include_tsan: true` refresh option and verifies its pinned closure
during the image build; the current code-level TSan suite is not part of image
promotion.

## Publication

`publish-linux-ci-images.yml` pushes unique candidate and immutable `build-*`
tags for the three routinely used packages. Each Docker build verifies the
image manifest, dependency metadata, and a representative compile/link smoke
test before promotion. Full normal, ASan/UBSan, aarch64, and TSan application
pipelines remain separate consumer checks in ordinary CI and do not gate image
publication. The TSan image's dependency verification runs during its image
build. Successful generations
also receive `validated-build-*` tags. The secondary `stable` tags move
first and `linux-noble:stable` moves last as the generation pointer. Consumers
resolve that pointer to its immutable `build-*` tag and use the same generation
for every Linux leg.

The armhf image remains available for explicit validation with
`include_armhf: true`. It is excluded from routine weekly/monthly refreshes
because its QEMU build and validation cost is disproportionate to current use.
Release and standalone armhf builds resolve the last promoted armhf generation
once and use that immutable tag; a missing armhf generation fails explicitly.

The TSan workflow resolves `linux-tsan-noble:stable` independently when its
optional label is used. A missing TSan generation fails explicitly instead of
silently using the normal image.

The dependency refresh workflow runs image publication monthly and when image
recipes change on `develop`. The monthly refresh includes TSan; weekly and
`all` refreshes omit it unless the explicit `linux-tsan` manual target is
selected. Its `linux` and `linux-images` manual targets run the normal and
arm64 refresh without armhf or TSan; use the direct publisher workflow when an
explicit armhf validation is needed. A monthly epoch invalidates the
package-install layer so repository security updates are not hidden by
BuildKit's cache. The direct publisher workflow also supports
retagging a retained `validated-build-*` generation as `stable` for rollback.

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
