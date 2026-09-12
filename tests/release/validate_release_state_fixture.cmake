cmake_minimum_required (VERSION 3.12)

include ("${RELEASE_STATE_READER}")
wsjt_read_release_state (
  "${RELEASE_STATE_FILE}"
  version channel rc revision)
