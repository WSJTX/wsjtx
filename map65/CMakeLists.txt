set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)   

set (map65_CXXSRCS
  about.cpp
  astro.cpp
  bandmap.cpp
  cqdatasender.cpp
  commons.cpp
  devsetup.cpp
  displaytext.cpp
  fortran_mutex.cpp
  getdev.cpp
#  getfile.cpp
  liveCQSender.cpp
  main.cpp
  mainwindow.cpp
  messages.cpp
  meterwidget.cpp
  plotter.cpp
  ../Network/PSKReporter.cpp
  ../Network/PSKReporterIPFIX.cpp
  Radio.cpp
  set570.cpp
  signalmeter.cpp
  soundin.cpp
  soundout.cpp
  txtune.cpp
  widegraph.cpp
  )

set (map65_UISRCS
  about.ui
  astro.ui
  bandmap.ui
  devsetup.ui
  mainwindow.ui
  messages.ui
  txtune.ui
  widegraph.ui
)

set (map65_C_and_CXXSRCS
  ${map65_CSRCS}
  ${map65_CXXSRCS}
  )
set_property (SOURCE ${map65_C_and_CXXSRCS} APPEND_STRING PROPERTY COMPILE_FLAGS " -include wsjtx_config.h")
set_property (SOURCE ${map65_C_and_CXXSRCS} APPEND PROPERTY OBJECT_DEPENDS ${CMAKE_BINARY_DIR}/wsjtx_config.h)

# build the subdirectories
add_subdirectory (libm65)

# UI generation
qt5_wrap_ui (map65_GENUISRCS ${map65_UISRCS})

# Executable: only the .rc file is Windows-only
if (WIN32)
  add_executable(map65
      ${map65_CXXSRCS}
      ${map65_CSRCS}
      ${map65_GENUISRCS}
      map65.rc
  )
else()
  add_executable(map65
      ${map65_CXXSRCS}
      ${map65_CSRCS}
      ${map65_GENUISRCS}
  )
endif()

# Include directories (same on all platforms)
target_include_directories(map65 PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/map65/libm65
    ${FFTW3_INCLUDE_DIRS}
)

# Link libraries (same on all platforms except libusb)
target_link_libraries(map65 PRIVATE
    wsjt_qt
    m65impl
    ${FFTW3_LIBRARIES}
    Qt5::Widgets
    Qt5::Network
    Qt5::SerialPort
    Portaudio::Portaudio
    Usb::Usb
)

# Linux needs libusb
# if (UNIX AND NOT APPLE)
#    find_package(PkgConfig REQUIRED)
#    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
#    target_include_directories(map65 PRIVATE ${LIBUSB_INCLUDE_DIRS})
#    target_link_libraries(map65 PRIVATE ${LIBUSB_LIBRARIES})
# endif()

# MAP65 must ALWAYS be a console subsystem executable on Windows
if (WIN32)
  set_target_properties(map65 PROPERTIES WIN32_EXECUTABLE OFF)
endif()

# WSJT_CREATE_WINMAIN now ONLY controls console visibility
if (WIN32 AND WSJT_CREATE_WINMAIN)
  target_compile_definitions(map65 PRIVATE MAP_GUI_SUBSYSTEM)
  endif()
