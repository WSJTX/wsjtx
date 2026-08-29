#
# Simulator / code-demonstrator executables (WSJT_BUILD_UTILS) --
# included from the top-level CMakeLists.txt at the equivalent point.
# Included (not add_subdirectory'd), so CMAKE_CURRENT_SOURCE_DIR stays
# the project root; relies on wsjt_link_common_fortran() and
# wsjt_add_simulator_usage_test(), both defined earlier in the
# top-level file.
#

# The Linux live-audio smoke tests synthesize their fixture at test time. Keep
# this one generator in the build graph even in lean CI configurations where
# the rest of the simulator utilities are disabled.
set (wsjt_jtty_live_audio_tests FALSE)
if (WSJT_ENABLE_TESTS AND WSJT_BUILD_FORTRAN_OPENMP
    AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
    AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  set (wsjt_jtty_live_audio_tests TRUE)
endif ()

if (WSJT_BUILD_UTILS OR wsjt_jtty_live_audio_tests)
  add_executable (sjtty lib/jtty/sjtty.f90)
  wsjt_link_common_fortran (sjtty)
endif ()

if(WSJT_BUILD_UTILS)

add_executable (jt4sim lib/jt4sim.f90)
wsjt_link_common_fortran (jt4sim)

add_executable (jt65sim lib/jt65sim.f90)
wsjt_link_common_fortran (jt65sim)

add_executable (sjtty_qrm lib/jtty/sjtty_qrm.f90)
wsjt_link_common_fortran (sjtty_qrm)

add_executable (rjtty lib/jtty/rjtty.f90)
wsjt_link_common_fortran (rjtty)

add_executable (sumsim lib/sumsim.f90)
wsjt_link_common_fortran (sumsim)

add_executable (cablog lib/cablog.f90)
target_link_libraries (cablog)

add_executable (test_snr lib/test_snr.f90)
wsjt_link_common_fortran (test_snr)

add_executable (q65sim lib/qra/q65/q65sim.f90)
wsjt_link_common_fortran (q65sim)
wsjt_add_simulator_usage_test (q65sim "Usage:[ ]+q65sim")

add_executable (EchoCallSim lib/EchoCallSim.f90)
wsjt_link_common_fortran (EchoCallSim)
wsjt_add_simulator_usage_test (EchoCallSim "Usage:[ ]+EchoCallSim")

add_executable (testEchoCall lib/testEchoCall.f90)
wsjt_link_common_fortran (testEchoCall)
if (WSJT_ENABLE_TESTS)
  add_test (
    NAME test_echocall_filename
    COMMAND ${CMAKE_COMMAND}
      -DECHOCALLSIM=$<TARGET_FILE:EchoCallSim>
      -DTESTECHOCALL=$<TARGET_FILE:testEchoCall>
      -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/test_echocall_filename
      -P ${CMAKE_SOURCE_DIR}/tests/unit/echo/test_echocall_input.cmake
  )
  set_tests_properties (test_echocall_filename PROPERTIES TIMEOUT 30)
endif ()

add_executable (cwsim lib/cwsim.f90)
wsjt_link_common_fortran (cwsim)

add_executable (q65code lib/qra/q65/q65code.f90)
wsjt_link_common_fortran (q65code)
add_test (
  NAME test_q65code
  COMMAND ${CMAKE_COMMAND}
    -DQ65CODE=$<TARGET_FILE:q65code>
    -P ${CMAKE_SOURCE_DIR}/tests/unit/q65/test_q65code.cmake
)

add_executable (test_q65 lib/test_q65.f90)
wsjt_link_common_fortran (test_q65)

add_executable (q65_ftn_test lib/qra/q65/q65_ftn_test.f90)
wsjt_link_common_fortran (q65_ftn_test)

add_executable (jt49sim lib/jt49sim.f90)
wsjt_link_common_fortran (jt49sim)

#add_executable (allsim lib/allsim.f90)
#target_link_libraries (allsim wsjt_fort wsjt_cxx)

add_executable (rtty_spec lib/rtty_spec.f90)
wsjt_link_common_fortran (rtty_spec)

add_executable (jt65code lib/jt65code.f90)
wsjt_link_common_fortran (jt65code)

add_executable (jt9code lib/jt9code.f90)
wsjt_link_common_fortran (jt9code)

add_executable (wsprcode
  lib/wsprcode/wsprcode.f90
  lib/wsprcode/nhash.c
  lib/lookup3.c)
wsjt_link_common_fortran (wsprcode)
	       
add_executable (encode77 lib/77bit/encode77.f90)
wsjt_link_common_fortran (encode77)
add_test (
  NAME test_encode77
  COMMAND ${CMAKE_COMMAND}
    -DENCODE77=$<TARGET_FILE:encode77>
    -DINPUT_DIR=${CMAKE_SOURCE_DIR}/tests/unit/packjt77
    -DINPUT_FILE=encode77_test_messages.txt
    -P ${CMAKE_SOURCE_DIR}/tests/unit/packjt77/test_encode77.cmake
)

add_executable (hash22calc lib/77bit/hash22calc.f90)
wsjt_link_common_fortran (hash22calc)

add_executable (wsprsim ${wsprsim_CSRCS})
target_link_libraries (wsprsim ${LIBM_LIBRARIES})

add_executable (jt4code lib/jt4code.f90)
wsjt_link_common_fortran (jt4code)

add_executable (msk144code lib/msk144code.f90)
wsjt_link_common_fortran (msk144code)

add_executable (ft8code lib/ft8/ft8code.f90)
wsjt_link_common_fortran (ft8code)

add_executable (ft4code lib/ft4/ft4code.f90)
wsjt_link_common_fortran (ft4code)

add_executable (echosim lib/echosim.f90)
wsjt_link_common_fortran (echosim)
wsjt_add_simulator_usage_test (echosim "Usage 1:[ ]+echosim")

add_executable (sfoxsim lib/superfox/sfoxsim.f90)
wsjt_link_common_fortran (sfoxsim)

add_executable (sfrx lib/superfox/sfrx.f90)
wsjt_link_common_fortran (sfrx)

add_executable (sftx lib/superfox/sftx.f90)
wsjt_link_common_fortran (sftx)

add_executable (msk144sim lib/msk144sim.f90)
wsjt_link_common_fortran (msk144sim)

add_executable (ft4sim lib/ft4/ft4sim.f90)
wsjt_link_common_fortran (ft4sim)

add_executable (ft4sim_mult lib/ft4/ft4sim_mult.f90)
wsjt_link_common_fortran (ft4sim_mult)

add_executable (fst4sim lib/fst4/fst4sim.f90)
wsjt_link_common_fortran (fst4sim)
wsjt_add_simulator_usage_test (fst4sim "Usage:[ ]+fst4sim")
if (WIN32)
  set_target_properties (fst4sim PROPERTIES
    LINK_FLAGS -Wl,--stack,0x4000000,--heap,0x6000000
    )
endif ()

add_executable (ldpcsim240_101 lib/fst4/ldpcsim240_101.f90)
wsjt_link_common_fortran (ldpcsim240_101)
add_test (
  NAME test_pack77_cli_fallbacks
  COMMAND ${CMAKE_COMMAND}
    -DFT4CODE=$<TARGET_FILE:ft4code>
    -DFT4SIM=$<TARGET_FILE:ft4sim>
    -DFST4SIM=$<TARGET_FILE:fst4sim>
    -DLDPCSIM240_101=$<TARGET_FILE:ldpcsim240_101>
    -DMSK144CODE=$<TARGET_FILE:msk144code>
    -P ${CMAKE_SOURCE_DIR}/tests/unit/packjt77/test_pack77_cli_fallbacks.cmake
)

add_executable (ldpcsim240_74 lib/fst4/ldpcsim240_74.f90)
wsjt_link_common_fortran (ldpcsim240_74)

endif(WSJT_BUILD_UTILS)
