#!/usr/bin/env bash

# Shared dependency tuple embedded in the normal and ARM Linux CI images.
# shellcheck disable=SC2034

LINUX_CI_IMAGE_SCHEMA=1


LINUX_HAMLIB_REF=4.7.2
LINUX_HAMLIB_COMMIT=40f63488fe0bd751b147f48d62fd217bf53713a0

LINUX_CI_PREFIX=/opt/wsjtx
LINUX_HAMLIB_PREFIX=${LINUX_CI_PREFIX}/hamlib
LINUX_FFTW_PREFIX=${LINUX_CI_PREFIX}/fftw

# shellcheck source=.github/scripts/fftw-version.sh
. "$(dirname "${BASH_SOURCE[0]}")/fftw-version.sh"
