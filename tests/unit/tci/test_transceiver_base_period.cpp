#include "Transceiver/TransceiverBase.hpp"

#include <stdexcept>

#include <boost/log/keywords/channel.hpp>

#include <QSignalSpy>
#include <QtTest>

namespace
{
  class PeriodRecordingTransceiver final
    : public TransceiverBase
  {
  public:
    explicit PeriodRecordingTransceiver (logger_type * logger)
      : TransceiverBase {logger, nullptr}
    {
    }

    QVector<double> applied_periods;
    QStringList events;
    double backend_period {15.0};
    bool fail_next_period {false};

  protected:
    int do_start () override {return 0;}
    void do_stop () override {backend_period = 15.0;}
    void do_frequency (Frequency, MODE, bool) override {}
    void do_tx_frequency (Frequency, MODE, bool) override {}
    void do_mode (MODE) override {}
    void do_ptt (bool) override {}

    void do_period (double period) override
    {
      events.append (QStringLiteral ("period"));
      if (fail_next_period)
        {
          fail_next_period = false;
          throw std::runtime_error {"period rejected"};
        }
      backend_period = period;
      applied_periods.append (period);
    }

    void do_modulator_start (TxEvidence::TxRequest const&) override
    {
      events.append (QStringLiteral ("modulator"));
    }
  };

  Transceiver::TransceiverState online_state ()
  {
    Transceiver::TransceiverState state;
    state.online (true);
    return state;
  }
}

class TestTransceiverBasePeriod final
  : public QObject
{
  Q_OBJECT

private slots:
  void initial_period_is_applied_even_when_it_matches_state_default ()
  {
    Transceiver::logger_type logger {
      boost::log::keywords::channel = "TEST"};
    PeriodRecordingTransceiver rig {&logger};
    QVector<double> expected {120.0};

    rig.set (online_state (), 1);

    QCOMPARE (rig.applied_periods, expected);
    QCOMPARE (rig.backend_period, 120.0);
  }

  void unchanged_period_is_not_reapplied ()
  {
    Transceiver::logger_type logger {
      boost::log::keywords::channel = "TEST"};
    PeriodRecordingTransceiver rig {&logger};
    auto state = online_state ();
    QVector<double> expected {120.0};

    rig.set (state, 1);
    rig.set (state, 2);

    QCOMPARE (rig.applied_periods, expected);
  }

  void changed_period_is_applied_once ()
  {
    Transceiver::logger_type logger {
      boost::log::keywords::channel = "TEST"};
    PeriodRecordingTransceiver rig {&logger};
    auto state = online_state ();
    QVector<double> expected {120.0, 15.0};

    rig.set (state, 1);
    state.period (15.0);
    rig.set (state, 2);
    rig.set (state, 3);

    QCOMPARE (rig.applied_periods, expected);
    QCOMPARE (rig.backend_period, 15.0);
  }

  void period_is_reapplied_after_restart ()
  {
    Transceiver::logger_type logger {
      boost::log::keywords::channel = "TEST"};
    PeriodRecordingTransceiver rig {&logger};
    auto state = online_state ();
    QVector<double> expected {120.0, 120.0};

    rig.set (state, 1);
    rig.stop ();
    rig.start (2);
    state = online_state ();
    rig.set (state, 3);

    QCOMPARE (rig.applied_periods, expected);
    QCOMPARE (rig.backend_period, 120.0);
  }

  void period_is_applied_before_modulator_start ()
  {
    Transceiver::logger_type logger {
      boost::log::keywords::channel = "TEST"};
    PeriodRecordingTransceiver rig {&logger};
    auto state = online_state ();
    QStringList expected {QStringLiteral ("period"),
                          QStringLiteral ("modulator")};
    state.tx_audio (true);

    rig.set (state, 1);

    QCOMPARE (rig.events, expected);
  }

  void failed_period_is_retried ()
  {
    Transceiver::logger_type logger {
      boost::log::keywords::channel = "TEST"};
    PeriodRecordingTransceiver rig {&logger};
    QSignalSpy failures {&rig, &Transceiver::failure};
    auto state = online_state ();
    QVector<double> expected {120.0};
    rig.fail_next_period = true;

    rig.set (state, 1);
    rig.set (state, 2);

    QCOMPARE (failures.count (), 1);
    QCOMPARE (rig.applied_periods, expected);
    QCOMPARE (rig.backend_period, 120.0);
  }
};

QTEST_GUILESS_MAIN (TestTransceiverBasePeriod)

#include "test_transceiver_base_period.moc"
