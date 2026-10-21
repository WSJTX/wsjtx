#include "wsjtx_config.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSemaphore>
#include <atomic>
#include "OperatingFrequency.hpp"
#include "TciSimServer.hpp"
#include "TciSimScript.hpp"

// Exercise the private dialog and its auto-connected slots without exposing a test API.
#include "Configuration.cpp"
#include "widgets/itoneAndicw.h"

int volatile itone[MAX_NUM_SYMBOLS] {};
float gran () { return 0; }

class RadioTestApplication final : public QApplication
{
public:
  using QApplication::QApplication;
  std::atomic<QThread *> catThread {nullptr};
  std::atomic<bool> wrongThread {false};
  std::atomic<bool> fenceArmed {false};
  QSemaphore completedCall;

  bool notify (QObject * receiver, QEvent * event) override
  {
    if (event->type () != QEvent::MetaCall || !qobject_cast<Transceiver *> (receiver))
      return QApplication::notify (receiver, event);
    static thread_local int depth = 0;
    ++depth;
    catThread.store (QThread::currentThread ());
    if (receiver->thread () != QThread::currentThread ()) wrongThread.store (true);
    auto const result = QApplication::notify (receiver, event);
    // Fence the outer call: TCI can process nested queued commands while awaiting a VFO.
    if (!--depth && fenceArmed.exchange (false)) completedCall.release ();
    return result;
  }
};

struct FrequencySession final : QObject
{
  using Frequency = Radio::Frequency;
  static constexpr Frequency nominal = 144200000;
  TciSimServer server;
  OperatingFrequency frequency {nominal};
  RigFrequencyChangePolicy::Activity activity;
  OperatingFrequency::Context context {true, true, true, false, false, false, true};
  OperatingFrequency::Observation previous {};
  OperatingFrequency::Dispatch dispatch;
  QVector<RigFrequency::Result> results;
  QStringList failures;
  Frequency appliedRx = 14074000;
  Frequency appliedTx = 14075000;
  Frequency withheldTarget = 0;
  QString withheld;
  bool wrongThread = false;

  explicit FrequencySession (Configuration& configuration)
  {
    dispatch = [&configuration] (RigFrequency::Request const& request) {
      return configuration.request_frequency (
        request, RigFrequencyChangePolicy::ChangeKind::TxPathCorrection);
    };
    configuration.set_frequency_activity_provider ([this] { checkThread (); return activity; });
    connect (&configuration, &Configuration::frequency_result, this,
      [this] (RigFrequency::Result const& result) {
        checkThread ();
        results.push_back (result);
        frequency.handleResult (result);
      });
    connect (&configuration, &Configuration::frequency_observation, this,
      [this] (RigFrequency::Evidence const& evidence) {
        checkThread ();
        frequency.observe (evidence, context);
      });
    connect (&configuration, &Configuration::transceiver_update, this,
      [this] (Transceiver::TransceiverState const& state) {
        checkThread ();
        activity.rigPtt = state.ptt ();
        activity.rigTune = state.tune ();
        context.transmitting = state.ptt ();
        OperatingFrequency::Observation current {
          state.online (), state.ptt (), state.split (), state.frequency (), state.tx_frequency ()};
        frequency.reconcile (previous, current, context, dispatch);
        previous = current;
      });
    connect (&configuration, &Configuration::transceiver_closing, this,
      [this] (bool) { checkThread (); frequency.disconnect (); });
    connect (&configuration, &Configuration::frequency_failure, this,
      [this] (QString const& reason) { failures.push_back (reason); frequency.requireRecovery (); });
    connect (&configuration, &Configuration::transceiver_failure, this,
      [this] (QString const& reason) { failures.push_back (reason); });
    connect (&server, &TciSimServer::client_arrived, this, [this] (int) {
      for (auto const& message : TciSimScript::greeting_messages (TciSimGreeting::Thetis))
        server.send_text (message);
    });
    connect (&server, &TciSimServer::text_received, this, [this] (QString const& raw, int) {
      for (auto message : raw.split (';', Qt::SkipEmptyParts))
        {
          auto const colon = message.indexOf (':');
          auto const verb = message.left (colon);
          auto const args = message.mid (colon + 1).split (',');
          if (verb == "vfo" && args.size () >= 2)
            {
              auto& applied = args.at (1) == "0" ? appliedRx : appliedTx;
              if (args.size () == 3)
                {
                  if (args.at (2).toULongLong () == withheldTarget)
                    { withheld = message; continue; }
                  applied = args.at (2).toULongLong ();
                }
              server.send_text (QString {"vfo:0,%1,%2"}.arg (args.at (1)).arg (applied));
            }
          else if (verb == "trx" || verb == "split_enable" || verb == "modulation"
                   || verb == "rx_enable" || verb == "start" || verb == "stop")
            server.send_text (message);
        }
    });
  }

  void checkThread () { wrongThread |= QThread::currentThread () != thread (); }
  bool apply (RigFrequency::Path path)
  {
    return frequency.applyCorrection (path, activity, false, dispatch);
  }
  void release ()
  {
    auto const message = withheld;
    auto const args = message.mid (message.indexOf (':') + 1).split (',');
    (args.at (1) == "0" ? appliedRx : appliedTx) = args.at (2).toULongLong ();
    withheld.clear ();
    withheldTarget = 0;
    server.send_text (message);
    server.flush ();
  }
};

class TestConfigurationRadio final : public QObject
{
  Q_OBJECT

  using RadioValidationError = Configuration::impl::RadioValidationError;

  QTemporaryDir directory_;
  QNetworkAccessManager network_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Configuration> configuration_;
  QString serial_rig_;
  QString network_rig_;
  std::unique_ptr<FrequencySession> session_;

  Configuration::impl& dialog () { return *configuration_->m_; }
  Ui::configuration_dialog& ui () { return *dialog ().ui_; }

  bool openFrequencySession ()
  {
    auto& session = *session_;
    ui ().rig_combo_box->setCurrentText ("TCI Client RX1");
    ui ().CAT_port_combo_box->setEditText (QString {"127.0.0.1:%1"}.arg (session.server.port ()));
    ui ().PTT_CAT_radio_button->setChecked (true);
    ui ().split_rig_radio_button->setChecked (true);
    ui ().tci_audio_check_box->setChecked (false);
    ui ().CAT_poll_interval_spin_box->setValue (0);
    dialog ().rig_params_ = dialog ().gather_rig_data ();
    dialog ().rig_is_dummy_ = false;
    if (!dialog ().open_rig ()) return false;
    session.frequency.setConnectionGeneration (configuration_->frequency_generation ());
    return session.server.wait_for ([&] { return session.previous.online || !session.failures.isEmpty (); }, 7000)
      && session.previous.online && session.failures.isEmpty ();
  }

private slots:
  void initTestCase ()
  {
    register_types ();
    QStandardPaths::setTestModeEnabled (true);
    QCoreApplication::setApplicationName ("test_configuration_radio");
    QVERIFY (directory_.isValid ());
  }

  void init ()
  {
    auto& app = *static_cast<RadioTestApplication *> (qApp);
    app.catThread.store (nullptr);
    app.wrongThread.store (false);
    settings_.reset (new QSettings {directory_.filePath ("settings.ini"), QSettings::IniFormat});
    settings_->clear ();
    configuration_.reset (new Configuration {&network_, QDir {directory_.path ()}, settings_.get (), nullptr});
    auto& d = dialog ();
    auto& rigs = const_cast<TransceiverFactory::Transceivers&> (d.transceiver_factory_.supported_transceivers ());
    using Caps = TransceiverFactory::Capabilities;
    for (auto it = rigs.cbegin (); it != rigs.cend (); ++it)
      {
        if (it->port_type_ == Caps::serial) serial_rig_ = it.key ();
        if (it->port_type_ == Caps::network) network_rig_ = it.key ();
      }
    QVERIFY (!serial_rig_.isEmpty ());
    QVERIFY (!network_rig_.isEmpty ());
    // OmniRig's Windows COM transport is unavailable here; use its registered capabilities.
    if (!rigs.contains ("OmniRig Rig 1"))
      rigs.insert ("OmniRig Rig 1", Caps {99904, Caps::none, true, false, true, true});
    d.enumerate_rigs ();
    d.rig_params_.rig_name = serial_rig_;
    d.rig_params_.serial_port = "/dev/test-cat";
    d.rig_params_.network_port = "localhost:4532";
    d.rig_params_.ptt_port = "/dev/test-cat";
    d.rig_params_.ptt_type = TransceiverFactory::PTT_method_DTR;
    d.rig_params_.handshake = TransceiverFactory::handshake_none;
    d.initialize_models ();
    // Audio availability is independent of these radio validation cases.
    if (ui ().sound_input_combo_box->currentIndex () < 0)
      ui ().sound_input_combo_box->addItem ("Test input");
    if (ui ().sound_input_channel_combo_box->currentIndex () < 0)
      ui ().sound_input_channel_combo_box->addItem ("Mono");
    if (ui ().sound_output_combo_box->currentIndex () < 0)
      ui ().sound_output_combo_box->addItem ("Test output");
    if (d.dns_lookup_id_ > -1) QHostInfo::abortHostLookup (d.dns_lookup_id_);
    d.dns_lookup_id_ = -1;
  }

  void cleanup ()
  {
    auto& app = *static_cast<RadioTestApplication *> (qApp);
    app.fenceArmed.store (false);
    if (session_)
      {
        configuration_->transceiver_offline ();
        session_->server.wait_for ([&] { return session_->server.client_disconnected (); }, 3000);
      }
    configuration_.reset ();
    session_.reset ();
    settings_.reset ();
  }

  void threadedFrequencyApplicationAcrossPttTail ()
  {
    session_.reset (new FrequencySession {*configuration_});
    auto& s = *session_;
    QVERIFY (s.server.listen ());
    QVERIFY (openFrequencySession ());
    s.frequency.setDesiredCorrection (125, -125);
    QVERIFY (s.frequency.requestNominal (FrequencySession::nominal, s.dispatch));
    QVERIFY (s.server.wait_for ([&] { return !s.frequency.snapshot ().pendingRx; }, 3000));
    QVERIFY (s.apply (RigFrequency::Path::Tx));
    QVERIFY (s.server.wait_for ([&] { return !s.frequency.snapshot ().pendingTx; }, 3000));
    configuration_->transceiver_ptt (true);
    QVERIFY (s.server.wait_for ([&] { return s.activity.rigPtt; }, 3000));
    s.activity.stopDelayActive = true;
    s.frequency.setDesiredCorrection (175, -175);
    QVERIFY (!s.apply (RigFrequency::Path::Rx));
    configuration_->transceiver_ptt (false);
    QVERIFY (s.server.wait_for ([&] { return !s.activity.rigPtt; }, 3000));
    QVERIFY (!s.apply (RigFrequency::Path::Rx));
    QCOMPARE (s.frequency.correctedRx (), FrequencySession::nominal + 125);
    s.activity.stopDelayActive = false;
    s.withheldTarget = FrequencySession::nominal + 175;
    QVERIFY (s.apply (RigFrequency::Path::Rx));
    QVERIFY (s.server.wait_for ([&] { return !s.withheld.isEmpty (); }, 1000));
    s.frequency.setDesiredCorrection (200, -200);
    QVERIFY (!s.apply (RigFrequency::Path::Rx));
    QCOMPARE (s.frequency.correctedRx (), FrequencySession::nominal + 125);
    s.release ();
    QVERIFY (s.server.wait_for ([&] { return !s.frequency.snapshot ().pendingRx; }, 3000));
    QVERIFY (s.apply (RigFrequency::Path::Rx));
    QVERIFY (s.apply (RigFrequency::Path::Tx));
    QVERIFY (s.server.wait_for ([&] {
      return !s.frequency.snapshot ().pendingRx && !s.frequency.snapshot ().pendingTx;
    }, 3000));
    QCOMPARE (s.appliedRx, FrequencySession::nominal + 200);
    QCOMPARE (s.appliedTx, FrequencySession::nominal - 200);
    QCOMPARE (s.frequency.correctedRx (), s.appliedRx);
    QCOMPARE (s.frequency.correctedTx (), s.appliedTx);
    QCOMPARE (s.frequency.snapshot ().establishedRxBasis, RigFrequency::Basis::Readback);
    QCOMPARE (s.frequency.snapshot ().establishedTxBasis, RigFrequency::Basis::Readback);
    QCOMPARE (s.frequency.rx (), FrequencySession::nominal);
    QCOMPARE (s.frequency.tx (), FrequencySession::nominal);
    QCOMPARE (s.frequency.remembered (), FrequencySession::nominal);
    QVERIFY (s.failures.isEmpty ());
    QVERIFY (!s.wrongThread);
    auto& app = *static_cast<RadioTestApplication *> (qApp);
    QCOMPARE (app.catThread.load (), dialog ().transceiver_thread_);
    QVERIFY (app.catThread.load () != QThread::currentThread ());
    QVERIFY (!app.wrongThread.load ());
  }

  void reconnectDiscardsQueuedFrequencyApplication ()
  {
    session_.reset (new FrequencySession {*configuration_});
    auto& s = *session_;
    QVERIFY (s.server.listen ());
    QVERIFY (openFrequencySession ());
    s.frequency.setDesiredCorrection (125, -125);
    QVERIFY (s.frequency.requestNominal (FrequencySession::nominal, s.dispatch));
    QVERIFY (s.server.wait_for ([&] { return !s.frequency.snapshot ().pendingRx; }, 3000));
    s.frequency.setDesiredCorrection (175, -175);
    s.withheldTarget = FrequencySession::nominal + 175;
    QVERIFY (s.apply (RigFrequency::Path::Rx));
    QVERIFY (s.server.wait_for ([&] { return !s.withheld.isEmpty (); }, 1000));
    auto& app = *static_cast<RadioTestApplication *> (qApp);
    while (app.completedCall.tryAcquire ()) {}
    app.fenceArmed.store (true);
    s.release ();
    // Keep the GUI queue stopped until CAT has emitted its result into that queue.
    QVERIFY (app.completedCall.tryAcquire (1, 3000));
    QVERIFY (s.frequency.snapshot ().pendingRx);
    auto const oldGeneration = configuration_->frequency_generation ();
    auto const resultsBeforeClose = s.results.size ();
    configuration_->transceiver_offline ();
    QVERIFY (s.server.wait_for ([&] { return s.server.client_disconnected (); }, 3000));
    QCOMPARE (s.results.size (), resultsBeforeClose);
    s.previous = {};
    QVERIFY (openFrequencySession ());
    QVERIFY (configuration_->frequency_generation () > oldGeneration);
    QVERIFY (s.frequency.recoveryRequired ());
    QVERIFY (!s.apply (RigFrequency::Path::Rx));
    QCOMPARE (s.frequency.rx (), FrequencySession::nominal);
    QCOMPARE (s.frequency.remembered (), FrequencySession::nominal);
    for (auto const& message : s.server.messages (TciSimServer::Direction::ClientToServer, "vfo"))
      QVERIFY (message.connection_generation != s.server.connection_generation () || message.args.size () != 3);
    s.frequency.retry (configuration_->frequency_generation ());
    QVERIFY (s.apply (RigFrequency::Path::Rx));
    QVERIFY (s.server.wait_for ([&] { return !s.frequency.snapshot ().pendingRx; }, 3000));
    QCOMPARE (s.appliedRx, FrequencySession::nominal + 175);
    QCOMPARE (s.frequency.correctedRx (), s.appliedRx);
    QVERIFY (!s.frequency.recoveryRequired ());
    QVERIFY (s.failures.isEmpty ());
    QVERIFY (!s.wrongThread);
  }

  void proxyCatBecomesExplicitSerialSelection_data ()
  {
    QTest::addColumn<bool> ("rts");
    QTest::newRow ("DTR") << false;
    QTest::newRow ("RTS") << true;
  }

  void proxyCatBecomesExplicitSerialSelection ()
  {
    QFETCH (bool, rts);
    auto& u = ui ();
    u.rig_combo_box->setCurrentText ("OmniRig Rig 1");
    (rts ? u.PTT_RTS_radio_button : u.PTT_DTR_radio_button)->setChecked (true);
    u.PTT_port_combo_box->setCurrentIndex (u.PTT_port_combo_box->findText ("CAT"));
    QCOMPARE (u.PTT_port_combo_box->currentText (), QString {"CAT"});
    QCOMPARE (dialog ().validate_radio_settings (), RadioValidationError::none);
    QSignalSpy changes {u.PTT_port_combo_box, &QComboBox::currentTextChanged};
    u.rig_combo_box->setCurrentText (serial_rig_);
    QCOMPARE (u.PTT_port_combo_box->currentText (), QString {});
    QCOMPARE (u.PTT_port_combo_box->currentIndex (), -1);
    QCOMPARE (changes.count (), 0);
    QCOMPARE (u.PTT_port_combo_box->findText ("USB"), -1);
    QVERIFY (u.CAT_port_combo_box->findText ("USB") >= 0);
    for (int i = 0; i != 3; ++i) dialog ().set_rig_invariants ();
    QCOMPARE (u.PTT_port_combo_box->currentText (), QString {});
    u.rig_combo_box->setCurrentText ("OmniRig Rig 1");
    QCOMPARE (u.PTT_port_combo_box->currentText (), QString {});
    u.rig_combo_box->setCurrentText (serial_rig_);
    QCOMPARE (u.PTT_port_combo_box->currentText (), QString {});
    QCOMPARE (dialog ().validate_radio_settings (), RadioValidationError::invalid_ptt_port);
    u.PTT_port_combo_box->setEditText ("/dev/manually-entered-ptt");
    QCOMPARE (dialog ().validate_radio_settings (), RadioValidationError::none);
    QCOMPARE (dialog ().gather_rig_data ().ptt_port, QString {"/dev/manually-entered-ptt"});
  }

  void networkToSerialUsesFinalPorts_data ()
  {
    QTest::addColumn<bool> ("same_port");
    QTest::addColumn<bool> ("hardware");
    QTest::addColumn<bool> ("rts");
    for (bool same : {false, true})
      for (bool hw : {false, true})
        for (bool rts : {false, true})
          QTest::newRow (qPrintable (QString {"same=%1,hardware=%2,rts=%3"}.arg (same).arg (hw).arg (rts)))
            << same << hw << rts;
  }

  void networkToSerialUsesFinalPorts ()
  {
    QFETCH (bool, same_port);
    QFETCH (bool, hardware);
    QFETCH (bool, rts);
    auto& u = ui ();
    (hardware ? u.CAT_handshake_hardware_radio_button : u.CAT_handshake_none_radio_button)->setChecked (true);
    (rts ? u.PTT_RTS_radio_button : u.PTT_DTR_radio_button)->setChecked (true);
    u.PTT_port_combo_box->setEditText (same_port ? "/dev/test-cat" : "/dev/test-ptt");
    for (int i = 0; i != 3; ++i)
      {
        u.rig_combo_box->setCurrentText (network_rig_);
        QVERIFY (u.PTT_RTS_radio_button->isEnabled ());
        u.rig_combo_box->setCurrentText (serial_rig_);
        QCOMPARE (u.CAT_port_combo_box->currentText (), QString {"/dev/test-cat"});
        QCOMPARE (u.PTT_RTS_radio_button->isEnabled (), !(same_port && hardware));
        QCOMPARE (u.force_DTR_combo_box->isEnabled (), !(same_port && !rts));
        QCOMPARE (u.force_RTS_combo_box->isEnabled (), !hardware && !(same_port && rts));
        dialog ().set_rig_invariants ();
        QCOMPARE (u.PTT_RTS_radio_button->isEnabled (), !(same_port && hardware));
        QCOMPARE (u.force_DTR_combo_box->isEnabled (), !(same_port && !rts));
        QCOMPARE (u.force_RTS_combo_box->isEnabled (), !hardware && !(same_port && rts));
      }
  }

  void editablePortsUpdateWithoutActivation ()
  {
    auto& u = ui ();
    QSignalSpy activated {u.PTT_port_combo_box, QOverload<int>::of (&QComboBox::activated)};
    QVERIFY (!u.force_DTR_combo_box->isEnabled ());
    u.PTT_port_combo_box->lineEdit ()->selectAll ();
    QTest::keyClicks (u.PTT_port_combo_box->lineEdit (), "/dev/typed-port");
    QCOMPARE (activated.count (), 0);
    QVERIFY (u.force_DTR_combo_box->isEnabled ());
    u.CAT_port_combo_box->setEditText ("/dev/typed-port");
    QVERIFY (!u.force_DTR_combo_box->isEnabled ());
    u.CAT_handshake_hardware_radio_button->setChecked (true);
    QVERIFY (!u.PTT_RTS_radio_button->isEnabled ());
    u.PTT_port_combo_box->setCurrentText ("/dev/separate-port");
    QVERIFY (u.PTT_RTS_radio_button->isEnabled ());
    QVERIFY (!u.force_RTS_combo_box->isEnabled ());
  }

  void validationRefreshesBlockedProgrammaticChanges_data ()
  {
    QTest::addColumn<QString> ("port");
    QTest::newRow ("unsupported proxy") << QString {"CAT"};
    QTest::newRow ("USB is not serial PTT") << QString {"USB"};
    QTest::newRow ("empty") << QString {};
    QTest::newRow ("whitespace") << QString {"   "};
  }

  void validationRefreshesBlockedProgrammaticChanges ()
  {
    QFETCH (QString, port);
    auto& u = ui ();
    {
      QSignalBlocker blocked {u.PTT_port_combo_box};
      u.PTT_port_combo_box->setEditText (port);
    }
    QCOMPARE (dialog ().validate_radio_settings (), RadioValidationError::invalid_ptt_port);
    QVERIFY (!dialog ().rig_active_);
    QCOMPARE (dialog ().rig_params_.ptt_port, QString {"/dev/test-cat"});
  }

  void cancelRestoresSavedPorts ()
  {
    auto& u = ui ();
    u.CAT_port_combo_box->setEditText ("/dev/edited-cat");
    u.PTT_port_combo_box->setEditText ("/dev/edited-ptt");
    u.configuration_dialog_button_box->button (QDialogButtonBox::Cancel)->click ();
    QCOMPARE (u.CAT_port_combo_box->currentText (), QString {"/dev/test-cat"});
    QCOMPARE (u.PTT_port_combo_box->currentText (), QString {"/dev/test-cat"});
    QVERIFY (!u.force_DTR_combo_box->isEnabled ());
  }

  void validationRefreshesHandshakeAfterBlockedRigChange ()
  {
    auto& u = ui ();
    u.rig_combo_box->setCurrentText (network_rig_);
    u.CAT_handshake_hardware_radio_button->setChecked (true);
    u.PTT_RTS_radio_button->setChecked (true);
    QVERIFY (u.PTT_RTS_radio_button->isEnabled ());
    {
      QSignalBlocker blocked {u.rig_combo_box};
      u.rig_combo_box->setCurrentText (serial_rig_);
    }
    QCOMPARE (dialog ().validate_radio_settings (), RadioValidationError::invalid_ptt_method);
    QCOMPARE (u.CAT_port_combo_box->currentText (), u.PTT_port_combo_box->currentText ());
    QVERIFY (!u.PTT_RTS_radio_button->isEnabled ());
    QVERIFY (!u.force_RTS_combo_box->isEnabled ());
  }

  void noneRetainsSerialPortSelection ()
  {
    auto& u = ui ();
    u.CAT_port_combo_box->setEditText ("/dev/edited-cat");
    u.rig_combo_box->setCurrentText ("None");
    u.rig_combo_box->setCurrentText (serial_rig_);
    QCOMPARE (u.CAT_port_combo_box->currentText (), QString {"/dev/edited-cat"});
  }

  void destructionCancelsDeferredFileInformation ()
  {
    configuration_.reset ();
    QTest::qWait (3000);
  }
};

int main (int argc, char ** argv)
{
  RadioTestApplication application {argc, argv};
  // The platform is loaded; loopback CAT and synthetic audio need no device plugins.
  QCoreApplication::setLibraryPaths ({});
  TestConfigurationRadio test;
  return QTest::qExec (&test, argc, argv);
}
#include "test_configuration_radio.moc"
