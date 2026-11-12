# SPDX-License-Identifier: GPL-3.0-or-later
# The Node.js example client, doc/codec/examples/node, needs Node.js 18 or later and nothing else.
if (NOT WSJT_ENABLE_CODEC_EXAMPLE_LANGUAGE_TESTS)
  return ()
endif ()
find_program (NODE_EXECUTABLE node)
if (NODE_EXECUTABLE)
  execute_process (COMMAND ${NODE_EXECUTABLE} --version
    OUTPUT_VARIABLE codec_example_node_version OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE codec_example_node_result ERROR_QUIET)
  string (REGEX REPLACE "^v" "" codec_example_node_version "${codec_example_node_version}")
endif ()
if (NODE_EXECUTABLE AND codec_example_node_result EQUAL 0 AND codec_example_node_version VERSION_GREATER_EQUAL 18)
  wsjt_add_codec_example_test (node ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/doc/codec/examples/node/codec_client.js)
else ()
  message (STATUS "Skipping test_codec_example_node: it needs Node.js 18 or later")
endif ()
