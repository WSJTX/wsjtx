# SPDX-License-Identifier: GPL-3.0-or-later
string (MD5 rig_name "${WORK_DIR}")
set (rig_name "CTEST-Q65-${rig_name}")
# Automated runs use Qt's test-mode settings directory and application suffix.
file (MAKE_DIRECTORY "$ENV{HOME}/.qttest/config" "${WORK_DIR}/tmp")
set (settings "$ENV{HOME}/.qttest/config/WSJT-X - ${rig_name} - test.ini")
file (WRITE "${settings}" [=[
[General]
SubMode_Q65=2
TRPeriod_Q65=30
[Common]
Mode=Q65
[Configuration]
VHFUHF=false
MyCall=N0CALL
MyGrid=AA00
PSKReporter=false
]=])

execute_process (
  COMMAND "${CMAKE_COMMAND}" -E env
    "QT_QPA_PLATFORM=offscreen"
    "TMPDIR=${WORK_DIR}/tmp"
    "${APPLICATION}" --startup-smoke-test --rig-name "${rig_name}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr
  TIMEOUT 20)
if (NOT "${result}" STREQUAL "0")
  message (FATAL_ERROR "Application startup failed (${result}):\n${stdout}\n${stderr}")
endif ()

file (STRINGS "${settings}" submode REGEX "^SubMode_Q65=")
# This default is absent from the fixture, so it also proves the profile was saved.
file (STRINGS "${settings}" saved REGEX "^Ftol_Q65=")
if (NOT "${submode}" STREQUAL "SubMode_Q65=2" OR NOT "${saved}" STREQUAL "Ftol_Q65=50")
  message (FATAL_ERROR "Q65 submode C was not saved: ${submode}; ${saved}")
endif ()
