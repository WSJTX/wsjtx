#include <QtTest>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "JttyReplay.hpp"

namespace
{
constexpr int step = 3456;
}

class TestJttyReplayOwnership : public QObject
{
  Q_OBJECT

  void verifyReplayBoundWithLiveInput ()
  {
    std::atomic<int> liveFrames {3 * step};
    std::mutex mutex;
    std::condition_variable changed;
    bool ready = false, released = false, done = false;
    bool producerReleased = false, producerReady = false, appendFinished = false;
    std::thread writer {[&] {
      {
        std::unique_lock<std::mutex> lock {mutex};
        ready = true;
        changed.notify_all ();
        producerReleased = changed.wait_for (lock, std::chrono::seconds {5},
                                             [&] { return released; });
      }
      if (producerReleased)
        liveFrames.store (4 * step);
      {
        std::lock_guard<std::mutex> lock {mutex};
        done = true;
        changed.notify_all ();
      }
    }};
    {
      std::unique_lock<std::mutex> lock {mutex};
      producerReady = changed.wait_for (lock, std::chrono::seconds {5}, [&] { return ready; });
    }
    QVector<int> visited;
    int const replayFrames = liveFrames.load ();
    replayJttyFrames (replayFrames, [&] (int k) {
      visited.append (k);
      if (k == step)
        {
          std::unique_lock<std::mutex> lock {mutex};
          released = true;
          changed.notify_all ();
          appendFinished = changed.wait_for (lock, std::chrono::seconds {5},
                                             [&] { return done; });
        }
      return false;
    });
    writer.join ();
    QVERIFY (producerReady);
    QVERIFY (producerReleased);
    QCOMPARE (liveFrames.load (), 4 * step);
    QVERIFY (appendFinished);
    // Manual replay is a finite request over its starting receive range;
    // audio arriving during a decode belongs to subsequent processing.
    QCOMPARE (visited, QVector<int> ({step, 2 * step}));
  }

private Q_SLOTS:
  void fixed_range_data ()
  {
    QTest::addColumn<int> ("frames");
    QTest::addColumn<QVector<int>> ("expected");
    QTest::newRow ("empty") << 0 << QVector<int> {};
    QTest::newRow ("short") << step - 1 << QVector<int> {};
    QTest::newRow ("boundary") << step << QVector<int> {};
    QTest::newRow ("past-boundary") << step + 1 << QVector<int> {step};
    QTest::newRow ("multiple") << 3 * step << QVector<int> {step, 2 * step};
  }

  void fixed_range ()
  {
    QFETCH (int, frames);
    QFETCH (QVector<int>, expected);
    QVector<int> visited;
    replayJttyFrames (frames, [&] (int k) { visited.append (k); return false; });
    QCOMPARE (visited, expected);
  }

  void picked_stop_processed_once ()
  {
    int frames = 8 * step;
    int stop = 5000;
    QVector<int> visited;
    replayJttyFrames (frames, [&] (int k) {
      visited.append (k);
      return k >= stop;
    });
    QCOMPARE (visited, QVector<int> ({step, 2 * step}));
  }

  void replay_is_bounded_at_start () { verifyReplayBoundWithLiveInput (); }
};

QTEST_GUILESS_MAIN (TestJttyReplayOwnership)
#include "test_jtty_replay_ownership.moc"
