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
)
set_target_properties (test_decoder_ipc_protocol PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories (test_decoder_ipc_protocol PRIVATE ${CMAKE_SOURCE_DIR}/lib)
target_compile_definitions (test_decoder_ipc_protocol PRIVATE
  JT9_TEST_EXECUTABLE="$<TARGET_FILE:jt9>")
add_dependencies (test_decoder_ipc_protocol jt9)
if (APPLE)
  target_link_libraries (test_decoder_ipc_protocol wsjt_cxx Qt5::Test test_decoder_ipc_fortran_probe)
else ()
  target_link_libraries (test_decoder_ipc_protocol wsjt_cxx Qt5::Test)
endif ()
add_test (NAME test_decoder_ipc_protocol COMMAND test_decoder_ipc_protocol)

add_executable (test_decoder_session
  test_decoder_session.cpp
  ${CMAKE_SOURCE_DIR}/DecoderIpc.cpp
)
target_link_libraries (test_decoder_session wsjt_cxx Qt5::Test)
add_test (NAME test_decoder_session COMMAND test_decoder_session)

add_executable (test_decoder_output_framer
  test_decoder_output_framer.cpp
  ${CMAKE_SOURCE_DIR}/DecoderOutputFramer.cpp
  ${CMAKE_SOURCE_DIR}/DecoderIpc.cpp
)
target_include_directories (test_decoder_output_framer PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/lib
)
target_link_libraries (test_decoder_output_framer wsjt_cxx Qt5::Test)
add_test (NAME test_decoder_output_framer COMMAND test_decoder_output_framer)

add_executable (test_decode_operating_context
  test_decode_operating_context.cpp
)
target_include_directories (test_decode_operating_context PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_decode_operating_context Qt5::Test)
add_test (NAME test_decode_operating_context COMMAND test_decode_operating_context)

add_library (test_decoder_ipc_fortran_layout STATIC
  ${CMAKE_SOURCE_DIR}/lib/decoder_ipc_layout.f90)
file (MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/layout-modules)
set_target_properties (test_decoder_ipc_fortran_layout PROPERTIES
  Fortran_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/layout-modules)
target_include_directories (test_decoder_ipc_fortran_layout PRIVATE ${CMAKE_SOURCE_DIR}/lib)
add_executable (test_decoder_ipc_layout
  test_decoder_ipc_layout.cpp
  ${CMAKE_SOURCE_DIR}/DecoderIpc.cpp
)
target_link_libraries (test_decoder_ipc_layout wsjt_cxx Qt5::Test test_decoder_ipc_fortran_layout)
add_test (NAME test_decoder_ipc_layout COMMAND test_decoder_ipc_layout)

add_executable (test_decoder_ipc_handshake
  test_decoder_ipc_handshake.cpp
  ${CMAKE_SOURCE_DIR}/DecoderIpc.cpp
)
target_link_libraries (test_decoder_ipc_handshake wsjt_cxx Qt5::Test)
target_compile_definitions (test_decoder_ipc_handshake PRIVATE
  DECODER_IPC_JT9="$<TARGET_FILE:jt9>"
  DECODER_IPC_SOURCE="${CMAKE_SOURCE_DIR}")
add_dependencies (test_decoder_ipc_handshake jt9)
add_test (NAME test_decoder_ipc_handshake COMMAND test_decoder_ipc_handshake)
set_tests_properties (test_decoder_ipc_handshake PROPERTIES TIMEOUT 90)

add_executable (test_decoder_diagnostics
  test_decoder_diagnostics.cpp
  ${CMAKE_SOURCE_DIR}/DecoderDiagnostics.cpp
)
target_include_directories (test_decoder_diagnostics PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_decoder_diagnostics wsjt_cxx Qt5::Test)
add_test (NAME test_decoder_diagnostics COMMAND test_decoder_diagnostics)

add_executable (test_decoder_process_supervisor
  test_decoder_process_supervisor.cpp
  ${CMAKE_SOURCE_DIR}/DecoderBackend.hpp
  ${CMAKE_SOURCE_DIR}/DecoderProcessSupervisor.cpp
  ${CMAKE_SOURCE_DIR}/DecoderDiagnostics.cpp
  ${CMAKE_SOURCE_DIR}/NonInheritingProcess.cpp
)
target_include_directories (test_decoder_process_supervisor PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_decoder_process_supervisor wsjt_cxx Qt5::Test)
add_test (NAME test_decoder_process_supervisor COMMAND test_decoder_process_supervisor)
set_tests_properties (test_decoder_process_supervisor PROPERTIES TIMEOUT 180)

add_executable (test_shmem_decoder_worker
  shmem_decoder_worker_stub.cpp
  ${CMAKE_SOURCE_DIR}/lib/NativeSharedMemory.cpp
  ${CMAKE_SOURCE_DIR}/lib/decoder_ipc_control.cpp
)
target_include_directories (test_shmem_decoder_worker PRIVATE ${CMAKE_SOURCE_DIR})
if (UNIX AND NOT WSJT_SHM_OPEN_IN_LIBC)
  target_link_libraries (test_shmem_decoder_worker rt)
endif ()
set_target_properties (test_shmem_decoder_worker PROPERTIES
  OUTPUT_NAME jt9
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shmem-worker)

add_executable (test_shmem_decoder_backend
  test_shmem_decoder_backend.cpp
  ${CMAKE_SOURCE_DIR}/DecoderBackend.hpp
  ${CMAKE_SOURCE_DIR}/ShmemDecoderBackend.cpp
  ${CMAKE_SOURCE_DIR}/DecoderProcessSupervisor.cpp
  ${CMAKE_SOURCE_DIR}/DecoderOutputFramer.cpp
  ${CMAKE_SOURCE_DIR}/DecoderDiagnostics.cpp
  ${CMAKE_SOURCE_DIR}/DecoderIpc.cpp
  ${CMAKE_SOURCE_DIR}/NonInheritingProcess.cpp
)
target_include_directories (test_shmem_decoder_backend PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions (test_shmem_decoder_backend PRIVATE
  WSJT_ENABLE_LIVE_AUDIO_TEST=1
  SHMEM_WORKER_DIRECTORY="$<TARGET_FILE_DIR:test_shmem_decoder_worker>")
add_dependencies (test_shmem_decoder_backend test_shmem_decoder_worker)
target_link_libraries (test_shmem_decoder_backend wsjt_cxx Qt5::Test)
add_test (NAME test_shmem_decoder_backend COMMAND test_shmem_decoder_backend)
set_tests_properties (test_shmem_decoder_backend PROPERTIES TIMEOUT 180)
