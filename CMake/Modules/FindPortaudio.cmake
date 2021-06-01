# - Try to find portaudio
#
# Once done, this will define:
#
#  Portaudio_FOUND - system has portaudio
#  Portaudio_VERSION - The version of the portaudio library which was found
#
# and the following imported targets::
#
#   Portaudio::Portaudio	- The portaudio library
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

libfind_pkg_detect (Portaudio portaudio-2.0
  FIND_PATH portaudio.h
  FIND_LIBRARY portaudio
  )

libfind_process (Portaudio)

# fix up extra link libraries for macOS as target_link_libraries gets
# it wrong for frameworks
unset (_next_is_framework)
unset (_Portaudio_EXTRA_LIBS)
foreach (_lib IN LISTS Portaudio_PKGCONF_LDFLAGS)
  if (_next_is_framework)
    list (APPEND _Portaudio_EXTRA_LIBS "-framework ${_lib}")
    unset (_next_is_framework)
  elseif (${_lib} STREQUAL "-framework")
    set (_next_is_framework TRUE)
  else ()
    list (APPEND _Portaudio_EXTRA_LIBS ${_lib})
  endif ()
endforeach ()

dump_cmake_variables ("Portaudio")
if (Portaudio_FOUND AND NOT TARGET Portaudio::Portaudio)
  add_library (Portaudio::Portaudio UNKNOWN IMPORTED)
  set_target_properties (Portaudio::Portaudio PROPERTIES
    IMPORTED_LOCATION "${Portaudio_LIBRARY}"
    INTERFACE_COMPILE_OPTIONS "${Portaudio_PKGCONF_CFLAGS_OTHER}"
    INTERFACE_INCLUDE_DIRECTORIES "${Portaudio_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "${_Portaudio_EXTRA_LIBS}"
    )
endif ()

print_target_properties (Portaudio::Portaudio)

mark_as_advanced (
  Portaudio_INCLUDE_DIR
  Portaudio_LIBRARY
  )
