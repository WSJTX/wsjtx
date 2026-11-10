#include "WSJTXLogging.hpp"

#include <string>
#include <exception>
#include <iostream>
#include <sstream>

#include <boost/version.hpp>
#include <boost/log/core.hpp>
#include <boost/log/utility/exception_handler.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/from_settings.hpp>
#include <boost/log/utility/setup/settings.hpp>
#include <boost/container/flat_map.hpp>

#include <QtGlobal>
#ifdef Q_OS_WIN
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/expressions/formatters/date_time.hpp>
#include <boost/log/expressions/predicates/channel_severity_filter.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/greg_day.hpp>
#endif
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QString>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QMessageLogContext>
#include <QApplication>

#include "Logger.hpp"
#include "qt_helpers.hpp"

namespace logging = boost::log;
namespace trivial = logging::trivial;
namespace container = boost::container;
#ifdef Q_OS_WIN
namespace keywords = logging::keywords;
namespace expr = logging::expressions;
namespace sinks = logging::sinks;
namespace posix_time = boost::posix_time;
namespace gregorian = boost::gregorian;

BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", trivial::severity_level)
BOOST_LOG_ATTRIBUTE_KEYWORD(channel, "Channel", std::string)
#endif

namespace
{
  // Top level exception handler that gets exceptions from filters and
  // formatters.
  struct exception_handler
  {
    typedef void result;

    void operator () (std::runtime_error const& e) const
    {
      std::cout << "std::runtime_error: " << e.what () << std::endl;
    }
    void operator () (std::logic_error const& e) const
    {
      std::cout << "std::logic_error: " << e.what () << std::endl;
      throw;
    }
  };

  // Reroute Qt messages to the system logger
  void qt_log_handler (QtMsgType type, QMessageLogContext const& context, QString const& msg)
  {
    // Convert Qt message types to logger severities
    auto severity = trivial::trace;
    switch (type)
      {
      case QtDebugMsg: severity = trivial::debug; break;
      case QtInfoMsg: severity = trivial::info; break;
      case QtWarningMsg: severity = trivial::warning; break;
      case QtCriticalMsg: severity = trivial::error; break;
      case QtFatalMsg: severity = trivial::fatal; break;
      }
    // Map non-default Qt categories to logger channels, Qt logger
    // context is mapped to the appropriate logger attributes.
    auto log = sys::get ();
    std::string file;
    std::string function;
    if (context.file)
      {
        file = context.file;
      }
    if (context.function)
      {
        function = context.function;
      }
    if (!context.category || !qstrcmp (context.category, "default"))
      {
        BOOST_LOG_SEV (log, severity)
          << boost::log::add_value ("Line", context.line)
          << boost::log::add_value ("File", file)
          << boost::log::add_value ("Function", function)
          << msg.toStdString ();
      }
    else
      {
        BOOST_LOG_SEV (log, severity)
          << boost::log::add_value ("Line", context.line)
          << boost::log::add_value ("File", file)
          << boost::log::add_value ("Function", function)
          << context.category << ": " << msg.toStdString ();
      }
  }

#ifdef Q_OS_WIN
  std::wstring log_string (QString const& value) { return value.toStdWString (); }
#else
  using log_settings = logging::settings;
  std::string log_string (QString const& value) { return value.toUtf8 ().toStdString (); }
#endif

#ifdef Q_OS_WIN
  void default_log_config (QDir const& app_data)
  {
    auto core = logging::core::get ();

    //
    // Define sinks, filters, and formatters using expression
    // templates for efficiency.
    //

    Logger::init ();          // Basic setup of attributes

    //
    // Sink intended for general use that passes everything above
    // selected severity levels per channel. Log file is appended
    // between sessions and rotated to limit storage space usage.
    //
    auto sys_sink = boost::make_shared<sinks::asynchronous_sink<sinks::text_file_backend>>
      (
       keywords::auto_flush = false
#if BOOST_VERSION / 100 >= 1070
       , keywords::file_name = app_data.absoluteFilePath ("wsjtx_syslog.log").toStdWString ()
       , keywords::target_file_name =
#else
       , keywords::file_name =
#endif
       app_data.absoluteFilePath ("logs/wsjtx_syslog_%Y-%m.log").toStdWString ()
       , keywords::time_based_rotation = sinks::file::rotation_at_time_point (gregorian::greg_day (1), 0, 0, 0)
       , keywords::open_mode = std::ios_base::out | std::ios_base::app
#if BOOST_VERSION / 100 >= 1063
       , keywords::enable_final_rotation = false
#endif
       );

    sys_sink->locked_backend ()->set_file_collector
      (
       sinks::file::make_collector
       (
        keywords::max_size = 5 * 1024 * 1024
        , keywords::min_free_space = 1024 * 1024 * 1024
        , keywords::max_files = 12
        , keywords::target = app_data.absoluteFilePath ("logs").toStdWString ()
        )
       );
    sys_sink->locked_backend ()->scan_for_files ();

    sys_sink->set_filter (WSJTXLogging::default_filter ());

    sys_sink->set_formatter
      (
       expr::stream
       << "[" << channel
       << "][" << expr::format_date_time<posix_time::ptime> ("TimeStamp", "%Y-%m-%d %H:%M:%S.%f")
       << "][" << expr::format_date_time<posix_time::time_duration> ("Uptime", "%O:%M:%S.%f")
       << "][" << trivial::severity
       << "] " << expr::message
       );

    core->add_sink (sys_sink);

    // Indicate start of logging
    LOG_INFO ("Log Start");
  }
#else
  void default_log_config (QDir const& app_data)
  {
    Logger::init ();

    // Let the loaded Boost library own its version-specific queue and file backend.
    log_settings settings;
    auto sink_setting = [&settings] (char const * name, QString const& value) {
      settings["Sinks"]["SYSLOG"][name] = log_string (value);
    };
    sink_setting ("Destination", "TextFile");
    sink_setting ("Asynchronous", "true");
    sink_setting ("AutoFlush", "false");
#if BOOST_VERSION / 100 >= 1070
    sink_setting ("FileName", app_data.absoluteFilePath ("wsjtx_syslog.log"));
    sink_setting ("TargetFileName",
#else
    sink_setting ("FileName",
#endif
      app_data.absoluteFilePath ("logs/wsjtx_syslog_%Y-%m.log"));
    sink_setting ("RotationTimePoint", "01 00:00:00");
    sink_setting ("Append", "true");
#if BOOST_VERSION / 100 >= 1063
    sink_setting ("EnableFinalRotation", "false");
#endif
    sink_setting ("Target", app_data.absoluteFilePath ("logs"));
    sink_setting ("MaxSize", "5242880");
    sink_setting ("MinFreeSpace", "1073741824");
    sink_setting ("MaxFiles", "12");
    sink_setting ("ScanForFiles", "Matching");
    sink_setting ("Filter", R"((%Channel% = "SYSLOG" & %Severity% >= error) |
      (%Channel% = "RIGCTRL" & %Severity% >= warning) |
      (%Channel% = "DATALOG" & %Severity% >= error) |
      (%Channel% = "DECODER" & %Severity% >= warning) | %Severity% >= fatal)");
    sink_setting ("Format", R"([%Channel%][%TimeStamp(format="%Y-%m-%d %H:%M:%S.%f")%])"
      R"([%Uptime(format="%O:%M:%S.%f")%][%Severity%] %Message%)");
    logging::init_from_settings (settings);
    LOG_INFO ("Log Start");
  }
#endif
}

WSJTXLogging::WSJTXLogging ()
  : WSJTXLogging {QStandardPaths::locate (QStandardPaths::ConfigLocation, "wsjtx_log_config.ini"),
                 QDir {QStandardPaths::writableLocation (QStandardPaths::AppLocalDataLocation)}}
{
}

WSJTXLogging::WSJTXLogging (QString const& configuration_file, QDir const& app_data)
{
  auto core = logging::core::get ();
  // Catch relevant exceptions from logging.
  core->set_exception_handler
    (
     logging::make_exception_handler<std::runtime_error, std::logic_error> (exception_handler {})
     );
 
  // Check for a user-defined logging configuration settings file.
  QFile log_config {configuration_file};
  if (log_config.exists () && log_config.open (QFile::ReadOnly) && log_config.isReadable ())
    {
      QTextStream ts {&log_config};
      auto config = ts.readAll ();

      // Substitution variables.
      container::flat_map<QString, QString> replacements =
        {
         {"DesktopLocation", QStandardPaths::writableLocation (QStandardPaths::DesktopLocation)},
         {"DocumentsLocation", QStandardPaths::writableLocation (QStandardPaths::DocumentsLocation)},
         {"TempLocation", QStandardPaths::writableLocation (QStandardPaths::TempLocation)},
         {"HomeLocation", QStandardPaths::writableLocation (QStandardPaths::HomeLocation)},
         {"CacheLocation", QStandardPaths::writableLocation (QStandardPaths::CacheLocation)},
         {"GenericCacheLocation", QStandardPaths::writableLocation (QStandardPaths::GenericCacheLocation)},
         {"GenericDataLocation", QStandardPaths::writableLocation (QStandardPaths::GenericDataLocation)},
         {"AppDataLocation", QStandardPaths::writableLocation (QStandardPaths::AppDataLocation)},
         {"AppLocalDataLocation", app_data.absolutePath ()},
        };
      // Parse the configration settings substituting the variable if found.
      QString new_config;
      int pos {0};
      QRegularExpression subst_vars {R"(\${([^}]+)})"};
      auto iter = subst_vars.globalMatch (config);
      while (iter.hasNext ())
        {
          auto match = iter.next ();
          auto const& name = match.captured (1);
          auto repl_iter = replacements.find (name);
          auto repl = repl_iter != replacements.end () ? repl_iter->second : "${" + name + "}";
          new_config += config.mid (pos, match.capturedStart (1) - 2 - pos) + repl;
          pos = match.capturedEnd (0);
        }
      new_config += config.mid (pos);
#ifdef Q_OS_WIN
      std::wistringstream stream {log_string (new_config)};
#else
      std::istringstream stream {log_string (new_config)};
#endif
      try
        {
          Logger::init_from_config (stream);
          LOG_INFO ("Read logging configuration file: " << log_config.fileName ());
        }
      catch (std::exception const& e)
        {
          default_log_config (app_data);
          LOG_ERROR ("Reading logging configuration file: " << log_config.fileName () << " - " << e.what ());
          LOG_INFO ("Reverting to default logging configuration");
        }
    }
  else                          // Default setup
    {
      default_log_config (app_data);
    }

  QFile f(QApplication::applicationDirPath() + "/qdebug");
  if(!f.exists()) {
    ::qInstallMessageHandler (&qt_log_handler);
  }
}

WSJTXLogging::~WSJTXLogging ()
{
  LOG_INFO ("Log Finish");
  auto core = logging::core::get ();
  core->flush ();
  core->remove_all_sinks ();
}
