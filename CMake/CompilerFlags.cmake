#
# Compiler/linker flags, Fortran-C interop, RPATH, and qmake queries
# -- included from the top-level CMakeLists.txt at the equivalent
# point. Included (not add_subdirectory'd), so CMAKE_CURRENT_SOURCE_DIR
# stays the project root; depends on package/compiler results from
# CMake/Dependencies.cmake, included immediately before this.
#
#
# C & C++ setup
#
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror -Wall -Wextra -fexceptions -frtti")

if (NOT APPLE)
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-pragmas")
  if (${OPENMP_FOUND})
    if (OpenMP_C_FLAGS)
      set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_C_FLAGS}")
      set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    endif ()
  endif ()
  set (CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fdata-sections -ffunction-sections")
  set (CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -fdata-sections -ffunction-sections")
  set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fdata-sections -ffunction-sections")
  set (CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -fdata-sections -ffunction-sections")
endif (NOT APPLE)

if (WIN32)
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
endif (WIN32)

if (APPLE AND ${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
else ()
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif ()


#
# Fortran setup
#
set (General_FFLAGS "-Wall -Wno-conversion -Wno-c-binding-type -fno-second-underscore")

# FFLAGS depend on the compiler
get_filename_component (Fortran_COMPILER_NAME ${CMAKE_Fortran_COMPILER} NAME)

if (Fortran_COMPILER_NAME MATCHES "gfortran.*")
  # gfortran

  # CMake compiler test is supposed to do this but doesn't yet
  if (CMAKE_OSX_DEPLOYMENT_TARGET)
    set (CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif (CMAKE_OSX_DEPLOYMENT_TARGET)
  if (CMAKE_OSX_SYSROOT)
    set (CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -isysroot ${CMAKE_OSX_SYSROOT}")
  endif (CMAKE_OSX_SYSROOT)

  # Add assembler flag to disable executable stack
  if (UNIX AND NOT APPLE AND Fortran_COMPILER_NAME MATCHES "gfortran.*")
     set (CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -Wa,--noexecstack")
  endif()

  set (CMAKE_Fortran_FLAGS_RELEASE "${CMAKE_Fortran_FLAGS_RELEASE} -funroll-loops -fno-f2c -ffpe-summary=invalid,zero,overflow,underflow ${General_FFLAGS}")

  set (CMAKE_Fortran_FLAGS_DEBUG   "${CMAKE_Fortran_FLAGS_DEBUG} -ggdb -O0 -fbacktrace -fcheck=all -fbounds-check -fno-f2c -ffpe-summary=invalid,zero,overflow,underflow ${General_FFLAGS}")

  # FPE traps currently disabled in Debug configuration builds until
  # we decide if they are meaningful, without these FP instructions
  # run in nonstop mode and do not trap
  #set (CMAKE_Fortran_FLAGS_DEBUG   "${CMAKE_Fortran_FLAGS_DEBUG} ${CMAKE_Fortran_FLAGS_DEBUG}  -ffpe-trap=invalid,zero,overflow")

elseif (Fortran_COMPILER_NAME MATCHES "ifort.*")
  # ifort (untested)
  set (CMAKE_Fortran_FLAGS_RELEASE "${CMAKE_Fortran_FLAGS_RELEASE} -f77rtl ${General_FFLAGS}")
  set (CMAKE_Fortran_FLAGS_DEBUG   "${CMAKE_Fortran_FLAGS_DEBUG} -f77rtl ${General_FFLAGS}")
elseif (Fortran_COMPILER_NAME MATCHES "g77")
  # g77
  set (CMAKE_Fortran_FLAGS_RELEASE "${CMAKE_Fortran_FLAGS_RELEASE} -funroll-all-loops -fno-f2c -m32 ${General_FFLAGS}")
  set (CMAKE_Fortran_FLAGS_DEBUG   "${CMAKE_Fortran_FLAGS_DEBUG} -fbounds-check -fno-f2c -m32 ${General_FFLAGS}")

else()
  message ("CMAKE_Fortran_COMPILER full path: ${CMAKE_Fortran_COMPILER}")
  message ("Fortran compiler: ${Fortran_COMPILER_NAME}")
  message ("No optimized Fortran compiler flags are known, we just try -O3...")
  set (CMAKE_Fortran_FLAGS_RELEASE "${CMAKE_Fortran_FLAGS_RELEASE} -O3 ${General_FFLAGS}")
  set (CMAKE_Fortran_FLAGS_DEBUG   "${CMAKE_Fortran_FLAGS_DEBUG} -g -fbacktrace -fcheck=all -fbounds-check ${General_FFLAGS}")
endif()

#
# Linker setup
#
if (NOT APPLE)
  set (CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,--gc-sections")
  set (CMAKE_EXE_LINKER_FLAGS_MINSIZEREL "${CMAKE_EXE_LINKER_FLAGS_MINSIZEREL} -Wl,--gc-sections")
endif (NOT APPLE)


#
# setup and test Fortran C/C++ interaction
#

include (FortranCInterface)
FortranCInterface_VERIFY (CXX)
FortranCInterface_HEADER (FC.h MACRO_NAMESPACE "FC_" SYMBOL_NAMESPACE "FC_"
  SYMBOLS
  grayline
  )

if (WSJT_ENABLE_ASAN_UBSAN)
  if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message (FATAL_ERROR "WSJT_ENABLE_ASAN_UBSAN is supported only on Linux.")
  endif ()
  if (NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR
      NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
      NOT CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
    message (FATAL_ERROR
      "WSJT_ENABLE_ASAN_UBSAN requires GNU C, C++, and Fortran compilers.")
  endif ()

  include (CheckCCompilerFlag)
  include (CheckCXXCompilerFlag)
  include (CheckFortranCompilerFlag)
  set (_wsjt_sanitizer_probe_flags
    "-fsanitize=address,undefined;-fno-sanitize-recover=all")
  set (_wsjt_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
  list (APPEND CMAKE_REQUIRED_LIBRARIES "-fsanitize=address,undefined")
  check_c_compiler_flag (
    "${_wsjt_sanitizer_probe_flags}" WSJT_C_ASAN_UBSAN_SUPPORTED)
  check_cxx_compiler_flag (
    "${_wsjt_sanitizer_probe_flags}" WSJT_CXX_ASAN_UBSAN_SUPPORTED)
  check_fortran_compiler_flag (
    "${_wsjt_sanitizer_probe_flags}" WSJT_FORTRAN_ASAN_UBSAN_SUPPORTED)
  set (CMAKE_REQUIRED_LIBRARIES "${_wsjt_saved_required_libraries}")
  if (NOT WSJT_C_ASAN_UBSAN_SUPPORTED OR
      NOT WSJT_CXX_ASAN_UBSAN_SUPPORTED OR
      NOT WSJT_FORTRAN_ASAN_UBSAN_SUPPORTED)
    message (FATAL_ERROR
      "The selected GNU toolchain cannot compile and link ASan+UBSan code in every project language.")
  endif ()

  add_library (wsjt_sanitizers INTERFACE)
  target_compile_options (wsjt_sanitizers INTERFACE
    $<$<COMPILE_LANGUAGE:C>:-fsanitize=address,undefined>
    $<$<COMPILE_LANGUAGE:CXX>:-fsanitize=address,undefined>
    $<$<COMPILE_LANGUAGE:Fortran>:-fsanitize=address,undefined>
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
    -g
    -O1
    )
  target_link_libraries (wsjt_sanitizers INTERFACE
    "-fsanitize=address,undefined")
  link_libraries (wsjt_sanitizers)
  message (STATUS "AddressSanitizer and UndefinedBehaviorSanitizer enabled")
endif ()


#
# sort out pre-requisites
#

#
# Setup RPATH so that built executable targets will run in both the
# build tree and the install location without having to set a
# (DYLD|LD)_LIBRARY_PATH override.
#

# use the full RPATH of the build tree
set (CMAKE_SKIP_BUILD_RPATH FALSE)

# when building, don't use the install RPATH, it will still be used
# later on in the install phase
set (CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

# set (CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

# add the automaticaly determined parts of the RPATH which point to
# directories outside of the build tree to the install RPATH
set (CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# the RPATH to be used when installing, but only if it's not a system
# directory
# list (FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}" isSystemDir)
# if ("${isSystemDir}" STREQUAL "-1")
#   set (CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
# endif ("${isSystemDir}" STREQUAL "-1")

set (QT_NEED_RPATH FALSE)
if (NOT "${QT_LIBRARY_DIR}" STREQUAL "/lib" AND NOT "${QT_LIBRARY_DIR}" STREQUAL "/usr/lib" AND NOT "${QT_LIBRARY_DIR}" STREQUAL "/lib64" AND NOT "${QT_LIBRARY_DIR}" STREQUAL "/usr/lib64")
  set (QT_NEED_RPATH TRUE)
endif ()

#
# stuff only qmake can tell us
#
get_target_property (QMAKE_EXECUTABLE Qt5::qmake LOCATION)
get_target_property (LCONVERT_EXECUTABLE Qt5::lconvert LOCATION)
function (QUERY_QMAKE VAR RESULT)
  exec_program (${QMAKE_EXECUTABLE} ARGS "-query ${VAR}" RETURN_VALUE return_code OUTPUT_VARIABLE output)
  if (NOT return_code)
    file (TO_CMAKE_PATH "${output}" output)
    set (${RESULT} ${output} PARENT_SCOPE)
  endif (NOT return_code)
  message (STATUS "Asking qmake for ${RESULT} and got ${output}")
endfunction (QUERY_QMAKE)

query_qmake (QT_INSTALL_PLUGINS QT_PLUGINS_DIR)
query_qmake (QT_INSTALL_TRANSLATIONS QT_TRANSLATIONS_DIR)
query_qmake (QT_INSTALL_IMPORTS QT_IMPORTS_DIR)
query_qmake (QT_HOST_DATA QT_DATA_DIR)
set (QT_MKSPECS_DIR ${QT_DATA_DIR}/mkspecs)
