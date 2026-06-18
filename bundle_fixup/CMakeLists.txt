get_target_property (QtCore_location Qt5::Core LOCATION)
get_filename_component (QtCore_location ${QtCore_location} PATH)
list (APPEND fixup_library_dirs ${QtCore_location})

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
  set (fixup_exes "${fixup_exe}")
  if (NOT WSJT_SKIP_MAP65)
    list (APPEND fixup_exes "\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/map65${CMAKE_EXECUTABLE_SUFFIX}")
  endif ()

  #get_filename_component (hamlib_lib_dir ${Hamlib_LIBRARIES} PATH)

  if (APPLE)
    # install required Qt plugins
    install (
      DIRECTORY
      ${QT_PLUGINS_DIR}/platforms
      ${QT_PLUGINS_DIR}/audio
      ${QT_PLUGINS_DIR}/accessible
      ${QT_PLUGINS_DIR}/imageformats
      ${QT_PLUGINS_DIR}/styles
      DESTINATION ${WSJT_PLUGIN_DESTINATION}
      CONFIGURATIONS Release MinSizeRel RelWithDebInfo
      #COMPONENT runtime
      FILES_MATCHING PATTERN "*${CMAKE_SHARED_LIBRARY_SUFFIX}"
      PATTERN "*minimal*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      PATTERN "*offscreen*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      PATTERN "*quick*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      PATTERN "*webgl*${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      PATTERN "*_debug${CMAKE_SHARED_LIBRARY_SUFFIX}" EXCLUDE
      PATTERN "*${CMAKE_SHARED_LIBRARY_SUFFIX}.dSYM" EXCLUDE
      )
    install (
      FILES
      ${QT_PLUGINS_DIR}/sqldrivers/libqsqlite${CMAKE_SHARED_LIBRARY_SUFFIX}
      DESTINATION ${WSJT_PLUGIN_DESTINATION}/sqldrivers
      CONFIGURATIONS Release MinSizeRel RelWithDebInfo
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
    # DLL directories
    get_filename_component (hamlib_lib_dir ${Hamlib_LIBRARY} PATH)
    list (APPEND fixup_library_dirs ${hamlib_lib_dir}/../bin)

    get_filename_component (usb_lib_dir ${Usb_LIBRARY} PATH)
    list (APPEND fixup_library_dirs ${usb_lib_dir})

    if (NOT WSJT_SKIP_MAP65)
      get_filename_component (portaudio_lib_dir ${Portaudio_LIBRARY} PATH)
      list (APPEND fixup_library_dirs ${portaudio_lib_dir}/../bin)
    endif ()

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
      CONFIGURATIONS Release MinSizeRel RelWithDebInfo
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
      CONFIGURATIONS Release MinSizeRel RelWithDebInfo
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

    # add paths for WIN32
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

  INSTALL (CODE "
        get_filename_component (the_path \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${WSJT_PLUGIN_DESTINATION}\" REALPATH)
        file (GLOB_RECURSE QTPLUGINS \"\${the_path}/*${CMAKE_SHARED_LIBRARY_SUFFIX}\")
        include (BundleUtilities)
        function (gp_resolve_item_override context item exepath dirs resolved_item_var resolved_var)
          string (TOLOWER \"\${item}\" item_lower)
          if (item_lower MATCHES \"^(api-ms-win-|ext-ms-).*\\\\.dll$\")
            set (api_set_root \"\$ENV{SystemRoot}\")
            if (NOT api_set_root)
              set (api_set_root \"\$ENV{windir}\")
            endif ()
            file (TO_CMAKE_PATH \"\${api_set_root}/System32/\${item}\" api_set_path)
            set (\${resolved_item_var} \"\${api_set_path}\" PARENT_SCOPE)
            set (\${resolved_var} TRUE PARENT_SCOPE)
          endif ()
        endfunction ()
        set (BU_CHMOD_BUNDLE_ITEMS ON)
        set (gp_tool ${gp_tool})
        set (fixup_exes \"${fixup_exes}\")
        foreach (the_exe_path IN LISTS fixup_exes)
          get_filename_component (the_exe \"\${the_exe_path}\" REALPATH)
          fixup_bundle (\"\${the_exe}\" \"\${QTPLUGINS}\" \"${fixup_library_dirs}\")
        endforeach ()"
    #COMPONENT runtime
    )
  endif (WIN32)

  if(APPLE)
    # Mark GCC/Fortran runtimes as system libs so fixup_bundle ignores them
    set(_m65_fortran_system_libs
        libgcc_s.1.1.dylib
        libgfortran.5.dylib
        libquadmath.0.dylib
    )
    set(BU_SYSTEM_LIBS ${BU_SYSTEM_LIBS} ${_m65_fortran_system_libs})
  endif()

  if(APPLE AND CMAKE_Fortran_COMPILER_ID STREQUAL "GNU")
    # Ask gfortran where its libs live
    execute_process(
        COMMAND ${CMAKE_Fortran_COMPILER} -print-file-name=libgfortran.5.dylib
        OUTPUT_VARIABLE _gfortran_path
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    get_filename_component(_gfortran_dir "${_gfortran_path}" DIRECTORY)

    # Append to fixup_library_dirs (a normal CMake list)
    list(APPEND fixup_library_dirs "${_gfortran_dir}")
  endif()

  if(APPLE)
    INSTALL (CODE "
      # Resolve plugin directory
      get_filename_component(the_path \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${WSJT_PLUGIN_DESTINATION}\" REALPATH)

      file(GLOB_RECURSE QTPLUGINS \"\${the_path}/*${CMAKE_SHARED_LIBRARY_SUFFIX}\")

      list(FILTER QTPLUGINS EXCLUDE REGEX \".*\\\\.exe\")
      list(FILTER QTPLUGINS EXCLUDE REGEX \".*\\\\.dll\")
      list(FILTER QTPLUGINS EXCLUDE REGEX \".*qthid.*\")
      list(FILTER QTPLUGINS EXCLUDE REGEX \".*rigctl.*\")

      include(BundleUtilities)
      set(BU_CHMOD_BUNDLE_ITEMS ON)
      set(BU_SYSTEM_LIBS \"${BU_SYSTEM_LIBS}\")

      set(fixup_library_dirs \"${fixup_library_dirs}\")

      set(fixup_exes \"${fixup_exes}\")
      foreach (the_exe_path IN LISTS fixup_exes)
        get_filename_component(the_exe \"\${the_exe_path}\" REALPATH)
        fixup_bundle(
          \"\${the_exe}\"
          \"\${QTPLUGINS}\"
          \"\${fixup_library_dirs}\"
        )
      endforeach ()
    ")
  endif (APPLE)

endif (APPLE OR WIN32)
