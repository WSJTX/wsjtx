# - Try to find portaudio
#
# Once done, this will define:
#
#  portaudio_FOUND - system has portaudio
#  portaudio_INCLUDE_DIRS - the portaudio include directories
#  portaudio_LIBRARIES - link these to use portaudio
#  portaudio_LIBRARY_DIRS - required shared/dynamic libraries are here
#
# If portaudio_STATIC is TRUE then static linking will be assumed
#

include (LibFindMacros)

set (portaudio_LIBRARY_DIRS)

# pkg-config?
find_path (__portaudio_pc_path NAMES portaudio-2.0.pc
  PATH_SUFFIXES lib/pkgconfig lib64/pkgconfig
  )
if (__portaudio_pc_path)
  set (__pc_path $ENV{PKG_CONFIG_PATH})
  list (APPEND __pc_path "${__portaudio_pc_path}")
  set (ENV{PKG_CONFIG_PATH} "${__pc_path}")
  unset (__pc_path CACHE)
endif ()
unset (__portaudio_pc_path CACHE)

# Use pkg-config to get hints about paths, libs and, flags
unset (__pkg_config_checked_hamlib CACHE)
libfind_pkg_check_modules (PORTAUDIO portaudio-2.0)

if (portaudio_STATIC)
  set (portaudio_PROCESS_INCLUDES PORTAUDIO_STATIC_INCLUDE_DIRS)
  set (portaudio_PROCESS_LIBS PORTAUDIO_STATIC_LDFLAGS)
  set (portaudio_LIBRARY_DIRS ${PORTAUDIO_STATIC_LIBRARY_DIRS})
else ()
  set (portaudio_PROCESS_INCLUDES PORTAUDIO_INCLUDE_DIRS)
  set (portaudio_PROCESS_LIBS PORTAUDIO_LDFLAGS)
  set (portaudio_LIBRARY_DIRS ${PORTAUDIO_LIBRARY_DIRS})
endif ()
libfind_process (portaudio)

# Handle the  QUIETLY and REQUIRED  arguments and set  PORTAUDIO_FOUND to
# TRUE if all listed variables are TRUE
include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (portaudio DEFAULT_MSG portaudio_INCLUDE_DIRS portaudio_LIBRARIES portaudio_LIBRARY_DIRS)
