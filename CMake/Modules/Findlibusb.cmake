# Findlibusb
# =======
#
# Find the usb library
#
# This will define the following variables::
#
#  libusb_FOUND	- True if the system has the usb library
#  libusb_VERSION	- The verion of the usb library which was found
#
# and the following imported targets::
#
#  libusb::libusb	- The libusb library
#
# If libusb_STATIC is TRUE then static linking will be assumed
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
libfind_pkg_check_modules (libusb_PC libusb-1.0)

# Include dir
find_path (libusb_INCLUDE_DIR
  NAMES libusb.h
  PATHS ${libusb_PC_INCLUDE_DIRS}
  )

# Library
if (libusb_STATIC)
  find_library (libusb_LIBRARY
    NAMES usb-1.0
    PATHS ${libusb_PC_STATIC_LIBRARY_DIRS}
    )
else ()
  find_library (libusb_LIBRARY
    NAMES usb-1.0
    PATHS ${libusb_PC_LIBRARY_DIRS}
    )
endif ()
set (libusb_PROCESS_INCLUDES libusb_INCLUDE_DIR)
set (libusb_PROCESS_LIBS libusb_LIBRARY)
libfind_process (libusb)
#dump_cmake_variables ("[lL][iI][bB][uU][sS]")

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (libusb
  REQUIRED_VARS
     libusb_LIBRARY
     libusb_INCLUDE_DIR
  VERSION_VAR libusb_VERSION
  )

if (libusb_FOUND)
  set (libusb_LIBRARIES ${libusb_LIBRARY})
  set (libusb_INCLUDE_DIRS ${libusb_INCLUDE_DIR})
  set (libusb_DEFINITIONS ${libusb_CFLAGS_OTHER})
endif ()

if (libusb_FOUND AND NOT TARGET libusb::libusb)
  add_library (libusb::libusb UNKNOWN IMPORTED)
  set_target_properties (libusb::libusb PROPERTIES
    IMPORTED_LOCATION "${libusb_LIBRARY}"
    INTERFACE_COMPILE_OPTIONS "${libusb_CFLAGS_OTHER}"
    INTERFACE_INCLUDE_DIRECTORIES "${libusb_INCLUDE_DIR}"
    )
endif ()

mark_as_advanced (
  libusb_INCLUDE_DIR
  libusb_LIBRARY
  )
