cmake_minimum_required (VERSION 2.8.10 FATAL_ERROR)

if (APPLE)
  #
  # The following variables define the portability and compatability attributes of the Mac OS X build
  # they are choosen with care and should not be changed without good cause.
  #
  # Among other things these options are chosen to match the portability and compatability options of the
  # Qt framework dylibs which can be checked as follows:
  #
  # otool -l <binary> | grep -A3 LC_VERSION_MIN_MACOSX
  #
  set (CMAKE_OSX_DEPLOYMENT_TARGET 10.10
    CACHE STRING "Earliest version of OS X supported

Earliest version we can support with Qt 5.8, C++11 & libc++ is 10.10.
Do not override this if you intend to build an official deployable installer.")
  set (CMAKE_OSX_SYSROOT /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.11.sdk
    CACHE STRING "Mac OS X SDK to build with

Change this to the newest SDK available that you can install on your system (10.11 preferred).
Do not override this if you intend to build an official deployable installer.")
endif (APPLE)

project (wsjtx C CXX Fortran)

#
# CMake policies
#
if (POLICY CMP0020)
  cmake_policy (SET CMP0020 NEW) # link to Qt winmain on Windows
endif (POLICY CMP0020)

if (POLICY CMP0043)
  cmake_policy (SET CMP0043 NEW) # ignore COMPILE_DEFINITIONS_<CONFIG>
endif (POLICY CMP0043)

if (POLICY CMP0063)
  cmake_policy (SET CMP0063 NEW) # honour visibility properties for all library types
endif (POLICY CMP0063)

if (POLICY CMP0071)
  cmake_policy (SET CMP0071 NEW) # run automoc and autouic on generated sources
endif (POLICY CMP0071)

include (${PROJECT_SOURCE_DIR}/CMake/VersionCompute.cmake)
message (STATUS "Building ${CMAKE_PROJECT_NAME}-${wsjtx_VERSION}")

#
# project information
#
set (PROJECT_NAME "WSJT-X")
set (PROJECT_VENDOR "Joe Taylor, K1JT")
set (PROJECT_CONTACT "Joe Taylor <k1jt@arrl.net>")
set (PROJECT_COPYRIGHT "Copyright (C) 2001-2019 by Joe Taylor, K1JT")
set (PROJECT_HOMEPAGE http://www.physics.princeton.edu/pulsar/K1JT/wsjtx.html)
set (PROJECT_MANUAL wsjtx-main)
set (PROJECT_MANUAL_DIRECTORY_URL http://www.physics.princeton.edu/pulsar/K1JT/wsjtx-doc/)
set (PROJECT_SAMPLES_URL http://downloads.sourceforge.net/project/wsjt/)
set (PROJECT_SAMPLES_UPLOAD_DEST frs.sourceforge.net:/home/frs/project/wsjt/)
set (PROJECT_SUMMARY_DESCRIPTION "${PROJECT_NAME} - Digital Modes for Weak Signal Communicaitons in Amateur Radio.")
set (PROJECT_DESCRIPTION "${PROJECT_SUMMARY_DESCRIPTION}
 ${PROJECT_NAME} is a computer program designed to facilitate basic amateur
 radio communication using very weak signals. The first four letters in
 the program name stand for `(W)eak (S)ignal communication by
 K1(JT),` while the suffix `-X` indicates that ${PROJECT_NAME} started as
 an extended and experimental branch of the program
 WSJT.
 .
 ${PROJECT_NAME} Version 1.8 offers nine different protocols or modes: FT8,
 JT4, JT9, JT65, QRA64, ISCAT, MSK144, WSPR, and Echo.
 The first five are designed for making reliable QSOs under extreme
 weak-signal conditions. They use nearly identical message structure
 and source encoding.  JT65 and QRA64 were designed for EME
 (`moonbounce`) on the VHF/UHF bands and have also proven very
 effective for worldwide QRP communication on the HF bands.  QRA64 has
 a number of advantages over JT65, including better performance on the
 very weakest signals.  We imagine that over time it may replace JT65
 for EME use.  JT9 was originally designed for the LF, MF, and lower HF
 bands.  Its submode JT9A is 2 dB more sensitive than JT65 while using
 less than 10% of the bandwidth.  JT4 offers a wide variety of tone
 spacings and has proven highly effective for EME on microwave bands up
 to 24 GHz.  These four `slow` modes use one-minute timed sequences
 of alternating transmission and reception, so a minimal QSO takes four
 to six minutes — two or three transmissions by each station, one
 sending in odd UTC minutes and the other even.  FT8 is operationally
 similar but four times faster (15-second T/R sequences) and less
 sensitive by a few dB.  On the HF bands, world-wide QSOs are possible
 with any of these modes using power levels of a few watts (or even
 milliwatts) and compromise antennas.  On VHF bands and higher, QSOs
 are possible (by EME and other propagation types) at signal levels 10
 to 15 dB below those required for CW.
 .
 ISCAT, MSK144, and optionally submodes JT9E-H are `fast`
 protocols designed to take advantage of brief signal enhancements from
 ionized meteor trails, aircraft scatter, and other types of scatter
 propagation. These modes use timed sequences of 5, 10, 15, or 30 s
 duration.  User messages are transmitted repeatedly at high rate (up
 to 250 characters per second, for MSK144) to make good use of the
 shortest meteor-trail reflections or `pings`.  ISCAT uses free-form
 messages up to 28 characters long, while MSK144 uses the same
 structured messages as the slow modes and optionally an abbreviated
 format with hashed callsigns.
 .
 WSPR (pronounced `whisper`) stands for (W)eak (S)ignal
 (P)ropagation (R)eporter.  The WSPR protocol was designed for probing
 potential propagation paths using low-power transmissions. WSPR
 messages normally carry the transmitting station’s callsign, grid
 locator, and transmitter power in dBm, and they can be decoded at
 signal-to-noise ratios as low as -28 dB in a 2500 Hz bandwidth.  WSPR
 users with internet access can automatically upload reception
 reports to a central database called wsprnet that provides a mapping
 facility, archival storage, and many other features.
 .
 Echo mode allows you to detect and measure your own station's echoes
 from the moon, even if they are far below the audible threshold.
 .
 ${PROJECT_NAME} provides spectral displays for receiver passbands as wide as
 5 kHz, flexible rig control for nearly all modern radios used by
 amateurs, and a wide variety of special aids such as automatic Doppler
 tracking for EME QSOs and Echo testing.  The program runs equally well
 on Windows, Macintosh, and Linux systems, and installation packages
 are available for all three platforms.
 .
 Be sure to read the online ${PROJECT_NAME} User's Guide.")

set (CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/CMake/Modules ${CMAKE_MODULE_PATH})

# make sure that the default configuration is a RELEASE
if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE RELEASE CACHE STRING
    "Choose the type of build, options are: None Debug Release."
    FORCE)
endif (NOT CMAKE_BUILD_TYPE)
if (CMAKE_BUILD_TYPE MATCHES "[Dd][Ee][Bb][Uu][Gg]")
  set (is_debug_build 1)
endif ()


#
# Options & features
#
#	Some of these directly effect compilation by being defined in
#	wsjtx_config.h.in which makes them available to the C/C++
#	pre-processor.
#
include (CMakeDependentOption)

# Allow the developer to select if Dynamic or Static libraries are built
OPTION (BUILD_SHARED_LIBS "Build Shared Libraries" OFF)
# Set the LIB_TYPE variable to STATIC
SET (LIB_TYPE STATIC)
if (BUILD_SHARED_LIBS)
  # User wants to build Dynamic Libraries, so change the LIB_TYPE variable to CMake keyword 'SHARED'
  set (LIB_TYPE SHARED)
endif (BUILD_SHARED_LIBS)

option (UPDATE_TRANSLATIONS "Update source translation translations/*.ts
files (WARNING: make clean will delete the source .ts files! Danger!)")
option (WSJT_SHARED_RUNTIME "Debugging option that allows running from a shared Cloud directory.")
option (WSJT_QDEBUG_TO_FILE "Redirect Qt debuging messages to a trace file.")
option (WSJT_TRACE_CAT "Debugging option that turns on CAT diagnostics.")
option (WSJT_TRACE_CAT_POLLS "Debugging option that turns on CAT diagnostics during polling.")
option (WSJT_HAMLIB_TRACE "Debugging option that turns on minimal Hamlib internal diagnostics.")
option (WSJT_SOFT_KEYING "Apply a ramp to CW keying envelope to reduce transients." ON)
option (WSJT_SKIP_MANPAGES "Skip *nix manpage generation.")
option (WSJT_GENERATE_DOCS "Generate documentation files." ON)
option (WSJT_RIG_NONE_CAN_SPLIT "Allow split operation with \"None\" as rig.")
option (WSJT_TRACE_UDP "Debugging option that turns on UDP message protocol diagnostics.")
option (WSJT_BUILD_UTILS "Build simulators and code demonstrators." ON)

CMAKE_DEPENDENT_OPTION (WSJT_HAMLIB_VERBOSE_TRACE "Debugging option that turns on full Hamlib internal diagnostics." OFF WSJT_HAMLIB_TRACE OFF)
CMAKE_DEPENDENT_OPTION (WSJT_QDEBUG_IN_RELEASE "Leave Qt debugging statements in Release configuration." OFF
  "NOT is_debug_build" OFF)
CMAKE_DEPENDENT_OPTION (WSJT_ENABLE_EXPERIMENTAL_FEATURES "Enable features not fully ready for public releases." ON
  is_debug_build OFF)
CMAKE_DEPENDENT_OPTION (WSJT_CREATE_WINMAIN
  "The wsjtx target is normally built as GUI executable with a WinMain entry point on Windows,
if you want a console application instead then set this option to OFF.

If you just want to see the debug output from the application then the easiest way is to
attach a debugger which will then receive the console output inside its console." ON
  "WIN32" OFF)

set (PROJECT_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
if (NOT PROJECT_ARCHITECTURE)
  # This is supposed to happen already on Windows
  if (CMAKE_SIZEOF_VOID_P MATCHES 8)
    set (PROJECT_ARCHITECTURE "x64")
  else ()
    set (PROJECT_ARCHITECTURE "$ENV{PROCESSOR_ARCHITECTURE}")
  endif ()
endif (NOT PROJECT_ARCHITECTURE)
message (STATUS "******************************************************")
message (STATUS "Building for for: ${CMAKE_SYSTEM_NAME}-${PROJECT_ARCHITECTURE}")
message (STATUS "******************************************************")

#
# install locations
#

if (APPLE)
  set (CMAKE_INSTALL_BINDIR ${CMAKE_PROJECT_NAME}.app/Contents/MacOS)
  set (CMAKE_INSTALL_DATAROOTDIR ${CMAKE_PROJECT_NAME}.app/Contents/Resources)
endif ()

include (GNUInstallDirs)

set (PLUGIN_DESTINATION ${CMAKE_INSTALL_LIBDIR}/plugins)
set (QT_CONF_DESTINATION ${CMAKE_INSTALL_BINDIR})
if (WIN32)
  set (PLUGIN_DESTINATION plugins)
elseif (APPLE)
  set (PLUGIN_DESTINATION ${CMAKE_INSTALL_BINDIR}/../PlugIns)
  set (QT_CONF_DESTINATION ${CMAKE_INSTALL_DATAROOTDIR})
endif ()

set (WSJT_PLUGIN_DESTINATION ${PLUGIN_DESTINATION} CACHE PATH "Path for plugins")
set (WSJT_QT_CONF_DESTINATION ${QT_CONF_DESTINATION} CACHE PATH "Path for the qt.conf file")


#
# Project sources
#
set (wsjt_qt_CXXSRCS
  qt_helpers.cpp
  widgets/MessageBox.cpp
  MetaDataRegistry.cpp
  NetworkServerLookup.cpp
  revision_utils.cpp
  WFPalette.cpp
  Radio.cpp
  RadioMetaType.cpp
  models/IARURegions.cpp
  models/Bands.cpp
  models/Modes.cpp
  models/FrequencyList.cpp
  models/StationList.cpp
  widgets/FrequencyLineEdit.cpp
  widgets/FrequencyDeltaLineEdit.cpp
  item_delegates/CandidateKeyFilter.cpp
  item_delegates/ForeignKeyDelegate.cpp
  validators/LiveFrequencyValidator.cpp
  GetUserId.cpp
  TraceFile.cpp
  AudioDevice.cpp
  Transceiver.cpp
  TransceiverBase.cpp
  EmulateSplitTransceiver.cpp
  TransceiverFactory.cpp
  PollingTransceiver.cpp
  HamlibTransceiver.cpp
  HRDTransceiver.cpp
  DXLabSuiteCommanderTransceiver.cpp
  NetworkMessage.cpp
  MessageClient.cpp
  widgets/LettersSpinBox.cpp
  widgets/HintedSpinBox.cpp
  widgets/RestrictedSpinBox.cpp
  widgets/HelpTextWindow.cpp
  SampleDownloader.cpp
  SampleDownloader/DirectoryDelegate.cpp
  SampleDownloader/Directory.cpp
  SampleDownloader/FileNode.cpp
  SampleDownloader/RemoteFile.cpp
  DisplayManual.cpp
  MultiSettings.cpp
  validators/MaidenheadLocatorValidator.cpp
  validators/CallsignValidator.cpp
  widgets/SplashScreen.cpp
  EqualizationToolsDialog.cpp
  widgets/DoubleClickablePushButton.cpp
  widgets/DoubleClickableRadioButton.cpp
  LotWUsers.cpp
  models/DecodeHighlightingModel.cpp
  widgets/DecodeHighlightingListView.cpp
  models/FoxLog.cpp
  widgets/AbstractLogWindow.cpp
  widgets/FoxLogWindow.cpp
  widgets/CabrilloLogWindow.cpp
  item_delegates/CallsignDelegate.cpp
  item_delegates/MaidenheadLocatorDelegate.cpp
  item_delegates/FrequencyDelegate.cpp
  item_delegates/FrequencyDeltaDelegate.cpp
  models/CabrilloLog.cpp
  logbook/AD1CCty.cpp
  logbook/WorkedBefore.cpp
  logbook/Multiplier.cpp
  )

set (wsjt_qtmm_CXXSRCS
  Audio/BWFFile.cpp
  )

set (jt9_FSRCS
  lib/jt9.f90
  lib/jt9a.f90
  )

set (jt9_CXXSRCS
  lib/ipcomm.cpp
  )

set (wsjtx_CXXSRCS
  logbook/logbook.cpp
  psk_reporter.cpp
  Modulator.cpp
  Detector.cpp
  widgets/logqso.cpp
  widgets/displaytext.cpp
  decodedtext.cpp
  getfile.cpp
  soundout.cpp
  soundin.cpp
  widgets/meterwidget.cpp
  widgets/signalmeter.cpp
  widgets/plotter.cpp
  widgets/widegraph.cpp
  widgets/echograph.cpp
  widgets/echoplot.cpp
  widgets/fastgraph.cpp
  widgets/fastplot.cpp
  widgets/about.cpp
  widgets/astro.cpp
  widgets/messageaveraging.cpp
  widgets/colorhighlighting.cpp
  WsprTxScheduler.cpp
  widgets/mainwindow.cpp
  Configuration.cpp
  main.cpp
  wsprnet.cpp
  WSPRBandHopping.cpp
  widgets/ExportCabrillo.cpp
  )

set (wsjt_CXXSRCS
  lib/crc10.cpp
  lib/crc13.cpp
  lib/crc14.cpp
  )
# deal with a GCC v6 UB error message
set_source_files_properties (
  lib/crc10.cpp
  lib/crc13.cpp
  lib/crc14.cpp
  PROPERTIES COMPILE_FLAGS -fpermissive)

if (WIN32)
  set (wsjt_CXXSRCS
    ${wsjt_CXXSRCS}
    killbyname.cpp
    )

  set (wsjt_qt_CXXSRCS
    ${wsjt_qt_CXXSRCS}
    OmniRigTransceiver.cpp
    )
endif (WIN32)

set (wsjt_FSRCS
  # put module sources first in the hope that they get rebuilt before use
  lib/crc.f90
  lib/fftw3mod.f90
  lib/hashing.f90
  lib/iso_c_utilities.f90
  lib/jt4.f90
  lib/jt4_decode.f90
  lib/jt65_decode.f90
  lib/jt65_mod.f90
  lib/ft8_decode.f90
  lib/ft4_decode.f90
  lib/jt9_decode.f90
  lib/options.f90
  lib/packjt.f90
  lib/77bit/packjt77.f90
  lib/readwav.f90
  lib/timer_C_wrapper.f90
  lib/timer_impl.f90
  lib/timer_module.f90
  lib/wavhdr.f90

  # remaining non-module sources
  lib/addit.f90
  lib/afc65b.f90
  lib/afc9.f90
  lib/ana64.f90
  lib/ana932.f90
  lib/analytic.f90
  lib/astro.f90
  lib/astrosub.f90
  lib/astro0.f90
  lib/avecho.f90
  lib/averms.f90
  lib/azdist.f90
  lib/badmsg.f90
  lib/ft8/baseline.f90
  lib/ft4/ft4_baseline.f90
  lib/bpdecode40.f90
  lib/bpdecode128_90.f90
  lib/ft8/bpdecode174_91.f90
  lib/baddata.f90
  lib/calibrate.f90
  lib/ccf2.f90
  lib/ccf65.f90
  lib/ft8/chkcrc13a.f90
  lib/ft8/chkcrc14a.f90
  lib/chkcall.f90
  lib/chkhist.f90
  lib/chkmsg.f90
  lib/chkss2.f90
  lib/ft4/clockit.f90
  lib/ft8/compress.f90
  lib/coord.f90
  lib/db.f90
  lib/decode4.f90
  lib/decode65a.f90
  lib/decode65b.f90
  lib/decode9w.f90
  lib/decoder.f90
  lib/deep4.f90
  lib/deg2grid.f90
  lib/degrade_snr.f90
  lib/demod64a.f90
  lib/determ.f90
  lib/downsam9.f90
  lib/encode232.f90
  lib/encode4.f90
  lib/encode_msk40.f90
  lib/encode_128_90.f90
  lib/ft8/encode174.f90
  lib/ft8/encode174_91.f90
  lib/entail.f90
  lib/ephem.f90
  lib/extract.f90
  lib/extract4.f90
  lib/extractmessage77.f90
  lib/ft8/extractmessage174.f90
  lib/ft8/extractmessage174_91.f90
  lib/fano232.f90
  lib/fast9.f90
  lib/fast_decode.f90
  lib/fchisq.f90
  lib/fchisq0.f90
  lib/fchisq65.f90
  lib/fil3.f90
  lib/fil3c.f90
  lib/fil4.f90
  lib/fil6521.f90
  lib/filbig.f90
  lib/ft8/filt8.f90
  lib/fitcal.f90
  lib/flat1.f90
  lib/flat1a.f90
  lib/flat1b.f90
  lib/flat2.f90
  lib/flat4.f90
  lib/flat65.f90
  lib/fmtmsg.f90
  lib/foldspec9f.f90
  lib/four2a.f90
  lib/ft8/foxfilt.f90
  lib/ft8/foxgen.f90
  lib/ft8/foxgen_wrap.f90
  lib/fqso_first.f90
  lib/freqcal.f90
  lib/ft8/ft8apset.f90
  lib/ft8/ft8b.f90
  lib/ft8/ft8code.f90
  lib/ft8/ft8_downsample.f90
  lib/ft8.f90
  lib/ft8dec.f90
  lib/ft8/ft8sim.f90
  lib/ft8/ft8sim_gfsk.f90
  lib/gen4.f90
  lib/gen65.f90
  lib/gen9.f90
  lib/geniscat.f90
  lib/ft8/genft8.f90
  lib/genmsk_128_90.f90
  lib/genmsk40.f90
  lib/ft4/genft4.f90
  lib/ft4/gen_ft4wave.f90
  lib/ft8/gen_ft8wave.f90
  lib/genqra64.f90
  lib/ft8/genft8refsig.f90
  lib/genwspr.f90
  lib/geodist.f90
  lib/getlags.f90
  lib/getmet4.f90
  lib/ft2/gfsk_pulse.f90
  lib/graycode.f90
  lib/graycode65.f90
  lib/grayline.f90
  lib/grid2deg.f90
  lib/ft8/h1.f90
  lib/hash.f90
  lib/hint65.f90
  lib/hspec.f90
  lib/indexx.f90
  lib/init_random_seed.f90
  lib/interleave4.f90
  lib/interleave63.f90
  lib/interleave9.f90
  lib/inter_wspr.f90
  lib/iscat.f90
  lib/jplsubs.f
  lib/jt9fano.f90
  lib/jtmsg.f90
  lib/libration.f90
  lib/lorentzian.f90
  lib/lpf1.f90
  lib/mixlpf.f90
  lib/makepings.f90
  lib/moondopjpl.f90
  lib/morse.f90
  lib/move.f90
  lib/msk40decodeframe.f90
  lib/msk144decodeframe.f90
  lib/msk40spd.f90
  lib/msk144spd.f90
  lib/msk40sync.f90
  lib/msk144sync.f90
  lib/msk40_freq_search.f90
  lib/msk144_freq_search.f90
  lib/mskrtd.f90
  lib/msk144signalquality.f90
  lib/msk144sim.f90
  lib/mskrtd.f90
  lib/nuttal_window.f90
  lib/ft4/ft4sim.f90
  lib/ft4/ft4sim_mult.f90
  lib/ft4/ft4_downsample.f90
  lib/77bit/my_hash.f90
  lib/wsprd/osdwspr.f90
  lib/ft8/osd174_91.f90
  lib/pctile.f90
  lib/peakdt9.f90
  lib/peakup.f90
  lib/plotsave.f90
  lib/platanh.f90
  lib/pltanh.f90
  lib/polyfit.f90
  lib/prog_args.f90
  lib/ps4.f90
  lib/qra64a.f90
  lib/refspectrum.f90
  lib/savec2.f90
  lib/sec_midn.f90
  lib/setup65.f90
  lib/sh65.f90
  lib/sh65snr.f90
  lib/slasubs.f
  lib/sleep_msec.f90
  lib/slope.f90
  lib/smo.f90
  lib/smo121.f90
  lib/softsym.f90
  lib/softsym9f.f90
  lib/softsym9w.f90
  lib/shell.f90
  lib/spec64.f90
  lib/spec9f.f90
  lib/stdmsg.f90
  lib/subtract65.f90
  lib/ft8/subtractft8.f90
  lib/ft4/subtractft4.f90
  lib/sun.f90
  lib/symspec.f90
  lib/symspec2.f90
  lib/symspec65.f90
  lib/sync4.f90
  lib/sync64.f90
  lib/sync65.f90
  lib/ft4/getcandidates4.f90
  lib/ft4/get_ft4_bitmetrics.f90
  lib/ft8/sync8.f90
  lib/ft8/sync8d.f90
  lib/ft4/sync4d.f90
  lib/sync9.f90
  lib/sync9f.f90
  lib/sync9w.f90
  lib/synciscat.f90
  lib/timf2.f90
  lib/tweak1.f90
  lib/twkfreq.f90
  lib/ft8/twkfreq1.f90
  lib/twkfreq65.f90
  lib/update_recent_calls.f90
  lib/update_msk40_hasharray.f90
  lib/ft8/watterson.f90
  lib/wav11.f90
  lib/wav12.f90
  lib/xcor.f90
  lib/xcor4.f90
  lib/wqdecode.f90
  lib/wqencode.f90
  lib/wspr_downsample.f90
  lib/zplot9.f90
  )

# temporary workaround for a gfortran v7.3 ICE on Fedora 27 64-bit
set_source_files_properties (lib/slasubs.f PROPERTIES COMPILE_FLAGS -O2)

set (ka9q_CSRCS
  lib/ftrsd/decode_rs.c
  lib/ftrsd/encode_rs.c
  lib/ftrsd/init_rs.c
  )
set_source_files_properties (${ka9q_CSRCS} PROPERTIES COMPILE_FLAGS -Wno-sign-compare)

set (qra_CSRCS
  lib/qra/qra64/qra64.c
  lib/qra/qra64/qra64_subs.c
  lib/qra/qracodes/npfwht.c
  lib/qra/qracodes/pdmath.c 
  lib/qra/qracodes/qra12_63_64_irr_b.c
  lib/qra/qracodes/qra13_64_64_irr_e.c
  lib/qra/qracodes/qracodes.c
  lib/qra/qracodes/normrnd.c
  )

set (wsjt_CSRCS
  ${ka9q_CSRCS}
  lib/ftrsd/ftrsdap.c
  lib/sgran.c
  lib/golay24_table.c
  lib/gran.c
  lib/igray.c
  lib/init_random_seed.c
  lib/ldpc32_table.c
  lib/wsprd/nhash.c
  lib/tab.c
  lib/tmoonsub.c
  lib/usleep.c
  lib/vit213.c
  lib/wisdom.c
  lib/wrapkarn.c
  ${ldpc_CSRCS}
  ${qra_CSRCS}
  )

set (wsjt_qt_UISRCS
  wf_palette_design_dialog.ui
  widgets/FoxLogWindow.ui
  widgets/CabrilloLogWindow.ui
  )

set (wsprsim_CSRCS
  lib/wsprd/wsprsim.c
  lib/wsprd/wsprsim_utils.c
  lib/wsprd/wsprd_utils.c
  lib/wsprd/fano.c
  lib/wsprd/tab.c
  lib/wsprd/nhash.c
  )

set (wsprd_CSRCS
  lib/wsprd/wsprd.c
  lib/wsprd/wsprsim_utils.c
  lib/wsprd/wsprd_utils.c
  lib/wsprd/fano.c
  lib/wsprd/jelinek.c
  lib/wsprd/tab.c
  lib/wsprd/nhash.c
  lib/init_random_seed.c
  )

set (wsjtx_UISRCS
  widgets/mainwindow.ui
  widgets/about.ui
  widgets/astro.ui
  widgets/colorhighlighting.ui
  widgets/echograph.ui
  widgets/fastgraph.ui
  widgets/messageaveraging.ui
  widgets/widegraph.ui
  widgets/logqso.ui
  Configuration.ui
  widgets/ExportCabrillo.ui
  )

set (UDP_library_CXXSRCS
  Radio.cpp
  RadioMetaType.cpp
  NetworkMessage.cpp
  MessageServer.cpp
  )

set (UDP_library_HEADERS
  Radio.hpp
  MessageServer.hpp
  ${PROJECT_BINARY_DIR}/udp_export.h
  )

set (message_aggregator_CXXSRCS
  UDPExamples/MessageAggregator.cpp
  UDPExamples/MessageAggregatorMainWindow.cpp
  UDPExamples/DecodesModel.cpp
  UDPExamples/BeaconsModel.cpp
  UDPExamples/ClientWidget.cpp
  validators/MaidenheadLocatorValidator.cpp
  )

set (message_aggregator_STYLESHEETS
  UDPExamples/qss/default.qss
  )

set (qcp_CXXSRCS
  qcustomplot-source/qcustomplot.cpp
  )

set (all_CXXSRCS
  ${wsjt_CXXSRCS}
  ${wsjt_qt_CXXSRCS}
  ${wsjt_qtmm_CXXSRCS}
  ${jt9_CXXSRCS}
  ${wsjtx_CXXSRCS}
  ${qcp_CXXSRCS}
  )

set (all_C_and_CXXSRCS
  ${wsjt_CSRCS}
  ${wsprsim_CSRCS}
  ${wsprd_CSRCS}
  ${all_CXXSRCS}
  )

set (TOP_LEVEL_RESOURCES
  shortcuts.txt
  mouse_commands.txt
  prefixes.txt
  cty.dat
  icons/Darwin/wsjtx.iconset/icon_128x128.png
  contrib/gpl-v3-logo.svg
  artwork/splash.png
  )

set (PALETTE_FILES
  Palettes/Banana.pal
  Palettes/Blue1.pal
  Palettes/Blue2.pal
  Palettes/Blue3.pal
  Palettes/Brown.pal
  Palettes/Cyan1.pal
  Palettes/Cyan2.pal
  Palettes/Cyan3.pal
  Palettes/Default.pal
  Palettes/Digipan.pal
  Palettes/Fldigi.pal
  Palettes/Gray1.pal
  Palettes/Gray2.pal
  Palettes/Green1.pal
  Palettes/Green2.pal
  Palettes/Jungle.pal
  Palettes/Linrad.pal
  Palettes/Negative.pal
  Palettes/Orange.pal
  Palettes/Pink.pal
  Palettes/Rainbow.pal
  Palettes/Scope.pal
  Palettes/Sunburst.pal
  Palettes/VK4BDJ.pal
  Palettes/YL2KF.pal
  Palettes/Yellow1.pal
  Palettes/Yellow2.pal
  Palettes/ZL1FZ.pal
)

if (APPLE)
  set (WSJTX_ICON_FILE ${CMAKE_PROJECT_NAME}.icns)
  set (ICONSRCS
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_16x16.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_16x16@2x.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_32x32.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_32x32@2x.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_128x128.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_128x128@2x.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_256x256.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_256x256@2x.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_512x512.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_512x512@2x.png
    )
  add_custom_command (
    OUTPUT ${WSJTX_ICON_FILE}
    COMMAND iconutil -c icns --output "${CMAKE_BINARY_DIR}/${WSJTX_ICON_FILE}" "${CMAKE_SOURCE_DIR}/icons/Darwin/${CMAKE_PROJECT_NAME}.iconset"
    DEPENDS ${ICONSRCS}
    COMMENT "Building Icons"
    )
endif (APPLE)

set_source_files_properties (${WSJTX_ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

if (WSJT_QDEBUG_IN_RELEASE)
  # context info in Qt message handler in release configuration
  set_property (DIRECTORY APPEND PROPERTY
    COMPILE_DEFINITIONS $<$<NOT:$<CONFIG:Debug>>:QT_MESSAGELOGCONTEXT>
    )
else (WSJT_QDEBUG_IN_RELEASE)
  # disable Qt trace and warning messages from release configurations
  set_property (DIRECTORY APPEND PROPERTY
    COMPILE_DEFINITIONS $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT;QT_NO_WARNING_OUTPUT>
    )
endif (WSJT_QDEBUG_IN_RELEASE)

set_property (SOURCE ${all_C_and_CXXSRCS} APPEND_STRING PROPERTY COMPILE_FLAGS " -include wsjtx_config.h")
set_property (SOURCE ${all_C_and_CXXSRCS} APPEND PROPERTY OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/wsjtx_config.h)

if (WIN32)
  # generate the OmniRig COM interface source
  find_program (DUMPCPP dumpcpp)
  if (DUMPCPP-NOTFOUND)
    message (FATAL_ERROR "dumpcpp tool not found")
  endif (DUMPCPP-NOTFOUND)
  execute_process (
    COMMAND ${DUMPCPP} -getfile {4FE359C5-A58F-459D-BE95-CA559FB4F270}
    OUTPUT_VARIABLE AXSERVER
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  string (STRIP "${AXSERVER}" AXSERVER)
  if (NOT AXSERVER)
    message (FATAL_ERROR "You need to install OmniRig on this computer")
  endif (NOT AXSERVER)
  string (REPLACE "\"" "" AXSERVER ${AXSERVER})
  file (TO_CMAKE_PATH ${AXSERVER} AXSERVERSRCS)
endif ()


#
# decide on platform specifc packing and fixing up
#
if (APPLE)
  set (WSJTX_BUNDLE_VERSION ${wsjtx_VERSION})

  # make sure CMAKE_INSTALL_PREFIX ends in /
  string (LENGTH "${CMAKE_INSTALL_PREFIX}" LEN)
  math (EXPR LEN "${LEN} -1" )
  string (SUBSTRING "${CMAKE_INSTALL_PREFIX}" ${LEN} 1 ENDCH)
  if (NOT "${ENDCH}" STREQUAL "/")
    set (CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}/")
  endif ()
endif (APPLE)


#
# find some useful tools
#
find_program(CTAGS ctags)
find_program(ETAGS etags)

#
# Boost
#
set (Boost_NO_SYSTEM_PATHS TRUE)
if (Boost_NO_SYSTEM_PATHS)
  set (BOOST_ROOT ${PROJECT_SOURCE_DIR}/boost)
endif ()
find_package (Boost 1.63 REQUIRED)

#
# OpenMP
#
find_package (OpenMP)

#
# fftw3 single precision library
#
find_package (FFTW3 COMPONENTS double single threads REQUIRED)

#
# libhamlib setup
#
set (hamlib_STATIC 1)
find_package (hamlib 3 REQUIRED)
find_program (RIGCTL_EXE rigctl)
find_program (RIGCTLD_EXE rigctld)
find_program (RIGCTLCOM_EXE rigctlcom)

message (STATUS "hamlib_INCLUDE_DIRS: ${hamlib_INCLUDE_DIRS}")
message (STATUS "hamlib_LIBRARIES: ${hamlib_LIBRARIES}")
message (STATUS "hamlib_LIBRARY_DIRS: ${hamlib_LIBRARY_DIRS}")


#
# Qt5 setup
#

# Widgets finds its own dependencies.
find_package (Qt5 COMPONENTS Widgets Multimedia PrintSupport Sql LinguistTools REQUIRED)

if (WIN32)
  add_definitions (-DQT_NEEDS_QTMAIN)
  find_package (Qt5AxContainer REQUIRED)
endif (WIN32)

#
# sub-directories
#
if (EXISTS ${CMAKE_SOURCE_DIR}/samples AND IS_DIRECTORY ${CMAKE_SOURCE_DIR}/samples)
  add_subdirectory (samples)
endif ()
if (WSJT_GENERATE_DOCS)
  add_subdirectory (doc)
endif (WSJT_GENERATE_DOCS)


#
# Library building setup
#
include (GenerateExportHeader)
set (CMAKE_CXX_VISIBILITY_PRESET hidden)
set (CMAKE_C_VISIBILITY_PRESET hidden)
set (CMAKE_Fortran_VISIBILITY_PRESET hidden)
set (CMAKE_VISIBILITY_INLINES_HIDDEN ON)
#set (CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE ON)


#
# C & C++ setup
#
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fexceptions -frtti")

if (NOT APPLE)
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-pragmas")
  if (${OPENMP_FOUND})
    if (OpenMP_C_FLAGS)
      set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_C_FLAGS}")
      set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    endif ()
  endif ()
  set (CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fdata-sections -ffunction-sections")
  set (CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -fdata-sections -ffunction-sections")
  set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fdata-sections -ffunction-sections")
  set (CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -fdata-sections -ffunction-sections")
endif (NOT APPLE)

if (WIN32)
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
endif (WIN32)
if (APPLE AND ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -stdlib=libc++")
else ()
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --std=gnu++11 -pthread")
endif ()


#
# Fortran setup
#
set (General_FFLAGS "-Wall -Wno-conversion -fno-second-underscore")

# FFLAGS depend on the compiler
get_filename_component (Fortran_COMPILER_NAME ${CMAKE_Fortran_COMPILER} NAME)

if (Fortran_COMPILER_NAME MATCHES "gfortran.*")
  # gfortran

  # CMake compiler test is supposed to do this but doesn't yet
  if (CMAKE_OSX_DEPLOYMENT_TARGET)
    set (CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif (CMAKE_OSX_DEPLOYMENT_TARGET)
  if (CMAKE_OSX_SYSROOT)
    set (CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -isysroot ${CMAKE_OSX_SYSROOT}")
  endif (CMAKE_OSX_SYSROOT)

  set (CMAKE_Fortran_FLAGS_RELEASE "${CMAKE_Fortran_FLAGS_RELEASE} -fbounds-check -funroll-all-loops -fno-f2c ${General_FFLAGS}")
  set (CMAKE_Fortran_FLAGS_DEBUG   "${CMAKE_Fortran_FLAGS_DEBUG} -fbounds-check -fno-f2c ${General_FFLAGS}")
elseif (Fortran_COMPILER_NAME MATCHES "ifort.*")
  # ifort (untested)
  set (CMAKE_Fortran_FLAGS_RELEASE "${CMAKE_Fortran_FLAGS_RELEASE} -f77rtl ${General_FFLAGS}")
  set (CMAKE_Fortran_FLAGS_DEBUG   "${CMAKE_Fortran_FLAGS_DEBUG} -f77rtl ${General_FFLAGS}")
elseif (Fortran_COMPILER_NAME MATCHES "g77")
  # g77
  set (CMAKE_Fortran_FLAGS_RELEASE "${CMAKE_Fortran_FLAGS_RELEASE} -funroll-all-loops -fno-f2c -m32 ${General_FFLAGS}")
  set (CMAKE_Fortran_FLAGS_DEBUG   "${CMAKE_Fortran_FLAGS_DEBUG} -fbounds-check -fno-f2c -m32 ${General_FFLAGS}")
else (Fortran_COMPILER_NAME MATCHES "gfortran.*")
  message ("CMAKE_Fortran_COMPILER full path: " ${CMAKE_Fortran_COMPILER})
  message ("Fortran compiler: " ${Fortran_COMPILER_NAME})
  message ("No optimized Fortran compiler flags are known, we just try -O3...")
  set (CMAKE_Fortran_FLAGS_RELEASE "${CMAKE_Fortran_FLAGS_RELEASE} -O3 ${General_FFLAGS}")
  set (CMAKE_Fortran_FLAGS_DEBUG   "${CMAKE_Fortran_FLAGS_DEBUG} -fbounds-check ${General_FFLAGS}")
endif (Fortran_COMPILER_NAME MATCHES "gfortran.*")


#
# Linker setup
#
if (NOT APPLE)
  set (CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,--gc-sections")
  set (CMAKE_EXE_LINKER_FLAGS_MINSIZEREL "${CMAKE_EXE_LINKER_FLAGS_MINSIZEREL} -Wl,--gc-sections")
endif (NOT APPLE)


#
# setup and test Fortran C/C++ interaction
#

include (FortranCInterface)
FortranCInterface_VERIFY (CXX)
FortranCInterface_HEADER (FC.h MACRO_NAMESPACE "FC_" SYMBOL_NAMESPACE "FC_"
  SYMBOLS
  grayline
  )


#
# sort out pre-requisites
#

#
# Setup RPATH so that built executable targets will run in both the
# build tree and the install location without having to set a
# (DYLD|LD)_LIBRARY_PATH override.
#

# use the full RPATH of the build tree
set (CMAKE_SKIP_BUILD_RPATH FALSE)

# when building, don't use the install RPATH, it will still be used
# later on in the install phase
set (CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

# set (CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

# add the automaticaly determined parts of the RPATH which point to
# directories outside of the build tree to the install RPATH
set (CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# the RPATH to be used when installing, but only if it's not a system
# directory
# list (FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}" isSystemDir)
# if ("${isSystemDir}" STREQUAL "-1")
#   set (CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
# endif ("${isSystemDir}" STREQUAL "-1")

set (QT_NEED_RPATH FALSE)
if (NOT "${QT_LIBRARY_DIR}" STREQUAL "/lib" AND NOT "${QT_LIBRARY_DIR}" STREQUAL "/usr/lib" AND NOT "${QT_LIBRARY_DIR}" STREQUAL "/lib64" AND NOT "${QT_LIBRARY_DIR}" STREQUAL "/usr/lib64")
  set (QT_NEED_RPATH TRUE)
endif ()

#
# stuff only qmake can tell us
#
get_target_property (QMAKE_EXECUTABLE Qt5::qmake LOCATION)
function (QUERY_QMAKE VAR RESULT)
  exec_program (${QMAKE_EXECUTABLE} ARGS "-query ${VAR}" RETURN_VALUE return_code OUTPUT_VARIABLE output)
  if (NOT return_code)
    file (TO_CMAKE_PATH "${output}" output)
    set (${RESULT} ${output} PARENT_SCOPE)
  endif (NOT return_code)
  message (STATUS "Asking qmake for ${RESULT} and got ${output}")
endfunction (QUERY_QMAKE)

query_qmake (QT_INSTALL_PLUGINS QT_PLUGINS_DIR)
query_qmake (QT_INSTALL_IMPORTS QT_IMPORTS_DIR)
query_qmake (QT_HOST_DATA QT_DATA_DIR)
set (QT_MKSPECS_DIR ${QT_DATA_DIR}/mkspecs)

# Tell CMake to run moc when necessary
set (CMAKE_AUTOMOC ON)
include_directories (${CMAKE_CURRENT_BINARY_DIR})

# don't use Qt "keywords" signal, slot, emit in generated files to
# avoid compatability issue with other libraries
# ADD_DEFINITIONS (-DQT_NO_KEYWORDS)
# ADD_DEFINITIONS (-DUNICODE)	#as per qmake

# As moc files are generated in the binary dir, tell CMake to always
# look for includes there:
set (CMAKE_INCLUDE_CURRENT_DIR ON)

# project definitions
add_definitions (-DQT5 -DCMAKE_BUILD -DBIGSYM=1)
if (CMAKE_HOST_UNIX)
  add_definitions (-DUNIX)
elseif (CMAKE_HOST_WIN32)
  add_definitions (-DWIN32)
endif ()

#
# source navigation
#
set (sources
  ${CMAKE_SOURCE_DIR}/*
  ${CMAKE_SOURCE_DIR}/logbook/*
  ${CMAKE_SOURCE_DIR}/lib/*
  )
add_custom_target (ctags COMMAND ${CTAGS} -o ${CMAKE_SOURCE_DIR}/tags -R ${sources})
add_custom_target (etags COMMAND ${ETAGS} -o ${CMAKE_SOURCE_DIR}/TAGS -R ${sources})


# Qt i18n
set (LANGUAGES
  en_GB
  pt_PT
  )
foreach (lang_ ${LANGUAGES})
  file (TO_NATIVE_PATH translations/wsjtx_${lang_}.ts ts_)
  list (APPEND TS_FILES ${ts_})
endforeach ()  
if (UPDATE_TRANSLATIONS)
  message (STATUS "UPDATE_TRANSLATIONS option is set.")
  qt5_create_translation (
    QM_FILES ${wsjt_qt_UISRCS} ${wsjtx_UISRCS} ${wsjt_qt_CXXSRCS} ${wsjtx_CXXSRCS}
    ${TS_FILES}
    )
else ()
  qt5_add_translation (QM_FILES ${TS_FILES})
endif ()
add_custom_target (translations DEPENDS ${QM_FILES})
set_property (DIRECTORY PROPERTY CLEAN_NO_CUSTOM TRUE)
# do this after i18n to stop lupdate walking the boost tree which it
# chokes on
if (Boost_FOUND)
  include_directories (${Boost_INCLUDE_DIRS})
endif ()

# embedded resources
function (add_resources resources path)
  foreach (resource_file_ ${ARGN})
    get_filename_component (name_ ${resource_file_} NAME)
    if (IS_ABSOLUTE "${resource_file_}")
      file (TO_NATIVE_PATH ${resource_file_} source_)
    else ()
      file (TO_NATIVE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/${resource_file_} source_)
    endif ()
    file (TO_NATIVE_PATH ${path}/${name_} dest_)
    set (resources_ "${resources_}\n    <file alias=\"${dest_}\">${source_}</file>")
    set (${resources} ${${resources}}${resources_} PARENT_SCOPE)
  endforeach (resource_file_ ${ARGN})
endfunction (add_resources resources path)

add_resources (wsjtx_RESOURCES "" ${TOP_LEVEL_RESOURCES})
add_resources (wsjtx_RESOURCES /Palettes ${PALETTE_FILES})
add_resources (wsjtx_RESOURCES /Translations ${QM_FILES})

configure_file (wsjtx.qrc.in wsjtx.qrc @ONLY)


# UI generation
qt5_wrap_ui (wsjt_qt_GENUISRCS ${wsjt_qt_UISRCS})
qt5_wrap_ui (wsjtx_GENUISRCS ${wsjtx_UISRCS})

# Resource generation
qt5_add_resources (wsjtx_RESOURCES_RCC
  ${CMAKE_BINARY_DIR}/wsjtx.qrc
  contrib/QDarkStyleSheet/qdarkstyle/style.qrc
  )

# AX COM servers
if (WIN32)
  include (QtAxMacros)
  wrap_ax_server (GENAXSRCS ${AXSERVERSRCS})
endif (WIN32)

#
# targets
#

# build a library of package functionality (without and optionally with OpenMP support)
add_library (wsjt_cxx STATIC ${wsjt_CSRCS} ${wsjt_CXXSRCS})

# build an OpenMP variant of the Fortran library routines
add_library (wsjt_fort STATIC ${wsjt_FSRCS})
target_link_libraries (wsjt_fort  ${FFTW3_LIBRARIES})
if (${OPENMP_FOUND} OR APPLE)
  add_library (wsjt_fort_omp STATIC ${wsjt_FSRCS})
  target_link_libraries (wsjt_fort_omp  ${FFTW3_LIBRARIES})
  if (OpenMP_C_FLAGS)
    set_target_properties (wsjt_fort_omp
      PROPERTIES
      COMPILE_FLAGS "${OpenMP_C_FLAGS}"
      )
  endif ()
  set_target_properties (wsjt_fort_omp
    PROPERTIES
    Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/fortran_modules_omp
    )
  file (MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/fortran_modules_omp)
  if (APPLE)
    # On  Mac  we don't  have  working  OpenMP  support in  the  C/C++
    # compilers so  we have to manually  set the correct flags  to get
    # OpenMP support in jt9.
    target_compile_options (wsjt_fort_omp
      PRIVATE
      $<$<COMPILE_LANGUAGE:Fortran>:-fopenmp>   # assumes GNU style Fortran compiler
      )
  endif (APPLE)
endif (${OPENMP_FOUND} OR APPLE)

# build a library for the QCustomPlot widget
add_library (qcp STATIC ${qcp_CXXSRCS})
target_include_directories (qcp PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/qcustomplot-source>)
target_link_libraries (qcp Qt5::Widgets Qt5::PrintSupport)

# build a library of package Qt functionality
add_library (wsjt_qt STATIC ${wsjt_qt_CXXSRCS} ${wsjt_qt_GENUISRCS} ${GENAXSRCS})
# set wsjtx_udp exports to static variants
target_compile_definitions (wsjt_qt PUBLIC UDP_STATIC_DEFINE)
target_link_libraries (wsjt_qt qcp Qt5::Widgets Qt5::Network Qt5::Sql)
target_include_directories (wsjt_qt BEFORE PRIVATE ${hamlib_INCLUDE_DIRS})
if (WIN32)
  target_link_libraries (wsjt_qt Qt5::AxContainer Qt5::AxBase)
endif (WIN32)

add_library (wsjt_qtmm STATIC ${wsjt_qtmm_CXXSRCS} ${wsjt_qtmm_GENUISRCS})
target_link_libraries (wsjt_qtmm Qt5::Multimedia)

# Always build these:
add_executable (wsprd ${wsprd_CSRCS} lib/indexx.f90 lib/wsprd/osdwspr.f90)
target_include_directories (wsprd PRIVATE ${FFTW3_INCLUDE_DIRS})
target_link_libraries (wsprd ${FFTW3_LIBRARIES})

add_executable (fmtave lib/fmtave.f90 wsjtx.rc)

add_executable (fcal lib/fcal.f90 wsjtx.rc)

add_executable (fmeasure lib/fmeasure.f90 wsjtx.rc)

add_executable (jt9 ${jt9_FSRCS} ${jt9_CXXSRCS} wsjtx.rc)
if (${OPENMP_FOUND} OR APPLE)
  if (APPLE)
    # On  Mac  we don't  have  working  OpenMP  support in  the  C/C++
    # compilers so we  have to manually set the  correct linking flags
    # and libraries to get OpenMP support in jt9.
    set_target_properties (jt9
      PROPERTIES
      Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/fortran_modules_omp
      LINK_LIBRARIES "gomp;gcc_s.1" # assume GNU libgcc OpenMP
      )
    target_compile_options (jt9
      PRIVATE
      $<$<COMPILE_LANGUAGE:Fortran>:-fopenmp>   # assumes GNU style Fortran compiler
      )
  else (APPLE)
    if (OpenMP_C_FLAGS)
      set_target_properties (jt9
        PROPERTIES
        COMPILE_FLAGS "${OpenMP_C_FLAGS}"
        LINK_FLAGS "${OpenMP_C_FLAGS}"
        )
    endif ()
    set_target_properties (jt9
      PROPERTIES
      Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/fortran_modules_omp
      )
  endif (APPLE)
  if (WIN32)
    # set_target_properties (jt9 PROPERTIES
    #   LINK_FLAGS -Wl,--stack,16777216
    #   )
  endif ()
  target_link_libraries (jt9 wsjt_fort_omp wsjt_cxx Qt5::Core)
else (${OPENMP_FOUND} OR APPLE)
  target_link_libraries (jt9 wsjt_fort wsjt_cxx Qt5::Core)
endif (${OPENMP_FOUND} OR APPLE)

if(WSJT_BUILD_UTILS) 
add_executable (jt4sim lib/jt4sim.f90 wsjtx.rc)
target_link_libraries (jt4sim wsjt_fort wsjt_cxx)

add_executable (jt65sim lib/jt65sim.f90 wsjtx.rc)
target_link_libraries (jt65sim wsjt_fort wsjt_cxx)

add_executable (qra64sim lib/qra/qra64/qra64sim.f90 wsjtx.rc)
target_link_libraries (qra64sim wsjt_fort wsjt_cxx)

add_executable (jt49sim lib/jt49sim.f90 wsjtx.rc)
target_link_libraries (jt49sim wsjt_fort wsjt_cxx)

add_executable (allsim lib/allsim.f90 wsjtx.rc)
target_link_libraries (allsim wsjt_fort wsjt_cxx)

add_executable (rtty_spec lib/rtty_spec.f90 wsjtx.rc)
target_link_libraries (rtty_spec wsjt_fort wsjt_cxx)

add_executable (jt65code lib/jt65code.f90 wsjtx.rc)
target_link_libraries (jt65code wsjt_fort wsjt_cxx)

add_executable (qra64code lib/qra64code.f90 wsjtx.rc)
target_link_libraries (qra64code wsjt_fort wsjt_cxx)

add_executable (jt9code lib/jt9code.f90 wsjtx.rc)
target_link_libraries (jt9code wsjt_fort wsjt_cxx)

add_executable (wsprcode lib/wsprcode/wsprcode.f90 lib/wsprcode/nhash.c
	       wsjtx.rc)
target_link_libraries (wsprcode wsjt_fort wsjt_cxx)

add_executable (wsprsim ${wsprsim_CSRCS})

add_executable (jt4code lib/jt4code.f90 wsjtx.rc)
target_link_libraries (jt4code wsjt_fort wsjt_cxx)

add_executable (msk144code lib/msk144code.f90 wsjtx.rc)
target_link_libraries (msk144code wsjt_fort wsjt_cxx)

add_executable (jt65 lib/jt65.f90 lib/jt65_test.f90 wsjtx.rc)
target_link_libraries (jt65 wsjt_fort wsjt_cxx)

add_executable (ft8code lib/ft8/ft8code.f90 wsjtx.rc)
target_link_libraries (ft8code wsjt_fort wsjt_cxx)

add_executable (ft8 lib/ft8.f90 wsjtx.rc)
target_link_libraries (ft8 wsjt_fort wsjt_cxx)

add_executable (ft8sim lib/ft8/ft8sim.f90 wsjtx.rc)
target_link_libraries (ft8sim wsjt_fort wsjt_cxx)

add_executable (ft8sim_gfsk lib/ft8/ft8sim_gfsk.f90 wsjtx.rc)
target_link_libraries (ft8sim_gfsk wsjt_fort wsjt_cxx)

add_executable (msk144sim lib/msk144sim.f90 wsjtx.rc)
target_link_libraries (msk144sim wsjt_fort wsjt_cxx)

add_executable (ft4sim lib/ft4/ft4sim.f90 wsjtx.rc)
target_link_libraries (ft4sim wsjt_fort wsjt_cxx)

add_executable (ft4sim_mult lib/ft4/ft4sim_mult.f90 wsjtx.rc)
target_link_libraries (ft4sim_mult wsjt_fort wsjt_cxx)

add_executable (record_time_signal Audio/tools/record_time_signal.cpp)
target_link_libraries (record_time_signal wsjt_cxx wsjt_qtmm wsjt_qt)

endif(WSJT_BUILD_UTILS)

# build the main application
add_executable (wsjtx MACOSX_BUNDLE
  ${wsjtx_CXXSRCS}
  ${wsjtx_GENUISRCS}
  wsjtx.rc
  ${WSJTX_ICON_FILE}
  ${wsjtx_RESOURCES_RCC}
  )

if (WSJT_CREATE_WINMAIN)
  set_target_properties (wsjtx PROPERTIES WIN32_EXECUTABLE ON)
endif (WSJT_CREATE_WINMAIN)

set_target_properties (wsjtx PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Darwin/Info.plist.in"
  MACOSX_BUNDLE_INFO_STRING "${WSJTX_DESCRIPTION_SUMMARY}"
  MACOSX_BUNDLE_ICON_FILE "${WSJTX_ICON_FILE}"
  MACOSX_BUNDLE_BUNDLE_VERSION ${wsjtx_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING "v${wsjtx_VERSION}"
  MACOSX_BUNDLE_LONG_VERSION_STRING "Version ${wsjtx_VERSION}"
  MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}"
  MACOSX_BUNDLE_BUNDLE_EXECUTABLE_NAME "${PROJECT_NAME}"
  MACOSX_BUNDLE_COPYRIGHT "${PROJECT_COPYRIGHT}"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.k1jt.wsjtx"
  )

target_include_directories (wsjtx PRIVATE ${FFTW3_INCLUDE_DIRS})
if (APPLE)
  target_link_libraries (wsjtx wsjt_fort wsjt_cxx wsjt_qt wsjt_qtmm ${hamlib_LIBRARIES} ${FFTW3_LIBRARIES})
else ()
  target_link_libraries (wsjtx wsjt_fort_omp wsjt_cxx wsjt_qt wsjt_qtmm ${hamlib_LIBRARIES} ${FFTW3_LIBRARIES})
  if (OpenMP_C_FLAGS)
    set_target_properties (wsjtx PROPERTIES
      COMPILE_FLAGS "${OpenMP_C_FLAGS}"
      LINK_FLAGS "${OpenMP_C_FLAGS}"
      )
  endif ()
  set_target_properties (wsjtx PROPERTIES
    Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/fortran_modules_omp
    )
  if (WIN32)
    set_target_properties (wsjtx PROPERTIES
      LINK_FLAGS -Wl,--stack,16777216
      )
  endif ()
endif ()
qt5_use_modules (wsjtx SerialPort) # not sure why the interface link library syntax above doesn't work

# make a library for WSJT-X UDP servers
# add_library (wsjtx_udp SHARED ${UDP_library_CXXSRCS})
add_library (wsjtx_udp-static STATIC ${UDP_library_CXXSRCS})
#target_include_directories (wsjtx_udp
#  INTERFACE
#  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/wsjtx>
#  )
target_include_directories (wsjtx_udp-static
  INTERFACE
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/wsjtx>
  )
#set_target_properties (wsjtx_udp PROPERTIES
#  PUBLIC_HEADER "${UDP_library_HEADERS}"
#  )
set_target_properties (wsjtx_udp-static PROPERTIES
  OUTPUT_NAME wsjtx_udp
  )
target_compile_definitions (wsjtx_udp-static PUBLIC UDP_STATIC_DEFINE)
#qt5_use_modules (wsjtx_udp Network)
qt5_use_modules (wsjtx_udp-static Network Gui)
generate_export_header (wsjtx_udp-static BASE_NAME udp)

add_executable (udp_daemon UDPExamples/UDPDaemon.cpp UDPExamples/udp_daemon.rc ${WSJTX_ICON_FILE})
target_link_libraries (udp_daemon wsjtx_udp-static)

add_resources (message_aggregator_RESOURCES /qss ${message_aggregator_STYLESHEETS})
configure_file (UDPExamples/message_aggregator.qrc.in message_aggregator.qrc @ONLY)
qt5_add_resources (message_aggregator_RESOURCES_RCC
  ${CMAKE_CURRENT_BINARY_DIR}/message_aggregator.qrc
  contrib/QDarkStyleSheet/qdarkstyle/style.qrc
  )
add_executable (message_aggregator
  ${message_aggregator_CXXSRCS}
  ${WSJTX_ICON_FILE}
  UDPExamples/message_aggregator.rc
  ${message_aggregator_RESOURCES_RCC}
  )
target_link_libraries (message_aggregator Qt5::Widgets wsjtx_udp-static)

if (WSJT_CREATE_WINMAIN)
  set_target_properties (message_aggregator PROPERTIES WIN32_EXECUTABLE ON)
endif (WSJT_CREATE_WINMAIN)

if (UNIX)
  if (NOT WSJT_SKIP_MANPAGES)
    add_subdirectory (manpages)
    add_dependencies (wsjtx manpages)
  endif (NOT WSJT_SKIP_MANPAGES)
  if (NOT APPLE)
    add_subdirectory (debian)
    add_dependencies (wsjtx debian)
  endif (NOT APPLE)
endif (UNIX)

#
# installation
#
install (TARGETS wsjtx
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  BUNDLE DESTINATION . COMPONENT runtime
  )

# install (TARGETS wsjtx_udp EXPORT udp
#   RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
#   LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#   ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
#   PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wsjtx
#   )
# install (TARGETS wsjtx_udp-static EXPORT udp-static
#   DESTINATION ${CMAKE_INSTALL_LIBDIR}
#   )

# install (EXPORT udp NAMESPACE wsjtx::
#   DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wsjtx
#   )
# install (EXPORT udp-static NAMESPACE wsjtx::
#   DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/wsjtx
#   )

install (TARGETS udp_daemon message_aggregator
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  )

install (TARGETS jt9 wsprd fmtave fcal fmeasure
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  )

if(WSJT_BUILD_UTILS)
install (TARGETS ft8code jt65code qra64code qra64sim jt9code jt4code
  msk144code 
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  )
endif(WSJT_BUILD_UTILS)  

install (PROGRAMS
  ${RIGCTL_EXE}
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  #COMPONENT runtime
  RENAME rigctl-wsjtx${CMAKE_EXECUTABLE_SUFFIX}
  )

install (PROGRAMS
  ${RIGCTLD_EXE}
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  #COMPONENT runtime
  RENAME rigctld-wsjtx${CMAKE_EXECUTABLE_SUFFIX}
  )

install (PROGRAMS
  ${RIGCTLCOM_EXE}
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  #COMPONENT runtime
  RENAME rigctlcom-wsjtx${CMAKE_EXECUTABLE_SUFFIX}
  )

install (FILES
  README
  COPYING
  AUTHORS
  THANKS
  NEWS
  INSTALL
  BUGS
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
  #COMPONENT runtime
  )

install (FILES
  contrib/Ephemeris/JPLEPH
  DESTINATION ${CMAKE_INSTALL_DATADIR}/${CMAKE_PROJECT_NAME}
  #COMPONENT runtime
  )

#
# Mac installer files
#
if (APPLE)
    install (FILES
        Darwin/ReadMe.txt
        Darwin/sysctl.conf
        DESTINATION .
        #COMPONENT runtime
    )
endif (APPLE)


#
# uninstall support
#
configure_file (
  "${CMAKE_CURRENT_SOURCE_DIR}/CMake/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  @ONLY)
add_custom_target (uninstall
  "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")


# creates or updates ${PROJECT_BINARY_DIR}/scs_version.h using cmake script
add_custom_target (revisiontag
  COMMAND ${CMAKE_COMMAND}
  -D SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
  -D BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
  -D OUTPUT_DIR=${PROJECT_BINARY_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/CMake/getsvn.cmake
  COMMENT "Generating Subversion revision information"
  VERBATIM
  )
# explicitly say that the wsjt_qt depends on custom target, this is
# done indirectly so that the revisiontag target gets built exactly
# once per build
add_dependencies(wsjt_qt revisiontag)


#
# versioning and configuration
#
configure_file (
  "${CMAKE_CURRENT_SOURCE_DIR}/wsjtx_config.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/wsjtx_config.h"
  )


if (NOT WIN32 AND NOT APPLE)
  # install a desktop file so wsjtx appears in the application start
  # menu with an icon
  install (
    FILES wsjtx.desktop message_aggregator.desktop
    DESTINATION share/applications
    #COMPONENT runtime
    )
  install (
    FILES icons/Unix/wsjtx_icon.png
    DESTINATION share/pixmaps
    #COMPONENT runtime
    )
endif (NOT WIN32 AND NOT APPLE)


#
# bundle fixup only done in Release or MinSizeRel configurations
#
if (NOT is_debug_build)

  get_target_property (QtCore_location Qt5::Core LOCATION)
  get_filename_component (QtCore_location ${QtCore_location} PATH)
  list (APPEND fixup_library_dirs ${QtCore_location})

  if (APPLE)
    set (CMAKE_POSTFLIGHT_SCRIPT
      "${wsjtx_BINARY_DIR}/postflight.sh")
    set (CMAKE_POSTUPGRADE_SCRIPT
      "${wsjtx_BINARY_DIR}/postupgrade.sh")
    configure_file ("${wsjtx_SOURCE_DIR}/Darwin/postflight.sh.in"
      "${CMAKE_POSTFLIGHT_SCRIPT}")
    configure_file ("${wsjtx_SOURCE_DIR}/Darwin/postupgrade.sh.in"
      "${CMAKE_POSTUPGRADE_SCRIPT}")
  endif ()

  if (APPLE OR WIN32)
    # install rules for including 3rd party libs such as Qt

    # install a qt.conf file
    install (CODE "
        get_filename_component (the_qt_conf \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${WSJT_QT_CONF_DESTINATION}/qt.conf\" REALPATH)
        file (WRITE \"\${the_qt_conf}\"
\"[Paths]
\")
"
      #COMPONENT runtime
      )

    # if a system Qt is used (e.g. installed in /usr/lib/), it will not be included in the installation
    set (fixup_exe "\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/${CMAKE_PROJECT_NAME}${CMAKE_EXECUTABLE_SUFFIX}")

    #get_filename_component (hamlib_lib_dir ${hamlib_LIBRARIES} PATH)

    if (APPLE)
      # install required Qt plugins
      install (
	DIRECTORY
	${QT_PLUGINS_DIR}/platforms
	${QT_PLUGINS_DIR}/audio
	${QT_PLUGINS_DIR}/accessible
	${QT_PLUGINS_DIR}/imageformats
	DESTINATION ${WSJT_PLUGIN_DESTINATION}
	CONFIGURATIONS Release MinSizeRel
	#COMPONENT runtime
	FILES_MATCHING PATTERN "*${CMAKE_SHARED_LIBRARY_SUFFIX}"
	PATTERN "*minimal*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
	PATTERN "*offscreen*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
	PATTERN "*quick*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
	PATTERN "*_debug${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
	)
      install (
	FILES
	${QT_PLUGINS_DIR}/sqldrivers/libqsqlite${CMAKE_SHARED_LIBRARY_SUFFIX}
	DESTINATION ${WSJT_PLUGIN_DESTINATION}/sqldrivers
	CONFIGURATIONS Release MinSizeRel
	#COMPONENT runtime
	)
      # install (
      # 	DIRECTORY
      # 	${QT_PLUGINS_DIR}/platforms
      # 	${QT_PLUGINS_DIR}/audio
      #         ${QT_PLUGINS_DIR}/accessible
      # 	DESTINATION ${WSJT_PLUGIN_DESTINATION}
      # 	CONFIGURATIONS Debug
      # 	#COMPONENT runtime
      # 	FILES_MATCHING PATTERN "*_debug${CMAKE_SHARED_LIBRARY_SUFFIX}"
      # 	PATTERN "*minimal*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      # 	PATTERN "*offscreen*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      # 	PATTERN "*quick*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      # 	)

      # add plugins path for Mac Bundle
      install (CODE "
          get_filename_component (the_qt_conf \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${WSJT_QT_CONF_DESTINATION}/qt.conf\" REALPATH)
          file (APPEND \"\${the_qt_conf}\"
\"Plugins = PlugIns
\")
"
	#COMPONENT runtime
	)
    endif (APPLE)

    if (WIN32)
      # DLL directory
      #set (hamlib_lib_dir ${hamlib_lib_dir}/../bin)

      get_filename_component (fftw_lib_dir ${FFTW3F_LIBRARY} PATH)
      list (APPEND fixup_library_dirs ${fftw_lib_dir})

      # install required Qt plugins
      install (
	DIRECTORY
	${QT_PLUGINS_DIR}/platforms
	${QT_PLUGINS_DIR}/styles
	${QT_PLUGINS_DIR}/accessible
	${QT_PLUGINS_DIR}/audio
	${QT_PLUGINS_DIR}/imageformats
	DESTINATION ${WSJT_PLUGIN_DESTINATION}
	CONFIGURATIONS Release MinSizeRel
	#COMPONENT runtime
	FILES_MATCHING PATTERN "*${CMAKE_SHARED_LIBRARY_SUFFIX}"
	PATTERN "*minimal*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
	PATTERN "*offscreen*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
	PATTERN "*quick*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
	PATTERN "*d${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
	)
      install (
	FILES
	${QT_PLUGINS_DIR}/sqldrivers/qsqlite${CMAKE_SHARED_LIBRARY_SUFFIX}
	DESTINATION ${WSJT_PLUGIN_DESTINATION}/sqldrivers
	CONFIGURATIONS Release MinSizeRel
	#COMPONENT runtime
	)
      # install (
      # 	DIRECTORY
      # 	${QT_PLUGINS_DIR}/platforms
      # 	${QT_PLUGINS_DIR}/accessible
      # 	${QT_PLUGINS_DIR}/audio
      # 	DESTINATION ${WSJT_PLUGIN_DESTINATION}
      # 	CONFIGURATIONS Debug
      # 	#COMPONENT runtime
      # 	FILES_MATCHING PATTERN "*d${CMAKE_SHARED_LIBRARY_SUFFIX}"
      # 	PATTERN "*minimal*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      # 	PATTERN "*offscreen*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      # 	PATTERN "*quick*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      # 	)

      # add plugins path for WIN32
      file (RELATIVE_PATH _plugins_path "${CMAKE_INSTALL_PREFIX}/${WSJT_QT_CONF_DESTINATION}" "${CMAKE_INSTALL_PREFIX}/${WSJT_PLUGIN_DESTINATION}")
      install (CODE "
        get_filename_component (the_qt_conf \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${WSJT_QT_CONF_DESTINATION}/qt.conf\" REALPATH)
        file (APPEND \"\${the_qt_conf}\"
\"Plugins = ${_plugins_path}
\")
"
	#COMPONENT runtime
	)

      set (gp_tool "objdump")	# we want MinGW tool - not MSVC (See GetPrerequisites.cmake)
    endif (WIN32)

    #list (APPEND fixup_library_dirs ${hamlib_lib_dir})
    list (APPEND fixup_library_dirs ${hamlib_LIBRARY_DIRS})

    install (CODE "
        get_filename_component (the_path \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${WSJT_PLUGIN_DESTINATION}\" REALPATH)
        file (GLOB_RECURSE QTPLUGINS \"\${the_path}/*${CMAKE_SHARED_LIBRARY_SUFFIX}\")
        set (CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/CMake/Modules ${CMAKE_MODULE_PATH})
        include (BundleUtilities)
        set (BU_CHMOD_BUNDLE_ITEMS ON)
        set (gp_tool ${gp_tool})
        # canonicalize path in install context
        get_filename_component (the_exe ${fixup_exe} REALPATH)
        fixup_bundle (\"\${the_exe}\" \"\${QTPLUGINS}\" \"${fixup_library_dirs}\")"
      #COMPONENT runtime
      )
  endif (APPLE OR WIN32)

endif (NOT is_debug_build)


#
# packaging
#
set (CPACK_MONOLITHIC_INSTALL 1)
set (CPACK_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
set (CPACK_PACKAGE_VERSION_MAJOR ${WSJTX_VERSION_MAJOR})
set (CPACK_PACKAGE_VERSION_MINOR ${WSJTX_VERSION_MINOR})
set (CPACK_PACKAGE_VERSION_PATCH ${WSJTX_VERSION_PATCH})

if (WIN32)
  set (CPACK_GENERATOR "NSIS")
elseif (APPLE)
  set (CPACK_GENERATOR "DragNDrop")
else ()
  find_program (DPKG_BUILDER dpkg-buildpackage DOC "Debian package builder")
  if (DPKG_BUILDER)
    #
    # Derive the correct filename for a Debian package because the DEB
    # generator doesn't do this correctly at present.
    #
    find_program (DPKG_PROGRAM dpkg DOC "dpkg program of Debian-based systems")
    if (DPKG_PROGRAM)
      execute_process (
	COMMAND ${DPKG_PROGRAM} --print-architecture
	OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
	OUTPUT_STRIP_TRAILING_WHITESPACE
	)
    else (DPKG_PROGRAM)
      set (CPACK_DEBIAN_PACKAGE_ARCHITECTURE noarch)
    endif (DPKG_PROGRAM)

    list (APPEND CPACK_GENERATOR "DEB")
  endif (DPKG_BUILDER)

  find_program (RPMBUILDER rpmbuild DOC "RPM package builder")
  if (RPMBUILDER)
    list (APPEND CPACK_GENERATOR "RPM")
  endif (RPMBUILDER)
endif ()

set (CPACK_DEBIAN_PACKAGE_DESCRIPTION "${PROJECT_DESCRIPTION}")
set (CPACK_DEBIAN_PACKAGE_HOMEPAGE "${PROJECT_HOMEPAGE}")
set (CPACK_DEBIAN_PACKAGE_DEPENDS "libgfortran3 (>=4.8.2), libqt5serialport5 (>=5.5), libqt5multimedia5-plugins (>=5.5), libqt5widgets5 (>=5.5), libqt5sql5-sqlite (>=5.5), libusb-1.0-0, libudev1, libc6 (>=2.19)")
set (CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

set (CPACK_RPM_PACKAGE_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
set (CPACK_RPM_PACKAGE_REQUIRES "qt5-qtserialport >= 5.5, qt5-qtmultimedia >= 5.5, qt5-qtsvg, libusb, systemd-udev, glibc >= 2, libgfortran >= 4.8.2")
set (CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION /usr/share/pixmaps /usr/share/applications /usr/share/man /usr/share/man1)

configure_file ("${PROJECT_SOURCE_DIR}/CMakeCPackOptions.cmake.in"
  "${PROJECT_BINARY_DIR}/CMakeCPackOptions.cmake" @ONLY)
set (CPACK_PROJECT_CONFIG_FILE "${PROJECT_BINARY_DIR}/CMakeCPackOptions.cmake")

include (CPack)
