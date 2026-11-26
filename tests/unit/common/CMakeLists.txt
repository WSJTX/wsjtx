qt5_wrap_ui (test_astro_UIS ${CMAKE_SOURCE_DIR}/widgets/astro.ui)
add_executable (test_astro
  test_astro.cpp
  ${CMAKE_SOURCE_DIR}/widgets/astro.cpp
  ${test_astro_UIS}
)
target_include_directories (test_astro PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries (test_astro wsjt_qt wsjt_fort_common Qt5::Test Qt5::Widgets)
wsjt_add_qt_test (test_astro test_astro)
set_tests_properties (test_astro PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable (test_radio_callsign test_radio_callsign.cpp)
target_link_libraries (test_radio_callsign wsjt_qt Qt5::Test)
wsjt_add_qt_test (test_radio_callsign test_radio_callsign)

add_executable (test_tx_output_fault_policy test_tx_output_fault_policy.cpp)
target_link_libraries (test_tx_output_fault_policy Qt5::Core Qt5::Test)
wsjt_add_qt_test (test_tx_output_fault_policy test_tx_output_fault_policy)

add_executable (test_frequency_list
  test_frequency_list.cpp
  ${CMAKE_SOURCE_DIR}/Logger.cpp
)
target_link_libraries (test_frequency_list wsjt_qt Boost::log_setup Qt5::Test)
wsjt_add_qt_test (test_frequency_list test_frequency_list)

add_executable (test_qt_helpers
  test_qt_helpers.cpp
  ${CMAKE_SOURCE_DIR}/qt_helpers.cpp
)
target_include_directories (test_qt_helpers PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_qt_helpers Qt5::Test Qt5::Network Qt5::Widgets)
wsjt_add_qt_test (test_qt_helpers test_qt_helpers)

add_executable (test_configuration_radio
  test_configuration_radio.cpp
  ${CMAKE_SOURCE_DIR}/OperatingFrequency.cpp)
add_dependencies (test_configuration_radio wsjt_qt_autogen)
target_include_directories (test_configuration_radio PRIVATE
  ${CMAKE_BINARY_DIR}
  ${CMAKE_BINARY_DIR}/wsjt_qt_autogen/include
  ${CMAKE_BINARY_DIR}/wsjt_qt_autogen/include_$<CONFIG>)
target_link_libraries (test_configuration_radio wsjt_qt wsjt_cxx wsjt_fort_common
  Boost::log_setup Qt5::Test tci_simulator)
wsjt_add_qt_test (test_configuration_radio test_configuration_radio)
set_tests_properties (test_configuration_radio PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  LABELS "frequency-control;threading"
  TIMEOUT 90)
if (WSJT_ENABLE_TSAN)
  set_property (TEST test_configuration_radio APPEND PROPERTY ENVIRONMENT
    "TSAN_OPTIONS=halt_on_error=1:exitcode=66")
endif ()

wsjt_add_qt_test (test_configuration_history_sources_fusion
  test_configuration_radio historySourceDialogKeepsControlsReachable)
set_tests_properties (test_configuration_history_sources_fusion PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_STYLE_OVERRIDE=Fusion"
  TIMEOUT 60)
wsjt_add_qt_test (test_configuration_history_sources_scaled
  test_configuration_radio historySourceDialogKeepsControlsReachable)
set_tests_properties (test_configuration_history_sources_scaled PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_ENABLE_HIGHDPI_SCALING=1;QT_SCALE_FACTOR=1.5"
  TIMEOUT 60)

add_executable (test_callsign_validator
  test_callsign_validator.cpp
  ${CMAKE_SOURCE_DIR}/validators/CallsignValidator.cpp
)
target_link_libraries (test_callsign_validator Qt5::Test Qt5::Gui)
wsjt_add_qt_test (test_callsign_validator test_callsign_validator)

add_executable (test_livecq_callsign
  test_livecq_callsign.cpp
)
target_link_libraries (test_livecq_callsign wsjt_qt Qt5::Test)
wsjt_add_qt_test (test_livecq_callsign test_livecq_callsign)

add_executable (test_decoded_message_fields test_decoded_message_fields.cpp)
target_link_libraries (test_decoded_message_fields wsjt_qt wsjt_fort_common Qt5::Test)
wsjt_add_qt_test (test_decoded_message_fields test_decoded_message_fields)

add_executable (test_decoded_time
  test_decoded_time.cpp
  ${CMAKE_SOURCE_DIR}/Network/DecodedTime.cpp
)
target_link_libraries (test_decoded_time Qt5::Test)
wsjt_add_qt_test (test_decoded_time test_decoded_time)

add_executable (test_displaytext
  test_displaytext.cpp
  ${CMAKE_SOURCE_DIR}/widgets/displaytext.cpp
)
target_link_libraries (test_displaytext wsjt_qt wsjt_fort_common Qt5::Test)
wsjt_add_qt_test (test_displaytext test_displaytext)

add_executable (test_fox_operator_actions
  test_fox_operator_actions.cpp
)
target_link_libraries (test_fox_operator_actions Qt5::Test)
wsjt_add_qt_test (test_fox_operator_actions test_fox_operator_actions)

add_executable (test_wf_palette
  test_wf_palette.cpp
)
target_link_libraries (test_wf_palette wsjt_qt Qt5::Test)
wsjt_add_qt_test (test_wf_palette test_wf_palette)

add_executable (test_decode_highlighting_model
  test_decode_highlighting_model.cpp
)
target_link_libraries (test_decode_highlighting_model wsjt_qt Qt5::Test)
wsjt_add_qt_test (test_decode_highlighting_model test_decode_highlighting_model)

add_executable (test_waterfall_scale
  test_waterfall_scale.cpp
)
target_link_libraries (test_waterfall_scale Qt5::Test)
wsjt_add_qt_test (test_waterfall_scale test_waterfall_scale)

add_executable (test_plotter_timestamps
  test_plotter_timestamps.cpp
  ${CMAKE_SOURCE_DIR}/widgets/plotter.cpp)
target_link_libraries (test_plotter_timestamps PRIVATE wsjt_qt Qt5::Test)
wsjt_link_common_fortran (test_plotter_timestamps)
wsjt_add_qt_test (test_plotter_timestamps test_plotter_timestamps)
if (WIN32)
  # The Windows offscreen plugin does not load system fonts.
  set_tests_properties (test_plotter_timestamps PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
else ()
  set_tests_properties (test_plotter_timestamps PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif ()

add_executable (test_http_user_agent
  test_http_user_agent.cpp
  )
target_link_libraries (test_http_user_agent wsjt_qt Qt5::Test)
wsjt_add_qt_test (test_http_user_agent test_http_user_agent)

add_executable (test_fox_guard_bands
  test_fox_guard_bands.cpp
  ${CMAKE_SOURCE_DIR}/FoxGuardBands.cpp
)
target_link_libraries (test_fox_guard_bands Qt5::Test)
wsjt_add_qt_test (test_fox_guard_bands test_fox_guard_bands)

add_executable (test_highlighting_rules
  test_highlighting_rules.cpp
  ${CMAKE_SOURCE_DIR}/HighlightingRules.cpp
)
target_link_libraries (test_highlighting_rules Qt5::Test)
wsjt_add_qt_test (test_highlighting_rules test_highlighting_rules)

add_executable (test_tx_start_policy
  test_tx_start_policy.cpp
)
target_link_libraries (test_tx_start_policy Qt5::Test)
wsjt_add_qt_test (test_tx_start_policy test_tx_start_policy)

add_executable (test_beacon_tx_controller
  test_beacon_tx_controller.cpp
  ${CMAKE_SOURCE_DIR}/BeaconTxController.cpp
)
target_link_libraries (test_beacon_tx_controller Qt5::Test)
wsjt_add_qt_test (test_beacon_tx_controller test_beacon_tx_controller)

add_executable (test_footswitch_input
  test_footswitch_input.cpp
  ${CMAKE_SOURCE_DIR}/FootswitchInput.cpp
)
target_link_libraries (test_footswitch_input Qt5::Test Qt5::SerialPort)
wsjt_add_qt_test (test_footswitch_input test_footswitch_input)

add_executable (test_round_robin_selection
  test_round_robin_selection.cpp
  ${CMAKE_SOURCE_DIR}/widgets/RoundRobinSelection.cpp
  ${CMAKE_SOURCE_DIR}/BeaconTxController.cpp
)
target_link_libraries (test_round_robin_selection Qt5::Test Qt5::Widgets)
wsjt_add_qt_test (test_round_robin_selection test_round_robin_selection)
set_tests_properties (test_round_robin_selection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable (test_log_window_deletion
  test_log_window_deletion.cpp
  ${CMAKE_SOURCE_DIR}/Logger.cpp
)
target_link_libraries (test_log_window_deletion wsjt_qt wsjt_cxx wsjt_fort_common
  Boost::log_setup Qt5::Test Qt5::Widgets Qt5::Sql)
wsjt_add_qt_test (test_log_window_deletion test_log_window_deletion)
set_tests_properties (test_log_window_deletion PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 60)

add_executable (test_rig_frequency_change_policy
  test_rig_frequency_change_policy.cpp
)
target_link_libraries (test_rig_frequency_change_policy Qt5::Test)
wsjt_add_qt_test (test_rig_frequency_change_policy test_rig_frequency_change_policy)

add_executable (test_wait_feature_policy
  test_wait_feature_policy.cpp
)
target_link_libraries (test_wait_feature_policy Qt5::Test)
wsjt_add_qt_test (test_wait_feature_policy test_wait_feature_policy)

add_executable (test_superfox_tx_planner
  test_superfox_tx_planner.cpp
  ${CMAKE_SOURCE_DIR}/SuperFoxTxPlanner.cpp
)
target_link_libraries (test_superfox_tx_planner Qt5::Test)
wsjt_add_qt_test (test_superfox_tx_planner test_superfox_tx_planner)

add_executable (test_auto_respond_selection_latch
  test_auto_respond_selection_latch.cpp
  ${CMAKE_SOURCE_DIR}/AutoRespondSelectionLatch.cpp
)
target_link_libraries (test_auto_respond_selection_latch Qt5::Test)
wsjt_add_qt_test (test_auto_respond_selection_latch test_auto_respond_selection_latch)

add_executable (test_auto_respond_scoring
  test_auto_respond_scoring.cpp
)
target_link_libraries (test_auto_respond_scoring Qt5::Test)
wsjt_add_qt_test (test_auto_respond_scoring test_auto_respond_scoring)

add_executable (test_auto_respond_period
  test_auto_respond_period.cpp
  ${CMAKE_SOURCE_DIR}/AutoRespondPeriod.cpp
)
target_link_libraries (test_auto_respond_period wsjt_qt wsjt_fort_common Qt5::Test)
wsjt_add_qt_test (test_auto_respond_period test_auto_respond_period)

add_executable (test_emulate_split_transceiver
  test_emulate_split_transceiver.cpp
  ${CMAKE_SOURCE_DIR}/OperatingFrequency.cpp
  ${CMAKE_SOURCE_DIR}/Transceiver/TransceiverBase.cpp
  ${CMAKE_SOURCE_DIR}/Transceiver/EmulateSplitTransceiver.cpp
  ${CMAKE_SOURCE_DIR}/Transceiver/Transceiver.cpp
  ${CMAKE_SOURCE_DIR}/qt_helpers.cpp
)
target_include_directories (test_emulate_split_transceiver PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_emulate_split_transceiver Boost::log Qt5::Test Qt5::Network Qt5::Widgets)
wsjt_add_qt_test (test_emulate_split_transceiver test_emulate_split_transceiver)

add_executable (test_tx_inhibit_transceiver
  test_tx_inhibit_transceiver.cpp
  ${CMAKE_SOURCE_DIR}/Transceiver/TxInhibitTransceiver.cpp
  ${CMAKE_SOURCE_DIR}/Transceiver/TransceiverBase.cpp
  ${CMAKE_SOURCE_DIR}/Transceiver/Transceiver.cpp
  ${CMAKE_SOURCE_DIR}/qt_helpers.cpp
)
target_include_directories (test_tx_inhibit_transceiver PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_tx_inhibit_transceiver Boost::log Qt5::Test Qt5::Network Qt5::Widgets)
wsjt_add_qt_test (test_tx_inhibit_transceiver test_tx_inhibit_transceiver)

add_executable (test_transceiver_frequency
  test_transceiver_frequency.cpp
  ${CMAKE_SOURCE_DIR}/Transceiver/TransceiverBase.cpp
  ${CMAKE_SOURCE_DIR}/Transceiver/Transceiver.cpp
  ${CMAKE_SOURCE_DIR}/qt_helpers.cpp
)
target_include_directories (test_transceiver_frequency PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_transceiver_frequency Boost::log Qt5::Test Qt5::Network Qt5::Widgets)
wsjt_add_qt_test (test_transceiver_frequency test_transceiver_frequency)

add_executable (test_radio_mode_policy test_radio_mode_policy.cpp)
target_link_libraries (test_radio_mode_policy wsjt_qt Qt5::Test)
wsjt_add_qt_test (test_radio_mode_policy test_radio_mode_policy)

add_executable (test_hamlib_mode
  test_hamlib_mode.cpp
)
target_include_directories (test_hamlib_mode PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_hamlib_mode wsjt_qt Qt5::Test)
wsjt_add_qt_test (test_hamlib_mode test_hamlib_mode)

add_executable (test_hamlib_transceiver
  test_hamlib_transceiver.cpp
)
target_include_directories (test_hamlib_transceiver PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_hamlib_transceiver wsjt_qt wsjt_fort_common Qt5::Test)
wsjt_add_qt_test (test_hamlib_transceiver test_hamlib_transceiver)

add_executable (test_hamlib_vfo_role_state
  test_hamlib_vfo_role_state.cpp
)
target_link_libraries (test_hamlib_vfo_role_state Hamlib::Hamlib Qt5::Test)
wsjt_add_qt_test (test_hamlib_vfo_role_state test_hamlib_vfo_role_state)

add_executable (test_active_station_list
  test_active_station_list.cpp
  ${CMAKE_SOURCE_DIR}/ActiveStationList.cpp
)
target_link_libraries (test_active_station_list Qt5::Test)
wsjt_add_qt_test (test_active_station_list test_active_station_list)

add_executable (test_frequency_editors
  test_frequency_editors.cpp
  ${CMAKE_SOURCE_DIR}/Logger.cpp
)
target_link_libraries (test_frequency_editors wsjt_qt Boost::log_setup Qt5::Test Qt5::Widgets Qt5::Sql)
wsjt_add_qt_test (test_frequency_editors test_frequency_editors)

add_executable (test_frequency_suggestions
  test_frequency_suggestions.cpp
  ${CMAKE_SOURCE_DIR}/Logger.cpp
)
target_link_libraries (test_frequency_suggestions wsjt_qt Boost::log_setup Qt5::Test Qt5::Widgets)
wsjt_add_qt_test (test_frequency_suggestions test_frequency_suggestions)
set_tests_properties (test_frequency_suggestions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable (test_decoded_message_reaction
  test_decoded_message_reaction.cpp
  ${CMAKE_SOURCE_DIR}/AutoRespondPeriod.cpp
)
target_link_libraries (test_decoded_message_reaction wsjt_qt wsjt_fort_common Qt5::Test)
wsjt_add_qt_test (test_decoded_message_reaction test_decoded_message_reaction)

add_executable (test_best_sp_controller test_best_sp_controller.cpp)
target_link_libraries (test_best_sp_controller wsjt_qt wsjt_fort_common Qt5::Test)
wsjt_add_qt_test (test_best_sp_controller test_best_sp_controller)

add_executable (test_decode_output_plan
  test_decode_output_plan.cpp
)
target_link_libraries (test_decode_output_plan wsjt_qt wsjt_fort_common Qt5::Test)
wsjt_add_qt_test (test_decode_output_plan test_decode_output_plan)

add_executable (test_auto_sequence_reaction
  test_auto_sequence_reaction.cpp
)
target_link_libraries (test_auto_sequence_reaction wsjt_qt wsjt_fort_common Qt5::Test)
wsjt_add_qt_test (test_auto_sequence_reaction test_auto_sequence_reaction)

add_executable (test_rr73_policy
  test_rr73_policy.cpp
  ${CMAKE_SOURCE_DIR}/Rr73Policy.cpp
)
target_link_libraries (test_rr73_policy Qt5::Test)
wsjt_add_qt_test (test_rr73_policy test_rr73_policy)

add_executable (test_qso_reaction
  test_qso_reaction.cpp
)
target_link_libraries (test_qso_reaction wsjt_qt wsjt_fort_common Qt5::Test)
wsjt_add_qt_test (test_qso_reaction test_qso_reaction)

add_executable (test_hound_transmission_policy
  test_hound_transmission_policy.cpp
  ${CMAKE_SOURCE_DIR}/HoundTransmissionPolicy.cpp
)
target_link_libraries (test_hound_transmission_policy Qt5::Test)
wsjt_add_qt_test (test_hound_transmission_policy test_hound_transmission_policy)

add_executable (test_qsy_message_parser
  test_qsy_message_parser.cpp
  ${CMAKE_SOURCE_DIR}/widgets/QSYMessageParser.cpp
)
target_link_libraries (test_qsy_message_parser Qt5::Test)
wsjt_add_qt_test (test_qsy_message_parser test_qsy_message_parser)

add_executable (test_spec_op_label
  test_spec_op_label.cpp
  ${CMAKE_SOURCE_DIR}/widgets/SpecOpLabel.cpp
)
target_link_libraries (test_spec_op_label Qt5::Test)
wsjt_add_qt_test (test_spec_op_label test_spec_op_label)

qt5_wrap_ui (test_active_stations_UIS ${CMAKE_SOURCE_DIR}/widgets/activeStations.ui)
add_executable (test_active_stations
  test_active_stations.cpp
  ${CMAKE_SOURCE_DIR}/widgets/activeStations.cpp
  ${test_active_stations_UIS}
)
target_include_directories (test_active_stations PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries (test_active_stations wsjt_qt Qt5::Test Qt5::Widgets)
wsjt_add_qt_test (test_active_stations test_active_stations)
set_tests_properties (test_active_stations PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if (WSJT_ENABLE_TSAN)
  # These tests exercise Qt GUI classes but do not test the window-system
  # integration. Offscreen QPA avoids Arch xcb/DBus startup reports while
  # preserving the tests' widget and event-loop behavior.
  set_tests_properties (
    test_qt_helpers
    test_displaytext
    test_wf_palette
    test_emulate_split_transceiver
    test_hamlib_mode
    test_frequency_editors
    test_decoded_message_reaction
    test_decode_output_plan
    test_auto_sequence_reaction
    test_qso_reaction
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif ()
qt5_wrap_ui (test_configuration_dialog_layout_UIS
  ${CMAKE_SOURCE_DIR}/Configuration.ui
)
add_executable (test_configuration_dialog_layout
  test_configuration_dialog_layout.cpp
  ${test_configuration_dialog_layout_UIS}
)
target_include_directories (test_configuration_dialog_layout PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries (test_configuration_dialog_layout wsjt_qt Qt5::Test Qt5::Widgets)
wsjt_add_qt_test (test_configuration_dialog_layout test_configuration_dialog_layout)
wsjt_add_qt_test (test_configuration_dialog_layout_fusion test_configuration_dialog_layout)
set_tests_properties (test_configuration_dialog_layout_fusion PROPERTIES
  ENVIRONMENT "QT_STYLE_OVERRIDE=Fusion"
)
wsjt_add_qt_test (test_configuration_dialog_layout_scaled test_configuration_dialog_layout)
set_tests_properties (test_configuration_dialog_layout_scaled PROPERTIES
  ENVIRONMENT "QT_ENABLE_HIGHDPI_SCALING=1;QT_SCALE_FACTOR=1.5"
)

add_executable (test_operating_frequency
  test_operating_frequency.cpp
  ${CMAKE_SOURCE_DIR}/OperatingFrequency.cpp
)
target_include_directories (test_operating_frequency PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_operating_frequency Qt5::Test)
wsjt_add_qt_test (test_operating_frequency test_operating_frequency)

add_executable (test_operating_band
  test_operating_band.cpp
  ${CMAKE_SOURCE_DIR}/OperatingBand.cpp
)
target_include_directories (test_operating_band PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_operating_band Qt5::Test)
wsjt_add_qt_test (test_operating_band test_operating_band)

add_executable (test_mode_entry
  test_mode_entry.cpp
  ${CMAKE_SOURCE_DIR}/ModeEntry.cpp
)
target_include_directories (test_mode_entry PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries (test_mode_entry Qt5::Test)
wsjt_add_qt_test (test_mode_entry test_mode_entry)

add_executable (test_frequency_adapter test_frequency_adapter.cpp)
target_link_libraries (test_frequency_adapter Qt5::Test)
wsjt_add_qt_test (test_frequency_adapter test_frequency_adapter)

add_executable (test_mainwindow_compact_controls
  test_mainwindow_compact_controls.cpp
  ${CMAKE_SOURCE_DIR}/widgets/TxDriveSlider.cpp
)
target_include_directories (test_mainwindow_compact_controls PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries (test_mainwindow_compact_controls Qt5::Test Qt5::Widgets)
wsjt_add_qt_test (test_mainwindow_compact_controls test_mainwindow_compact_controls)
wsjt_add_qt_test (test_mainwindow_compact_controls_scaled test_mainwindow_compact_controls)
set_tests_properties (test_mainwindow_compact_controls_scaled PROPERTIES
  ENVIRONMENT "QT_ENABLE_HIGHDPI_SCALING=1;QT_SCALE_FACTOR=1.5"
)

qt5_add_translation (test_l10n_QM_FILES
  l10n/wsjtx_hu.ts
  l10n/wsjtx_de.ts
  l10n/wsjtx_de_AT.ts
  l10n/wsjtx_en_GB.ts
  l10n/qt_hu.ts
  l10n/qt_de.ts
  l10n/local_de.ts
)
set (test_l10n_RESOURCES "<RCC><qresource prefix=\"/Translations\">")
foreach (catalog ${test_l10n_QM_FILES})
  get_filename_component (catalog_name ${catalog} NAME)
  string (APPEND test_l10n_RESOURCES "<file alias=\"${catalog_name}\">${catalog}</file>")
endforeach ()
string (APPEND test_l10n_RESOURCES "</qresource></RCC>")
file (WRITE ${CMAKE_CURRENT_BINARY_DIR}/test_l10n.qrc "${test_l10n_RESOURCES}")
qt5_add_resources (test_l10n_RCC ${CMAKE_CURRENT_BINARY_DIR}/test_l10n.qrc)
add_executable (test_l10n_loader
  test_l10n_loader.cpp
  ${CMAKE_SOURCE_DIR}/L10nLoader.cpp
  ${CMAKE_SOURCE_DIR}/Logger.cpp
  ${test_l10n_QM_FILES}
  ${test_l10n_RCC}
)
target_link_libraries (test_l10n_loader Boost::log_setup Qt5::Test Qt5::Widgets Qt5::Network)
wsjt_add_qt_test (test_l10n_loader test_l10n_loader)
set_tests_properties (test_l10n_loader PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable (test_configuration_preferences test_configuration_preferences.cpp)
target_link_libraries (test_configuration_preferences wsjt_qt wsjt_cxx wsjt_fort_common
  Boost::log_setup Qt5::Test)
wsjt_add_qt_test (test_configuration_preferences test_configuration_preferences)
set_tests_properties (test_configuration_preferences PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 60)
if (CMAKE_HOST_WIN32 AND WIN32)
  # The offscreen plugin uses FreeType and needs a font directory for the chooser.
  set_property (TEST test_configuration_preferences APPEND PROPERTY ENVIRONMENT
    "QT_QPA_FONTDIR=$ENV{SystemRoot}/Fonts")
endif ()

foreach (target test_displaytext test_frequency_editors)
  add_dependencies (${target} translations)
  target_compile_definitions (${target} PRIVATE WSJT_JAPANESE_QM="${CMAKE_BINARY_DIR}/wsjtx_ja.qm")
endforeach ()

add_executable (test_configuration_udp_destinations test_configuration_udp_destinations.cpp)
target_link_libraries (test_configuration_udp_destinations wsjt_qt wsjt_cxx wsjt_fort_common
  Boost::log_setup Qt5::Test)
wsjt_add_qt_test (test_configuration_udp_destinations test_configuration_udp_destinations)
set_tests_properties (test_configuration_udp_destinations PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 60)

set (test_widegraph_palette_RESOURCES "<RCC><qresource prefix=\"/Palettes\">")
foreach (palette Default Digipan)
  string (APPEND test_widegraph_palette_RESOURCES
    "<file alias=\"${palette}.pal\">${CMAKE_SOURCE_DIR}/Palettes/${palette}.pal</file>")
endforeach ()
string (APPEND test_widegraph_palette_RESOURCES "</qresource></RCC>")
file (WRITE ${CMAKE_CURRENT_BINARY_DIR}/test_widegraph_palette.qrc "${test_widegraph_palette_RESOURCES}")
qt5_add_resources (test_widegraph_palette_RCC ${CMAKE_CURRENT_BINARY_DIR}/test_widegraph_palette.qrc)
qt5_wrap_ui (test_widegraph_palette_UIS ${CMAKE_SOURCE_DIR}/widgets/widegraph.ui)
add_executable (test_widegraph_palette test_widegraph_palette.cpp
  ${CMAKE_SOURCE_DIR}/widgets/widegraph.cpp
  ${CMAKE_SOURCE_DIR}/widgets/plotter.cpp
  ${CMAKE_SOURCE_DIR}/widgets/JttySpectrum.cpp
  ${test_widegraph_palette_UIS}
  ${test_l10n_QM_FILES} ${test_l10n_RCC} ${test_widegraph_palette_RCC})
target_include_directories (test_widegraph_palette PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries (test_widegraph_palette wsjt_qt wsjt_cxx wsjt_fort_common
  Boost::log_setup Qt5::Test)
wsjt_add_qt_test (test_widegraph_palette test_widegraph_palette)
set_tests_properties (test_widegraph_palette PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
  TIMEOUT 30)
