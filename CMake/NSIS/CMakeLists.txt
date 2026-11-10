add_executable (wsjtx-launch-user WIN32 launch-user.c)
target_link_libraries (wsjtx-launch-user PRIVATE ole32 oleaut32 uuid shell32)

file (GENERATE
  OUTPUT "${PROJECT_BINARY_DIR}/NSIS-launcher-$<CONFIG>.cmake"
  CONTENT "set(WSJTX_NSIS_LAUNCHER \"$<TARGET_FILE:wsjtx-launch-user>\")\n")
