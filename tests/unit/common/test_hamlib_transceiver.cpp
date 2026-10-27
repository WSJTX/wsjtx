#include "wsjtx_config.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <hamlib/rig.h>
#ifndef HAMLIB_STATE
#include <hamlib/rig_state.h>
#endif
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "Transceiver/HamlibTransceiver.hpp"
#include "widgets/itoneAndicw.h"

// The transceiver factory links TCI's references to application-owned DSP data.
int volatile itone[MAX_NUM_SYMBOLS] {};
float gran () { return 0.0f; }

namespace
{
  class RigFixture
  {
  public:
    explicit RigFixture (vfo_t receive_vfo, bool main_sub = false, bool single_vfo_addressing = false)
      : rx_vfo {receive_vfo}
      , tx_vfo {main_sub ? RIG_VFO_SUB : receive_vfo == RIG_VFO_A ? RIG_VFO_B : RIG_VFO_A}
      , vfos {main_sub ? RIG_VFO_MAIN | RIG_VFO_SUB : RIG_VFO_A | RIG_VFO_B}
    {
      active = this;
      auto dummy = std::unique_ptr<RIG, decltype (&rig_cleanup)> {rig_init (RIG_MODEL_DUMMY), rig_cleanup};
      if (!dummy) return;
      std::vector<rig_model_t> occupied_buckets;
      rig_list_foreach ([] (rig_caps const * existing, rig_ptr_t context)
                        {
                          static_cast<std::vector<rig_model_t> *> (context)
                            ->push_back (existing->rig_model % 65535);
                          return 1;
                        }, &occupied_buckets);
      // Empty buckets avoid Hamlib 4.7.2's fatal collisions and remain valid with newer registries.
      for (int index = MAX_MODELS_PER_BACKEND - 1; index > 0; --index)
        {
          auto const candidate = RIG_MAKE_MODEL (RIG_DUMMY, index);
          if (std::find (occupied_buckets.begin (), occupied_buckets.end (), candidate % 65535)
              == occupied_buckets.end ())
            {
              model = candidate;
              break;
            }
        }
      if (!model) return;
      original = *dummy->caps;
      caps = original;
      caps.rig_model = model;
      caps.model_name = "WSJT-X split-mode test";
      // The constructor accepts network rigs; Dummy initializes an in-memory port.
      caps.port_type = RIG_PORT_NETWORK;
      caps.rig_init = [] (RIG * rig)
        {
          auto& self = *active;
          auto rc = self.original.rig_init (rig);
          if (RIG_OK != rc) return rc;
          self.rig = rig;
          HAMLIB_STATE (rig)->vfo_list = self.vfos;
          rc = rig_set_conf (rig, rig_token_lookup (rig, "rig_pathname"), "");
          if (RIG_OK != rc) return rc;
          rc = rig_set_conf (rig, rig_token_lookup (rig, "poll_interval"), "0");
          if (RIG_OK != rc) return rc;
          return self.original.set_vfo (rig, self.rx_vfo);
        };
      caps.set_mode = [] (RIG * rig, vfo_t vfo, rmode_t mode, pbwidth_t width)
        {
          active->mode_targets.push_back (vfo);
          if (active->reject_tx_mode && vfo == active->tx_vfo) return -RIG_EIO;
          return active->original.set_mode (rig, vfo, mode, width);
        };
      caps.get_mode = [] (RIG * rig, vfo_t vfo, rmode_t * mode, pbwidth_t * width)
        {
          active->mode_queries.push_back (vfo);
          return active->original.get_mode (rig, vfo, mode, width);
        };
      caps.set_freq = [] (RIG * rig, vfo_t vfo, freq_t frequency)
        {
          active->frequency_targets.push_back (vfo);
          return active->original.set_freq (rig, vfo, frequency);
        };
      caps.get_vfo = [] (RIG * rig, vfo_t * vfo)
        {
          ++active->vfo_queries;
          return active->original.get_vfo (rig, vfo);
        };
      if (single_vfo_addressing) caps.get_vfo = nullptr;
      caps.set_ptt = [] (RIG * rig, vfo_t vfo, ptt_t ptt)
        {
          if (RIG_PTT_OFF != ptt)
            {
              pbwidth_t width;
              active->mode_at_ptt_result = active->original.get_mode (rig, active->tx_vfo,
                                                                     &active->mode_at_ptt, &width);
              ++active->ptt_on_count;
            }
          return active->original.set_ptt (rig, vfo, ptt);
        };
      registered = RIG_OK == rig_register (&caps);
    }

    ~RigFixture ()
    {
      // Hamlib workers must finish before their capabilities and callback context die.
      if (transceiver) transceiver->stop ();
      transceiver.reset ();
      if (registered) rig_unregister (model);
      active = nullptr;
    }

    void start ()
    {
      TransceiverFactory::ParameterPack parameters {};
      parameters.ptt_type = TransceiverFactory::PTT_method_CAT;
      parameters.split_mode = TransceiverFactory::split_mode_rig;
      transceiver.reset (new HamlibTransceiver {&logger, model, parameters});
      QObject::connect (transceiver.get (), &Transceiver::failure,
                        [&] (QString const& message) { failure = message; });
      QObject::connect (transceiver.get (), &Transceiver::update,
                        [&] (Transceiver::TransceiverState const& value, unsigned) { state = value; });
      transceiver->start (1);
    }

    void establish_split ()
    {
      state.online (true);
      state.frequency (14074000);
      state.mode (Transceiver::USB);
      transceiver->set (state, 2);
      state.tx_frequency (14075000);
      state.split (true);
      transceiver->set (state, 3);
      mode_targets.clear ();
      vfo_queries = 0;
    }

    static RigFixture * active;
    rig_model_t model = 0;
    vfo_t rx_vfo;
    vfo_t tx_vfo;
    vfo_t vfos;
    bool reject_tx_mode = false;
    bool registered = false;
    RIG * rig = nullptr;
    rig_caps original {};
    rig_caps caps {};
    Transceiver::logger_type logger;
    std::unique_ptr<HamlibTransceiver> transceiver;
    Transceiver::TransceiverState state;
    QString failure;
    std::vector<vfo_t> mode_targets;
    std::vector<vfo_t> mode_queries;
    std::vector<vfo_t> frequency_targets;
    int vfo_queries = 0;
    int ptt_on_count = 0;
    int mode_at_ptt_result = -RIG_EIO;
    rmode_t mode_at_ptt = RIG_MODE_NONE;
  };

  RigFixture * RigFixture::active = nullptr;
}

class TestHamlibTransceiver : public QObject
{
  Q_OBJECT

private:
  QTemporaryDir directory_;
  QString previous_directory_;

  Q_SLOT void initTestCase ()
  {
    QVERIFY (directory_.isValid ());
    previous_directory_ = QDir::currentPath ();
    QVERIFY (QDir::setCurrent (directory_.path ()));
    QStandardPaths::setTestModeEnabled (true);
    QCoreApplication::setOrganizationName ("WSJT-X tests");
    QCoreApplication::setApplicationName (QDir {directory_.path ()}.dirName ());
    rig_set_debug (RIG_DEBUG_NONE);
  }

  Q_SLOT void cleanupTestCase ()
  {
    QVERIFY (QDir::setCurrent (previous_directory_));
  }

  Q_SLOT void splitModeIsEstablishedBeforePtt_data ()
  {
    QTest::addColumn<int> ("receive_vfo");
    QTest::addColumn<bool> ("main_sub");
    QTest::newRow ("A-receives-B-transmits") << int {RIG_VFO_A} << false;
    QTest::newRow ("B-receives-A-transmits") << int {RIG_VFO_B} << false;
    QTest::newRow ("MAIN-receives-SUB-transmits") << int {RIG_VFO_MAIN} << true;
  }

  Q_SLOT void splitModeIsEstablishedBeforePtt ()
  {
    QFETCH (int, receive_vfo);
    QFETCH (bool, main_sub);
    RigFixture fixture {vfo_t (receive_vfo), main_sub};
    QVERIFY (fixture.registered);
    fixture.start ();
    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    fixture.establish_split ();
    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    QVERIFY (fixture.state.split ());

    fixture.state.mode (Transceiver::DIG_U);
    fixture.state.ptt (true);
    fixture.transceiver->set (fixture.state, 4);

    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    QCOMPARE (fixture.ptt_on_count, 1);
    QCOMPARE (fixture.mode_at_ptt_result, RIG_OK);
    QCOMPARE (fixture.mode_at_ptt, rmode_t {RIG_MODE_PKTUSB});
    QVERIFY (std::find (fixture.mode_targets.begin (), fixture.mode_targets.end (), fixture.tx_vfo)
             != fixture.mode_targets.end ());
  }

  Q_SLOT void receiveVfoChangeRefreshesRolesBeforePtt ()
  {
    RigFixture fixture {RIG_VFO_A};
    QVERIFY (fixture.registered);
    fixture.start ();
    fixture.establish_split ();
    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    QCOMPARE (rig_set_vfo (fixture.rig, RIG_VFO_B), RIG_OK);
    QCOMPARE (rig_set_split_vfo (fixture.rig, RIG_VFO_B, RIG_SPLIT_ON, RIG_VFO_A), RIG_OK);
    fixture.tx_vfo = RIG_VFO_A;

    fixture.state.mode (Transceiver::DIG_U);
    fixture.state.ptt (true);
    fixture.transceiver->set (fixture.state, 4);

    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    QCOMPARE (fixture.ptt_on_count, 1);
    QCOMPARE (fixture.mode_at_ptt_result, RIG_OK);
    QCOMPARE (fixture.mode_at_ptt, rmode_t {RIG_MODE_PKTUSB});
  }

  Q_SLOT void splitTransmitKeepsEstablishedRoles ()
  {
    RigFixture fixture {RIG_VFO_B};
    QVERIFY (fixture.registered);
    fixture.start ();
    fixture.establish_split ();
    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    fixture.state.ptt (true);
    fixture.transceiver->set (fixture.state, 4);
    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    // Radios may report the active transmit VFO while keyed.
    QCOMPARE (rig_set_vfo (fixture.rig, RIG_VFO_A), RIG_OK);
#if HAVE_HAMLIB_CACHING
    QCOMPARE (rig_set_cache_timeout_ms (fixture.rig, HAMLIB_CACHE_ALL, 0), RIG_OK);
#elif HAVE_HAMLIB_OLD_CACHING
    QCOMPARE (rig_set_cache_timeout_ms (fixture.rig, CACHE_ALL, 0), RIG_OK);
#endif
    fixture.vfo_queries = 0;
    fixture.mode_queries.clear ();

    fixture.state.frequency (fixture.state.frequency () + 10);
    fixture.state.mode (Transceiver::DIG_U);
    fixture.transceiver->set (fixture.state, 5);

    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    QCOMPARE (fixture.vfo_queries, 0);
    QVERIFY (!fixture.mode_queries.empty ());
    QVERIFY (std::find (fixture.mode_queries.begin (), fixture.mode_queries.end (), RIG_VFO_B)
             == fixture.mode_queries.end ());
  }

  Q_SLOT void singleVfoRigIgnoresReceiveChangesDuringSplitTransmit ()
  {
    RigFixture fixture {RIG_VFO_A, false, true};
    QVERIFY (fixture.registered);
    fixture.start ();
    fixture.establish_split ();
    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    fixture.state.ptt (true);
    fixture.transceiver->set (fixture.state, 4);
    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    QCOMPARE (fixture.ptt_on_count, 1);
    fixture.vfo_queries = 0;
    fixture.mode_queries.clear ();
    fixture.mode_targets.clear ();
    fixture.frequency_targets.clear ();

    fixture.state.frequency (fixture.state.frequency () + 10);
    fixture.state.mode (Transceiver::DIG_U);
    fixture.transceiver->set (fixture.state, 5);

    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    QCOMPARE (fixture.vfo_queries, 0);
    QVERIFY (fixture.mode_queries.empty ());
    QVERIFY (fixture.mode_targets.empty ());
    QVERIFY (fixture.frequency_targets.empty ());
  }

  Q_SLOT void transmitModeCorrectionRemainsBestEffort ()
  {
    RigFixture fixture {RIG_VFO_B};
    QVERIFY (fixture.registered);
    fixture.start ();
    fixture.establish_split ();
    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    fixture.reject_tx_mode = true;

    fixture.state.mode (Transceiver::DIG_U);
    fixture.state.ptt (true);
    fixture.transceiver->set (fixture.state, 4);

    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    QVERIFY (std::find (fixture.mode_targets.begin (), fixture.mode_targets.end (), fixture.tx_vfo)
             != fixture.mode_targets.end ());
    QCOMPARE (fixture.ptt_on_count, 1);
    QCOMPARE (fixture.mode_at_ptt_result, RIG_OK);
    QCOMPARE (fixture.mode_at_ptt, rmode_t {RIG_MODE_USB});
  }

  Q_SLOT void optionalAndCompatibleModesArePreserved_data ()
  {
    QTest::addColumn<rmode_t> ("backend_mode");
    QTest::addColumn<Transceiver::MODE> ("requested_mode");
    QTest::newRow ("mode-None") << rmode_t {RIG_MODE_USB} << Transceiver::UNK;
    QTest::newRow ("DATA1") << rmode_t {RIG_MODE_USBD1} << Transceiver::DIG_U;
    QTest::newRow ("DATA2") << rmode_t {RIG_MODE_USBD2} << Transceiver::DIG_U;
    QTest::newRow ("DATA3") << rmode_t {RIG_MODE_USBD3} << Transceiver::DIG_U;
  }

  Q_SLOT void optionalAndCompatibleModesArePreserved ()
  {
    QFETCH (rmode_t, backend_mode);
    QFETCH (Transceiver::MODE, requested_mode);
    RigFixture fixture {RIG_VFO_B};
    QVERIFY (fixture.registered);
    fixture.start ();
    fixture.establish_split ();
    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    QCOMPARE (rig_set_mode (fixture.rig, fixture.rx_vfo, backend_mode, 1800), RIG_OK);
    QCOMPARE (rig_set_mode (fixture.rig, fixture.tx_vfo, backend_mode, 1800), RIG_OK);
    fixture.mode_targets.clear ();

    fixture.state.frequency (fixture.state.frequency () + 10);
    fixture.state.mode (requested_mode);
    fixture.state.ptt (true);
    fixture.transceiver->set (fixture.state, 4);

    QVERIFY2 (fixture.failure.isEmpty (), qPrintable (fixture.failure));
    QCOMPARE (fixture.ptt_on_count, 1);
    QCOMPARE (fixture.mode_at_ptt_result, RIG_OK);
    QCOMPARE (fixture.mode_at_ptt, backend_mode);
    QVERIFY (fixture.mode_targets.empty ());
    rmode_t mode;
    pbwidth_t width;
    QCOMPARE (fixture.original.get_mode (fixture.rig, fixture.tx_vfo, &mode, &width), RIG_OK);
    QCOMPARE (width, pbwidth_t {1800});
  }
};

QTEST_GUILESS_MAIN (TestHamlibTransceiver)
#include "test_hamlib_transceiver.moc"
