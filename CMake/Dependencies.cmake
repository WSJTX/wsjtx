#
# Dependency detection -- included from the top-level CMakeLists.txt
# at the equivalent point. Included (not add_subdirectory'd), so
# CMAKE_CURRENT_SOURCE_DIR stays the project root and results (found
# packages, RIGCTL*_EXE, LIBM_LIBRARIES, etc.) remain visible to the
# rest of the top-level file exactly as when this was inline.
#
#
# find some useful tools
#
include (CheckTypeSize)
include (CheckCSourceCompiles)
include (CheckIncludeFiles)
include (CheckSymbolExists)
include (generate_version_info)

find_program(CTAGS ctags)
find_program(ETAGS etags)

#
# Platform checks
#
check_include_files ("stdlib.h;stdarg.h;string.h;float.h" STDC_HEADERS)
check_include_files (stdio.h HAVE_STDIO_H)
check_include_files (stdlib.h HAVE_STDLIB_H)
check_include_files (unistd.h HAVE_UNISTD_H)
check_include_files (sys/ioctl.h HAVE_SYS_IOCTL_H)
check_include_files (sys/types.h HAVE_SYS_TYPES_H)
check_include_files (fcntl.h HAVE_FCNTL_H)
check_include_files (sys/stat.h HAVE_SYS_STAT_H)
check_include_files ("linux/ppdev.h;linux/parport.h" HAVE_LINUX_PPDEV_H)
check_include_files ("dev/ppbus/ppi.h;dev/ppbus/ppbconf.h" HAVE_DEV_PPBUS_PPI_H)

#
# Standard C Math Library
#
set (LIBM_TEST_SOURCE "#include<math.h>\nfloat f; int main(){sqrt(f);return 0;}")
check_c_source_compiles ("${LIBM_TEST_SOURCE}" HAVE_MATH)
if (HAVE_MATH)
  set (LIBM_LIBRARIES)
else ()
  set (CMAKE_REQUIRED_LIBRARIES m)
  check_c_source_compiles ("${LIBM_TEST_SOURCE}" HAVE_LIBM_MATH)
  unset (CMAKE_REQUIRED_LIBRARIES)
  if (NOT HAVE_LIBM_MATH)
    message (FATAL_ERROR "Unable to use C math library functions")
  endif ()
  set (LIBM_LIBRARIES m)
endif ()

#
# Boost
#
if (WIN32)
  set (Boost_USE_STATIC_LIBS OFF)
endif ()
find_package (Boost 1.62 REQUIRED COMPONENTS log_setup log)

#
# OpenMP
#
find_package (OpenMP)

if (APPLE)
  set (WSJT_MACOS_LIBGOMP_LIBRARY "" CACHE FILEPATH "Path to the static GNU OpenMP runtime archive")
  if (NOT WSJT_MACOS_LIBGOMP_LIBRARY)
    execute_process (
      COMMAND ${CMAKE_Fortran_COMPILER} -print-file-name=libgomp.a
      OUTPUT_VARIABLE WSJT_MACOS_LIBGOMP_LIBRARY
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      )
  endif ()
  if (NOT WSJT_MACOS_LIBGOMP_LIBRARY OR NOT EXISTS "${WSJT_MACOS_LIBGOMP_LIBRARY}")
    message (FATAL_ERROR "Static GNU OpenMP runtime archive not found. Set WSJT_MACOS_LIBGOMP_LIBRARY to libgomp.a for deployable macOS builds.")
  endif ()
endif ()

#
# fftw3 single precision library
#
find_package (FFTW3 COMPONENTS single threads REQUIRED)

#
# hamlib setup
#
find_package (Hamlib REQUIRED)
find_program (RIGCTL_EXE rigctl)
find_program (RIGCTLD_EXE rigctld)
find_program (RIGCTLCOM_EXE rigctlcom)

check_type_size (CACHE_ALL HAMLIB_OLD_CACHING)
check_symbol_exists (rig_set_cache_timeout_ms "hamlib/rig.h" HAVE_HAMLIB_CACHING)
set (CMAKE_REQUIRED_INCLUDES ${Hamlib_INCLUDE_DIR})
check_c_source_compiles ("
#include <hamlib/rig.h>
int main(void) { return sizeof(&rig_get_conf2) > 0 ? 0 : 1; }
" HAVE_HAMLIB_GET_CONF2)
unset (CMAKE_REQUIRED_INCLUDES)

find_package (Usb REQUIRED)

if (WSJT_FOX_OTP)
  add_definitions (-DFOX_OTP)
endif ()

#
# Qt5 setup
#

# Widgets finds its own dependencies.
find_package (Qt5 COMPONENTS Widgets SerialPort Multimedia PrintSupport Sql LinguistTools WebSockets REQUIRED)

if (WIN32)
  add_definitions (-DQT_NEEDS_QTMAIN)
  find_package (Qt5AxContainer REQUIRED)
endif (WIN32)

#
# Library building setup
#
include (GenerateExportHeader)
set (CMAKE_CXX_VISIBILITY_PRESET hidden)
set (CMAKE_C_VISIBILITY_PRESET hidden)
set (CMAKE_Fortran_VISIBILITY_PRESET hidden)
set (CMAKE_VISIBILITY_INLINES_HIDDEN ON)
#set (CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE ON)

