find_package (portaudio REQUIRED)

find_package (usb REQUIRED)

set (map65_CXXSRCS
  about.cpp
  astro.cpp
  bandmap.cpp
  devsetup.cpp
  displaytext.cpp
  getdev.cpp
  getfile.cpp
  main.cpp
  mainwindow.cpp
  messages.cpp
  meterwidget.cpp
  plotter.cpp
  set570.cpp
  signalmeter.cpp
  soundin.cpp
  soundout.cpp
  txtune.cpp
  widegraph.cpp
  )

if (WIN32)
  set (map65_CXXSRCS ${map65_CXXSRCS} killbyname.cpp)
endif (WIN32)

set (map65_CSRCS
  paInputDevice.c
  pa_get_device_info.c
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

# build the subdirectories
add_subdirectory (libm65)

# UI generation
qt5_wrap_ui (map65_GENUISRCS ${map65_UISRCS})

add_executable (map65 ${map65_CXXSRCS} ${map65_CSRCS} ${map65_GENUISRCS} map65.rc)
target_include_directories (map65 PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
target_link_libraries (map65 m65impl ${FFTW3_LIBRARIES} Qt5::Widgets Qt5::Network ${PORTAUDIO_STATIC_LDFLAGS} usb::usb)

install (
  TARGETS map65
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  )
