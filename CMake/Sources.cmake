#
# Project sources -- included from the top-level CMakeLists.txt at the
# equivalent point in the file. This is included (not add_subdirectory'd),
# so CMAKE_CURRENT_SOURCE_DIR stays the project root and all paths below
# remain relative to it, exactly as when this content was inline.
#
set (fort_qt_CXXSRCS
  lib/shmem.cpp
  )

set (wsjt_qt_CXXSRCS
  ActiveStationList.cpp
  DecDataMutex.cpp
  helper_functions.cpp
  qt_helpers.cpp
  widgets/MessageBox.cpp
  MetaDataRegistry.cpp
  Network/NetworkServerLookup.cpp
  Network/DecodedTime.cpp
  revision_utils.cpp
  L10nLoader.cpp
  HighDpiScaling.cpp
  WFPalette.cpp
  Radio.cpp
  RadioMetaType.cpp
  NonInheritingProcess.cpp
  models/IARURegions.cpp
  models/Bands.cpp
  models/Modes.cpp
  models/FrequencyList.cpp
  models/StationList.cpp
  widgets/FrequencyLineEdit.cpp
  widgets/FrequencyDeltaLineEdit.cpp
  item_delegates/CandidateKeyFilter.cpp
  item_delegates/ForeignKeyDelegate.cpp
  item_delegates/MessageItemDelegate.cpp
  validators/LiveFrequencyValidator.cpp
  validators/LiveCQCallsign.cpp
  GetUserId.cpp
  Audio/AudioDevice.cpp
  Audio/TxPlaybackDiagnostics.cpp
  Audio/TxAudioQueue.cpp
  Modulator/JttyPcmFifo.cpp
  Transceiver/Transceiver.cpp
  Transceiver/TransceiverBase.cpp
  Transceiver/EmulateSplitTransceiver.cpp
  Transceiver/TxInhibitTransceiver.cpp
  Transceiver/TransceiverFactory.cpp
  Transceiver/PollingTransceiver.cpp
  Transceiver/HamlibMode.cpp
  Transceiver/HamlibTransceiver.cpp
  Transceiver/TCITransceiver.cpp
  Transceiver/HRDMessage.cpp
  Transceiver/HRDTransceiver.cpp
  Transceiver/DXLabSuiteCommanderTransceiver.cpp
  Network/NetworkMessage.cpp
  Network/MessageClient.cpp
  widgets/LettersSpinBox.cpp
  widgets/HintedSpinBox.cpp
  widgets/RestrictedSpinBox.cpp
  widgets/HelpTextWindow.cpp
  widgets/SettingsDialogLayout.cpp
  SampleDownloader.cpp
  SampleDownloader/DirectoryDelegate.cpp
  SampleDownloader/Directory.cpp
  SampleDownloader/FileNode.cpp
  SampleDownloader/RemoteFile.cpp
  DisplayManual.cpp
  PerformanceTrace.cpp
  WSJTXLogging.cpp
  Decoder/decodedtext.cpp
  qmap/qmap_decode_record.cpp
  Configuration.cpp
  logbook/logbook.cpp
  logbook/AdifQso.cpp
  MultiSettings.cpp
  validators/MaidenheadLocatorValidator.cpp
  validators/CallsignValidator.cpp
  widgets/SplashScreen.cpp
  EqualizationToolsDialog.cpp
  widgets/DoubleClickablePushButton.cpp
  widgets/DoubleClickableRadioButton.cpp
  mycustomspinbox.cpp
  Network/LotWUsers.cpp
  Network/FileDownload.cpp
  Network/FoxVerifier.cpp
  Network/Cloudlog.cpp
  Network/eqsl.cpp
  models/DecodeHighlightingModel.cpp
  widgets/DecodeHighlightingListView.cpp
  models/FoxLog.cpp
  widgets/AbstractLogWindow.cpp
  widgets/FoxLogWindow.cpp
  widgets/CabrilloLogWindow.cpp
  item_delegates/CallsignDelegate.cpp
  item_delegates/MaidenheadLocatorDelegate.cpp
  item_delegates/FrequencyDelegate.cpp
  item_delegates/FrequencyDeltaDelegate.cpp
  item_delegates/SQLiteDateTimeDelegate.cpp
  models/CabrilloLog.cpp
  logbook/AD1CCty.cpp
  logbook/WorkedBefore.cpp
  logbook/Multiplier.cpp
  Network/NetworkAccessManager.cpp
  widgets/LazyFillComboBox.cpp
  widgets/CheckableItemComboBox.cpp
  widgets/BandComboBox.cpp
  widgets/BandHopping.cpp
  otpgenerator.cpp
  MessageFilter.cpp
  MessageFilterRules.cpp
  MessageFilterLogic.cpp
  DecodeOutputPlan.cpp
  DecodedMessageReaction.cpp
  PrefixSuffix.cpp
  CountryNames.cpp
  HelpText.cpp
  FoxGuardBands.cpp
  )

set (wsjt_qtmm_CXXSRCS
  Audio/AudioStreamDescriptor.cpp
  Audio/BWFFile.cpp
  Audio/WavFile.cpp
  )

set (jt9_FSRCS
  lib/jt9.f90
  lib/jt9a.f90
  lib/streaming_io.f90
  lib/stream_setmode.c   # Windows stdin binary-mode helper (no-op on POSIX)
  lib/stream_stdin.c     # portable stdin read-fully helper (all platforms)
  lib/jt9_version.c
  )

# jt9stream sources: the Qt-free streaming decoder (B2). Same jt9.f90 main as
# jt9 but ALWAYS the jt9a stub (hard-coded, not jt9a.f90) so it links zero Qt.
set (jt9stream_FSRCS
  lib/jt9.f90
  lib/jt9a_stub.f90
  lib/streaming_io.f90
  lib/stream_setmode.c   # Windows stdin binary-mode helper (no-op on POSIX)
  lib/stream_stdin.c     # portable stdin read-fully helper (all platforms)
  lib/jt9_version.c
  )

set (wsjtx_CXXSRCS
  Audio/AudioInputSource.hpp
  Audio/AudioStreamClock.hpp
  DecoderOutputFramer.cpp
  Network/PSKReporter.cpp
  Network/PSKReporterConfiguration.cpp
  Network/PSKReporterIPFIX.cpp
  Audio/WavLoadCoordinator.cpp
  Audio/WavInputLoader.cpp
  Modulator/Modulator.cpp
  Detector/Detector.cpp
  widgets/logqso.cpp
  widgets/displaytext.cpp
  getfile.cpp
  Audio/soundout.cpp
  Audio/soundin.cpp
  widgets/meterwidget.cpp
  widgets/signalmeter.cpp
  widgets/plotter.cpp
  widgets/widegraph.cpp
  widgets/echograph.cpp
  widgets/echoplot.cpp
  widgets/fastgraph.cpp
  widgets/fastplot.cpp
  widgets/about.cpp
  widgets/astro.cpp
  widgets/messageaveraging.cpp
  widgets/activeStations.cpp
  widgets/colorhighlighting.cpp
  WSPR/WsprTxScheduler.cpp
  BeaconTxController.cpp
  AutoRespondPeriod.cpp
  AutoRespondSelectionLatch.cpp
  HighlightingRules.cpp
  Rr73Policy.cpp
  HoundTransmissionPolicy.cpp
  SuperFoxTxPlanner.cpp
  DecoderIpc.cpp
  Ft8MtdDecodeScheduler.cpp
  OperatingFrequency.cpp
  Ft8MtdDecodeCoordinator.cpp
  FastDecode.cpp
  widgets/SpecOpLabel.cpp
  widgets/mainwindow.cpp
  widgets/mainwindow_jtty.cpp
  Modulator/JttyTxStream.cpp
  widgets/mainwindow_settings.cpp
  widgets/mainwindow_show_messages.cpp
  widgets/mainwindow_arrl_digi.cpp
  widgets/mainwindow_bandhopping.cpp
  widgets/mainwindow_slots.cpp
  widgets/RoundRobinSelection.cpp
  main.cpp
  Network/wsprnet.cpp
  WSPR/WSPRBandHopping.cpp
  widgets/ExportCabrillo.cpp
  widgets/QSYMessage.cpp 
  widgets/QSYMessageParser.cpp
  widgets/QSYMessageCreator.cpp
  widgets/qsymonitor.cpp
  widgets/MMTTYIF.cpp
  )

if (WSJT_ENABLE_TESTS)
  list (APPEND wsjtx_CXXSRCS
    Audio/FixtureAudioInput.cpp
    Audio/FixtureSoundOutput.cpp
    Ft8TxLoopbackTestController.cpp
    JttyTxLoopbackTestController.cpp
    LiveAudioTestController.cpp
    )
endif ()

set (wsjt_CXXSRCS
  Logger.cpp
  lib/decoder_ipc_control.cpp
  lib/crc10.cpp
  lib/crc13.cpp
  lib/crc14.cpp
  ${wsjt_fox_CXXSRCS}
  )
# deal with a GCC v6 UB error message
set_source_files_properties (
  lib/crc10.cpp
  lib/crc13.cpp
  lib/crc14.cpp
  PROPERTIES COMPILE_FLAGS -fpermissive)

if (WIN32)
  set (wsjt_CXXSRCS
    ${wsjt_CXXSRCS}
    killbyname.cpp
    )

  set (wsjt_qt_CXXSRCS
    ${wsjt_qt_CXXSRCS}
    Transceiver/OmniRigTransceiver.cpp
    )
endif (WIN32)

set (wsjt_FSRCS
  # put module sources first in the hope that they get rebuilt before use
  lib/types.f90
  lib/C_interface_module.f90
  lib/decoder_ipc_atomic.f90
  lib/decode_completion.f90
  lib/jt9_input_validation.f90
  lib/msk_spectrum.f90
  lib/jpl_ephemeris_status.f90
  lib/shmem.f90
  lib/crc.f90
  lib/fftw3mod.f90
  lib/hashing.f90
  lib/iso_c_utilities.f90
  lib/streaming_emit.f90
  lib/streaming_control.f90
  lib/jt9_params_init.f90
  lib/streaming_apply.f90
  lib/jt4.f90
  lib/jt4_decode.f90
  lib/jt65_decode.f90
  lib/jt65_mod.f90
  lib/jt65_mod6.f90 #ft8md
  lib/ft8_decode.f90
  lib/ft4_decode.f90
  lib/fst4_decode.f90
  lib/get_q3list.f90
  lib/jt9_decode.f90
  lib/options.f90
  lib/packjt.f90
  lib/77bit/packjt77_schema.f90
  lib/77bit/packjt77_grammar.f90
  lib/77bit/packjt77.f90
  lib/qra/q65/q65_workspace.f90
  lib/qra/q65/q65.f90
  lib/q65_decode.f90
  lib/readwav.f90
  lib/timer_C_wrapper.f90
  lib/timer_impl.f90
  lib/timer_module.f90
  lib/wavhdr.f90
  lib/qra/q65/q65_encoding_modules.f90
  lib/ft8/ft8_a7.f90
  lib/ft8/ft8_a8d.f90
  lib/superfox/sfox_mod.f90
  lib/superfox/julian.f90
  lib/superfox/popen_module.f90
  lib/superfox/qpc/qpc_mod.f90
  lib/ft8var/ft8_decode_ranges.f90
  lib/ft8var/ft8_mtd_residual.f90
  lib/ft8var/ft8_decodevar.f90
  lib/jtty/jtty_source_codec.f90
  lib/jtty/jtty_mod.f90
  lib/jtty/jtty_tbcc_code_profile.f90
  lib/jtty/jtty_tbcc_list_decoder.f90
  lib/jtty/tbcc.f90
  lib/jtty/jtty_tbcc_decoder.f90
  lib/jtty/jtty_payload_correlators.f90
  lib/jtty/jtty_fec_mod.f90
  lib/jtty/jttycom.f90
  lib/jtty/jtty_mdecode.f90

  # remaining non-module sources
  lib/addit.f90
  lib/afc65b.f90
  lib/afc9.f90
  lib/ana64.f90
  lib/ana932.f90
  lib/analytic.f90
  lib/astro.f90
  lib/astrosub.f90
  lib/astro0.f90
  lib/avecho.f90
  lib/averms.f90
  lib/azdist.f90
  lib/ft8/baseline.f90
  lib/ft4/ft4_baseline.f90
  lib/blanker.f90
  lib/bpdecode40.f90
  lib/bpdecode128_90.f90
  lib/ft8/bpdecode174_91.f90
  lib/baddata.f90
  lib/cablog.f90
  lib/calibrate.f90
  lib/ccf2.f90
  lib/ccf65.f90
  lib/ft8/chkcrc13a.f90
  lib/ft8/chkcrc14a.f90
  lib/chkcall.f90
  lib/chkhist.f90
  lib/chkmsg.f90
  lib/chkss2.f90
  lib/ft4/clockit.f90
  lib/ft8/compress.f90
  lib/coord.f90
  lib/db.f90
  lib/decode4.f90
  lib/decode65a.f90
  lib/decode65b.f90
  lib/decode9w.f90
  lib/decode_echo.f90
  lib/ft8/decode174_91.f90
  lib/decoder_callbacks.f90
  lib/decoder.f90
  lib/env_module.f90
  lib/deep4.f90
  lib/deg2grid.f90
  lib/degrade_snr.f90
  lib/demod64a.f90
  lib/determ.f90
  lib/downsam9.f90
  lib/echosim.f90
  lib/echo_snr.f90
  lib/encode232.f90
  lib/encode4.f90
  lib/encode_msk40.f90
  lib/encode_128_90.f90
  lib/ft8/encode174_91.f90
  lib/ft8/encode174_91_nocrc.f90
  lib/entail.f90
  lib/ephem.f90
  lib/extract.f90
  lib/extract4.f90
  lib/extractmessage77.f90
  lib/fano232.f90
  lib/fast9.f90
  lib/fast_decode.f90
  lib/fchisq.f90
  lib/fchisq0.f90
  lib/fchisq65.f90
  lib/fil3.f90
  lib/fil3c.f90
  lib/fil4.f90
  lib/fil6521.f90
  lib/filbig.f90
  lib/ft8/filt8.f90
  lib/fitcal.f90
  lib/flat1.f90
  lib/flat1a.f90
  lib/flat1b.f90
  lib/flat2.f90
  lib/flat4.f90
  lib/flat65.f90
  lib/fmtmsg.f90
  lib/foldspec9f.f90
  lib/four2a.f90
  lib/fspread_lorentz.f90
  lib/ft8/foxfilt.f90
  lib/ft8/foxgen.f90
#  lib/ft8/foxgen_wrap.f90
  lib/freqcal.f90
  lib/ft8/ft8apset.f90
  lib/ft8/ft8b.f90
  lib/ft8/ft8code.f90
  lib/ft8/ft8_downsample.f90
  lib/ft8/ft8sim.f90
  lib/gen4.f90
  lib/gen65.f90
  lib/gen9.f90
  lib/gen_cw_wave.f90
  lib/gen_echocall.f90
  lib/genwave.f90
  lib/ft8/genft8.f90
  lib/qra/q65/genq65.f90
  lib/genmsk_128_90.f90
  lib/genmsk40.f90
  lib/ft4/ft4code.f90
  lib/ft4/genft4.f90
  lib/ft4/gen_ft4wave.f90
  lib/ft8/gen_ft8wave.f90
  lib/ft8/genft8refsig.f90
  lib/genwspr.f90
  lib/geodist.f90
  lib/ft8/get_crc14.f90
  lib/getlags.f90
  lib/getmet4.f90
  lib/ft8/get_spectrum_baseline.f90
  lib/gfsk_pulse.f90
  lib/graycode.f90
  lib/graycode65.f90
  lib/grayline.f90
  lib/grid2deg.f90
  lib/ft8/h1.f90
  lib/hash.f90
  lib/hint65.f90
  lib/hspec.f90
  lib/indexx.f90
  lib/init_random_seed.f90
  lib/interleave4.f90
  lib/interleave63.f90
  lib/interleave9.f90
  lib/inter_wspr.f90
  lib/jplsubs.f
  lib/jt9fano.f90
  lib/libration.f90
  lib/lorentzian.f90
  lib/fst4/lorentzian_fading.f90
  lib/lpf1.f90
  lib/map65_mmdec.f90
  lib/mixlpf.f90
  lib/makepings.f90
  lib/moondopjpl.f90
  lib/morse.f90
  lib/move.f90
  lib/msk40decodeframe.f90
  lib/msk144decodeframe.f90
  lib/msk40spd.f90
  lib/msk144spd.f90
  lib/msk40sync.f90
  lib/msk144sync.f90
  lib/msk40_freq_search.f90
  lib/msk144_freq_search.f90
  lib/mskrtd.f90
  lib/msk144signalquality.f90
  lib/msk144sim.f90
  lib/mskrtd.f90
  lib/nuttal_window.f90
  lib/decode_msk144.f90
  lib/ft4/ft4sim.f90
  lib/ft4/ft4sim_mult.f90
  lib/ft4/ft4_downsample.f90
  lib/77bit/my_hash.f90
  lib/wsprd/osdwspr.f90
  lib/ft8/osd174_91.f90
  lib/osd128_90.f90
  lib/pctile.f90
  lib/peakdt9.f90
  lib/peakup.f90
  lib/plotsave.f90
  lib/platanh.f90
  lib/pltanh.f90
  lib/polyfit.f90
  lib/prog_args.f90
  lib/ps4.f90
  lib/qra/q65/q65_ap.f90
  lib/qra/q65/q65_loops.f90
  lib/qra/q65/q65_set_list.f90
  lib/qra/q65/q65_set_list2.f90
  lib/refspectrum.f90
  lib/savec2.f90
  lib/save_echo_params.f90
  lib/sec0.f90
  lib/sec_midn.f90
  lib/setup65.f90
  lib/sh65.f90
  lib/sh65snr.f90
  lib/slasubs.f
  lib/sleep_msec.f90
  lib/slope.f90
  lib/smo.f90
  lib/smo121.f90
  lib/softsym.f90
  lib/softsym9f.f90
  lib/softsym9w.f90
  lib/shell.f90
  lib/spec64.f90
  lib/spec9f.f90
  lib/stdmsg.f90
  lib/subtract65.f90
  lib/ft8/subtractft8.f90
  lib/ft4/subtractft4.f90
  lib/sun.f90
  lib/symspec.f90
  lib/symspec2.f90
  lib/symspec65.f90
  lib/sync4.f90
  lib/sync65.f90
  lib/ft4/getcandidates4.f90
  lib/ft4/get_ft4_bitmetrics.f90
  lib/ft8/sync8.f90
  lib/ft8/sync8d.f90
  lib/ft4/sync4d.f90
  lib/sync9.f90
  lib/sync9f.f90
  lib/sync9w.f90
  lib/test_snr.f90
  lib/timf2.f90
  lib/tweak1.f90
  lib/twkfreq.f90
  lib/ft8/twkfreq1.f90
  lib/twkfreq65.f90
  lib/update_recent_calls.f90
  lib/update_msk40_hasharray.f90
  lib/ft8/watterson.f90
  lib/wav11.f90
  lib/wav12.f90
  lib/xcor.f90
  lib/xcor4.f90
  lib/wqdecode.f90
  lib/wqencode.f90
  lib/wspr_downsample.f90
  lib/zplot9.f90
  lib/fst4/decode240_101.f90
  lib/fst4/decode240_74.f90
  lib/fst4/encode240_101.f90
  lib/fst4/encode240_74.f90
  lib/fst4/fst4sim.f90
  lib/fst4/gen_fst4wave.f90
  lib/fst4/genfst4.f90
  lib/fst4/get_fst4_bitmetrics.f90
  lib/fst4/get_fst4_bitmetrics2.f90
  lib/fst4/ldpcsim240_101.f90
  lib/fst4/ldpcsim240_74.f90
  lib/fst4/osd240_101.f90
  lib/fst4/osd240_74.f90
  lib/fst4/fastosd240_74.f90
  lib/fst4/get_crc24.f90
  lib/fst4/fst4_baseline.f90
  lib/77bit/hash22calc.f90
  lib/ft8var/agccft8.f90
  lib/ft8var/baddatavar.f90
  lib/ft8var/bpdecode174_91var.f90
  lib/ft8var/chkfalse8var.f90
  lib/ft8var/chkflscallvar.f90
  lib/ft8var/chkgridvar.f90
  lib/ft8var/chklong8.f90
  lib/ft8var/chkspecial8var.f90
  lib/ft8var/cwfilter.f90
  lib/ft8var/datacor.f90
  lib/ft8var/encode174_91var.f90
  lib/ft8var/extract_callvar.f90
  lib/ft8var/filbigvar.f90
  lib/ft8var/fillhashvar.f90
  lib/ft8var/filtersfreevar.f90
  lib/ft8var/four2avar.f90
  lib/ft8var/ft4_mod1.f90
  lib/ft8var/ft8apsetvar.f90
  lib/ft8var/ft8bvar.f90
  lib/ft8var/ft8_downsamplevar.f90
  lib/ft8var/ft8_mod1.f90
  lib/ft8var/ft8mf1var.f90
  lib/ft8var/ft8mfcqvar.f90  
  lib/ft8var/ft8svar.f90
  lib/ft8var/ft8sdvar.f90
  lib/ft8var/ft8sd1var.f90
  lib/ft8var/genft8var.f90
  lib/ft8var/gen_ft8wavevar.f90
  lib/ft8var/genft8sdvar.f90
  lib/ft8var/jt65_mod2var.f90
  lib/ft8var/jt65_mod5.f90
  lib/ft8var/jt65_mod9.f90
  lib/ft8var/msgparservar.f90
  lib/ft8var/osd174_91var.f90
  lib/ft8var/partint.f90
  lib/ft8var/partintft8.f90
  lib/ft8var/rms_augapvar.f90
  lib/ft8var/searchcallsvar.f90
  lib/ft8var/subtractft8var.f90
  lib/ft8var/sync8var.f90
  lib/ft8var/sync8dvar.f90
  lib/ft8var/tone8.f90
  lib/ft8var/tone8myc.f90
  lib/ft8var/tonesdvar.f90
  lib/ft8var/twkfreq1var.f90  
  lib/superfox/foxgen2.f90
  lib/superfox/qpc_decode2.f90
  lib/superfox/qpc_likelihoods2.f90
  lib/superfox/qpc_snr.f90
  lib/superfox/qpc_sync.f90
  lib/superfox/sfox_ana.f90
  lib/superfox/sfox_assemble.f90
  lib/superfox/sfox_demod.f90
  lib/superfox/sfox_pack.f90
  lib/superfox/sfox_remove_ft8.f90
  lib/superfox/sfox_remove_tone.f90
  lib/superfox/sfox_unpack.f90
  lib/superfox/sfox_wave.f90
  lib/superfox/sfox_wave_gfsk.f90
  lib/superfox/sfrx_sub.f90
  lib/superfox/sftx_sub.f90
  lib/superfox/twkfreq2.f90
  lib/superfox/sfox_gen_gfsk.f90
  lib/superfox/ran1.f90
  lib/superfox/sfoxsim.f90
  lib/jtty/jtty_decode.f90
  lib/jtty/rjtty_sub.f90
  lib/jtty/jtty_peakup.f90
  lib/jtty/jtty_block_pow.f90
  lib/jtty/ana64a.f90
  lib/jtty/genjtty.f90
  lib/jtty/gen_jttywave.f90
  lib/jtty/gen_syncwave.f90
  lib/jtty/subtract_jtty.f90
  )

# temporary workaround for a gfortran v7.3 ICE on Fedora 27 64-bit
set_source_files_properties (lib/slasubs.f PROPERTIES COMPILE_FLAGS -O2)

set (ka9q_CSRCS
  lib/ftrsd/decode_rs.c
  lib/ftrsd/encode_rs.c
  lib/ftrsd/init_rs.c
  )
set_source_files_properties (${ka9q_CSRCS} PROPERTIES COMPILE_FLAGS -Wno-sign-compare)

set (qra_CSRCS
  lib/qra/qracodes/qra12_63_64_irr_b.c
  lib/qra/qracodes/qra13_64_64_irr_e.c
  lib/qra/q65/npfwht.c
  lib/qra/q65/pdmath.c
  lib/qra/q65/qracodes.c
  lib/qra/q65/normrnd.c
  lib/qra/q65/qra15_65_64_irr_e23.c
  lib/qra/q65/q65.c
  lib/qra/q65/q65_subs.c
  )

set (wsjt_CSRCS
  ${ka9q_CSRCS}
  lib/lookup3.c
  lib/ft8var/ft8_tsan.c
  lib/ftrsd/ftrsdap.c
  lib/sgran.c
  lib/golay24_table.c
  lib/gran.c
  lib/igray.c
  lib/init_random_seed.c
  lib/ldpc32_table.c
  lib/wsprd/nhash.c
  lib/tab.c
  lib/tmoonsub.c
  lib/usleep.c
  lib/vit213.c
  lib/wisdom.c
  lib/wrapkarn.c
  ${ldpc_CSRCS}
  ${qra_CSRCS}

  lib/superfox/qpc/dbgprintf.c
  lib/superfox/qpc/nhash2.c
  lib/superfox/qpc/np_qpc.c
  lib/superfox/qpc/np_rnd.c
  lib/superfox/qpc/qpc_fwht.c
  lib/superfox/qpc/qpc_n127k50q128.c
  lib/superfox/qpc/qpc_subs.c

  )

set (wsjt_qt_UISRCS
  wf_palette_design_dialog.ui
  widgets/FoxLogWindow.ui
  widgets/CabrilloLogWindow.ui
  Configuration.ui
  )

set (wsprsim_CSRCS
  lib/lookup3.c
  lib/wsprd/wsprsim.c
  lib/wsprd/wsprsim_utils.c
  lib/wsprd/wsprd_utils.c
  lib/wsprd/fano.c
  lib/wsprd/tab.c
  lib/wsprd/nhash.c
  )

set (wsprd_CSRCS
  lib/lookup3.c
  lib/wsprd/wsprd.c
  lib/wsprd/wsprsim_utils.c
  lib/wsprd/wsprd_utils.c
  lib/wsprd/fano.c
  lib/wsprd/jelinek.c
  lib/wsprd/tab.c
  lib/wsprd/nhash.c
  lib/init_random_seed.c
  lib/wsprd/wsprd_stream.c
  )

set (wsjtx_UISRCS
  widgets/mainwindow.ui
  widgets/about.ui
  widgets/astro.ui
  widgets/colorhighlighting.ui
  widgets/echograph.ui
  widgets/fastgraph.ui
  widgets/messageaveraging.ui
  widgets/activeStations.ui
  widgets/widegraph.ui
  widgets/logqso.ui
  widgets/ExportCabrillo.ui
  widgets/QSYMessage.ui
  widgets/QSYMessageCreator.ui
  widgets/qsymonitor.ui
  )

set (UDP_library_CXXSRCS
  Radio.cpp
  RadioMetaType.cpp
  Network/NetworkMessage.cpp
  UDPExamples/MessageServer.cpp
  )

set (UDP_library_HEADERS
  Radio.hpp
  UDPExamples/MessageServer.hpp
  ${PROJECT_BINARY_DIR}/udp_export.h
  )

set (message_aggregator_CXXSRCS
  UDPExamples/MessageAggregator.cpp
  UDPExamples/MessageAggregatorMainWindow.cpp
  UDPExamples/DecodesModel.cpp
  UDPExamples/BeaconsModel.cpp
  UDPExamples/ClientWidget.cpp
  validators/MaidenheadLocatorValidator.cpp
  )

set (message_aggregator_STYLESHEETS
  UDPExamples/qss/default.qss
  )

set (qcp_CXXSRCS
  qcustomplot-source/qcustomplot.cpp
  )

set (all_CXXSRCS
  ${wsjt_CXXSRCS}
  ${fort_qt_CXXSRCS}
  ${wsjt_qt_CXXSRCS}
  ${wsjt_qtmm_CXXSRCS}
  ${wsjtx_CXXSRCS}
  ${qcp_CXXSRCS}
  )

set (all_C_and_CXXSRCS
  ${wsjt_CSRCS}
  ${wsprsim_CSRCS}
  ${wsprd_CSRCS}
  ${all_CXXSRCS}
  )

set (TOP_LEVEL_RESOURCES
  icons/Darwin/wsjtx.iconset/icon_128x128.png
  contrib/gpl-v3-logo.svg
  artwork/splash.png
  )

set (WSJTX_DATA_FILES
  cty.dat
  cty.dat_copyright.txt
  grid.dat
  sat.dat
  contrib/Ephemeris/JPLEPH
  eclipse.txt
  ALLCALL7.TXT
  CALL3.TXT
  )

set (PALETTE_FILES
  Palettes/Banana.pal
  Palettes/Blue1.pal
  Palettes/Blue2.pal
  Palettes/Blue3.pal
  Palettes/Brown.pal
  Palettes/Cyan1.pal
  Palettes/Cyan2.pal
  Palettes/Cyan3.pal
  Palettes/Default.pal
  Palettes/Digipan.pal
  Palettes/Fldigi.pal
  Palettes/Gray1.pal
  Palettes/Gray2.pal
  Palettes/Green1.pal
  Palettes/Green2.pal
  Palettes/Jungle.pal
  Palettes/Linrad.pal
  Palettes/W9MDB.pal
  Palettes/Negative.pal
  Palettes/Orange.pal
  Palettes/Pink.pal
  Palettes/Rainbow.pal
  Palettes/Scope.pal
  Palettes/Sunburst.pal
  Palettes/VK4BDJ.pal
  Palettes/YL2KF.pal
  Palettes/Yellow1.pal
  Palettes/Yellow2.pal
  Palettes/ZL1FZ.pal
)

if (APPLE)
  set (WSJTX_ICON_FILE ${CMAKE_PROJECT_NAME}.icns)
  set (wsjtx_BUNDLE_DATA_FILES ${WSJTX_DATA_FILES})
  set_source_files_properties (${wsjtx_BUNDLE_DATA_FILES}
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources/wsjtx)
  set (ICONSRCS
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_16x16.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_16x16@2x.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_32x32.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_32x32@2x.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_128x128.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_128x128@2x.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_256x256.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_256x256@2x.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_512x512.png
    icons/Darwin/${CMAKE_PROJECT_NAME}.iconset/icon_512x512@2x.png
    )
  add_custom_command (
    OUTPUT ${WSJTX_ICON_FILE}
    COMMAND iconutil -c icns --output "${CMAKE_BINARY_DIR}/${WSJTX_ICON_FILE}" "${CMAKE_SOURCE_DIR}/icons/Darwin/${CMAKE_PROJECT_NAME}.iconset"
    DEPENDS ${ICONSRCS}
    COMMENT "Building Icons"
    )
else ()
  set (WSJTX_ICON_FILE icons/windows-icons/wsjtx.ico)
endif (APPLE)

set_source_files_properties (${WSJTX_ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# suppress intransigent compiler diagnostics
set_source_files_properties (lib/decoder_callbacks.f90 PROPERTIES COMPILE_FLAGS "-Wno-unused-dummy-argument")
set_source_files_properties (
  lib/filbig.f90
  lib/ft8var/filbigvar.f90
  PROPERTIES COMPILE_FLAGS "-Wno-aliasing")

# foxgen.f90's fname is genuine C++-supplied state (see mainwindow.cpp
# call sites) that the current Fortran implementation doesn't happen to
# reference; removing it would mean touching those call sites too, so we
# suppress the warning here instead, matching lib/decoder_callbacks.f90 above. The
# equivalent qmap/libqmap and map65/libm65 cases are set in those
# subdirectories' own CMakeLists.txt -- set_source_files_properties() is
# scoped to the directory it's called from, so it can't be done from here.
set_source_files_properties (lib/ft8/foxgen.f90 PROPERTIES COMPILE_FLAGS "-Wno-unused-dummy-argument")

if (NOT WSJT_QDEBUG_IN_RELEASE)
  set_property (DIRECTORY APPEND PROPERTY
    COMPILE_DEFINITIONS $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>
    )
endif ()

set_property (SOURCE ${all_C_and_CXXSRCS} APPEND_STRING PROPERTY COMPILE_FLAGS " -include wsjtx_config.h")
set_property (SOURCE ${all_C_and_CXXSRCS} APPEND PROPERTY OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/wsjtx_config.h)

if (WIN32)
  # generate the OmniRig COM interface source
  if (OMNIRIG_TYPE_LIB)
    # CI / headless build: type library path provided directly. Bypasses
    # COM registration which is not available in CI. Pass via
    # -DOMNIRIG_TYPE_LIB=<path-to-OmniRig.tlb>.
    file (TO_CMAKE_PATH "${OMNIRIG_TYPE_LIB}" AXSERVERSRCS)
    message (STATUS "Using OmniRig type library: ${AXSERVERSRCS}")
  else ()
    # Normal local build: query the COM registry for the type library.
    find_program (DUMPCPP NAMES dumpcpp-qt5 dumpcpp)
    if (NOT DUMPCPP)
      message (FATAL_ERROR "dumpcpp tool not found")
    endif ()
    execute_process (
      COMMAND ${DUMPCPP} -getfile {4FE359C5-A58F-459D-BE95-CA559FB4F270}
      OUTPUT_VARIABLE AXSERVER
      OUTPUT_STRIP_TRAILING_WHITESPACE
      )
    string (STRIP "${AXSERVER}" AXSERVER)
    if (NOT AXSERVER)
      message (FATAL_ERROR "You need to install OmniRig on this computer, or pass -DOMNIRIG_TYPE_LIB=<path>")
    endif (NOT AXSERVER)
    string (REPLACE "\"" "" AXSERVER ${AXSERVER})
    file (TO_CMAKE_PATH ${AXSERVER} AXSERVERSRCS)
  endif ()
endif ()
