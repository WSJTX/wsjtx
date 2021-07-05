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

add_executable (map65 ${map65_CXXSRCS} ${map65_CSRCS} ${map65_GENUISRCS} map65.rc)
target_include_directories (map65 PRIVATE ${CMAKE_SOURCE_DIR} ${FFTW3_INCLUDE_DIRS})
target_link_libraries (map65 wsjt_qt m65impl ${FFTW3_LIBRARIES} Qt5::Widgets Qt5::Network Portaudio::Portaudio Usb::Usb)

if (WSJT_CREATE_WINMAIN)
  set_target_properties (map65 PROPERTIES WIN32_EXECUTABLE ON)
endif (WSJT_CREATE_WINMAIN)

if (WIN32)
  install (
    CODE "get_filename_component (_path \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/wsjtx_dir.txt\" REALPATH)
  if (WIN32)
    set (_separator \"\\\\\")
  else ()
    set (_separator \"/\")
  endif ()
  file (WRITE \"\${_path}\" \".\${_separator}\\n\")"
    )

  install (
    TARGETS map65
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
    BUNDLE DESTINATION . COMPONENT runtime
    )
endif ()