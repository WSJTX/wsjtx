include (CMakeParseArguments)

# set_build_type() function
#
# Configure  the output  artefacts  and their  names for  development,
# Release Candidate (RC), or General Availability (GA) build type.
#
# Usage:
#	set_build_type ()
#	set_build_type (RC n)
#	set_build_type (GA)
#
#	With no arguments or with RC 0 a development is specified. The
#	variable BUILD_TYPE_REVISION is set to "-devel".
#
#	With RC n  with n>0 specifies a Release  Candidate build.  The
#	variable BUIlD_TYPE_REVISION is set to "-rcn".
#
#	With GA  a General Availability  release is specified  and the
#	variable BUIlD_TYPE_REVISION is unset.
#
macro (set_build_type)
  set (WSJT_RELEASE_CHANNEL "DEVEL" CACHE STRING "Build release channel: DEVEL, RC, or GA.")
  set_property (CACHE WSJT_RELEASE_CHANNEL PROPERTY STRINGS DEVEL RC GA)
  set (WSJT_RC_NUMBER "" CACHE STRING "Release candidate number used when WSJT_RELEASE_CHANNEL is RC.")

  set (options GA)
  set (oneValueArgs RC)
  set (multiValueArgs)
  cmake_parse_arguments (BUILD_TYPE "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if (BUILD_TYPE_UNPARSED_ARGUMENTS)
    message (FATAL_ERROR "Unrecognized macro arguments: \"${BUILD_TYPE_UNPARSED_ARGUMENTS}\"")
  endif ()
  if (BUILD_TYPE_GA AND BUILD_TYPE_RC)
    message (FATAL_ERROR "Only specify one build type from RC or GA.")
  endif ()

  string (TOUPPER "${WSJT_RELEASE_CHANNEL}" _WSJT_RELEASE_CHANNEL)
  if (ARGC GREATER 0)
    if (BUILD_TYPE_GA)
      set (_WSJT_RELEASE_CHANNEL "GA")
    elseif (BUILD_TYPE_RC)
      if ("${BUILD_TYPE_RC}" STREQUAL "0")
        set (_WSJT_RELEASE_CHANNEL "DEVEL")
      else ()
        set (_WSJT_RELEASE_CHANNEL "RC")
        set (WSJT_RC_NUMBER "${BUILD_TYPE_RC}")
      endif ()
    else ()
      set (_WSJT_RELEASE_CHANNEL "DEVEL")
    endif ()
  endif ()

  if (NOT _WSJT_RELEASE_CHANNEL MATCHES "^(DEVEL|RC|GA)$")
    message (FATAL_ERROR "WSJT_RELEASE_CHANNEL must be DEVEL, RC, or GA; got \"${WSJT_RELEASE_CHANNEL}\".")
  endif ()

  set (BUILD_TYPE_REVISION "")
  if (_WSJT_RELEASE_CHANNEL STREQUAL "DEVEL")
    set (BUILD_TYPE_REVISION "-devel")
  elseif (_WSJT_RELEASE_CHANNEL STREQUAL "RC")
    if (NOT WSJT_RC_NUMBER MATCHES "^[1-9][0-9]*$")
      message (FATAL_ERROR "WSJT_RC_NUMBER must be a positive integer when WSJT_RELEASE_CHANNEL is RC.")
    endif ()
    set (BUILD_TYPE_REVISION "-rc${WSJT_RC_NUMBER}")
  endif ()
  set (WSJT_RELEASE_CHANNEL "${_WSJT_RELEASE_CHANNEL}" CACHE STRING "Build release channel: DEVEL, RC, or GA." FORCE)
  message (STATUS "Building ${PROJECT_NAME} v${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}${BUILD_TYPE_REVISION}")
endmacro ()
