# SPDX-License-Identifier: GPL-3.0-or-later
# The Rust example registers when WSJT_ENABLE_CODEC_EXAMPLE_LANGUAGE_TESTS is on and cargo is found. cargo builds it into this build directory when the tests run, as the fixture build_codec_example_rust, so building WSJT-X never needs cargo or the network; the first build fetches serde_json, pinned by the example's Cargo.lock.
if (NOT WSJT_ENABLE_CODEC_EXAMPLE_LANGUAGE_TESTS)
  return ()
endif ()
find_program (CARGO_EXECUTABLE cargo)
if (NOT CARGO_EXECUTABLE)
  message (STATUS "Skipping the Rust codec example test: cargo not found")
  return ()
endif ()

set (codec_example_rust_target ${CMAKE_CURRENT_BINARY_DIR}/rust-target)
add_test (NAME build_codec_example_rust
  COMMAND ${CARGO_EXECUTABLE} build --release --locked
    --manifest-path ${CMAKE_SOURCE_DIR}/doc/codec/examples/rust/Cargo.toml
    --target-dir ${codec_example_rust_target})
set_tests_properties (build_codec_example_rust PROPERTIES
  FIXTURES_SETUP codec_example_rust LABELS "codec-examples;integration" TIMEOUT 600)
wsjt_add_codec_example_test (rust ${codec_example_rust_target}/release/codec_client${CMAKE_EXECUTABLE_SUFFIX})
set_tests_properties (test_codec_example_rust PROPERTIES FIXTURES_REQUIRED codec_example_rust)
