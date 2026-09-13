set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7l)

set(WSJT_ARMHF_TOOLCHAIN_PREFIX "/opt/wsjtx/armhf-toolchain"
    CACHE PATH "ARMHF cross-toolchain prefix")
set(WSJT_ARMHF_SYSROOT "/opt/wsjtx/armhf-sysroot"
    CACHE PATH "Debian Bookworm ARMHF sysroot")
set(WSJT_QT_HOST_PATH "/usr/lib/qt5/bin"
    CACHE PATH "Native Qt 5 tools used while cross-compiling")
set(WSJT_QT_TARGET_TRANSLATIONS_DIR
    "${WSJT_ARMHF_SYSROOT}/usr/share/qt5/translations"
    CACHE PATH "Qt 5 target translations directory")

set(_wsjt_armhf_triplet arm-linux-gnueabihf)
set(_wsjt_armhf_bin "${WSJT_ARMHF_TOOLCHAIN_PREFIX}/bin")
set(CMAKE_C_COMPILER "${_wsjt_armhf_bin}/${_wsjt_armhf_triplet}-gcc")
set(CMAKE_CXX_COMPILER "${_wsjt_armhf_bin}/${_wsjt_armhf_triplet}-g++")
set(CMAKE_Fortran_COMPILER "${_wsjt_armhf_bin}/${_wsjt_armhf_triplet}-gfortran")
set(CMAKE_AR "${_wsjt_armhf_bin}/${_wsjt_armhf_triplet}-ar")
set(CMAKE_NM "${_wsjt_armhf_bin}/${_wsjt_armhf_triplet}-nm")
set(CMAKE_OBJCOPY "${_wsjt_armhf_bin}/${_wsjt_armhf_triplet}-objcopy")
set(CMAKE_OBJDUMP "${_wsjt_armhf_bin}/${_wsjt_armhf_triplet}-objdump")
set(CMAKE_RANLIB "${_wsjt_armhf_bin}/${_wsjt_armhf_triplet}-ranlib")
# GNU readelf is architecture-independent, and this stable path exists in
# both the builder and ARMv7 runtime containers for CTest commands.
set(CMAKE_READELF "/usr/bin/readelf")
set(CMAKE_STRIP "${_wsjt_armhf_bin}/${_wsjt_armhf_triplet}-strip")

set(CMAKE_SYSROOT "${WSJT_ARMHF_SYSROOT}")
set(CMAKE_LIBRARY_ARCHITECTURE "${_wsjt_armhf_triplet}")
set(CMAKE_FIND_ROOT_PATH "${WSJT_ARMHF_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# crosstool-NG uses a conventional sysroot layout. Debian keeps ARMHF
# headers and start files in triplet-qualified directories instead.
set(_wsjt_armhf_include "${WSJT_ARMHF_SYSROOT}/usr/include/${_wsjt_armhf_triplet}")
set(_wsjt_armhf_lib "${WSJT_ARMHF_SYSROOT}/usr/lib/${_wsjt_armhf_triplet}")
set(_wsjt_armhf_base_lib "${WSJT_ARMHF_SYSROOT}/lib/${_wsjt_armhf_triplet}")
set(CMAKE_C_FLAGS_INIT "-isystem ${_wsjt_armhf_include}")
set(CMAKE_CXX_FLAGS_INIT "-isystem ${_wsjt_armhf_include}")
set(_wsjt_armhf_link_flags
    "-B${_wsjt_armhf_lib}/ -L${_wsjt_armhf_lib} -Wl,-rpath-link,${_wsjt_armhf_lib} -Wl,-rpath-link,${_wsjt_armhf_base_lib}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_wsjt_armhf_link_flags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_wsjt_armhf_link_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_wsjt_armhf_link_flags}")

set(ENV{PKG_CONFIG_SYSROOT_DIR} "${WSJT_ARMHF_SYSROOT}")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_LIBDIR}
    "${_wsjt_armhf_lib}/pkgconfig:${WSJT_ARMHF_SYSROOT}/usr/share/pkgconfig")

# These are target programs installed from the cross-built Hamlib staging tree.
set(RIGCTL_EXE "${WSJT_ARMHF_SYSROOT}/usr/bin/rigctl" CACHE FILEPATH "" FORCE)
set(RIGCTLD_EXE "${WSJT_ARMHF_SYSROOT}/usr/bin/rigctld" CACHE FILEPATH "" FORCE)
set(RIGCTLCOM_EXE "${WSJT_ARMHF_SYSROOT}/usr/bin/rigctlcom" CACHE FILEPATH "" FORCE)
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE armhf CACHE STRING "" FORCE)
set(CPACK_RPM_PACKAGE_ARCHITECTURE armv7l CACHE STRING "" FORCE)
