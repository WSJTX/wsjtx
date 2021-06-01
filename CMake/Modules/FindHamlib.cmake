#
# Find the hamlib library
#
# This will define the following variables::
#
#  Hamlib_FOUND		- True if the system has the usb library
#  Hamlib_VERSION	- The verion of the usb library which was found
#
# and the following imported targets::
#
#  Hamlib::Hamlib	- The hamlib library
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

# Get all propreties that cmake supports
execute_process(COMMAND cmake --help-property-list OUTPUT_VARIABLE CMAKE_PROPERTY_LIST)

# Convert command output into a CMake list
STRING(REGEX REPLACE ";" "\\\\;" CMAKE_PROPERTY_LIST "${CMAKE_PROPERTY_LIST}")
STRING(REGEX REPLACE "\n" ";" CMAKE_PROPERTY_LIST "${CMAKE_PROPERTY_LIST}")
# Fix https://stackoverflow.com/questions/32197663/how-can-i-remove-the-the-location-property-may-not-be-read-from-target-error-i
#list(FILTER CMAKE_PROPERTY_LIST EXCLUDE REGEX "^LOCATION$|^LOCATION_|_LOCATION$")
# For some reason, "TYPE" shows up twice - others might too?
list(REMOVE_DUPLICATES CMAKE_PROPERTY_LIST)

# build whitelist by filtering down from CMAKE_PROPERTY_LIST in case cmake is
# a different version, and one of our hardcoded whitelisted properties
# doesn't exist!
unset(CMAKE_WHITELISTED_PROPERTY_LIST)
foreach(prop ${CMAKE_PROPERTY_LIST})
  if(prop MATCHES "^(INTERFACE|[_a-z]|IMPORTED_LIBNAME_|MAP_IMPORTED_CONFIG_)|^(COMPATIBLE_INTERFACE_(BOOL|NUMBER_MAX|NUMBER_MIN|STRING)|EXPORT_NAME|IMPORTED(_GLOBAL|_CONFIGURATIONS|_LIBNAME)?|NAME|TYPE|NO_SYSTEM_FROM_IMPORTED)$")
    list(APPEND CMAKE_WHITELISTED_PROPERTY_LIST ${prop})
  endif()
endforeach(prop)

function(print_properties)
  message ("CMAKE_PROPERTY_LIST = ${CMAKE_PROPERTY_LIST}")
endfunction(print_properties)

function(print_whitelisted_properties)
  message ("CMAKE_WHITELISTED_PROPERTY_LIST = ${CMAKE_WHITELISTED_PROPERTY_LIST}")
endfunction(print_whitelisted_properties)

function(print_target_properties tgt)
  if(NOT TARGET ${tgt})
    message("There is no target named '${tgt}'")
    return()
  endif()
  
  get_target_property(target_type ${tgt} TYPE)
  if(target_type STREQUAL "INTERFACE_LIBRARY")
    set(PROP_LIST ${CMAKE_WHITELISTED_PROPERTY_LIST})
  else()
    set(PROP_LIST ${CMAKE_PROPERTY_LIST})
  endif()
  
  foreach (prop ${PROP_LIST})
    string(REPLACE "<CONFIG>" "${CMAKE_BUILD_TYPE}" prop ${prop})
    # message ("Checking ${prop}")
    get_property(propval TARGET ${tgt} PROPERTY ${prop} SET)
    if (propval)
      get_target_property(propval ${tgt} ${prop})
      message ("${tgt} ${prop} = ${propval}")
    endif()
  endforeach(prop)
endfunction(print_target_properties)

include (LibFindMacros)

libfind_pkg_detect (Hamlib hamlib
  FIND_PATH hamlib/rig.h PATH_SUFFIXES hamlib
  FIND_LIBRARY hamlib
  )

libfind_package (Hamlib Usb)

libfind_process (Hamlib)

if (NOT Hamlib_PKGCONF_FOUND)
  if (WIN32)
    set (Hamlib_LIBRARIES ${Hamlib_LIBRARIES};${Usb_LIBRARY};ws2_32)
  else ()
    set (Hamlib_LIBRARIES ${Hamlib_LIBRARIES};m;dl)
  endif ()
elseif (UNIX AND NOT APPLE)
  set (Hamlib_LIBRARIES ${Hamlib_PKGCONF_STATIC_LDFLAGS})
endif ()

# fix up extra link libraries for macOS as target_link_libraries gets
# it wrong for frameworks
unset (_next_is_framework)
unset (_Hamlib_EXTRA_LIBS)
foreach (_lib IN LISTS Hamlib_LIBRARIES Hamlib_PKGCONF_LDFLAGS)
  if (_next_is_framework)
    list (APPEND _Hamlib_EXTRA_LIBS "-framework ${_lib}")
    unset (_next_is_framework)
  elseif (${_lib} STREQUAL "-framework")
    set (_next_is_framework TRUE)
  else ()
    list (APPEND _Hamlib_EXTRA_LIBS ${_lib})
  endif ()
endforeach ()

if (Hamlib_FOUND AND NOT TARGET Hamlib::Hamlib)
  add_library (Hamlib::Hamlib UNKNOWN IMPORTED)
  set_target_properties (Hamlib::Hamlib PROPERTIES
    IMPORTED_LOCATION "${Hamlib_LIBRARY}"
    INTERFACE_COMPILE_OPTIONS "${Hamlib_PKGCONF_STATIC_OTHER}"
    INTERFACE_INCLUDE_DIRECTORIES "${Hamlib_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "${_Hamlib_EXTRA_LIBS}"
    )
endif ()

dump_cmake_variables ("Hamlib")
print_target_properties (Hamlib::Hamlib)

mark_as_advanced (
  Hamlib_INCLUDE_DIR
  Hamlib_LIBRARY
  Hamlib_LIBRARIES
  )
