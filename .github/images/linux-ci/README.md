# Linux CI images

WSJT-X publishes seven private, single-platform build environments to GHCR:

| Package | Platform | CI consumers |
| --- | --- | --- |
| `wsjtx-internal/linux-noble` | `linux/amd64` | Normal and ASan/UBSan |
| `wsjtx-internal/linux-tsan-noble` | `linux/amd64` | TSan |
| `wsjtx-internal/linux-arm64-bookworm` | `linux/arm64` | aarch64 |
| `wsjtx-internal/linux-armhf-cross-bookworm` | `linux/amd64` | Native armhf cross-build |
| `wsjtx-internal/linux-armv7-bookworm` | `linux/arm/v7` | armhf tests and packaging under QEMU |
| `wsjtx-internal/linux-armhf-cross-trixie` | `linux/amd64` | Native armhf Trixie cross-build |
| `wsjtx-internal/linux-armv7-trixie` | `linux/arm/v7` | armhf Trixie tests and packaging under QEMU |

The normal images contain the compiler, development and packaging closure, and
the slow-moving pFUnit and Hamlib prefixes. The TSan image instead contains the
pinned, instrumented Qt, Boost, and Hamlib tuple. ARMHF has two builder/runtime
pairs. Bookworm retains the custom GCC 13.4 toolchain, 32-bit `time_t`, and
glibc 2.36 ceiling. Trixie uses Debian GCC 14 cross packages, 64-bit `time_t`,
and a glibc 2.41 ceiling. Each native x86-64 builder uses its distro's Qt host
tools and ARMHF sysroot; its runtime partner contains the matching target
execution and packaging closure. `ARMHF_DISTRO=bookworm|trixie` selects the
profile, defaulting to Bookworm. Trixie C/C++ builds explicitly define
`_TIME_BITS=64` and `_FILE_OFFSET_BITS=64`, including static Hamlib.
WSJT-X objects and binaries are never baked into these images.

Official Linux, macOS, and Windows builds select FFTW 3.3.11 through
`.github/scripts/fftw-version.sh`. Linux and macOS build the checksum-pinned
source with their existing toolchains; Windows extracts a checksum-pinned MSYS2
package into a private dependency prefix. Single precision and pthread support
are required, with architecture-specific SIMD support. The linked runtime test
checks the selected version and executes a threaded transform. A matching source
version does not imply identical floating-point output across architectures.

AppImages, portable archives, macOS bundles, and Windows packages carry the
selected FFTW libraries. Native DEB and RPM packages retain their system FFTW
dependency, so their installed runtime follows the distribution's package.

The Ubuntu and GCC base tags intentionally follow their maintained upstream
security updates. Each published generation records the resolved package set,
compiler identity, dependency pins, and recipe fingerprint. Normal-image
promotion requires image checks for every selected environment and full x64 and
ARM64 WSJT-X builds using the exact immutable candidates. Those builds also publish
the candidate generation's ccache snapshots before `stable` moves. TSan publication
is an explicit image cohort and verifies its pinned closure during the image
build; the current code-level TSan suite is not part of image promotion.

## Publication

For a single-platform check, dispatch `publish-linux-ci-images.yml` with
`candidate_profile=x86_64|aarch64|armhf-bookworm|armhf-trixie`, `promote=false`,
`include_armhf=false`, and `image_set=normal`. Each ARMHF profile builds its
cross-builder followed by its matching runtime image. The resulting immutable
tag can then be passed to `build-linux.yml` with the matching architecture and
ARMHF distro for application tests and packaging. Broaden validation after that
build passes. `candidate_profile=all` retains the complete-cohort publication path.

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

Both ARMHF pairs are included when the cache-refresh workflow refreshes the
normal Linux image cohort. All four ARMHF images are built, promoted, and rolled
back in the same generation. Image manifests bind each pair's distro, time ABI,
compiler, sysroot package digest, and compiler runtime libraries. Compile/link
smokes assert the expected `time_t` width and exercise time functions under QEMU.
These image checks precede promotion; full ARMHF application tests and packaging
run in full CI and releases. ARMHF cache warming follows successful promotion.

Opt-in full-ci, release, and standalone armhf builds resolve the last promoted
Bookworm runtime generation once and require both complete distro pairs; a missing
member fails explicitly. Manual full-ci can instead name an immutable candidate
generation for pre-promotion acceptance testing.

Each complete ARMHF build creates distro-qualified DEB, RPM, AppImage, and CLI
archives. Before uploading them, CI installs the DEB in a fresh matching stock
Debian ARMHF container using that distro's repositories, then checks ELF
dependencies and runs the bounded startup smoke under Xvfb. The check has no
build tree, sysroot, or private compiler-runtime prefix and clears library/plugin
overrides. It catches dependencies masked by the packaging image; it does not
replace physical-device audio or rig testing.

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

## First dual-distro release rollout

1. Land the recipes and consumers together on the protected source branch.
   Publish the normal image cohort with `include_armhf: true`, producing all six
   non-TSan images. Promotion requires the image checks and existing x64/ARM64
   application gates; ARMHF warming occurs afterward and does not gate promotion.
2. Run full CI against the exact new immutable generation. Require both ARMHF
   distro builds, runtime tests, artifact checks, and clean DEB installation to
   pass before preparing the release dependency selection.
3. Run `prepare-release-dependencies.yml` from protected `develop`, selecting the
   protected source branch and its matching `validated-build-*` generation.
   Review and commit the generated schema-2 `release-linux-images.json`, containing
   six real image digests, before creating a release candidate. Preparation verifies
   and copies the images; it does not manufacture a lock before publication.

Historical protected source branches use their own preparation helper and retain
their original four-image contract. New source requires both ARMHF pairs and a
six-image lock. The beta scheduler defers source revisions that predate the new
profiles. Already-published releases retain their original manifests and assets.

## ARMHF toolchain reuse

The Bookworm cross-builder has an independent toolchain stage pinned to a
Debian Bookworm amd64 base digest, which monthly refreshes do not update. Its inputs are the
bootstrap package recipe, apt helper, `armhf-toolchain-config.sh`, and
`armhf-cross-toolchain.config`. The final image
uses the maintained Bookworm base and refreshes host packages, the ARMHF sysroot,
and Hamlib independently. Monthly refresh epochs and full image fingerprints do
not invalidate the toolchain stage.

The Trixie recipe installs Debian's versioned GCC 14 C, C++, and Fortran cross
packages, preserving the shared compiler-path interface through symlinks. Both
recipes use the same sysroot/Hamlib preparation helper. Each runtime image copies
its own builder's compiler libraries and a dereferenced static QEMU executable;
mixing Bookworm and Trixie image halves is rejected.

The ARMHF cross-builder exports intermediate layers to the `buildcache` tag in
its own GHCR package with registry `mode=max` caching. Internal and public-release
publication use their respective package namespaces for both imports and exports.
A fresh builder can therefore reuse the completed toolchain stage. Only source
archives use a local BuildKit cache mount; each actual toolchain build starts with
a clean build directory and retains its detailed failure log in the job output.

For an intentional toolchain refresh, update the pinned base digest when needed
and increment `ARMHF_TOOLCHAIN_REBUILD_REVISION` in `armhf-toolchain-config.sh`.
The revision invalidates the bootstrap package installation as well as compiler
construction. Compiler, ABI, crosstool-NG, and ct-ng configuration changes also
invalidate the relevant toolchain layers. Publish with `include_armhf: true` and
validate the resulting image pair before promoting it. Final image manifests
and compiler signatures are regenerated against the final host runtime libraries;
reusing compiler files does not bypass compatibility checks.

To validate reuse, export a baseline cache and import it into a fresh Buildx
builder after changing only the refresh epoch or a downstream dependency input.
The ct-ng build must be cached while the dependency work reruns. A changed ct-ng
configuration or rebuild revision must execute the toolchain build again. Keep
these validation publications separate from stable promotion and retain their
build logs, image verification results, and timings.

## ccache compatibility

The image recipe and ccache compatibility identities intentionally answer
different questions. The recipe and legacy `toolchain_id` describe the complete
image for verification and provenance. Actions-cache keys instead use the
architecture, ARMHF distro or sanitizer profile when applicable, a `gcc<major>-v2`
compatibility ID, and the immutable image generation.

Within that archive, ccache uses a verified content signature covering the C
and C++ drivers, GCC frontends and LTO executable, assembler, their resolved
runtime-library closure, the target, and GCC specs. Compiler changes therefore
miss safely inside the compatible archive.
Source, flags, generated sources, and included header contents retain ccache's
normal per-object invalidation. Changes to unrelated image packages do not
discard otherwise compatible objects.

A new generation first restores its own snapshot and then falls back to any
compatible older generation. The build recompiles only genuine misses and
saves an augmented generation-specific snapshot. Recipe-stale fallback builds
may restore caches but are not allowed to save them. ARMHF save keys and every
restore prefix include the distro, so Bookworm and Trixie never share archives.

ARMHF warming compiles production and test targets and runs the binary audits,
then saves objects without preparing an AppDir or executing runtime tests and
packaging. Image-pair verification, including its brief QEMU checks, remains
required. The `armhf_build_only` workflow input defaults to false, preserving
full ARMHF CI; build-only cache writes require `develop`.

The cache-refresh workflow selects ARMHF weekly, for relevant build/configuration
changes on `develop`, and through its `linux-armhf` manual target. Source-only
pushes do not select it. Successful internal ARMHF image promotion warms both
distro builds for the exact new generation afterward; a warmer failure does not
undo promotion.
Standalone and post-promotion warmers share a concurrency group, and an image
refresh suppresses duplicate standalone warming.

## Retention

The weekly retention job keeps `stable`, preserves unrecognized and untagged
versions, retains at least the newest three recognized package versions, and
deletes only recognized build, validated-build, and candidate versions older
than 60 days. Manual retention runs are dry-run only; scheduled runs apply the
printed plan. The ARMHF `buildcache` tag is protected as an unrecognized tag;
older cache manifests left untagged when it moves are also preserved.

The Buildx jobs deliberately disable provenance and SBOM attachment. Even a
single-platform attestation is represented through an image index with
untagged child manifests, which cannot be safely classified from GitHub's
package-version REST response alone.
