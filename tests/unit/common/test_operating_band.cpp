#include <QtTest>

#include <limits>
#include "OperatingBand.hpp"

namespace
{
  using namespace OperatingBand;

  State receiving (Frequency frequency = 14074000, QString const& band = "20m")
  {
    State state;
    state.frequency.rx = state.frequency.tx = frequency;
    state.frequency.correctedRx = state.frequency.correctedTx = frequency;
    state.dial = frequency;
    state.rxBand = state.band = band;
    return state;
  }

  Context ft8 ()
  {
    Context context;
    context.mode = "FT8";
    context.period = 15;
    context.waterfallStart = 200;
    context.waterfallSpan = 3000;
    context.txAudio = 1500;
    context.bandButtonsVisible = true;
    return context;
  }

  bool hasBandWork (Plan const& plan)
  {
    return plan.rxChanged || plan.rxBandChanged || plan.bandChanged || plan.refreshRxPresentation || plan.refreshWidegraph
      || plan.refreshButtons || plan.frequencyEffects || plan.disarmAutoRespond || plan.selectionEffects || plan.stopAutoTx
      || plan.flushSpots || plan.monitor || plan.resetFox || plan.clearLastCall
      || plan.initializeFreqCal || plan.updateTxPlan || plan.clearTemporaryMskQsy || plan.mskBase || plan.restorePower
      || plan.eraseActivity || plan.setSkedFrequency || plan.reporting || plan.apSuppression
      || plan.mskRestore;
  }
}

class TestOperatingBand final : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void displayed_frequency_preserves_rx_and_split_rules ()
  {
    auto frequency = receiving ().frequency;
    frequency.tx = 2301065000;
    frequency.correctedRx = frequency.rx + 125;
    frequency.correctedTx = frequency.tx - 75;
    QCOMPARE (displayedDial (frequency, false, false), frequency.correctedRx);
    QCOMPARE (displayedDial (frequency, false, true), frequency.correctedRx);
    QCOMPARE (displayedDial (frequency, true, true), frequency.correctedTx);
    QCOMPARE (displayedDial (frequency, true, false), frequency.tx);
  }

  void external_qsy_compares_previous_nominal_rx_band ()
  {
    Controller controller;
    controller.transition (receiving (), ft8 (), Event::Observation);
    auto const plan = controller.transition (receiving (7074000, "40m"), ft8 (), Event::Observation);
    QVERIFY (plan.rxChanged);
    QVERIFY (plan.rxBandChanged);
    QVERIFY (plan.bandChanged);
    QVERIFY (!plan.selectionEffects);
    QVERIFY (!plan.monitor);
    QVERIFY (plan.reporting);
    QCOMPARE (plan.reporting->rx, Frequency {7074000});
    QCOMPARE (plan.reporting->band, QString {"40m"});
  }

  void same_band_qsy_invalidates_rx_work_without_band_cancellation ()
  {
    Controller controller;
    controller.transition (receiving (), ft8 (), Event::Observation);
    auto const plan = controller.transition (receiving (14080000), ft8 (), Event::NominalRequest);
    QVERIFY (plan.rxChanged);
    QVERIFY (!plan.rxBandChanged);
    QVERIFY (!plan.bandChanged);
    QVERIFY (!plan.selectionEffects);
    QVERIFY (plan.reporting);
    QVERIFY (!plan.eraseActivity);
  }

  void audio_only_and_duplicate_observations_do_no_band_work ()
  {
    Controller controller;
    auto state = receiving (50280000, "6m");
    auto context = ft8 ();
    context.mode = "MSK144";
    auto const first = controller.transition (state, context, Event::Selection);
    state.frequency.audioOffset = 500;
    auto plan = controller.transition (state, context, Event::NominalRequest);
    QVERIFY (!hasBandWork (plan));
    QVERIFY (!plan.nominalChanged);
    QVERIFY (controller.current (first.revision));
    plan = controller.transition (state, context, Event::Observation);
    QVERIFY (!hasBandWork (plan));
    QVERIFY (controller.current (first.revision));
  }

  void tx_plan_while_receiving_does_not_supersede_selection ()
  {
    Controller controller;
    auto state = receiving (10489540000ULL, "3cm");
    auto const selection = controller.transition (state, ft8 (), Event::Selection);
    state.frequency.tx = state.frequency.correctedTx = 2400050000;
    auto const plan = controller.transition (state, ft8 (), Event::NominalRequest);
    QVERIFY (plan.nominalChanged);
    QVERIFY (!hasBandWork (plan));
    QVERIFY (controller.current (selection.revision));
  }

  void msk_calling_frequency_presentation_catches_up_when_disabled ()
  {
    Controller controller;
    auto context = ft8 ();
    context.mode = "MSK144";
    auto state = receiving (50280000, "6m");
    QVERIFY (controller.transition (state, context, Event::Observation).refreshRxPresentation);
    context.callingFrequency = true;
    state = receiving (50300000, "6m");
    auto plan = controller.transition (state, context, Event::NominalRequest);
    QVERIFY (plan.rxChanged);
    QVERIFY (!plan.refreshRxPresentation);
    context.callingFrequency = false;
    plan = controller.transition (state, context, Event::Observation);
    QVERIFY (!plan.rxChanged);
    QVERIFY (plan.refreshRxPresentation);
    QVERIFY (plan.refreshButtons);
    QVERIFY (!controller.transition (state, context, Event::Observation).refreshRxPresentation);
  }

  void same_row_selection_keeps_selection_effects ()
  {
    Controller controller;
    auto const state = receiving ();
    auto context = ft8 ();
    controller.transition (state, context, Event::Observation);
    context.reportingEnabled = context.fox = true;
    auto const plan = controller.transition (state, context, Event::Selection);
    QVERIFY (!plan.rxChanged);
    QVERIFY (!plan.rxBandChanged);
    QVERIFY (!plan.bandChanged);
    QVERIFY (plan.selectionEffects);
    QVERIFY (plan.flushSpots);
    QVERIFY (plan.resetFox);
    QVERIFY (plan.clearLastCall);
    QVERIFY (plan.updateTxPlan);
    QVERIFY (plan.refreshWidegraph);
    QVERIFY (plan.monitor && *plan.monitor);
    QVERIFY (!plan.stopAutoTx);
    QVERIFY (!plan.reporting);
    QVERIFY (plan.apSuppression);
  }

  void echo_monitoring_is_an_explicit_policy ()
  {
    Controller controller;
    auto context = ft8 ();
    controller.transition (receiving (144174000, "2m"), context, Event::Observation);
    context.mode = "Echo";
    context.period = 3;
    context.monitoring = true;
    auto plan = controller.transition (receiving (144120000, "2m"), context, Event::Selection);
    QVERIFY (plan.monitor && !*plan.monitor);
    QVERIFY (plan.reporting);
    QCOMPARE (plan.reporting->interval, 1800);
    QVERIFY (plan.apSuppression);
    QCOMPARE (plan.apSuppression->interval, 4500);
    context.monitoring = false;
    plan = controller.transition (receiving (144120000, "2m"), context, Event::ModeConfigured);
    QVERIFY (!plan.monitor);
  }

  void monitor_policy_uses_event_and_retune_result_data ()
  {
    QTest::addColumn<QString> ("mode");
    QTest::addColumn<int> ("event");
    QTest::addColumn<bool> ("acceptedRetune");
    QTest::addColumn<bool> ("selectionEffects");
    QTest::addColumn<bool> ("monitoring");
    QTest::addColumn<bool> ("transmitting");
    QTest::addColumn<bool> ("bandChange");
    QTest::addColumn<int> ("monitor");
    QTest::addColumn<bool> ("selected");
    constexpr int preserve {-1};
    constexpr int stop {0};
    constexpr int start {1};
    auto const row = [] (char const * name, QString const& mode, Event event,
                        bool acceptedRetune, bool selectionEffects, bool monitoring,
                        bool transmitting, bool bandChange, int monitor, bool selected) {
      QTest::newRow (name) << mode << int (event) << acceptedRetune << selectionEffects
                          << monitoring << transmitting << bandChange << monitor << selected;
    };
    row ("observation", "FT8", Event::Observation, false, false, false, false, false, preserve, false);
    row ("external-band-change", "FT8", Event::Observation, false, false, false, false, true, preserve, false);
    row ("nominal-request", "FT8", Event::NominalRequest, false, false, false, false, true, preserve, false);
    row ("selection", "FT8", Event::Selection, false, false, false, false, false, start, true);
    row ("selection-monitoring", "FT8", Event::Selection, false, false, true, false, false, preserve, true);
    row ("selection-transmitting", "FT8", Event::Selection, false, false, false, true, false, preserve, true);
    row ("configured-no-retune", "FT8", Event::ModeConfigured, false, true, false, false, false, preserve, false);
    row ("configured-retune", "FT8", Event::ModeConfigured, true, true, false, false, false, start, true);
    row ("configured-monitoring", "FT8", Event::ModeConfigured, true, true, true, false, false, preserve, true);
    row ("configured-effects-disabled", "FT8", Event::ModeConfigured, true, false, false, false, false, start, false);
    row ("configured-transmitting", "FT8", Event::ModeConfigured, true, true, false, true, false, preserve, true);
    row ("calibration-retune", "FreqCal", Event::ModeConfigured, true, false, false, false, false, start, false);
    row ("calibration-preserved", "FreqCal", Event::ModeConfigured, false, false, false, false, false, preserve, false);
    row ("jtty-configured", "JTTY", Event::ModeConfigured, false, false, false, false, false, start, false);
    row ("jtty-transmitting", "JTTY", Event::ModeConfigured, false, false, false, true, false, preserve, false);
    row ("jtty-observation", "JTTY", Event::Observation, false, false, false, false, false, preserve, false);
    row ("jtty-nominal", "JTTY", Event::NominalRequest, false, false, false, false, true, preserve, false);
    row ("echo-configured", "Echo", Event::ModeConfigured, true, true, true, false, false, stop, true);
    row ("echo-no-retune", "Echo", Event::ModeConfigured, false, false, true, false, false, stop, false);
    row ("echo-already-stopped", "Echo", Event::ModeConfigured, true, true, false, false, false, preserve, true);
    row ("echo-selection", "Echo", Event::Selection, false, false, true, false, false, stop, true);
    row ("echo-manual-monitoring", "Echo", Event::Observation, false, false, true, false, false, preserve, false);
    row ("echo-same-band-nominal", "Echo", Event::NominalRequest, false, false, true, false, false, preserve, false);
    row ("echo-external-band-change", "Echo", Event::Observation, false, false, true, false, true, stop, false);
    row ("echo-nominal-band-change", "Echo", Event::NominalRequest, false, false, true, false, true, stop, false);
  }

  void monitor_policy_uses_event_and_retune_result ()
  {
    QFETCH (QString, mode);
    QFETCH (int, event);
    QFETCH (bool, acceptedRetune);
    QFETCH (bool, selectionEffects);
    QFETCH (bool, monitoring);
    QFETCH (bool, transmitting);
    QFETCH (bool, bandChange);
    QFETCH (int, monitor);
    QFETCH (bool, selected);
    Controller controller;
    auto context = ft8 ();
    context.mode = mode;
    controller.transition (receiving (), context, Event::Observation);
    context.acceptedRetune = acceptedRetune;
    context.selectionEffects = selectionEffects;
    context.monitoring = monitoring;
    context.transmitting = transmitting;
    auto const state = bandChange ? receiving (7074000, "40m") : receiving ();
    auto const plan = controller.transition (state, context, Event (event));
    QCOMPARE (plan.monitor.has_value (), monitor != -1);
    if (monitor != -1) QCOMPARE (*plan.monitor, monitor != 0);
    QCOMPARE (plan.selectionEffects, selected);
  }

  void mode_configuration_refreshes_same_band_without_selection ()
  {
    Controller controller;
    auto context = ft8 ();
    auto const state = receiving (5357000, "60m");
    auto const first = controller.transition (state, context, Event::Observation);
    QVERIFY (first.apSuppression);
    context.mode = "FST4";
    context.period = 120;
    auto const plan = controller.transition (state, context, Event::ModeConfigured);
    QVERIFY (plan.refreshWidegraph);
    QVERIFY (!plan.bandChanged);
    QVERIFY (!plan.selectionEffects);
    QVERIFY (!plan.reporting);
    QVERIFY (plan.apSuppression);
    QCOMPARE (plan.apSuppression->interval, 180000);
    QVERIFY (!controller.completeAP (first.apSuppression->identity));
    QVERIFY (controller.completeAP (plan.apSuppression->identity));
    QVERIFY (!controller.completeAP (plan.apSuppression->identity));
  }

  void configured_mode_and_accepted_selection_share_one_plan ()
  {
    Controller controller;
    auto context = ft8 ();
    controller.transition (receiving (), context, Event::Observation);
    context.mode = "JT65";
    context.period = 60;
    context.acceptedRetune = context.selectionEffects = true;
    auto const plan = controller.transition (receiving (14076000), context, Event::ModeConfigured);
    QVERIFY (plan.rxChanged);
    QVERIFY (plan.selectionEffects);
    QVERIFY (plan.updateTxPlan);
    QVERIFY (plan.monitor && *plan.monitor);
    QVERIFY (plan.reporting);
    QVERIFY (plan.apSuppression);
    QCOMPARE (plan.reporting->interval, 36000);
    QCOMPARE (plan.apSuppression->interval, 90000);
  }

  void mode_only_and_preserved_mode_reconfiguration_disarm_auto_response ()
  {
    Controller controller;
    auto context = ft8 ();
    auto const state = receiving ();
    controller.transition (state, context, Event::ModeConfigured);
    context.mode = "Q65";
    context.period = 30;
    auto plan = controller.transition (state, context, Event::ModeConfigured);
    QVERIFY (!plan.frequencyEffects);
    QVERIFY (!plan.selectionEffects);
    QVERIFY (plan.disarmAutoRespond);

    plan = controller.transition (state, context, Event::ModeConfigured);
    QVERIFY (!plan.frequencyEffects);
    QVERIFY (plan.disarmAutoRespond);
    QVERIFY (!controller.transition (state, context, Event::Observation).disarmAutoRespond);
  }

  void selection_waterfall_boundaries_data ()
  {
    QTest::addColumn<quint64> ("frequency");
    QTest::addColumn<int> ("audio");
    QTest::addColumn<bool> ("stop");
    QTest::newRow ("below") << quint64 {14074000} << 199 << true;
    QTest::newRow ("lower-bound") << quint64 {14074000} << 200 << false;
    QTest::newRow ("upper-inside") << quint64 {14074000} << 3199 << false;
    QTest::newRow ("upper-bound") << quint64 {14074000} << 3200 << true;
    QTest::newRow ("high-frequency-exact-bound")
      << std::numeric_limits<quint64>::max () << 3200 << true;
    QTest::newRow ("high-frequency-exact-inside")
      << std::numeric_limits<quint64>::max () << 3199 << false;
  }

  void selection_waterfall_boundaries ()
  {
    QFETCH (quint64, frequency);
    QFETCH (int, audio);
    QFETCH (bool, stop);
    Controller controller;
    auto context = ft8 ();
    context.txAudio = audio;
    auto const state = receiving (frequency);
    controller.transition (state, context, Event::Observation);
    QCOMPARE (controller.transition (state, context, Event::Selection).stopAutoTx, stop);
  }

  void selection_preserves_auto_for_hopping_wait_and_wspr ()
  {
    auto context = ft8 ();
    for (int exception = 0; exception != 3; ++exception)
      {
        Controller controller;
        context.bandHopping = exception == 0;
        context.waitAndCall = exception == 1;
        context.mode = exception == 2 ? "WSPR" : "FT8";
        controller.transition (receiving (), context, Event::Observation);
        auto const plan = controller.transition (receiving (7074000, "40m"), context, Event::Selection);
        QVERIFY (!plan.stopAutoTx);
        QVERIFY (plan.selectionEffects);
      }
  }

  void split_display_change_preserves_rx_identity_and_captured_reporting_band ()
  {
    Controller controller;
    auto context = ft8 ();
    context.rememberTxPower = true;
    auto state = receiving (10489540000ULL, "3cm");
    state.frequency.tx = state.frequency.correctedTx = 2301065000;
    auto const receivingPlan = controller.transition (state, context, Event::Observation);
    QVERIFY (receivingPlan.reporting);
    state.dial = displayedDial (state.frequency, true, true);
    state.band = "13cm";
    auto const transmittingPlan = controller.transition (state, context, Event::Observation);
    QVERIFY (transmittingPlan.bandChanged);
    QVERIFY (!transmittingPlan.rxChanged);
    QVERIFY (!transmittingPlan.rxBandChanged);
    QVERIFY (transmittingPlan.restorePower);
    QVERIFY (transmittingPlan.reporting);
    QVERIFY (!controller.completeReporting (receivingPlan.reporting->identity, state.frequency.rx));
    auto const report = controller.completeReporting (transmittingPlan.reporting->identity, state.frequency.rx);
    QVERIFY (report);
    QCOMPARE (report->rx, Frequency {10489540000ULL});
    QCOMPARE (report->band, QString {"3cm"});
    QVERIFY (!controller.completeReporting (transmittingPlan.reporting->identity, state.frequency.rx));
  }

  void corrected_display_band_does_not_cancel_rx_qso ()
  {
    Controller controller;
    auto state = receiving (14349900, "20m");
    auto context = ft8 ();
    context.rememberTxPower = true;
    context.eraseActivity = true;
    controller.transition (state, context, Event::Observation);
    state.frequency.correctedRx += 200;
    state.dial = state.frequency.correctedRx;
    state.band.clear ();
    auto const plan = controller.transition (state, context, Event::Observation);
    QVERIFY (!plan.rxChanged);
    QVERIFY (!plan.rxBandChanged);
    QVERIFY (plan.bandChanged);
    QVERIFY (plan.restorePower);
    QVERIFY (plan.eraseActivity);
    QVERIFY (controller.state ());
    QCOMPARE (controller.state ()->band, QString {});
    QCOMPARE (controller.state ()->rxBand, QString {"20m"});
  }

  void tuning_and_activity_preservation_limit_band_effects ()
  {
    Controller controller;
    auto context = ft8 ();
    context.rememberTxPower = context.eraseActivity = true;
    controller.transition (receiving (), context, Event::Observation);
    context.tuning = context.preserveActivity = true;
    auto plan = controller.transition (receiving (7074000, "40m"), context, Event::Observation);
    QVERIFY (plan.bandChanged);
    QVERIFY (!plan.restorePower);
    QVERIFY (!plan.eraseActivity);
    context.tuning = context.preserveActivity = false;
    plan = controller.transition (receiving (), context, Event::Observation);
    QVERIFY (plan.restorePower);
    QVERIFY (plan.eraseActivity);
    QVERIFY (plan.setSkedFrequency);
  }

  void stale_reporting_and_ap_callbacks_cannot_release_new_suppression ()
  {
    Controller controller;
    auto const first = controller.transition (receiving (), ft8 (), Event::Observation);
    auto const second = controller.transition (receiving (7074000, "40m"), ft8 (), Event::Selection);
    QVERIFY (first.reporting);
    QVERIFY (first.apSuppression);
    QVERIFY (second.reporting);
    QVERIFY (second.apSuppression);
    QVERIFY (!controller.completeReporting (first.reporting->identity, 7074000));
    QVERIFY (!controller.completeReporting (second.reporting->identity, 14074000));
    QVERIFY (!controller.completeAP (first.apSuppression->identity));
    QVERIFY (!controller.completeAP (second.reporting->identity));
    QVERIFY (controller.completeReporting (second.reporting->identity, 7074000));
    QVERIFY (controller.completeAP (second.apSuppression->identity));
  }

  void msk_temporary_qsy_preserves_base_and_stale_restore_is_rejected ()
  {
    Controller controller;
    auto context = ft8 ();
    context.mode = "MSK144";
    auto plan = controller.transition (receiving (50280000, "6m"), context, Event::ModeConfigured);
    QVERIFY (plan.mskBase && *plan.mskBase == Frequency {50280000});
    QVERIFY (plan.mskRestore);
    auto const originalRestore = plan.mskRestore->identity;
    context.temporaryMskQsy = true;
    plan = controller.transition (receiving (50300000, "6m"), context, Event::NominalRequest);
    QVERIFY (!plan.mskBase);
    context.temporaryMskQsy = false;
    context.preserveMskBase = true;
    plan = controller.transition (receiving (50280000, "6m"), context, Event::NominalRequest);
    QVERIFY (!plan.mskBase);
    context.mode = "FT8";
    plan = controller.transition (receiving (50280000, "6m"), context, Event::ModeConfigured);
    QVERIFY (plan.mskBase && *plan.mskBase == Frequency {0});
    QVERIFY (!controller.completeMskRestore (originalRestore));
  }

  void selection_and_real_state_changes_supersede_older_plans ()
  {
    Controller controller;
    auto const state = receiving ();
    auto const first = controller.transition (state, ft8 (), Event::Selection);
    auto const second = controller.transition (state, ft8 (), Event::Selection);
    QVERIFY (!controller.current (first.revision));
    QVERIFY (controller.current (second.revision));
    controller.transition (receiving (7074000, "40m"), ft8 (), Event::Observation);
    controller.transition (state, ft8 (), Event::Observation);
    QVERIFY (!controller.current (second.revision));
  }

  void deliberate_selection_and_mode_entry_end_temporary_msk_qsy ()
  {
    Controller controller;
    auto context = ft8 ();
    context.mode = "MSK144";
    controller.transition (receiving (50280000, "6m"), context, Event::ModeConfigured);
    context.temporaryMskQsy = context.preserveMskBase = true;
    auto const temporary = receiving (50300000, "6m");
    auto plan = controller.transition (temporary, context, Event::NominalRequest);
    QVERIFY (!plan.clearTemporaryMskQsy);
    QVERIFY (!plan.mskBase);
    plan = controller.transition (temporary, context, Event::Observation);
    QVERIFY (!plan.clearTemporaryMskQsy);
    QVERIFY (!plan.mskBase);

    auto const selected = receiving (144360000, "2m");
    plan = controller.transition (selected, context, Event::Selection);
    QVERIFY (plan.clearTemporaryMskQsy);
    QVERIFY (plan.mskBase && *plan.mskBase == selected.dial);
    plan = controller.transition (selected, context, Event::ModeConfigured);
    QVERIFY (plan.clearTemporaryMskQsy);
    QVERIFY (plan.mskBase && *plan.mskBase == selected.dial);
  }

  void manual_msk_period_change_supersedes_delayed_restore ()
  {
    Controller controller;
    auto context = ft8 ();
    context.mode = "MSK144";
    auto const state = receiving (144360000, "2m");
    auto const first = controller.transition (state, context, Event::ModeConfigured);
    QVERIFY (first.mskRestore);
    context.period = 30;
    controller.transition (state, context, Event::Observation);
    QVERIFY (!controller.completeMskRestore (first.mskRestore->identity));
  }

  void calibration_reference_uses_audio_offset_and_rejects_invalid_dial ()
  {
    auto const target = calibrationDial (10000000, 1500);
    QVERIFY (target);
    QCOMPARE (*target, Frequency {9998500});
    QVERIFY (!calibrationDial (1000, 1500));
    QVERIFY (!calibrationDial (1500, 1500));
    QVERIFY (!calibrationDial (10000000, -1));
  }

  void calibration_reference_inverts_calibration_dial ()
  {
    for (auto const audio : {0, 999, 1198, 1500})
      {
        auto const dial = calibrationDial (5000000, audio);
        QVERIFY (dial);
        auto const reference = calibrationReference (*dial, audio);
        QVERIFY (reference);
        QCOMPARE (*reference, Frequency {5000000});
      }
    QVERIFY (!calibrationReference (10000000, -1));
    QVERIFY (!calibrationReference (std::numeric_limits<Frequency>::max (), 1));
    auto const top = calibrationReference (std::numeric_limits<Frequency>::max () - 1, 1);
    QVERIFY (top);
    QCOMPARE (*top, std::numeric_limits<Frequency>::max ());
  }

  void rig_tuning_ranges_answer_unknown_inside_or_outside ()
  {
    using RigFrequency::tunable;
    QVERIFY (!tunable ({}, 196500).has_value ());
    RigFrequency::Ranges const ranges {{1800000, 2000000}, {3000000, 30000000}};
    QCOMPARE (tunable (ranges, 1800000), std::optional<bool> {true});
    QCOMPARE (tunable (ranges, 2000000), std::optional<bool> {true});
    QCOMPARE (tunable (ranges, 9998500), std::optional<bool> {true});
    QCOMPARE (tunable (ranges, 196500), std::optional<bool> {false});
    QCOMPARE (tunable (ranges, 2500000), std::optional<bool> {false});
    QCOMPARE (tunable (ranges, 30000001), std::optional<bool> {false});
  }
};

QTEST_APPLESS_MAIN (TestOperatingBand)
#include "test_operating_band.moc"
