# - Try to find portaudio
#
# Once done, this will define:
#
#  portaudio_FOUND - system has portaudio
#  portaudio_VERSION - The version of the portaudio library which was found
#
# and the following imported targets::
#
#   portaudio::portaudio	- The portaudio library
#
# If portaudio_STATIC is TRUE then static linking will be assumed
#

function(dump_cmake_variables)
  get_cmake_property(_variableNames VARIABLES)
  list (SORT _variableNames)
  foreach (_variableName ${_variableNames})
    if (ARGV0)
      unset(MATCHED)
      string(REGEX MATCH ${ARGV0} MATCHED ${_variableName})
      if (NOT MATCHED)
        continue()
      endif()
    endif()
    message(STATUS "${_variableName}=${${_variableName}}")
  endforeach()
endfunction()

include (LibFindMacros)

# Use pkg-config to get hints about paths, libs and, flags
libfind_pkg_check_modules (portaudio_PC portaudio-2.0)

# Include dir
find_path (portaudio_INCLUDE_DIR
  NAMES portaudio.h
  PATHS ${portaudio_PC_INCLUDE_DIRS}
  )

# Library
if (portaudio_STATIC)
  find_library (portaudio_LIBRARY
    NAMES portaudio
    PATHS ${portaudio_PC_STATIC_LIBRARY_DIRS}
    )
else ()
  find_library (portaudio_LIBRARY
    NAMES portaudio
    PATHS ${portaudio_PC_LIBRARY_DIRS}
    )
endif ()
set (portaudio_PROCESS_INCLUDES portaudio_INCLUDE_DIR)
set (portaudio_PROCESS_LIBS portaudio_LIBRARY)
libfind_process (portaudio)

# Handle the  QUIETLY and REQUIRED  arguments and set  PORTAUDIO_FOUND to
# TRUE if all listed variables are TRUE
include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (portaudio
  REQUIRED_VARS
     portaudio_LIBRARY
     portaudio_INCLUDE_DIR
  VERSION_VAR portaudio_VERSION
  )

if (portaudio_FOUND)
  set (portaudio_LIBRARIES ${portaudio_LIBRARY})
  set (portaudio_INCLUDE_DIRS ${portaudio_INCLUDE_DIR})
  set (portaudio_DEFINITIONS ${portaudio_CFLAGS_OTHER})
endif ()

if (portaudio_FOUND AND NOT TARGET portaudio::portaudio)
  add_library (portaudio::portaudio UNKNOWN IMPORTED)
  set_target_properties (portaudio::portaudio PROPERTIES
    IMPORTED_LOCATION "${portaudio_LIBRARY}"
    INTERFACE_COMPILE_OPTIONS "${portaudio_CFLAGS_OTHER}"
    INTERFACE_INCLUDE_DIRECTORIES "${portaudio_INCLUDE_DIR}"
    )
endif ()

mark_as_advanced (
  portaudio_INCLUDE_DIR
  portaudio_LIBRARY
  )
