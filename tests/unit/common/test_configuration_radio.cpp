#include "wsjtx_config.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QSemaphore>
#include <atomic>
#include "OperatingFrequency.hpp"
#include "TciSimServer.hpp"
#include "TciSimScript.hpp"
#include <QThreadPool>
#include <QScrollBar>
#include <QSet>
#include <functional>
#include "logbook/WorkedBefore.hpp"

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

  bool inspectHistoryDialog (std::function<void (QDialog&)> const& inspect)
  {
    bool found {false};
    auto& d = dialog ();
    QTimer::singleShot (0, &d, [&] {
        auto nested = qobject_cast<QDialog *> (QApplication::activeModalWidget ());
        if (!nested)
          {
            for (auto candidate : d.findChildren<QDialog *> ())
              if (candidate->isVisible ()) { nested = candidate; break; }
          }
        if (!nested) return;
        found = true;
        inspect (*nested);
        nested->reject ();
      });
    d.on_history_sources_push_button_clicked (false);
    return found;
  }

  bool writeHistoryFile (QString const& path, QByteArray const& contents)
  {
    QFile file {path};
    return file.open (QIODevice::WriteOnly) && file.write (contents) == contents.size ();
  }

  QByteArray historyFileContents (QString const& path)
  {
    QFile file {path};
    return file.open (QIODevice::ReadOnly) ? file.readAll () : QByteArray {};
  }

  bool prepareHistoryDirectory (QTemporaryDir const& directory)
  {
    dialog ().writeable_data_dir_ = QDir {directory.path ()};
    dialog ().data_dir_ = QDir {directory.path ()};
    return directory.isValid () && writeHistoryFile (directory.filePath ("cty.dat"),
      "United States: 05: 08: NA: 37.0: 95.0: 5.0: K:\n K,W,N;\n"
      "Germany: 14: 28: EU: 51.0: -10.0: -1.0: DL:\n DL;\n");
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

  void combinedHistoryPreservesLookupSemantics ()
  {
    QTemporaryDir history;
    QVERIFY (prepareHistoryDirectory (history));
    auto const primary = history.filePath ("wsjtx_log.adi");
    auto const supplemental = history.filePath ("station.adi");
    QByteArray const primary_record {"<call:5>K1ABC<gridsquare:6>FN42aa<band:3>20m<mode:3>FT8<eor>"};
    QByteArray const supplemental_records {primary_record
      + "<call:5>DL1AA<gridsquare:6>JO31bb<band:3>40m<mode:4>MFSK<submode:3>FT4<eor>"
        "<call:5>K2XYZ<gridsquare:4>FN31<band:3>20m<mode:3>FT8<eor>"};
    QVERIFY (writeHistoryFile (primary, primary_record));
    QVERIFY (writeHistoryFile (supplemental, supplemental_records));
    dialog ().supplemental_history_files_ = QStringList {supplemental};

    WorkedBefore worked {configuration_.get ()};
    QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
    QTRY_COMPARE (finished.count (), 1);
    QCOMPARE (finished.at (0).at (0).toInt (), 3);
    QVERIFY (finished.at (0).at (2).toString ().isEmpty ());
    QVERIFY (worked.call_worked ("k1abc", "ft8", "20m"));
    QVERIFY (worked.call_worked ("DL1AA", "FT4", "40m"));
    QVERIFY (worked.call_worked ("K2XYZ", {}, {}));
    QVERIFY (!worked.call_worked ("DL1AA", "FT8", "40m"));
    QVERIFY (!worked.call_worked ("K1ABC", "FT8", "40m"));
    QVERIFY (worked.grid_worked ("JO31cc", "FT4", "40m"));
    QVERIFY (worked.country_worked ("Germany", "FT4", "40m", AD1CCty::EntityView::DXCC));
    QVERIFY (worked.continent_worked (AD1CCty::Continent::NA, "FT8", "20m"));
    QVERIFY (worked.CQ_zone_worked (14, "FT4", "40m"));
    QVERIFY (worked.ITU_zone_worked (28, "FT4", "40m"));
  }

  void historySourcesNormalizeLexicallyAndResolveAliasesWhenLoading ()
  {
    QTemporaryDir history;
    QVERIFY (prepareHistoryDirectory (history));
    auto const primary = history.filePath ("wsjtx_log.adi");
    auto const source = history.filePath ("station.adi");
    auto const missing = history.filePath ("unavailable.adi");
    QVERIFY (writeHistoryFile (primary, "<call:5>K1ABC<band:3>20m<mode:3>FT8<eor>"));
    QVERIFY (writeHistoryFile (source, "<call:5>DL1AA<band:3>40m<mode:3>FT8<eor>"));
    QStringList files {primary, source, history.filePath ("./station.adi"), missing, missing};
    QStringList expected {source, missing};
#ifndef Q_OS_WIN
    auto const alias = history.filePath ("alias.adi");
    auto const primary_alias = history.filePath ("primary-alias.adi");
    QVERIFY (QFile::link (source, alias));
    QVERIFY (QFile::link (primary, primary_alias));
    files.append (alias);
    files.append (primary_alias);
    expected.append (alias);
    expected.append (primary_alias);
#endif
    QCOMPARE (WorkedBefore::normalize_history_files (files, primary), expected);
    dialog ().supplemental_history_files_ = expected;
    WorkedBefore worked {configuration_.get ()};
    QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
    QTRY_COMPARE (finished.count (), 1);
    QCOMPARE (finished.at (0).at (0).toInt (), 2);
    QVERIFY (worked.call_worked ("K1ABC", "FT8", "20m"));
    QVERIFY (worked.call_worked ("DL1AA", "FT8", "40m"));
    QVERIFY (worked.load_status ().contains ("2 of 3 files loaded"));
    QVERIFY (worked.load_errors ().contains (QDir::toNativeSeparators (missing)));
    QCOMPARE (configuration_->supplemental_history_files (), expected);
  }

  void supplementalRemovalAndLoggingLeaveSourceUntouched ()
  {
    QTemporaryDir history;
    QVERIFY (prepareHistoryDirectory (history));
    auto const supplemental = history.filePath ("station.adi");
    QByteArray const contents {"<call:5>DL1AA<band:3>40m<mode:3>FT8<eor>"};
    QVERIFY (writeHistoryFile (supplemental, contents));
    dialog ().supplemental_history_files_ = QStringList {supplemental};
    WorkedBefore worked {configuration_.get ()};
    QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
    QTRY_COMPARE (finished.count (), 1);
    QVERIFY (worked.call_worked ("DL1AA", {}, {}));
    QByteArray const new_record {"<call:5>K3NEW<gridsquare:4>FN20<band:3>20m<mode:3>FT8"};
    QVERIFY (worked.add ("K3NEW", "FN20", "20m", "FT8", new_record));
    QCOMPARE (historyFileContents (supplemental), contents);
    QVERIFY (historyFileContents (worked.path ()).contains (new_record));

    dialog ().supplemental_history_files_.clear ();
    worked.reload ();
    QTRY_COMPARE (finished.count (), 2);
    QVERIFY (!worked.call_worked ("DL1AA", {}, {}));
    QVERIFY (worked.call_worked ("K3NEW", "FT8", "20m"));
    QCOMPARE (historyFileContents (supplemental), contents);
  }

  void partialHistoryIdentifiesFailedSources ()
  {
    QTemporaryDir history;
    QVERIFY (prepareHistoryDirectory (history));
    auto const readable = history.filePath ("readable.adi");
    auto const missing = history.filePath ("missing.adi");
    auto const malformed = history.filePath ("malformed.adi");
    auto const unreadable = history.filePath ("directory.adi");
    QVERIFY (writeHistoryFile (readable, "<call:5>K1ABC<band:3>20m<mode:3>FT8<eor>"));
    QVERIFY (writeHistoryFile (malformed, "not an ADIF export"));
    QVERIFY (QDir {}.mkpath (unreadable));
    dialog ().supplemental_history_files_ = QStringList {readable, missing, malformed, unreadable};
    WorkedBefore worked {configuration_.get ()};
    QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
    QTRY_COMPARE (finished.count (), 1);
    QCOMPARE (finished.at (0).at (0).toInt (), 1);
    auto const error = finished.at (0).at (2).toString ();
    QVERIFY2 (error.contains (QDir::toNativeSeparators (missing)), qPrintable (error));
    QVERIFY2 (error.contains (QDir::toNativeSeparators (malformed)), qPrintable (error));
    QVERIFY2 (error.contains (QDir::toNativeSeparators (unreadable)), qPrintable (error));
    QCOMPARE (worked.load_errors (), error);
    QVERIFY (worked.call_worked ("K1ABC", "FT8", "20m"));
    QCOMPARE (configuration_->supplemental_history_files ().size (), 4);

    dialog ().supplemental_history_files_ = QStringList {missing};
    worked.reload ();
    QTRY_COMPARE (finished.count (), 2);
    QCOMPARE (finished.at (1).at (0).toInt (), 0);
    QVERIFY (worked.load_errors ().contains (QDir::toNativeSeparators (missing)));
    QVERIFY (!worked.call_worked ("K1ABC", {}, {}));

    dialog ().supplemental_history_files_.clear ();
    worked.reload ();
    QTRY_COMPARE (finished.count (), 3);
    QCOMPARE (finished.at (2).at (0).toInt (), 0);
    QVERIFY (finished.at (2).at (2).toString ().isEmpty ());
    QVERIFY (worked.load_errors ().isEmpty ());
    QVERIFY (!worked.call_worked ("K1ABC", {}, {}));
  }

  void newerHistorySelectionSupersedesQueuedCompletion ()
  {
    QTemporaryDir history;
    QVERIFY (prepareHistoryDirectory (history));
    auto const old_source = history.filePath ("old.adi");
    auto const new_source = history.filePath ("new.adi");
    QVERIFY (writeHistoryFile (old_source, "<call:5>DL1AA<band:3>40m<mode:3>FT8<eor>"));
    QVERIFY (writeHistoryFile (new_source, "<call:5>K1ABC<band:3>20m<mode:3>FT8<eor>"));
    dialog ().supplemental_history_files_ = QStringList {old_source};
    WorkedBefore worked {configuration_.get ()};
    QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
    dialog ().supplemental_history_files_ = QStringList {new_source};
    worked.reload ();
    worked.reload ();
    QTRY_COMPARE (finished.count (), 1);
    QVERIFY (!worked.call_worked ("DL1AA", {}, {}));
    QVERIFY (worked.call_worked ("K1ABC", "FT8", "20m"));
    QCOMPARE (finished.at (0).at (0).toInt (), 1);
  }

  void loggedQsoSurvivesCompletedScanAwaitingDelivery ()
  {
    QTemporaryDir history;
    QVERIFY (prepareHistoryDirectory (history));
    QVERIFY (writeHistoryFile (history.filePath ("wsjtx_log.adi"),
                              "<call:5>K1ABC<band:3>20m<mode:3>FT8<eor>"));
    WorkedBefore worked {configuration_.get ()};
    QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
    QVERIFY (QThreadPool::globalInstance ()->waitForDone (5000));
    QCOMPARE (finished.count (), 0);
    QVERIFY (worked.add ("K3NEW", "FN20", "20m", "FT8",
                        "<call:5>K3NEW<gridsquare:4>FN20<band:3>20m<mode:3>FT8"));
    QTRY_COMPARE (finished.count (), 1);
    QVERIFY (worked.call_worked ("K1ABC", "FT8", "20m"));
    QVERIFY (worked.call_worked ("K3NEW", "FT8", "20m"));
    QCOMPARE (finished.at (0).at (0).toInt (), 2);
  }

  void reentrantReloadPreservesCompletionErrors ()
  {
    QTemporaryDir history;
    QVERIFY (prepareHistoryDirectory (history));
    auto const missing = history.filePath ("missing.adi");
    auto const readable = history.filePath ("readable.adi");
    QVERIFY (writeHistoryFile (readable, "<call:5>K1ABC<band:3>20m<mode:3>FT8<eor>"));
    dialog ().supplemental_history_files_ = QStringList {missing};
    WorkedBefore worked {configuration_.get ()};
    int completions {0};
    QString error_after_reload;
    connect (&worked, &WorkedBefore::finished_loading, &worked,
             [&] (int, QString, QString const& error) {
               if (++completions == 1)
                 {
                   dialog ().supplemental_history_files_ = QStringList {readable};
                   worked.reload ();
                   error_after_reload = error;
                 }
             });
    QSignalSpy finished {&worked, &WorkedBefore::finished_loading};
    QTRY_COMPARE (finished.count (), 2);
    QVERIFY (error_after_reload.contains (QDir::toNativeSeparators (missing)));
    QVERIFY (finished.at (0).at (2).toString ().contains (QDir::toNativeSeparators (missing)));
    QVERIFY (finished.at (1).at (2).toString ().isEmpty ());
    QVERIFY (worked.call_worked ("K1ABC", "FT8", "20m"));
  }

  void supplementalSettingsRequireAcceptanceAndRestore ()
  {
    auto const source = directory_.filePath ("unavailable-station-history.adi");
    auto& d = dialog ();
    ui ().rig_combo_box->setCurrentText ("None");
    d.rig_params_ = d.gather_rig_data ();
    QSignalSpy changed {configuration_.get (), &Configuration::supplemental_history_files_changed};
    d.next_supplemental_history_files_ = QStringList {source};
    QVERIFY (configuration_->supplemental_history_files ().isEmpty ());
    d.reject ();
    QVERIFY (d.next_supplemental_history_files_.isEmpty ());
    QCOMPARE (changed.count (), 0);

    auto const primary = writable_file_path (configuration_->writeable_data_dir (), "wsjtx_log.adi");
    d.next_supplemental_history_files_ = QStringList {source, source, primary};
    d.accept ();
    QCOMPARE (configuration_->supplemental_history_files (), QStringList {source});
    QCOMPARE (changed.count (), 1);
    d.accept ();
    QCOMPARE (changed.count (), 1);
    configuration_.reset ();
    settings_->sync ();
    configuration_.reset (new Configuration {&network_, QDir {directory_.path ()}, settings_.get (), nullptr});
    QCOMPARE (configuration_->supplemental_history_files (), QStringList {source});
    QCOMPARE (dialog ().next_supplemental_history_files_, QStringList {source});
  }

  void historySourceDialogAnnotatesPrimaryAliasesWithoutChangingPaths ()
  {
#ifdef Q_OS_WIN
    QSKIP ("QFile::link creates shortcuts rather than filesystem symlinks on Windows.");
#else
    QTemporaryDir history;
    QVERIFY (prepareHistoryDirectory (history));
    auto const primary = history.filePath ("wsjtx_log.adi");
    auto const other = history.filePath ("other.adi");
    auto const alias = history.filePath ("alias.adi");
    auto const missing = history.filePath ("missing.adi");
    QByteArray const primary_contents {"<call:5>K1ABC<eor>"};
    QByteArray const other_contents {"<call:5>W1XYZ<eor>"};
    QVERIFY (writeHistoryFile (primary, primary_contents));
    QVERIFY (writeHistoryFile (other, other_contents));
    QVERIFY (QFile::link (primary, alias));
    auto& d = dialog ();
    QStringList const paths {alias, missing};
    d.supplemental_history_files_ = paths;
    d.next_supplemental_history_files_ = paths;

    auto inspect_alias = [&] (bool expected_annotation) {
        QVERIFY (inspectHistoryDialog ([&] (QDialog& nested) {
            auto sources = nested.findChild<QListWidget *> ();
            QVERIFY (sources);
            QCOMPARE (sources->count (), 2);
            QCOMPARE (sources->item (0)->data (Qt::UserRole).toString (), alias);
            QCOMPARE (sources->item (1)->data (Qt::UserRole).toString (), missing);
            QCOMPARE (sources->item (0)->text ().contains ("Same file as primary log"), expected_annotation);
            QCOMPARE (sources->item (0)->data (Qt::AccessibleTextRole).toString (), sources->item (0)->text ());
            QCOMPARE (sources->item (0)->toolTip (), sources->item (0)->text ());
            QCOMPARE (sources->item (1)->text (), QDir::toNativeSeparators (missing));
          }));
        QCOMPARE (d.next_supplemental_history_files_, paths);
        QCOMPARE (configuration_->supplemental_history_files (), paths);
      };
    inspect_alias (true);
    QVERIFY (QFile::remove (alias));
    QVERIFY (QFile::link (other, alias));
    inspect_alias (false);
    QVERIFY (QFile::remove (alias));
    inspect_alias (false);
    QCOMPARE (historyFileContents (primary), primary_contents);
    QCOMPARE (historyFileContents (other), other_contents);
#endif
  }

  void historySourceDialogKeepsControlsReachable_data ()
  {
    QTest::addColumn<int> ("font_delta");
    QTest::newRow ("default-font") << 0;
    QTest::newRow ("larger-accessibility-font") << 4;
  }

  void historySourceDialogKeepsControlsReachable ()
  {
    QFETCH (int, font_delta);
    QTemporaryDir history;
    QVERIFY (prepareHistoryDirectory (history));
    auto& d = dialog ();
    auto font = d.font ();
    font.setPointSize (font.pointSize () + font_delta);
    auto const source = history.filePath (QString (180, 'x') + ".adi");
    d.next_supplemental_history_files_ = QStringList {source};
    configuration_->set_worked_before_status ("Incomplete history: one source could not be read.",
                                              source + ": " + QString (400, 'E'));

    QVERIFY (inspectHistoryDialog ([&] (QDialog& nested) {
        auto scroll = nested.findChild<QScrollArea *> ();
        auto sources = nested.findChild<QListWidget *> ();
        auto status = nested.findChild<QTextEdit *> ();
        auto primary = nested.findChild<QLineEdit *> ();
        auto buttons = nested.findChild<QDialogButtonBox *> ();
        QVERIFY (scroll);
        QVERIFY (sources);
        QVERIFY (status);
        QVERIFY (primary);
        QVERIFY (buttons);
        nested.setFont (font);
        nested.resize ({640, 480});
        nested.layout ()->activate ();
        QApplication::processEvents ();
        QCOMPARE (nested.size (), QSize (640, 480));
        QCOMPARE (status->font ().pointSizeF (), font.pointSizeF ());
        QCOMPARE (primary->font ().pointSizeF (), font.pointSizeF ());
        auto const geometry_in = [] (QWidget * widget, QWidget * ancestor) {
            return QRect {widget->mapTo (ancestor, {}), widget->size ()};
          };
        auto layout_diagnostic = [&] {
            QString diagnostic;
            {
              QDebug debug {&diagnostic};
              debug << "viewport" << scroll->viewport ()->rect ()
                    << "content" << scroll->widget ()->size () << scroll->widget ()->minimumSizeHint ()
                    << "horizontalMaximum" << scroll->horizontalScrollBar ()->maximum ()
                    << "font" << font.pointSizeF () << nested.fontMetrics ().height ();
              for (auto label : nested.findChildren<QLabel *> ())
                debug << "label" << label->text ().left (80) << label->minimumSizeHint ();
            }
            return diagnostic;
          };
        QVERIFY2 (scroll->horizontalScrollBar ()->maximum () == 0, qPrintable (layout_diagnostic ()));
        auto ok = buttons->button (QDialogButtonBox::Ok);
        auto cancel = buttons->button (QDialogButtonBox::Cancel);
        QVERIFY (ok);
        QVERIFY (cancel);
        QVERIFY (nested.contentsRect ().contains (geometry_in (buttons, &nested)));
        QVERIFY (nested.contentsRect ().contains (geometry_in (ok, &nested)));
        QVERIFY (nested.contentsRect ().contains (geometry_in (cancel, &nested)));
        QVERIFY (!scroll->isAncestorOf (buttons));
        QVERIFY (sources->horizontalScrollBar ()->maximum () > 0);
        if (font_delta) QVERIFY (scroll->verticalScrollBar ()->maximum () > 0);

        sources->setCurrentRow (0);
        QSet<QWidget *> expected_focus {primary, sources, status, ok, cancel};
        for (auto button : nested.findChildren<QPushButton *> ())
          if (button->isEnabled () && button->focusPolicy () != Qt::NoFocus)
            expected_focus.insert (button);
        QSet<QWidget *> observed_focus;
        ok->setFocus ();
        QApplication::processEvents ();
        for (int step = 0; step != 80; ++step)
          {
            auto focused = nested.focusWidget ();
            QVERIFY (focused);
            observed_focus.insert (focused);
            if (scroll->widget ()->isAncestorOf (focused))
              {
                auto focus_rect = focused->inputMethodQuery (Qt::ImCursorRectangle).toRect ();
                if (focus_rect == focused->QWidget::inputMethodQuery (Qt::ImCursorRectangle).toRect ())
                  focus_rect = focused->rect ();
                auto visible_control = focused->rect ();
                if (auto inner_scroll = qobject_cast<QAbstractScrollArea *> (focused))
                  visible_control = geometry_in (inner_scroll->viewport (), focused);
                auto const clipped_focus = focus_rect.intersected (visible_control);
                QVERIFY2 (!clipped_focus.isEmpty (), qPrintable (focused->accessibleName ()));
                auto const visible_focus = QRect {focused->mapTo (scroll->viewport (), clipped_focus.topLeft ()), clipped_focus.size ()};
                QString diagnostic;
                {
                  QDebug debug {&diagnostic};
                  debug << "focused" << focused->accessibleName () << visible_focus
                        << "viewport" << scroll->viewport ()->rect () << "font" << font.pointSizeF ()
                        << layout_diagnostic ();
                }
                QVERIFY2 (scroll->viewport ()->rect ().contains (visible_focus), qPrintable (diagnostic));
              }
            QTest::keyClick (focused, Qt::Key_Tab);
            QApplication::processEvents ();
          }
        for (auto control : expected_focus)
          QVERIFY2 (observed_focus.contains (control), qPrintable (control->accessibleName () + ' ' + control->objectName ()));
        for (auto control : {static_cast<QWidget *> (sources), static_cast<QWidget *> (status)})
          {
            auto const bounds = geometry_in (control, scroll->widget ());
            scroll->ensureVisible (bounds.center ().x (), bounds.center ().y (),
                                   bounds.width () / 2 + 1, bounds.height () / 2 + 1);
            QApplication::processEvents ();
            QString diagnostic;
            {
              QDebug debug {&diagnostic};
              debug << "control" << control->accessibleName () << geometry_in (control, scroll->viewport ())
                    << "viewport" << scroll->viewport ()->rect ()
                    << "content" << scroll->widget ()->size () << scroll->widget ()->minimumSizeHint ()
                    << "controlMinimum" << control->minimumSizeHint ()
                    << "scrollPosition" << scroll->horizontalScrollBar ()->value () << scroll->verticalScrollBar ()->value ()
                    << "scrollMaximum" << scroll->horizontalScrollBar ()->maximum () << scroll->verticalScrollBar ()->maximum ();
            }
            QVERIFY2 (scroll->viewport ()->rect ().contains (geometry_in (control, scroll->viewport ())), qPrintable (diagnostic));
          }
        QCOMPARE (sources->item (0)->data (Qt::UserRole).toString (), source);
        QVERIFY (status->toPlainText ().contains (source));
      }));
  }

  void historySourceDialogStagesRemovalAndShowsLiveStatus ()
  {
    QTemporaryDir history;
    QVERIFY (history.isValid ());
    auto const source = history.filePath ("station.adi");
    QByteArray const contents {"<call:5>K1ABC<eor>"};
    QVERIFY (writeHistoryFile (source, contents));
    auto& d = dialog ();
    d.supplemental_history_files_ = QStringList {source};
    d.next_supplemental_history_files_ = QStringList {source};
    QSignalSpy changed {configuration_.get (), &Configuration::supplemental_history_files_changed};
    QString const summary {"Incomplete history: one source could not be read."};
    QString const errors {"station.adi: unavailable"};
    QStringList observed_paths;
    QList<bool> observed_removals;
    QStringList observed_statuses;
    auto edit = [&] (bool accept) {
        QTimer::singleShot (0, &d, [&] {
            auto nested = qobject_cast<QDialog *> (QApplication::activeModalWidget ());
            if (!nested)
              {
                for (auto candidate : d.findChildren<QDialog *> ())
                  if (candidate->isVisible ()) { nested = candidate; break; }
              }
            if (!nested) return;
            auto sources = nested->findChild<QListWidget *> ();
            auto status = nested->findChild<QTextEdit *> ();
            QPushButton * remove {nullptr};
            for (auto button : nested->findChildren<QPushButton *> ())
              if (button->text () == "&Remove") remove = button;
            observed_paths.append (sources && sources->count () == 1
              ? sources->item (0)->data (Qt::UserRole).toString () : QString {});
            if (sources && remove && sources->count () == 1)
              {
                sources->setCurrentRow (0);
                remove->click ();
              }
            observed_removals.append (sources && sources->count () == 0);
            configuration_->set_worked_before_status (summary, errors);
            observed_statuses.append (status ? status->toPlainText () : QString {});
            nested->done (accept ? QDialog::Accepted : QDialog::Rejected);
          });
        d.on_history_sources_push_button_clicked (false);
      };

    edit (false);
    QCOMPARE (d.next_supplemental_history_files_, QStringList {source});
    edit (true);
    QVERIFY (d.next_supplemental_history_files_.isEmpty ());
    QCOMPARE (configuration_->supplemental_history_files (), QStringList {source});
    QCOMPARE (changed.count (), 0);
    d.reject ();
    QCOMPARE (d.next_supplemental_history_files_, QStringList {source});
    QCOMPARE (observed_paths, QStringList ({source, source}));
    QCOMPARE (observed_removals, QList<bool> ({true, true}));
    QCOMPARE (observed_statuses, QStringList ({summary + "\n\n" + errors, summary + "\n\n" + errors}));
    QCOMPARE (historyFileContents (source), contents);
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
