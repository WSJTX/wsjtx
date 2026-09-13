#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: build-hamlib-armhf-cross.sh COMMIT SYSROOT TOOLCHAIN_PREFIX" >&2
  exit 2
fi

commit=$1
sysroot=$2
toolchain_prefix=$3
triplet=arm-linux-gnueabihf
target_include="$sysroot/usr/include/$triplet"
target_lib="$sysroot/usr/lib/$triplet"
target_base_lib="$sysroot/lib/$triplet"

export PATH="$toolchain_prefix/bin:$PATH"
export PKG_CONFIG_SYSROOT_DIR="$sysroot"
export PKG_CONFIG_PATH=
export PKG_CONFIG_LIBDIR="$target_lib/pkgconfig:$sysroot/usr/share/pkgconfig"

git init hamlib-src
git -C hamlib-src remote add origin https://github.com/Hamlib/Hamlib.git
git -C hamlib-src fetch --depth 1 origin "$commit"
git -C hamlib-src checkout --detach FETCH_HEAD
actual_commit="$(git -C hamlib-src rev-parse HEAD)"
if [ "$actual_commit" != "$commit" ]; then
  echo "Hamlib source mismatch: expected $commit, got $actual_commit" >&2
  exit 1
fi

cd hamlib-src
./bootstrap
./configure \
  --build=x86_64-pc-linux-gnu \
  --host="$triplet" \
  --prefix=/usr \
  --disable-shared --enable-static \
  --without-cxx-binding \
  --without-readline \
  --without-indi \
  CC="${triplet}-gcc" \
  CXX="${triplet}-g++" \
  AR="${triplet}-ar" \
  NM="${triplet}-nm" \
  OBJDUMP="${triplet}-objdump" \
  RANLIB="${triplet}-ranlib" \
  STRIP="${triplet}-strip" \
  CFLAGS="--sysroot=$sysroot -isystem $target_include -g -O2 -fPIC -fdata-sections -ffunction-sections" \
  CXXFLAGS="--sysroot=$sysroot -isystem $target_include -g -O2 -fPIC -fdata-sections -ffunction-sections" \
  LDFLAGS="--sysroot=$sysroot -B$target_lib/ -L$target_lib -Wl,-rpath-link,$target_lib -Wl,-rpath-link,$target_base_lib -Wl,--gc-sections"
make -j"$(nproc)"
make DESTDIR="$sysroot" install

for utility in rigctl rigctld rigctlcom; do
  metadata="$(file "$sysroot/usr/bin/$utility")"
  case "$metadata" in
    *"ELF 32-bit"*"ARM, EABI5"*) ;;
    *)
      echo "Cross-built Hamlib utility has the wrong architecture: $metadata" >&2
      exit 1
      ;;
  esac
done
test -f "$sysroot/usr/lib/libhamlib.a"
