# Findusb
# =======
#
# Find the usb library
#
# This will define the following variables::
#
#  usb_FOUND	- True if the system has the usb library
#  usb_VERSION	- The verion of the usb library which was found
#
# and the following imported targets::
#
#  usb::usb	- The libusb library

find_package (PkgConfig)
pkg_check_modules (PC_usb QUIET usb)

find_path (usb_INCLUDE_DIR
  NAMES libusb.h
  PATHS ${PC_usb_INCLUDE_DIRS}
  PATH_SUFFIXES libusb-1.0
  )
find_library (usb_LIBRARY
  NAMES libusb-1.0
  PATHS $PC_usb_LIBRARY_DIRS}
)

set (usb_VERSION ${PC_usb_VERSION})

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (usb
  FOUND_VAR usb_FOUND
  REQUIRED_VARS
     usb_LIBRARY
     usb_INCLUDE_DIR
  VERSION_VAR usb_VERSION
  )

if (usb_FOUND)
  set (usb_LIBRARIES ${usb_LIBRARY})
  set (usb_INCLUDE_DIRS ${usb_INCLUDE_DIR})
  set (usb_DEFINITIONS ${PC_usb_CFLAGS_OTHER})
endif ()

if (usb_FOUND AND NOT TARGET usb::usb)
  add_library (usb::usb UNKNOWN IMPORTED)
  set_target_properties (usb::usb PROPERTIES
    IMPORTED_LOCATION "${usb_LIBRARY}"
    INTERFACE_COMPILE_OPTIONS "${PC_usb_CFLAGS_OTHER}"
    INTERFACE_INCLUDE_DIRECTORIES "${usb_INCLUDE_DIR}"
    )
endif ()

mark_as_advanced (
  usb_INCLUDE_DIR
  usb_LIBRARY
  )
