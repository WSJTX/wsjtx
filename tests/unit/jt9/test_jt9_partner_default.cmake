# SPDX-License-Identifier: GPL-3.0-or-later
# jt9 and wsjt-codec start with no QSO partner, as WSJT-X does with an empty
# DX Call: FT8's a8 pass, which looks for the partner, runs only when -x and -g
# name one. The timer report shows whether ft8_a8d ran.
file (REMOVE_RECURSE "${WORK_DIR}")
file (MAKE_DIRECTORY "${WORK_DIR}")

# Decodes SAMPLE as FT8 in a fresh directory; sets a8 to whether ft8_a8d ran.
function (decode name program)
  set (directory "${WORK_DIR}/${name}")
  file (MAKE_DIRECTORY "${directory}")
  execute_process (
    COMMAND "${program}" -8 -a "${directory}" -t "${directory}" ${ARGN} "${SAMPLE}"
    WORKING_DIRECTORY "${directory}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if (NOT result EQUAL 0 OR NOT stdout MATCHES "CQ F5RXL IN94")
    message (FATAL_ERROR "${name} did not decode the sample (exit ${result}): ${stdout}${stderr}")
  endif ()
  file (READ "${directory}/timer.out" timers)
  if (timers MATCHES "ft8_a8d")
    set (a8 TRUE PARENT_SCOPE)
  else ()
    set (a8 FALSE PARENT_SCOPE)
  endif ()
endfunction ()

foreach (program IN ITEMS "${JT9}" "${WSJT_CODEC}")
  get_filename_component (name "${program}" NAME_WE)
  decode ("${name}-default" "${program}")
  if (a8)
    message (FATAL_ERROR "${name} ran the a8 pass with no DX call given")
  endif ()
  decode ("${name}-partner" "${program}" -x W9XYZ -g EN37)
  if (NOT a8)
    message (FATAL_ERROR "${name} did not run the a8 pass with -x W9XYZ -g EN37")
  endif ()
endforeach ()
