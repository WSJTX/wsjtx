set (qmap_CXXSRCS
  about.cpp
  astro.cpp
  devsetup.cpp
  displaytext.cpp
  getfile.cpp
  main.cpp
  mainwindow.cpp
  meterwidget.cpp
  plotter.cpp
  signalmeter.cpp
  soundin.cpp
  widegraph.cpp
  )

if (WIN32)
  set (qmap_CXXSRCS ${qmap_CXXSRCS})
endif (WIN32)

set (qmap_UISRCS
  about.ui
  astro.ui
  devsetup.ui
  mainwindow.ui
  widegraph.ui
)

set (qmap_C_and_CXXSRCS
  ${qmap_CSRCS}
  ${qmap_CXXSRCS}
  )
set_property (SOURCE ${qmap_C_and_CXXSRCS} APPEND_STRING PROPERTY COMPILE_FLAGS " -include wsjtx_config.h")
set_property (SOURCE ${qmap_C_and_CXXSRCS} APPEND PROPERTY OBJECT_DEPENDS ${CMAKE_BINARY_DIR}/wsjtx_config.h)

# build the subdirectories
add_subdirectory (libqmap)

# UI generation
qt5_wrap_ui (qmap_GENUISRCS ${qmap_UISRCS})

add_executable (qmap ${qmap_CXXSRCS} ${qmap_CSRCS} ${qmap_GENUISRCS} qmap.rc)
target_include_directories (qmap PRIVATE ${CMAKE_SOURCE_DIR} ${FFTW3_INCLUDE_DIRS})
target_link_libraries (qmap wsjt_qt qmap_impl ${FFTW3_LIBRARIES} Qt5::Widgets Qt5::Network Usb::Usb)

if (WSJT_CREATE_WINMAIN)
  set_target_properties (qmap PROPERTIES WIN32_EXECUTABLE ON)
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
    TARGETS qmap
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
    BUNDLE DESTINATION . COMPONENT runtime
    )
endif ()
