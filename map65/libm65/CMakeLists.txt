set (libm65_FSRCS
# Modules come first:
  cacb_mod.f90
  datcom_ptrs_mod.f90
  decodes_mod.f90
  extract_shared_mod.f90
  gran_interface.f90
  m65_mod.f90
  four2a_legacy_wrap.f90
  npar_ptrs_mod.f90
  pfx_mod.f90
  pr_mod.f90
  rs_mod.f90
  stdout_channel_mod.f90
  wideband_sync.f90
  fftw3_f77_interfaces.f90
  sec0_mod.f90
  ftrsd2_mod.f90

# Non-module Fortran routines:
  afc65b.f90
  astro.f90
  astro0.f90
  astrosub.f90
  ccf2.f90
  ccf65.f90
  cgen65.f90
  chkhist.f90
  chkmsg.f90
  coord.f90
  dcoord.f90
  debug_log.f90
  decode0.f90
  decode1a.f90
  decode65b.f90
  deep65.f90
  deg2grid.f90
  demod64a.f90
  display.f90
  dot.f90
  dpol.f90
  encode65.f90
  extract.f90
  fchisq.f90
  fchisq0.f90
  fftw3.f90
  fil6521.f90
  filbig.f90
  fmtmsg.f90
  four2a.f90
  ftninit.f90
  ftnquit.f90
  q65b.F90
  gen65.f90
  gen_q65_cwave.f90
  gen_q65_wave.f90
  geocentric.f90
  getdphi.f90
  getpfx1.f90
  getpfx2.f90
  graycode.f90
  graycode65.f90
  grid2deg.f90
  grid2k.f90
  indexx.f90
  interleave63.f90
  iqcal.f90
  iqfix.f90
  k2grid.f90
  lorentzian.f90
  m65a.f90
  map65a.f90
  moon2.f90
  moondop.f90
  nchar.f90
  noisegen.f90
  packjt.f90
  pctile.f90
  pfx.f90
  pfxdump.f90
  polfit.f90
  recvpkt.f90
  rfile3a.f90
  run_m65.f90
  s3avg.f90
  sec_midn.f90
  set.f90
  setup65.f90
  shell.f90
  sleep_msec.f90
  smo.f90
  sun.f90
  symspec.f90
  test_astro_pipeline.f90
#  timer.f90
  timf2.f90
  tm2.f90
  toxyz.f90
  trimlist.f90
  twkfreq.f90
  twkfreq_xy.f90
  txpol.f90
  wavhdr.f90

  f77_wisdom.f
  )

set (libm65_ka9q_CSRCS
  decode_rs.c
  encode_rs.c
  init_rs.c
)
set_source_files_properties (${libm65_ka9q_CSRCS} PROPERTIES COMPILE_FLAGS -Wno-sign-compare)

set (libm65_CSRCS
  ${libm65_ka9q_CSRCS}
  ftrsd2.c
#  gran.c
  igray.c
  tmoonsub.c
  usleep.c
  wrapkarn.c
)

# Platform-specific sources
if (WIN32)
  list(APPEND libm65_CSRCS
       ptt.c
       win_event_shim.c
       stdout_shared_memory.cpp
  )
else ()
  list(APPEND libm65_CSRCS
       ptt_unix.c
       posix_event_shim.c
       stdout_shared_memory.cpp
  )
endif ()

#set (libm65_CXXSRCS
#  ipcomm.cpp
#  )

add_definitions (-DBIGSYM=1)
set_source_files_properties (sec_midn.f90 PROPERTIES COMPILE_FLAGS -fno-second-underscore)
if(Qt6_FOUND)
  set_source_files_properties(m65a.f90 PROPERTIES COMPILE_FLAGS "-Wno-aliasing -fallow-argument-mismatch")
else()  
  set_source_files_properties(m65a.f90 PROPERTIES COMPILE_FLAGS "-Wno-aliasing")
endif()

set (libm65_C_and_CXXSRCS
  ${libm65_CSRCS}
  ${libm65_CXXSRCS}
  )
set_property (SOURCE ${libm65_C_and_CXXSRCS} APPEND_STRING PROPERTY COMPILE_FLAGS " -include wsjtx_config.h")
set_property (SOURCE ${libm65_C_and_CXXSRCS} APPEND PROPERTY OBJECT_DEPENDS ${CMAKE_BINARY_DIR}/wsjtx_config.h)


#
# build our targets
#
add_library (m65impl STATIC ${libm65_FSRCS} ${libm65_CSRCS} ${libm65_CXXSRCS})

target_compile_options(m65impl PRIVATE
    $<$<COMPILE_LANGUAGE:Fortran>:
    #    -fcheck=bounds
    #    -fcheck=all
    #    -frecursive
        -fbacktrace
        -Wall
		-Wmaybe-uninitialized
    #    -finit-real=snan # added for modernization testing
    #    -finit-integer=-999999 # added for modernization testing
    >
)

set_source_files_properties(stdout_channel_mod.f90 PROPERTIES
    Fortran_PREPROCESS ON
)

# MAP65 should not choose between wsjt_fort and wsjt_fort_omp locally. The
# common interface target keeps the variant policy in one place and still
# resolves to wsjt_fort_omp when OPENMP_ONLY leaves no non-OpenMP library.
# Default BOTH builds intentionally use the non-OpenMP common target here;
# release Windows and Linux builds pin OPENMP_ONLY when MAP65 needs OpenMP.
target_link_libraries (m65impl PUBLIC Qt5::Core)
wsjt_link_common_fortran (m65impl)

if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_compile_definitions(m65impl PRIVATE _WIN32)
elseif (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_definitions(m65impl PRIVATE _WIN64)
endif()

if (WIN32)
    target_link_libraries(m65impl PUBLIC kernel32)
endif()

add_executable (mapsim mapsim.f90)
add_dependencies(mapsim m65impl)
target_include_directories(mapsim PRIVATE ${CMAKE_BINARY_DIR}/mod )
target_link_libraries (mapsim m65impl ${FFTW3_LIBRARIES})

#add_executable (synctest synctest.f90)
#target_link_libraries (synctest m65impl ${FFTW3_LIBRARIES})

if (WIN32)
  install (
    TARGETS mapsim
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
    BUNDLE DESTINATION . COMPONENT runtime
    )
endif ()
