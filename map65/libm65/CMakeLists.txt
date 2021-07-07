set (libm65_FSRCS
# Modules come first:
  wideband_sync.f90

# Non-module Fortran routines:
  afc65b.f90
  astro.f90
  astro0.f90
  astrosub.f90
  averms.f90
  badmsg.f90
  ccf2.f90
  ccf65.f90
  cgen65.f90
  chkhist.f90
  chkmsg.f90
  coord.f90
  dcoord.f90
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
  fil6521.f90
  filbig.f90
  fmtmsg.f90
  four2a.f90
  ftninit.f90
  ftnquit.f90
  q65b.f90
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
  jt65code.f90
  k2grid.f90
  lorentzian.f90
  map65a.f90
  moon2.f90
  moondop.f90
  nchar.f90
  noisegen.f90
  packjt.f90
  pfxdump.f90
  polfit.f90
  recvpkt.f90
  rfile3a.f90
  s3avg.f90
  sec_midn.f90
  set.f90
  setup65.f90
  shell.f90
  sleep_msec.f90
  smo.f90
  sun.f90
  symspec.f90
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

if (WIN32)
  set (libm65_CSRCS ${libm65_CSRCS} ptt.c)
else ()
  set (libm65_CSRCS ${libm65_CSRCS} ptt_unix.c)
endif ()

set (libm65_CXXSRCS
  ipcomm.cpp
  )

add_definitions (-DBIGSYM=1)
set_source_files_properties (sec_midn.f90 PROPERTIES COMPILE_FLAGS -fno-second-underscore)

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
target_link_libraries (m65impl wsjt_fort wsjt_cxx Qt5::Core)

add_executable (m65 m65.f90 m65a.f90)
target_link_libraries (m65 m65impl ${FFTW3_LIBRARIES})

add_executable (mapsim mapsim.f90)
target_link_libraries (mapsim m65impl ${FFTW3_LIBRARIES})

#add_executable (synctest synctest.f90)
#target_link_libraries (synctest m65impl ${FFTW3_LIBRARIES})

if (WIN32)
  install (
    TARGETS m65 mapsim
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
    BUNDLE DESTINATION . COMPONENT runtime
    )
endif ()
