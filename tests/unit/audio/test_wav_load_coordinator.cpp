#include <QtTest>

#include <atomic>

#include <QCoreApplication>
#include <QEventLoop>
#include <QSemaphore>
#include <QThread>

#include "Audio/WavLoadCoordinator.hpp"
#include "DecDataMutex.hpp"

class TestWavLoadCoordinator : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void init ()
  {
    set_dec_data_input_blocked (false);
  }

  Q_SLOT void cleanup ()
  {
    set_dec_data_input_blocked (false);
  }

  Q_SLOT void delivers_single_flight_lifecycle_data ()
  {
    QTest::addColumn<bool> ("valid");
    QTest::newRow ("valid-result") << true;
    QTest::newRow ("invalid-result") << false;
  }

  Q_SLOT void delivers_single_flight_lifecycle ()
  {
    QFETCH (bool, valid);

    WavLoadCoordinator coordinator;
    QSemaphore entered;
    QSemaphore release;
    std::atomic_bool rejected_work_ran {false};
    QStringList lifecycle;
    int result_count {0};
    QThread * delivery_thread {nullptr};
    bool loading_during_result {false};
    bool gate_during_result {false};
    bool controls_enabled {true};
    bool gate_at_idle {true};
    bool loading_at_idle {true};

    connect (&coordinator, &WavLoadCoordinator::loadingChanged,
             this, [&] (bool loading) {
               lifecycle.append (loading ? "loading" : "idle");
               controls_enabled = !loading;
               if (!loading)
                 {
                   gate_at_idle = dec_data_input_blocked ();
                   loading_at_idle = coordinator.isLoading ();
                 }
             });
    connect (&coordinator, &WavLoadCoordinator::resultReady, this, [&] {
      lifecycle.append ("result");
      ++result_count;
      delivery_thread = QThread::currentThread ();
      loading_during_result = coordinator.isLoading ();
      gate_during_result = dec_data_input_blocked ();
    });

    QVERIFY (coordinator.start ([&] {
      entered.release ();
      release.tryAcquire (1, 5000);
      auto result = std::make_shared<Radio::WavInputResult> ();
      result->valid = valid;
      return result;
    }));
    QVERIFY2 (entered.tryAcquire (1, 5000), "load A did not enter its worker");
    QVERIFY (!coordinator.start ([&] {
      rejected_work_ran.store (true, std::memory_order_release);
      return std::make_shared<Radio::WavInputResult> ();
    }));

    QCoreApplication::processEvents (QEventLoop::AllEvents, 100);
    QCOMPARE (result_count, 0);
    QVERIFY (!rejected_work_ran.load (std::memory_order_acquire));
    QVERIFY (coordinator.isLoading ());
    QVERIFY (dec_data_input_blocked ());
    QVERIFY (!controls_enabled);

    release.release ();
    coordinator.waitForFinished ();
    QTRY_VERIFY_WITH_TIMEOUT (!coordinator.isLoading (), 5000);

    QCOMPARE (lifecycle, QStringList ({"loading", "result", "idle"}));
    QCOMPARE (result_count, 1);
    QCOMPARE (delivery_thread, coordinator.thread ());
    QVERIFY (loading_during_result);
    QVERIFY (gate_during_result);
    QVERIFY (coordinator.result ());
    QCOMPARE (coordinator.result ()->valid, valid);
    QVERIFY (controls_enabled);
    QVERIFY (!gate_at_idle);
    QVERIFY (!loading_at_idle);
    QVERIFY (!dec_data_input_blocked ());

    QVERIFY (coordinator.start ([] {
      return std::make_shared<Radio::WavInputResult> ();
    }));
    coordinator.waitForFinished ();
    QTRY_VERIFY_WITH_TIMEOUT (!coordinator.isLoading (), 5000);
    QCOMPARE (result_count, 2);
  }
};

QTEST_GUILESS_MAIN (TestWavLoadCoordinator)

#include "test_wav_load_coordinator.moc"
