#include "Logger.hpp"
#include "WSJTXLogging.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <boost/log/core.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <set>
#include <thread>
#include <vector>

namespace
{
  QByteArray read_file (QString const& path)
  {
    QFile file {path};
    if (!file.open (QIODevice::ReadOnly)) return {};
    return file.readAll ();
  }

  void emit_channel (char const * channel, boost::log::trivial::severity_level severity,
                     char const * message)
  {
    boost::log::sources::severity_channel_logger_mt<boost::log::trivial::severity_level>
      logger {boost::log::keywords::channel = channel};
    BOOST_LOG_SEV (logger, severity) << message;
  }
}

class LoggingLifecycle final : public QObject
{
  Q_OBJECT

private:
  QString config_path_;
  QString data_path_;

  void write_config (QByteArray const& config)
  {
    QFile file {config_path_};
    QVERIFY (file.open (QIODevice::WriteOnly));
    QCOMPARE (file.write (config), qint64 (config.size ()));
  }

private slots:
  void initTestCase ()
  {
    auto config = QStandardPaths::writableLocation (QStandardPaths::ConfigLocation);
    auto data = QStandardPaths::writableLocation (QStandardPaths::AppLocalDataLocation);
    auto root = QString::fromUtf8 (qgetenv ("WSJT_LOG_TEST_ROOT"));
    if (!config.startsWith (root + '/') || !data.startsWith (root + '/'))
      QSKIP ("Platform does not isolate QStandardPaths through XDG environment variables");
    config_path_ = QDir {config}.filePath ("wsjtx_log_config.ini");
    data_path_ = data;
    QVERIFY (QDir {}.mkpath (config));
  }

  void init ()
  {
    QFile::remove (config_path_);
    QDir {data_path_}.removeRecursively ();
    QVERIFY (QDir {}.mkpath (data_path_));
    auto core = boost::log::core::get ();
    core->remove_all_sinks ();
    core->reset_filter ();
    core->set_logging_enabled (true);
  }

  void cleanup ()
  {
    qInstallMessageHandler (nullptr);
    boost::log::core::get ()->remove_all_sinks ();
  }

  void default_filter_format_append_and_flush ()
  {
    {
      WSJTXLogging logging;
      LOG_WARN ("excluded-system-warning");
      LOG_ERROR ("included-system-error");
      LOG_DATA_INFO ("excluded-data-info");
      LOG_DATA_ERROR ("included-data-error");
      emit_channel ("RIGCTRL", boost::log::trivial::info, "excluded-rig-info");
      emit_channel ("RIGCTRL", boost::log::trivial::warning, "included-rig-warning");
      emit_channel ("DECODER", boost::log::trivial::warning, "included-decoder-warning");
      emit_channel ("OTHER", boost::log::trivial::error, "excluded-other-error");
      emit_channel ("OTHER", boost::log::trivial::fatal, "included-other-fatal");
      qCritical ("included-qt-critical");
    }
    auto path = QDir {data_path_}.filePath ("wsjtx_syslog.log");
    auto first = read_file (path);
    QVERIFY (!first.contains ("excluded-"));
    for (auto message : {"included-system-error", "included-data-error", "included-rig-warning",
                         "included-decoder-warning", "included-other-fatal", "included-qt-critical"})
      QVERIFY2 (first.contains (message), message);
    QRegularExpression format {
      R"(^\[SYSLOG\]\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{6}\]\[\d+:\d{2}:\d{2}\.\d{6}\]\[error\] included-system-error)"};
    QVERIFY2 (format.match (QString::fromUtf8 (first)).hasMatch (), first.constData ());
    {
      WSJTXLogging logging;
      LOG_ERROR ("appended-session");
    }
    auto second = read_file (path);
    QVERIFY (second.startsWith (first));
    QCOMPARE (second.count ("appended-session"), 1);
    QVERIFY (QDir {QDir {data_path_}.filePath ("logs")}.entryList (QDir::Files).isEmpty ());
  }

  void asynchronous_writers_flush_exactly_once ()
  {
    write_config (R"([Sinks.SYSLOG]
Destination="TextFile"
Asynchronous="true"
AutoFlush="false"
FileName="${AppLocalDataLocation}/threaded.log"
EnableFinalRotation="false"
Format="%Message%"
Filter="%Severity% >= info"
)");
    {
      WSJTXLogging logging;
      std::vector<std::thread> writers;
      for (int thread = 0; thread < 4; ++thread)
        writers.emplace_back ([thread] {
          for (int record = 0; record < 250; ++record)
            LOG_INFO ("record:" << thread << ':' << record);
        });
      for (auto& writer : writers) writer.join ();
    }
    auto output = read_file (QDir {data_path_}.filePath ("threaded.log"));
    std::set<QByteArray> expected;
    for (int thread = 0; thread < 4; ++thread)
      for (int record = 0; record < 250; ++record)
        expected.insert ("record:" + QByteArray::number (thread) + ':' + QByteArray::number (record));
    int count = 0;
    for (auto const& line : output.split ('\n'))
      if (line.startsWith ("record:"))
        {
          QCOMPARE (expected.erase (line), std::size_t {1});
          ++count;
        }
    QCOMPARE (count, 1000);
    QVERIFY (expected.empty ());
    QCOMPARE (output.count ("Log Finish"), 1);
  }

  void custom_rotation_and_retention ()
  {
    write_config (R"([Sinks.SYSLOG]
Destination="TextFile"
Asynchronous="true"
AutoFlush="false"
FileName="${AppLocalDataLocation}/active.log"
Target="${AppLocalDataLocation}/rotated"
TargetFileName="${AppLocalDataLocation}/rotated/part_%5N.log"
RotationSize="256"
MaxFiles="2"
EnableFinalRotation="true"
Format="%Message%"
)");
    {
      WSJTXLogging logging;
      for (int record = 0; record < 100; ++record)
        LOG_ERROR ("rotation-record:" << record << " padding-to-force-file-rotation");
    }
    auto directory = QDir {QDir {data_path_}.filePath ("rotated")};
    auto files = directory.entryList (QStringList {"part_*.log"}, QDir::Files);
    QCOMPARE (files.size (), 2);
    QByteArray output;
    for (auto const& file : files) output += read_file (directory.filePath (file));
    QVERIFY (output.contains ("rotation-record:99 "));
    QVERIFY (!output.contains ("rotation-record:0 "));
    QVERIFY (output.contains ("Log Finish"));
  }

  void malformed_configuration_falls_back ()
  {
    write_config ("[Sinks.SYSLOG]\nDestination=\"unknown-test-destination\"\n");
    {
      WSJTXLogging logging;
      LOG_ERROR ("fallback-is-operational");
    }
    auto output = read_file (QDir {data_path_}.filePath ("wsjtx_syslog.log"));
    QVERIFY (output.contains ("Reading logging configuration file:"));
    QCOMPARE (output.count ("fallback-is-operational"), 1);
  }
};

int main (int argc, char ** argv)
{
  QTemporaryDir directory;
  if (!directory.isValid ()) return 1;
  auto root = directory.path ();
  auto data = root + QString::fromUtf8 ("/données ログ");
  qputenv ("WSJT_LOG_TEST_ROOT", root.toUtf8 ());
  qputenv ("XDG_CONFIG_HOME", (root + "/config").toUtf8 ());
  qputenv ("XDG_DATA_HOME", data.toUtf8 ());
  QCoreApplication app {argc, argv};
  app.setApplicationName ("WSJTXLoggingTest");
  LoggingLifecycle test;
  auto result = QTest::qExec (&test, argc, argv);
  if (result)
    {
      directory.setAutoRemove (false);
      qWarning () << "Logging test artifacts:" << directory.path ();
    }
  return result;
}

#include "test_logging_lifecycle.moc"
