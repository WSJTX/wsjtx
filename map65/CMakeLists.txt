cmake_minimum_required (VERSION 2.8.9)

project (map65 C CXX Fortran)

set (MAP65_VERSION_MAJOR 1)
set (MAP65_VERSION_MINOR 3)

if (POLICY CMP0020)
  cmake_policy (SET CMP0020 NEW) # link to Qt winmain on Windows
endif (POLICY CMP0020)

# make sure that the default is a RELEASE
if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE RELEASE CACHE STRING
    "Choose the type of build, options are: None Debug Release."
    FORCE)
endif (NOT CMAKE_BUILD_TYPE)

# C++ setup

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")

set (CXXSRCS
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
  set (CXXSRCS ${CXXSRCS} killbyname.cpp)
endif (WIN32)

set (CSRCS
  paInputDevice.c
  pa_get_device_info.c
)

set_property (SOURCE ${CXXSRCS} ${CSRCS} APPEND PROPERTY COMPILE_FLAGS "-include map65_config.h")

set (UISRCS
  about.ui
  astro.ui
  bandmap.ui
  devsetup.ui
  mainwindow.ui
  messages.ui
  txtune.ui
  widegraph.ui
)

# sort out pre-requisites

# libfftw3 setup

find_path (fftw3f_INCLUDES fftw3.f)
find_library (fftw3f_LIBRARIES NAMES fftw3f fftw3f-3)
include_directories (${fftw3f_INCLUDES})

if (WIN32)
   find_library (usb_RUNTIME NAMES usb0 PATH_SUFFIXES bin)
   set (CMAKE_INSTALL_PREFIX C:/MAP65)
endif (WIN32)

# Qt5 setup

# Widgets finds its own dependencies.
find_package (Qt5Widgets REQUIRED)
find_package (Qt5Multimedia REQUIRED)

# Tell CMake to run moc when necessary
set (CMAKE_AUTOMOC ON)

# don't use Qt "keywords" signal, slot, emit in generated files to
# avoid compatability issue with other libraries
#ADD_DEFINITIONS (-DQT_NO_KEYWORDS)

# As moc files are generated in the binary dir, tell CMake to always
# look for includes there:
set (CMAKE_INCLUDE_CURRENT_DIR ON)

# project definitions
add_definitions (-DQT5)
if (CMAKE_HOST_UNIX)
  add_definitions (-DUNIX)
elseif (CMAKE_HOST_WIN32)
  add_definitions (-DWIN32)
endif ()

#set_property (DIRECTORY APPEND PROPERTY COMPILE_DEFINITIONS_RELEASE QT_NO_DEBUG_OUTPUT QT_NO_WARNING_OUTPUT)
set_property (DIRECTORY APPEND PROPERTY COMPILE_DEFINITIONS_RELEASE QT_NO_WARNING_OUTPUT)

# build the subdirectories
add_subdirectory (libm65)

# UI generation
qt5_wrap_ui (GENUISRCS ${UISRCS})

add_executable (map65 ${CXXSRCS} ${CSRCS} ${GENUISRCS} map65.rc)
target_link_libraries (map65 m65impl ${fftw3f_LIBRARIES} ${CMAKE_CURRENT_SOURCE_DIR}/palir-02.dll ${CMAKE_CURRENT_SOURCE_DIR}/libusb0.dll ${CMAKE_CURRENT_SOURCE_DIR}/libwsock32.a)

if (WIN32)
  target_link_libraries (map65)
#  set_target_properties (map65 PROPERTIES LINK_FLAGS_RELEASE "${LINKER_FLAGS_RELEASE} ")
  set_target_properties (map65 PROPERTIES LINK_FLAGS_RELEASE "${LINKER_FLAGS_RELEASE} -mwindows")
endif (WIN32)
qt5_use_modules (map65 Widgets Multimedia OpenGL)


install (
  TARGETS map65
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  )

# install (DIRECTORY Palettes DESTINATION bin PATTERN *.pal)
# install (DIRECTORY samples DESTINATION bin/save)

install (FILES
#  CALL3.TXT
#  prefixes.txt
  DESTINATION bin
  )

if (WIN32)
  install (
    FILES ${fftw3f_LIBRARIES} ${usb_RUNTIME} 
    DESTINATION bin COMPONENT Runtime
    )
endif (WIN32)

# a custom target that is always built
ADD_CUSTOM_TARGET (revisiontag ALL)

# creates svnversion.h using cmake script
ADD_CUSTOM_COMMAND (TARGET revisiontag COMMAND ${CMAKE_COMMAND}
  -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} 
  -P ${CMAKE_CURRENT_SOURCE_DIR}/getsvn.cmake)

# explicitly say that the executable depends on custom target
add_dependencies(map65 revisiontag)

# versioning

configure_file (
  "${PROJECT_SOURCE_DIR}/map65_config.h.in"
  "${PROJECT_BINARY_DIR}/map65_config.h"
  )

include_directories ("${PROJECT_BINARY_DIR}")
