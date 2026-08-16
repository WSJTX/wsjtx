# inhibit-agent — production KEY (CTS) -> localhost/LAN TX Inhibit hold.
# CLI for scripting; GUI for operators. Linux and Windows.

set (INHIBIT_AGENT_CORE
  agent.cpp
  agent.hpp
  keying_monitor.hpp
  )

add_executable (inhibit-agent
  main_cli.cpp
  ${INHIBIT_AGENT_CORE}
  )
target_include_directories (inhibit-agent PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}
  )
target_link_libraries (inhibit-agent Qt5::Core Qt5::Network Qt5::SerialPort)
if (WIN32)
  set_target_properties (inhibit-agent PROPERTIES WIN32_EXECUTABLE OFF)
endif ()
set_target_properties (inhibit-agent PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
  )

add_executable (inhibit-agent-gui
  main_gui.cpp
  ${INHIBIT_AGENT_CORE}
  )
target_include_directories (inhibit-agent-gui PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}
  )
target_link_libraries (inhibit-agent-gui Qt5::Widgets Qt5::Network Qt5::SerialPort)
if (WIN32)
  set_target_properties (inhibit-agent-gui PROPERTIES WIN32_EXECUTABLE ON)
endif ()
set_target_properties (inhibit-agent-gui PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
  )

install (TARGETS inhibit-agent inhibit-agent-gui
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  )
