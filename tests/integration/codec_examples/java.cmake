# SPDX-License-Identifier: GPL-3.0-or-later
# The Java example client, doc/codec/examples/java, registers when WSJT_ENABLE_CODEC_EXAMPLE_LANGUAGE_TESTS is on and a JDK 17 or later and Maven are found. Maven builds it into this build directory when the tests run, as the fixture build_codec_example_java, so building WSJT-X never needs Maven or the network; the first build fetches Jackson and Maven's plugins into Maven's local repository.
if (NOT WSJT_ENABLE_CODEC_EXAMPLE_LANGUAGE_TESTS)
  return ()
endif ()
find_package (Java 17 COMPONENTS Runtime Development QUIET)
if (WIN32)
  find_program (MAVEN_EXECUTABLE mvn.cmd)
else ()
  find_program (MAVEN_EXECUTABLE mvn)
endif ()
if (NOT Java_FOUND OR NOT MAVEN_EXECUTABLE)
  message (STATUS "Skipping test_codec_example_java: it needs a JDK 17 or later and Maven")
  return ()
endif ()

# Maven builds with the JDK whose javac CMake found.
get_filename_component (codec_example_java_home "${Java_JAVAC_EXECUTABLE}" REALPATH)
get_filename_component (codec_example_java_home "${codec_example_java_home}" DIRECTORY)
get_filename_component (codec_example_java_home "${codec_example_java_home}" DIRECTORY)
set (codec_example_java_target ${CMAKE_CURRENT_BINARY_DIR}/java-target)
add_test (NAME build_codec_example_java
  COMMAND ${CMAKE_COMMAND} -E env "JAVA_HOME=${codec_example_java_home}"
    ${MAVEN_EXECUTABLE} -B -q -f ${CMAKE_SOURCE_DIR}/doc/codec/examples/java/pom.xml package "-Dclient.build.directory=${codec_example_java_target}")
set_tests_properties (build_codec_example_java PROPERTIES
  FIXTURES_SETUP codec_example_java LABELS "codec-examples;integration" TIMEOUT 600)
wsjt_add_codec_example_test (java ${Java_JAVA_EXECUTABLE} -jar ${codec_example_java_target}/codec-client.jar)
set_tests_properties (test_codec_example_java PROPERTIES FIXTURES_REQUIRED codec_example_java)
