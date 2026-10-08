#
# Install rules -- included from the top-level CMakeLists.txt at the
# equivalent point. Included (not add_subdirectory'd), so
# CMAKE_CURRENT_SOURCE_DIR/CMAKE_CURRENT_BINARY_DIR stay the project
# root/build-dir, matching configure_file() and install() paths below
# that were written assuming that. Covers install(TARGETS/FILES/
# PROGRAMS/DIRECTORY) rules, Windows OpenSSL runtime DLL bundling,
# macOS installer files, and the uninstall target. Versioning/
# packaging (CPack, revisiontag) stays in the top-level file.
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

install (TARGETS udp_daemon message_aggregator wsjtx_app_version
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  )

# build-macos.yml stages /usr/local/wsjtx for the macOS package from
# installed-cli-tools.txt, and collects the programs of the macOS release
# tarballs from cli-tools.txt. cli-tools.txt also lists every program in
# CMake/release-tarballs.txt whatever the build options, so a macOS release
# build that lacks one fails naming it.
set (wsjt_installed_cli_tools jt9 wsprd fmtave fcal fmeasure wsjt-codec)
if (WSJT_BUILD_UTILS)
  list (APPEND wsjt_installed_cli_tools
    ft8code jt65code jt9code jt4code msk144code q65code fst4sim ft8sim q65sim
    EchoCallSim testEchoCall echosim hash22calc cablog sjtty rjtty)
endif ()
install (TARGETS ${wsjt_installed_cli_tools}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  )
string (JOIN "\n" _wsjt_installed_cli_tools_lines ${wsjt_installed_cli_tools})
file (WRITE "${CMAKE_BINARY_DIR}/installed-cli-tools.txt"
  "${_wsjt_installed_cli_tools_lines}\n")
include ("${CMAKE_SOURCE_DIR}/CMake/Modules/read_release_tarballs.cmake")
set_property (DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/CMake/release-tarballs.txt")
wsjt_read_release_tarballs ("${CMAKE_SOURCE_DIR}/CMake/release-tarballs.txt"
  _wsjt_tarball_programs)
set (_wsjt_cli_tools ${wsjt_installed_cli_tools} ${_wsjt_tarball_programs})
list (REMOVE_DUPLICATES _wsjt_cli_tools)
string (JOIN "\n" _wsjt_cli_tools_lines ${_wsjt_cli_tools})
file (WRITE "${CMAKE_BINARY_DIR}/cli-tools.txt" "${_wsjt_cli_tools_lines}\n")

if (NOT WSJT_SKIP_QMAP)
  install (TARGETS qmap
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
    BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
    )
endif ()

install (TARGETS map65
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  )

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
  README.md
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
  RENAME README
  #COMPONENT runtime
  )

install (FILES
  COPYING
  AUTHORS
  THANKS
  NEWS
  BUGS
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
  #COMPONENT runtime
  )

install (FILES
  ${WSJTX_DATA_FILES}
  DESTINATION ${WSJT_DATA_DESTINATION}
  #COMPONENT runtime
  )

install (DIRECTORY
  example_log_configurations
  DESTINATION ${CMAKE_INSTALL_DOCDIR}
  FILES_MATCHING REGEX "^.*[^~]$"
  #COMPONENT runtime
  )

if (APPLE)
  set (WSJT_SOUND_DESTINATION ${CMAKE_INSTALL_DATAROOTDIR})
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set (WSJT_SOUND_DESTINATION ${WSJT_DATA_DESTINATION})
else ()
  set (WSJT_SOUND_DESTINATION ${CMAKE_INSTALL_BINDIR})
endif ()
install (DIRECTORY
  ${PROJECT_SOURCE_DIR}/sounds
  DESTINATION ${WSJT_SOUND_DESTINATION}
  #COMPONENT runtime
  )

if (WIN32)
  # Bundle OpenSSL runtime DLLs from the build environment.
  # Qt's QSslSocket loads OpenSSL by filename at runtime; the names depend
  # on what Qt was built against (libssl-1_1*.dll for older Qt-on-OpenSSL-1.1,
  # libssl-3-x64.dll for current MSYS2 Qt-on-OpenSSL-3). Search the runtime
  # directories CMake can infer from Qt, MinGW, MSYS2, or JTSDK so SuperFox
  # verification, Update Hamlib, LotW download, and eQSL keep working across
  # supported build environments.
  set (WSJT_OPENSSL_RUNTIME_DIR "" CACHE PATH
    "Directory containing OpenSSL runtime DLLs to bundle on Windows.")
  option (WSJT_ALLOW_MISSING_OPENSSL_RUNTIME_DLLS
    "Allow Windows installs without bundled OpenSSL runtime DLLs." OFF)

  set (_openssl_runtime_dirs)
  macro (_add_openssl_runtime_dir _dir)
    if (NOT "${_dir}" STREQUAL "")
      file (TO_CMAKE_PATH "${_dir}" _openssl_candidate_dir)
      if (NOT _openssl_candidate_dir MATCHES "(^|.*/)usr/bin/?$")
        list (FIND _openssl_runtime_dirs "${_openssl_candidate_dir}" _openssl_candidate_index)
        if (_openssl_candidate_index EQUAL -1)
          list (APPEND _openssl_runtime_dirs "${_openssl_candidate_dir}")
        endif ()
      endif ()
    endif ()
  endmacro ()

  _add_openssl_runtime_dir ("${WSJT_OPENSSL_RUNTIME_DIR}")

  if (TARGET Qt5::Core)
    get_target_property (_qt_core_runtime Qt5::Core LOCATION)
    if (_qt_core_runtime)
      get_filename_component (_qt_core_runtime_dir "${_qt_core_runtime}" DIRECTORY)
      _add_openssl_runtime_dir ("${_qt_core_runtime_dir}")
    endif ()
  endif ()

  if (CMAKE_CXX_COMPILER)
    get_filename_component (_cxx_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    _add_openssl_runtime_dir ("${_cxx_compiler_dir}")
  endif ()

  foreach (_openssl_env_prefix IN ITEMS MINGW_PREFIX MSYSTEM_PREFIX)
    if (NOT "$ENV{${_openssl_env_prefix}}" STREQUAL "")
      file (TO_CMAKE_PATH "$ENV{${_openssl_env_prefix}}" _openssl_env_path)
      if (NOT _openssl_env_path MATCHES "(^|.*/)usr/?$")
        _add_openssl_runtime_dir ("${_openssl_env_path}/bin")
      endif ()
    endif ()
  endforeach ()

  if (NOT "$ENV{JTSDK_TOOLS}" STREQUAL "")
    file (TO_CMAKE_PATH "$ENV{JTSDK_TOOLS}" _jtsdk_tools_path)
    _add_openssl_runtime_dir ("${_jtsdk_tools_path}/msys64/mingw64/bin")
  endif ()

  _add_openssl_runtime_dir ("/mingw64/bin")

  set (_openssl_runtime_dlls)
  foreach (_openssl_runtime_dir IN LISTS _openssl_runtime_dirs)
    file (GLOB _openssl_ssl_dlls "${_openssl_runtime_dir}/libssl-*.dll")
    file (GLOB _openssl_crypto_dlls "${_openssl_runtime_dir}/libcrypto-*.dll")
    if (_openssl_ssl_dlls AND _openssl_crypto_dlls)
      list (APPEND _openssl_runtime_dlls
        ${_openssl_ssl_dlls}
        ${_openssl_crypto_dlls}
      )
      set (_openssl_runtime_source_dir "${_openssl_runtime_dir}")
      break ()
    endif ()
  endforeach ()

  if (_openssl_runtime_dlls)
    message (STATUS "Using OpenSSL runtime DLLs from ${_openssl_runtime_source_dir}")
    install (FILES ${_openssl_runtime_dlls}
      DESTINATION ${CMAKE_INSTALL_BINDIR}
      #COMPONENT runtime
    )
  elseif (WSJT_ALLOW_MISSING_OPENSSL_RUNTIME_DLLS)
    message (WARNING
      "OpenSSL runtime DLLs not found in: ${_openssl_runtime_dirs}. "
      "TLS features (SuperFox, Update Hamlib, LotW, eQSL) will fail on "
      "installs without OpenSSL in PATH."
    )
  else ()
    message (FATAL_ERROR
      "OpenSSL runtime DLLs were not found for the Windows install image.\n"
      "TLS features (SuperFox, Update Hamlib, LotW, eQSL) require bundled "
      "OpenSSL DLLs.\n"
      "Searched: ${_openssl_runtime_dirs}\n"
      "Install the MinGW OpenSSL package, for example:\n"
      "  pacman -S mingw-w64-x86_64-openssl\n"
      "Or point CMake at the DLL directory:\n"
      "  -DWSJT_OPENSSL_RUNTIME_DIR=/path/to/mingw64/bin\n"
      "To build without bundling TLS runtime DLLs:\n"
      "  -DWSJT_ALLOW_MISSING_OPENSSL_RUNTIME_DLLS=ON"
    )
  endif ()
endif (WIN32)

#
# Mac installer files
#
if (APPLE)
    install (FILES
        Darwin/ReadMe.txt
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
