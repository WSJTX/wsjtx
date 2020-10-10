message (STATUS "Checking for revision information")
set (__scs_header_file ${BINARY_DIR}/scs_version.h.txt)
file (WRITE ${__scs_header_file} "/* SCS version information */\n\n")
if (EXISTS "${SOURCE_DIR}/.svn")
  message (STATUS "Checking for Subversion revision information")
  find_package (Subversion QUIET REQUIRED)
  # the FindSubversion.cmake module is part of the standard distribution
  include (FindSubversion)
  # extract working copy information for SOURCE_DIR into MY_XXX variables
  Subversion_WC_INFO (${SOURCE_DIR} MY)
  message ("${MY_WC_INFO}")
  # determine if the working copy has outstanding changes
  execute_process (COMMAND ${Subversion_SVN_EXECUTABLE} status ${SOURCE_DIR}
    OUTPUT_FILE "${BINARY_DIR}/svn_status.txt"
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  file (STRINGS "${BINARY_DIR}/svn_status.txt" __svn_changes
    REGEX "^[^?].*$"
    )
  set (SCS_VERSION "r${MY_WC_LAST_CHANGED_REV}")
  if (__svn_changes)
    message (STATUS "Source tree based on revision ${SCS_VERSION} appears to have local changes")
    file (APPEND  ${__scs_header_file} "#define SCS_VERSION_IS_DIRTY 1\n")
    set (SCS_VERSION_STR "${SCS_VERSION}-dirty")
    foreach (__svn_change ${__svn_changes})
      message (STATUS "${__svn_change}")
    endforeach (__svn_change ${__svn_changes})
  else ()
    set (SCS_VERSION_STR "${SCS_VERSION}")
  endif ()
  message (STATUS "${SOURCE_DIR} contains a .svn and is revision ${SCS_VERSION}")
elseif (EXISTS "${SOURCE_DIR}/.git")
  if (EXISTS "${SOURCE_DIR}/.git/svn/.metadata")  # try git-svn
    message (STATUS "Checking for Subversion revision information using git-svn")
    include (${SOURCE_DIR}/CMake/Modules/FindGitSubversion.cmake)
    # extract working copy information for SOURCE_DIR into MY_XXX variables
    GitSubversion_WC_INFO (${SOURCE_DIR} MY)
    message ("${MY_WC_INFO}")
    set (SCS_VERSION "r${MY_WC_LAST_CHANGED_REV}")
    # try and determine if the working copy has outstanding changes
    execute_process (COMMAND ${GIT_EXECUTABLE} --git-dir=${SOURCE_DIR}/.git --work-tree=${SOURCE_DIR} svn dcommit --dry-run
      RESULT_VARIABLE __git_svn_status
      OUTPUT_FILE "${BINARY_DIR}/svn_status.txt"
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    file (STRINGS "${BINARY_DIR}/svn_status.txt" __svn_changes
      REGEX "^diff-tree"
      )
    if ((NOT ${__git_svn_status} EQUAL 0) OR __svn_changes)
      message (STATUS "Source tree based on revision ${MY_WC_LAST_CHANGED_REV} appears to have local changes")
      file (APPEND ${__scs_header_file} "#define SCS_VERSION_IS_DIRTY 1\n")
      set (SCS_VERSION_STR "${SCS_VERSION}-dirty")
    else ()
      set (SCS_VERSION_STR "${SCS_VERSION}")
    endif ()
  else ()
    #
    # try git
    #
    message (STATUS "Checking for gitrevision information")
    include (${SOURCE_DIR}/CMake/Modules/GetGitRevisionDescription.cmake)
    get_git_head_revision (${SOURCE_DIR} GIT_REFSPEC GIT_SHA1)
    git_local_changes (${SOURCE_DIR} GIT_SANITARY)
    string (SUBSTRING "${GIT_SHA1}" 0 6 SCS_VERSION)
    if ("${GIT_SANITARY}" STREQUAL "DIRTY")
      message (STATUS "Source tree based on revision ${GIT_REFSPEC} ${SCS_VERSION} appears to have local changes")
      file (APPEND ${__scs_header_file} "#define SCS_VERSION_IS_DIRTY 1\n")
      set (SCS_VERSION_STR "${SCS_VERSION}-dirty")
      execute_process (COMMAND ${GIT_EXECUTABLE} --git-dir=${SOURCE_DIR}/.git --work-tree=${SOURCE_DIR} status
	ERROR_QUIET
	OUTPUT_STRIP_TRAILING_WHITESPACE)
    else ()
      set (SCS_VERSION_STR "${SCS_VERSION}")
    endif ()
    message (STATUS "refspec: ${GIT_REFSPEC} - SHA1: ${SCS_VERSION}")
  endif ()
else()
  message (STATUS "No SCS found")
endif ()
file (APPEND ${__scs_header_file} "#define SCS_VERSION ${SCS_VERSION}\n")
file (APPEND ${__scs_header_file} "#define SCS_VERSION_STR \"${SCS_VERSION_STR}\"\n")

# copy the file to the final header only if the version changes
# reduces needless rebuilds
execute_process (COMMAND ${CMAKE_COMMAND} -E copy_if_different ${__scs_header_file} "${OUTPUT_DIR}/scs_version.h")
file (REMOVE ${__scs_header_file})
