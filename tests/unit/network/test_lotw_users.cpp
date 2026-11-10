#include <QtTest>

#include <atomic>
#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRunnable>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <ctime>
#endif

#include "Network/LotWUsers.hpp"

namespace
{
  bool write_csv (QString const& path, QByteArray const& data)
  {
    QFile file {path};
    return file.open (QIODevice::WriteOnly) && file.write (data) == data.size ();
  }

  QByteArray record (char const * call, QDate const& date)
  {
    return QByteArray {call} + ',' + date.toString (Qt::ISODate).toLatin1 () + ",12:00:00\n";
  }

  QDate utc_today ()
  {
    return QDateTime::currentDateTimeUtc ().date ();
  }

  class FailingReply final : public QNetworkReply
  {
  public:
    FailingReply (QNetworkAccessManager::Operation operation, QNetworkRequest const& request,
                  QObject * parent, bool automatic_failure)
      : QNetworkReply {parent}
    {
      setOperation (operation);
      setRequest (request);
      setUrl (request.url ());
      open (QIODevice::ReadOnly | QIODevice::Unbuffered);
      if (automatic_failure) QTimer::singleShot (0, this, &FailingReply::fail);
    }

    void fail ()
    {
      if (isFinished ()) return;
      setError (QNetworkReply::HostNotFoundError, "Simulated download failure");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
      Q_EMIT errorOccurred (QNetworkReply::HostNotFoundError);
#else
      Q_EMIT error (QNetworkReply::HostNotFoundError);
#endif
      setFinished (true);
      Q_EMIT finished ();
    }

    void abort () override
    {
      if (isFinished ()) return;
      setError (QNetworkReply::OperationCanceledError, "Request aborted");
      setFinished (true);
      Q_EMIT finished ();
    }

  private:
    qint64 readData (char *, qint64) override { return -1; }
  };

  class FailingNetworkAccessManager final : public QNetworkAccessManager
  {
  public:
    bool automatic_failure {true};
    QPointer<FailingReply> reply;

  protected:
    QNetworkReply * createRequest (Operation operation, QNetworkRequest const& request,
                                   QIODevice * outgoing_data) override
    {
      Q_UNUSED (outgoing_data);
      reply = new FailingReply {operation, request, this, automatic_failure};
      return reply;
    }
  };

  class BlockedThreadPool final
  {
    class Blocker final : public QRunnable
    {
    public:
      Blocker (QSemaphore& entered, QSemaphore& released) : entered_ {entered}, released_ {released} {}
      void run () override
      {
        entered_.release ();
        released_.acquire ();
      }
    private:
      QSemaphore& entered_;
      QSemaphore& released_;
    };

  public:
    BlockedThreadPool () : previous_max_ {QThreadPool::globalInstance ()->maxThreadCount ()}
    {
      auto pool = QThreadPool::globalInstance ();
      pool->setMaxThreadCount (1);
      pool->start (new Blocker {entered_, released_});
    }

    ~BlockedThreadPool ()
    {
      released_.release ();
      QThreadPool::globalInstance ()->waitForDone ();
      QThreadPool::globalInstance ()->setMaxThreadCount (previous_max_);
    }

    bool wait_until_blocked () { return entered_.tryAcquire (1, 5000); }

  private:
    int previous_max_;
    QSemaphore entered_;
    QSemaphore released_;
  };

#ifdef Q_OS_UNIX
  class ScopedTimeZone final
  {
  public:
    explicit ScopedTimeZone (QByteArray const& value)
      : previous_ {qgetenv ("TZ")}
      , was_set_ {qEnvironmentVariableIsSet ("TZ")}
    {
      qputenv ("TZ", value);
      tzset ();
    }

    ~ScopedTimeZone ()
    {
      if (was_set_) qputenv ("TZ", previous_);
      else qunsetenv ("TZ");
      tzset ();
    }

  private:
    QByteArray previous_;
    bool was_set_;
  };
#endif
}

class TestLotWUsers final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void upload_age_requires_valid_past_activity ()
  {
    for (int attempt = 0; attempt < 3; ++attempt)
      {
        QTemporaryDir directory;
        QVERIFY (directory.isValid ());
        auto const path = directory.filePath ("users.csv");
        auto const today = utc_today ();
        auto const date = today.toString (Qt::ISODate).toLatin1 ();
        QVERIFY (write_csv (path, record ("RECENT", today) + record ("BOUNDARY", today.addDays (-30))
                           + record ("STALE", today.addDays (-31)) + record ("FUTURE", today.addDays (1))
                           + record ("DUPLICATE", today.addDays (-60)) + record ("DUPLICATE", today)
                           + "TWO_FIELD," + date + '\n' + "PADDED, " + date + " ,12:00:00\n"
                           + "DUPLICATE,not-a-date,12:00:00\nINVALID,2025-02-30,12:00:00\nMALFORMED\n"
                           + record ("", today)));
        QNetworkAccessManager network;
        LotWUsers users {&network};
        QSignalSpy finished {&users, &LotWUsers::load_finished};
        users.set_age_constraint (30);
        users.set_local_file_path (path);
        users.load ({}, false);
        QTRY_COMPARE (finished.size (), 1);
        auto const recent = users.user ("RECENT");
        auto const boundary = users.user ("BOUNDARY");
        auto const stale = users.user ("STALE");
        auto const future = users.user ("FUTURE");
        auto const duplicate = users.user ("DUPLICATE");
        auto const two_field = users.user ("TWO_FIELD");
        auto const padded = users.user ("PADDED");
        auto const invalid = users.user ("INVALID");
        auto const malformed = users.user ("MALFORMED");
        auto const empty = users.user ("");
        auto const absent = users.user ("ABSENT");
        users.set_age_constraint (31);
        auto const stale_at_31 = users.user ("STALE");
        users.set_age_constraint (0);
        auto const recent_at_0 = users.user ("RECENT");
        auto const boundary_at_0 = users.user ("BOUNDARY");
        if (utc_today () != today) continue;
        QVERIFY (recent);
        QVERIFY (boundary);
        QVERIFY (!stale);
        QVERIFY (!future);
        QVERIFY (duplicate);
        QVERIFY (two_field);
        QVERIFY (padded);
        QVERIFY (!invalid);
        QVERIFY (!malformed);
        QVERIFY (!empty);
        QVERIFY (!absent);
        QVERIFY (stale_at_31);
        QVERIFY (recent_at_0);
        QVERIFY (!boundary_at_0);
        return;
      }
    QFAIL ("UTC date changed during each attempt");
  }

  void utc_upload_dates_do_not_use_local_day ()
  {
#ifdef Q_OS_UNIX
    ScopedTimeZone time_zone {"UTC+24"};
    auto const today = utc_today ();
    if (QDate::currentDate () != today.addDays (-1)) QSKIP ("Local time zone override is unavailable");
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const path = directory.filePath ("users.csv");
    QVERIFY (write_csv (path, record ("TODAY_UTC", today)));
    QNetworkAccessManager network;
    LotWUsers users {&network};
    QSignalSpy finished {&users, &LotWUsers::load_finished};
    users.set_age_constraint (0);
    users.set_local_file_path (path);
    users.load ({}, false);
    QTRY_COMPARE (finished.size (), 1);
    auto const uploaded_today = users.user ("TODAY_UTC");
    if (utc_today () != today) QSKIP ("UTC date changed during the test");
    QVERIFY (uploaded_today);
#else
    QSKIP ("Local time zone override requires a Unix platform");
#endif
  }

  void failed_fetch_preserves_in_flight_cache_load ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const path = directory.filePath ("users.csv");
    QVERIFY (write_csv (path, record ("CACHED", utc_today ())));
    FailingNetworkAccessManager network;
    LotWUsers users {&network};
    QSignalSpy errors {&users, &LotWUsers::LotW_users_error};
    QSignalSpy finished {&users, &LotWUsers::load_finished};
    users.set_local_file_path (path);
    users.load ({}, false);
    users.load ("https://example.invalid/lotw-user-activity.csv", true, true);
    QTRY_COMPARE (errors.size (), 1);
    QTRY_COMPARE (finished.size (), 1);
    QVERIFY (users.user ("CACHED"));
  }

  void failed_fetch_finishes_after_missing_cache_data ()
  {
    QTest::addColumn<bool> ("downloadFirst");
    QTest::newRow ("download finishes first") << true;
    QTest::newRow ("cache finishes first") << false;
  }

  void failed_fetch_finishes_after_missing_cache ()
  {
    QFETCH (bool, downloadFirst);
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const path = directory.filePath ("users.csv");
    QVERIFY (write_csv (path, record ("PUBLISHED", utc_today ())));
    FailingNetworkAccessManager network;
    network.automatic_failure = false;
    LotWUsers users {&network};
    QSignalSpy errors {&users, &LotWUsers::LotW_users_error};
    QSignalSpy finished {&users, &LotWUsers::load_finished};
    users.set_local_file_path (path);
    users.load ({}, false);
    QTRY_COMPARE (finished.size (), 1);
    QVERIFY (users.user ("PUBLISHED"));
    finished.clear ();
    {
      BlockedThreadPool blocked;
      QVERIFY (blocked.wait_until_blocked ());
      users.load ({}, false);
      QVERIFY (QFile::remove (path));
      users.load ("https://example.invalid/lotw-user-activity.csv", true, true);
      QVERIFY (network.reply);
      if (downloadFirst)
        {
          network.reply->fail ();
          QTRY_COMPARE (errors.size (), 1);
          QCOMPARE (finished.size (), 0);
        }
    }
    if (!downloadFirst)
      {
        QCoreApplication::processEvents ();
        QCOMPARE (finished.size (), 0);
        network.reply->fail ();
      }
    QTRY_COMPARE (errors.size (), 1);
    QTRY_COMPARE (finished.size (), 1);
    QCoreApplication::processEvents ();
    QCOMPARE (finished.size (), 1);
    QVERIFY (users.user ("PUBLISHED"));
  }

  void completion_publishes_once_on_owner_thread ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const path = directory.filePath ("users.csv");
    QVERIFY (write_csv (path, record ("FIRST", QDate::currentDate ())
                       + record ("LAST", QDate::currentDate ())));
    QNetworkAccessManager network;
    LotWUsers users {&network};
    std::atomic<bool> wrong_thread {false};
    bool complete_snapshot {false};
    auto * owner_thread = users.thread ();
    connect (&users, &LotWUsers::load_finished, &users, [&] {
      if (QThread::currentThread () != owner_thread) wrong_thread.store (true);
      else complete_snapshot = users.user ("FIRST") && users.user ("LAST");
    }, Qt::DirectConnection);
    int completion_count {0};
    connect (&users, &LotWUsers::load_finished, &users, [&] {
      ++completion_count;
    }, Qt::QueuedConnection);
    users.set_local_file_path (path);
    users.load ({}, false);
    QTRY_VERIFY (completion_count > 0);
    QVERIFY (!wrong_thread.load ());
    QVERIFY (complete_snapshot);
    QCOMPARE (completion_count, 1);
    QVERIFY (users.user ("FIRST"));
    QVERIFY (!users.user ("ABSENT"));
    QCoreApplication::processEvents ();
    QCOMPARE (completion_count, 1);
  }

  void reload_replaces_snapshot_and_coalesces_pending_requests ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const initial = directory.filePath ("initial.csv");
    auto const obsolete = directory.filePath ("obsolete.csv");
    auto const latest = directory.filePath ("latest.csv");
    QVERIFY (write_csv (initial, record ("INITIAL", QDate::currentDate ())));
    QVERIFY (write_csv (obsolete, record ("OBSOLETE", QDate::currentDate ())));
    QVERIFY (write_csv (latest, record ("LATEST", QDate::currentDate ())));
    QNetworkAccessManager network;
    LotWUsers users {&network};
    users.set_local_file_path (initial);
    users.load ({}, false);
    QTRY_VERIFY (users.user ("INITIAL"));
    QCoreApplication::processEvents ();
    int completion_count {0};
    connect (&users, &LotWUsers::load_finished, &users, [&] {
      ++completion_count;
    }, Qt::QueuedConnection);

    users.set_local_file_path (obsolete);
    users.load ({}, false);
    users.set_local_file_path (latest);
    users.load ({}, false);
    QVERIFY (users.user ("INITIAL"));
    QTRY_VERIFY (completion_count > 0);
    QVERIFY (users.user ("LATEST"));
    QVERIFY (!users.user ("INITIAL"));
    QVERIFY (!users.user ("OBSOLETE"));
    QCoreApplication::processEvents ();
    QCOMPARE (completion_count, 1);
  }

  void failed_reload_retains_previous_snapshot ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const path = directory.filePath ("users.csv");
    QVERIFY (write_csv (path, record ("RETAINED", QDate::currentDate ())));
    QNetworkAccessManager network;
    LotWUsers users {&network};
    users.set_local_file_path (path);
    users.load ({}, false);
    QTRY_VERIFY (users.user ("RETAINED"));
    QCoreApplication::processEvents ();
    int error_count {0};
    connect (&users, &LotWUsers::LotW_users_error, &users, [&] {
      ++error_count;
    }, Qt::QueuedConnection);
    int completion_count {0};
    connect (&users, &LotWUsers::load_finished, &users, [&] {
      ++completion_count;
    }, Qt::QueuedConnection);
    users.set_local_file_path (directory.path ());
    users.load ({}, false);
    QTRY_COMPARE (error_count, 1);
    QTRY_COMPARE (completion_count, 1);
    QVERIFY (users.user ("RETAINED"));
  }
};

QTEST_GUILESS_MAIN (TestLotWUsers)

#include "test_lotw_users.moc"
