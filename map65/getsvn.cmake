find_package (Subversion)
if (Subversion_FOUND AND EXISTS ${PROJECT_SOURCE_DIR}/.svn)
  # the FindSubversion.cmake module is part of the standard distribution
  include (FindSubversion)
  # extract working copy information for SOURCE_DIR into MY_XXX variables
  Subversion_WC_INFO (${SOURCE_DIR} MY)
  # write a file with the SVNVERSION define
  file (WRITE svnversion.h.txt "#define SVNVERSION ${MY_WC_REVISION}\n")
else (Subversion_FOUND AND EXISTS ${PROJECT_SOURCE_DIR}/.svn)
  file (WRITE svnversion.h.txt "#define SVNVERSION local\n")
endif (Subversion_FOUND AND EXISTS ${PROJECT_SOURCE_DIR}/.svn)

# copy the file to the final header only if the version changes
# reduces needless rebuilds
execute_process (COMMAND ${CMAKE_COMMAND} -E copy_if_different
  svnversion.h.txt svnversion.h)
