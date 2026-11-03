#include "Logger.hpp"
#include "WSJTXLogging.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

#include <boost/log/core.hpp>
#include <set>
#include <sstream>
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

  QString stream_settings (QString path)
  {
    path.replace ('\\', "\\\\").replace ('\"', "\\\"");
    return QStringLiteral ("[Sinks.SYSLOG]\nDestination=\"TextFile\"\n"
      "Asynchronous=\"true\"\nAutoFlush=\"false\"\nEnableFinalRotation=\"false\"\n"
      "Format=\"%Message%\"\nFileName=\"%1\"\n").arg (path);
  }
}

class LoggingPlatform final : public QObject
{
  Q_OBJECT

private:
  QtMessageHandler previous_handler_ {};

private slots:
  void init ()
  {
    previous_handler_ = qInstallMessageHandler (nullptr);
    auto core = boost::log::core::get ();
    core->remove_all_sinks ();
    core->reset_filter ();
    core->set_logging_enabled (true);
  }

  void cleanup ()
  {
    boost::log::core::get ()->flush ();
    boost::log::core::get ()->remove_all_sinks ();
    qInstallMessageHandler (previous_handler_);
  }

  void native_configuration_supports_paths_and_threads_data ()
  {
    QTest::addColumn<QString> ("suffix");
    QTest::newRow ("ascii") << QStringLiteral ("/logs");
    QTest::newRow ("unicode") << QString::fromUtf8 ("/données ログ");
  }

  void native_configuration_supports_paths_and_threads ()
  {
    QFETCH (QString, suffix);
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto subdirectory = directory.path () + suffix;
    QVERIFY (QDir {}.mkpath (subdirectory));
    auto path = subdirectory + "/threaded.log";
    auto config = stream_settings (path);
#ifdef Q_OS_WIN
    std::wistringstream stream {config.toStdWString ()};
#else
    std::istringstream stream {config.toUtf8 ().toStdString ()};
#endif
    Logger::init_from_config (stream);
    std::vector<std::thread> writers;
    for (int thread = 0; thread < 4; ++thread)
      writers.emplace_back ([thread] {
        for (int record = 0; record < 250; ++record)
          LOG_INFO ("record:" << thread << ':' << record);
      });
    for (auto& writer : writers) writer.join ();
    LOG_INFO ("stream-final");
    boost::log::core::get ()->flush ();
    boost::log::core::get ()->remove_all_sinks ();
    auto output = read_file (path);
    std::set<QByteArray> expected;
    for (int thread = 0; thread < 4; ++thread)
      for (int record = 0; record < 250; ++record)
        expected.insert ("record:" + QByteArray::number (thread) + ':' + QByteArray::number (record));
    int count = 0;
    for (auto const& line : output.split ('\n'))
      {
        auto record = line.trimmed ();
        if (record.startsWith ("record:"))
          {
            QCOMPARE (expected.erase (record), std::size_t {1});
            ++count;
          }
      }
#if defined(Q_OS_WIN) && defined(__GLIBCXX__)
    if (suffix != "/logs" && count == 0)
      QEXPECT_FAIL ("unicode", "Boost.Filesystem uses narrow file streams with MinGW; also fails before the factory change", Abort);
#endif
    QCOMPARE (count, 1000);
    QVERIFY (expected.empty ());
    QCOMPARE (output.count ("stream-final"), 1);
  }

  void wide_configuration_parser_remains_available ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto path = directory.filePath ("wide.log");
    std::wistringstream stream {stream_settings (path).toStdWString ()};
    Logger::init_from_config (stream);
    LOG_ERROR ("wide-parser-record");
    boost::log::core::get ()->flush ();
    boost::log::core::get ()->remove_all_sinks ();
    QCOMPARE (read_file (path).count ("wide-parser-record"), 1);
  }

  void default_configuration_uses_application_directory_data ()
  {
    QTest::addColumn<QString> ("suffix");
    QTest::newRow ("ascii") << QStringLiteral ("logs");
    QTest::newRow ("unicode") << QString::fromUtf8 ("ログ");
  }

  void default_configuration_uses_application_directory ()
  {
    QFETCH (QString, suffix);
#if defined(Q_OS_WIN) && defined(__GLIBCXX__)
    if (suffix != "logs")
      QSKIP ("Original Boost.Filesystem MinGW backend cannot open paths outside the active code page");
#endif
    QStandardPaths::setTestModeEnabled (true);
    if (!QStandardPaths::locate (QStandardPaths::ConfigLocation, "wsjtx_log_config.ini").isEmpty ())
      QSKIP ("An existing test-mode logging configuration must not be used or modified");
    auto name = "WSJTXLoggingTest-" + suffix + '-'
      + QUuid::createUuid ().toString (QUuid::WithoutBraces);
    QCoreApplication::setApplicationName (name);
    auto path = QStandardPaths::writableLocation (QStandardPaths::AppLocalDataLocation);
    QVERIFY (path.endsWith ('/' + name));
    QVERIFY (!QFile::exists (path));
    QVERIFY (QDir {}.mkpath (path));
    {
      WSJTXLogging logging;
      LOG_WARN ("platform-default-excluded");
      LOG_ERROR ("platform-default-included");
      qCritical ("platform-qt-routing");
    }
    auto output = read_file (QDir {path}.filePath ("wsjtx_syslog.log"));
    QVERIFY (QDir {path}.removeRecursively ());
    QCOMPARE (output.count ("platform-default-included"), 1);
    QCOMPARE (output.count ("platform-qt-routing"), 1);
    QVERIFY (!output.contains ("platform-default-excluded"));
    QVERIFY (output.contains ("[SYSLOG]["));
    QVERIFY (output.contains ("][error] platform-default-included"));
  }
};

QTEST_GUILESS_MAIN (LoggingPlatform)
#include "test_logging_platform.moc"
