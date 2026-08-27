if (APPLE)
  add_library (test_decoder_ipc_fortran_probe STATIC decoder_ipc_fortran_probe.f90)
  target_include_directories (test_decoder_ipc_fortran_probe PRIVATE ${CMAKE_SOURCE_DIR}/lib)
  set (decoder_ipc_fortran_probe_sources)
else ()
  set (decoder_ipc_fortran_probe_sources decoder_ipc_fortran_probe.f90)
endif ()

add_executable (test_decoder_ipc_protocol
  test_decoder_ipc_protocol.cpp
  ${decoder_ipc_fortran_probe_sources}
  ${CMAKE_SOURCE_DIR}/DecoderIpc.cpp
  ${CMAKE_SOURCE_DIR}/lib/decoder_ipc_control.cpp
)
set_target_properties (test_decoder_ipc_protocol PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories (test_decoder_ipc_protocol PRIVATE ${CMAKE_SOURCE_DIR}/lib)
if (APPLE)
  target_link_libraries (test_decoder_ipc_protocol Qt5::Test test_decoder_ipc_fortran_probe)
else ()
  target_link_libraries (test_decoder_ipc_protocol Qt5::Test)
endif ()
add_test (NAME test_decoder_ipc_protocol COMMAND test_decoder_ipc_protocol)

add_executable (test_decoder_output_framer
  test_decoder_output_framer.cpp
  ${CMAKE_SOURCE_DIR}/DecoderOutputFramer.cpp
  ${CMAKE_SOURCE_DIR}/DecoderIpc.cpp
  ${CMAKE_SOURCE_DIR}/lib/decoder_ipc_control.cpp
)
target_include_directories (test_decoder_output_framer PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/lib
)
target_link_libraries (test_decoder_output_framer Qt5::Test)
add_test (NAME test_decoder_output_framer COMMAND test_decoder_output_framer)

add_executable (test_decode_operating_context
  test_decode_operating_context.cpp
)
target_include_directories (test_decode_operating_context PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_decode_operating_context Qt5::Test)
add_test (NAME test_decode_operating_context COMMAND test_decode_operating_context)
