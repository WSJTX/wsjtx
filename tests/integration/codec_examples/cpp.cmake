# SPDX-License-Identifier: GPL-3.0-or-later
# The C++17 example client, built with the project's compiler and warning flags.
find_package (Threads REQUIRED)
add_executable (codec_example_cpp ${CMAKE_SOURCE_DIR}/doc/codec/examples/cpp/codec_client.cpp)
target_link_libraries (codec_example_cpp PRIVATE Threads::Threads)
wsjt_add_codec_example_test (cpp $<TARGET_FILE:codec_example_cpp>)
