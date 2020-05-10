set (SAMPLE_FILES
  FT4/000000_000002.wav
  FT8/181201_180245.wav
  ISCAT/ISCAT-A/VK7MO_110401_235515.wav
  ISCAT/ISCAT-B/K0AWU_100714_115000.wav
  JT4/JT4A/DF2ZC_070926_040700.WAV
  JT4/JT4F/OK1KIR_141105_175700.WAV
  JT65/JT65B/DL7UAE_040308_002400.wav
  JT65/JT65B/000000_0001.wav
  JT65/JT65B/000000_0002.wav
  JT65/JT65B/000000_0003.wav
  JT65/JT65B/000000_0004.wav
  JT65/JT65B/000000_0005.wav
  JT65/JT65B/000000_0006.wav
  JT65/JT65B/000000_0007.wav
  JT9+JT65/130610_2343.wav
  JT9/130418_1742.wav
  MSK144/181211_120500.wav
  MSK144/181211_120800.wav
  QRA64/QRA64C/161113_0111.wav
  WSPR/150426_0918.wav
  )

set (contents_file_name contents_${WSJTX_VERSION_MAJOR}.${WSJTX_VERSION_MINOR}.json)
set (contents_file ${CMAKE_CURRENT_BINARY_DIR}/${contents_file_name})
set (web_tree ${CMAKE_CURRENT_BINARY_DIR}/web)
set_source_files_properties (${contents_file} PROPERTIES GENERATED ON)

add_custom_command (
  OUTPUT ${contents_file}
  COMMAND ${CMAKE_COMMAND} ARGS -Dcontents_file=${contents_file} -DFILES="${SAMPLE_FILES}" -DDEST=${CMAKE_CURRENT_BINARY_DIR} -P make_contents.cmake
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${SAMPLE_FILES} make_contents.cmake
)

find_program (RSYNC_EXECUTABLE rsync)
add_custom_command (
  OUTPUT upload.timestamp
  COMMAND ${CMAKE_COMMAND} ARGS -E copy_if_different ${contents_file} ${web_tree}/samples/${contents_file_name}
  COMMAND ${RSYNC_EXECUTABLE} ARGS -avz --progress ${CMAKE_CURRENT_BINARY_DIR}/web/samples ${PROJECT_SAMPLES_UPLOAD_DEST}
  COMMAND ${CMAKE_COMMAND} ARGS -E touch upload.timestamp
  DEPENDS ${contents_file}
  COMMENT "Uploading WSJT-X samples to web server"
  )
add_custom_target (upload-samples DEPENDS upload.timestamp)
