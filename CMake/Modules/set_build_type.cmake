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
  if (NOT BUILD_TYPE_GA)
    if (BUILD_TYPE_RC)
      set (BUILD_TYPE_REVISION "-rc${BUILD_TYPE_RC}")
    else ()
      set (BUILD_TYPE_REVISION "-devel")
    endif ()
  endif ()
  message (STATUS "Building ${PROJECT_NAME} v${PROJECT_VERSION}${BUILD_TYPE_REVISION}")
endmacro ()
