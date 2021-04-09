cmake_minimum_required (VERSION 2.8.8)

project (libm65 C CXX Fortran)

if (POLICY CMP0020)
  cmake_policy (SET CMP0020 NEW) # link to Qt winmain on Windows
endif (POLICY CMP0020)

# make sure that the default is a RELEASE
if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE RELEASE CACHE STRING
    "Choose the type of build, options are: None Debug Release."
    FORCE)
endif (NOT CMAKE_BUILD_TYPE)

set (CMAKE_POSITION_INDEPENDENT_CODE ON)

# Fortran setup

# FFLAGS depend on the compiler
get_filename_component (Fortran_COMPILER_NAME ${CMAKE_Fortran_COMPILER} NAME)

if (Fortran_COMPILER_NAME MATCHES "gfortran.*")
  # gfortran
  #  set (CMAKE_Fortran_FLAGS_RELEASE "-funroll-all-loops -fno-f2c -O3")
  set (CMAKE_Fortran_FLAGS_RELEASE "-O2 -fbounds-check")
  set (CMAKE_Fortran_FLAGS_DEBUG   "-fno-f2c -O0 -g")
elseif (Fortran_COMPILER_NAME MATCHES "ifort.*")
  # ifort (untested)
  set (CMAKE_Fortran_FLAGS_RELEASE "-f77rtl -O3")
  set (CMAKE_Fortran_FLAGS_DEBUG   "-f77rtl -O0 -g")
elseif (Fortran_COMPILER_NAME MATCHES "g77")
  # g77
  set (CMAKE_Fortran_FLAGS_RELEASE "-funroll-all-loops -fno-f2c -O3 -m32")
  set (CMAKE_Fortran_FLAGS_DEBUG   "-fno-f2c -O0 -g -m32")
else (Fortran_COMPILER_NAME MATCHES "gfortran.*")
  message ("CMAKE_Fortran_COMPILER full path: " ${CMAKE_Fortran_COMPILER})
  message ("Fortran compiler: " ${Fortran_COMPILER_NAME})
  message ("No optimized Fortran compiler flags are known, we just try -O2...")
  set (CMAKE_Fortran_FLAGS_RELEASE "-O2")
  set (CMAKE_Fortran_FLAGS_DEBUG   "-O0 -g")
endif (Fortran_COMPILER_NAME MATCHES "gfortran.*")

# C++ setup

if (UNIX)
	SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
endif (UNIX)

# setup and test Fortran C/C++ interaction

include (FortranCInterface)
FortranCInterface_VERIFY (CXX QUIET)
FortranCInterface_HEADER (FC.h MACRO_NAMESPACE "FC_" SYMBOL_NAMESPACE "FC_"
  SYMBOLS )


set (FSRCS
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
  gen65.f90
  genqra64a.f90
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
  pctile.f90
  pfxdump.f90
  qra64b.f90
  qra64c.f90
  qra64zap.f90
  recvpkt.f90
  rfile3a.f90
  s3avg.f90
  sec_midn.f90
  set.f90
  setup65.f90
  shell.f90
  sleep_msec.f90
  smo.f90
  spec64.f90
  sun.f90
  symspec.f90
  sync64.f90
  timer.f90
  timf2.f90
  tm2.f90
  toxyz.f90
  trimlist.f90
  twkfreq.f90
  twkfreq_xy.f90

  f77_wisdom.f
  )

set (CSRCS
  decode_rs.c
  encode_rs.c
  ftrsd2.c
  gran.c
  igray.c
  init_rs.c
  tmoonsub.c
  usleep.c
  wrapkarn.c
  qra64/qra64.c
  qra64/qra64_subs.c
  qracodes/npfwht.c
  qracodes/pdmath.c 
  qracodes/qra12_63_64_irr_b.c
  qracodes/qra13_64_64_irr_e.c
  qracodes/qracodes.c
  qracodes/normrnd.c
)

if (WIN32)
  set (CSRCS ${CSRCS} ptt.c)
else (set (CSRCS ${CSRCS} ptt_unix.c))
endif (WIN32)

set (CXXSRCS
  ipcomm.cpp
  )

add_definitions (-DBIGSYM=1)
set_source_files_properties (sec_midn.f90 PROPERTIES COMPILE_FLAGS -fno-second-underscore)

#
# build our targets
#
add_library (m65impl STATIC ${FSRCS} ${CSRCS} ${CXXSRCS})
qt5_use_modules (m65impl Core)

add_executable (m65 m65.f90 m65a.f90)
target_link_libraries (m65 m65impl ${fftw3f_LIBRARIES})
add_dependencies (m65 fftw3f)
qt5_use_modules (m65 Core)

install (
  TARGETS m65
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  )
