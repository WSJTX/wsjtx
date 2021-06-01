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

if (Portaudio_FOUND AND NOT TARGET Portaudio::Portaudio)
  add_library (Portaudio::Portaudio UNKNOWN IMPORTED)
  set_target_properties (Portaudio::Portaudio PROPERTIES
    IMPORTED_LOCATION "${Portaudio_LIBRARY}"
    INTERFACE_COMPILE_OPTIONS "${Portaudio_PKGCONF_CFLAGS_OTHER}"
    INTERFACE_INCLUDE_DIRECTORIES "${Portaudio_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "${_Portaudio_EXTRA_LIBS}"
    )
endif ()

mark_as_advanced (
  Portaudio_INCLUDE_DIR
  Portaudio_LIBRARY
  )
