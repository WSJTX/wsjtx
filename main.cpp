#include <iostream>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>
#include <iterator>
#include <algorithm>
#include <ios>
#include <locale>
#include <memory>
#include <fftw3.h>

#include <QApplication>
#include <QSharedMemory>
#include <QProcessEnvironment>
#include <QTemporaryFile>
#include <QDateTime>
#include <QLocale>
#include <QTranslator>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QObject>
#include <QSettings>
#include <QSysInfo>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QLockFile>
#include <QSplashScreen>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QTimer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QByteArray>
#include <QBitArray>
#include <QMetaType>
#include <QPushButton>
#include <QMessageBox>
#include <QProgressDialog>

#include "ExceptionCatchingApplication.hpp"
#include "Logger.hpp"
#include "PerformanceTrace.hpp"
#include "revision_utils.hpp"
#include "HighDpiScaling.hpp"
#include "MetaDataRegistry.hpp"
#include "qt_helpers.hpp"
#include "L10nLoader.hpp"
#include "SettingsGroup.hpp"
//#include "TraceFile.hpp"
#include "WSJTXLogging.hpp"
#include "MultiSettings.hpp"
#include "widgets/mainwindow.h"
#include "Audio/AudioInputSource.hpp"
#include "Audio/soundout.h"
#ifdef WSJT_ENABLE_LIVE_AUDIO_TEST
#include "Audio/BWFFile.hpp"
#include "Audio/FixtureAudioInput.hpp"
#include "Audio/FixtureSoundOutput.hpp"
#include "Ft8TxLoopbackTestController.hpp"
#include "JttyTxLoopbackTestController.hpp"
#include "LiveAudioTestController.hpp"
#include <QAudioFormat>
#endif
#include "commons.h"
#include "DecoderIpc.hpp"
#include "lib/init_random_seed.h"
#include "Radio.hpp"
#include "models/FrequencyList.hpp"
#include "widgets/SplashScreen.hpp"
#include "widgets/MessageBox.hpp"       // last to avoid nasty MS macro definitions

extern "C" {
  // Fortran procedures we need
  void four2a_(_Complex float *, int * nfft, int * ndim, int * isign, int * iform, int len);
}

namespace
{
#if QT_VERSION < QT_VERSION_CHECK (5, 15, 0)
  struct RNGSetup
  {
    RNGSetup ()
    {
      // one time seed of pseudo RNGs from current time
      auto seed = QDateTime::currentMSecsSinceEpoch ();
      qsrand (seed);            // this is good for rand() as well
    }
  } seeding;
#endif

  void safe_stream_QVariant (boost::log::record_ostream& os, QVariant const& v)
  {
    switch (static_cast<QMetaType::Type> (v.type ()))
      {
      case QMetaType::QByteArray:
        os << "0x"
#if QT_VERSION >= QT_VERSION_CHECK (5, 9, 0)
           << v.toByteArray ().toHex (':').toStdString ()
#else
           << v.toByteArray ().toHex ().toStdString ()
#endif
          ;
        break;

      case QMetaType::QBitArray:
        {
          auto const& bits = v.toBitArray ();
          os << "0b";
          for (int i = 0; i < bits.size (); ++ i)
            {
              os << (bits[i] ? '1' : '0');
            }
        }
        break;

      default:
        os << v.toString ();
      }
  }

  QString lock_file_details (QLockFile const& lock, QString const& path)
  {
    QStringList details {QCoreApplication::translate ("main", "Lock file: %1").arg (QDir::toNativeSeparators (path))};
    qint64 pid;
    QString hostname;
    QString appname;
    if (lock.getLockInfo (&pid, &hostname, &appname))
      {
        details << QCoreApplication::translate ("main", "Owner process: %1").arg (pid);
        if (!appname.isEmpty ())
          {
            details << QCoreApplication::translate ("main", "Owner application: %1").arg (appname);
          }
        if (!hostname.isEmpty ())
          {
            details << QCoreApplication::translate ("main", "Owner host: %1").arg (hostname);
          }
      }
    else
      {
        details << QCoreApplication::translate ("main", "Owner information is not available.");
      }
    return details.join ('\n');
  }

  QString diagnostic_text (QString text)
  {
    return text.replace ('\\', "\\\\")
      .replace ('\n', "\\n")
      .replace ('\r', "\\r")
      .replace ('\t', "\\t")
      .replace ('"', "\\\"");
  }

  char const * window_modality_name (Qt::WindowModality modality)
  {
    switch (modality)
      {
      case Qt::NonModal: return "NonModal";
      case Qt::WindowModal: return "WindowModal";
      case Qt::ApplicationModal: return "ApplicationModal";
      }
    return "Unknown";
  }

  void report_unexpected_modal (QWidget const& modal)
  {
    std::cerr << "WSJT-X startup smoke: unexpected modal window:"
              << " class=" << modal.metaObject ()->className ()
              << " objectName=\"" << diagnostic_text (modal.objectName ()).toStdString () << '"'
              << " title=\"" << diagnostic_text (modal.windowTitle ()).toStdString () << '"'
              << " visible=" << (modal.isVisible () ? "true" : "false")
              << " modal=" << (modal.isModal () ? "true" : "false")
              << " modality=" << window_modality_name (modal.windowModality ());

    if (auto const *message_box = qobject_cast<QMessageBox const *> (&modal))
      {
        std::cerr << " text=\"" << diagnostic_text (message_box->text ()).toStdString () << '"'
                  << " informativeText=\""
                  << diagnostic_text (message_box->informativeText ()).toStdString () << '"';
      }
    if (auto const *progress_dialog = qobject_cast<QProgressDialog const *> (&modal))
      {
        std::cerr << " labelText=\""
                  << diagnostic_text (progress_dialog->labelText ()).toStdString () << '"';
      }
    std::cerr << std::endl;
  }

  enum class LockFileAction
  {
    RemoveLockFile,
    Retry,
    Stop
  };

  struct LockFileRemovalResult
  {
    bool removed;
    QString error;
  };

  LockFileRemovalResult remove_stale_lock_file (QLockFile& lock, QString const& path)
  {
    if (lock.removeStaleLockFile ())
      {
        return {true, QString {}};
      }

    QFile lock_file {path};
    if (!lock_file.exists ())
      {
        return {true, QString {}};
      }
    if (lock_file.remove ())
      {
        return {true, QString {}};
      }
    return {false, QCoreApplication::translate ("main", "Remove error: %1").arg (lock_file.errorString ())};
  }

  LockFileAction query_lock_file_recovery (QLockFile const& lock, QString const& path)
  {
    MessageBox message_box {MessageBox::Question
      , QCoreApplication::translate ("main", "Another instance may be running")
      , MessageBox::NoButton};
    message_box.setInformativeText (QCoreApplication::translate ("main", "WSJT-X could not lock its temporary instance file. "
                                                                 "\n\nIf another WSJT-X window is still running, choose No. "
                                                                 "\nUse a unique rig name to run more than one instance. "
                                                                 "\n\nIf WSJT-X crashed or will not restart, choose Remove Lock File. "
                                                                 "\nChoose Retry after closing the other instance."));
    message_box.setDetailedText (lock_file_details (lock, path));

    auto remove_button = message_box.addButton (QCoreApplication::translate ("main", "Remove Lock File"), MessageBox::ActionRole);
    auto retry_button = message_box.addButton (MessageBox::Retry);
    auto no_button = message_box.addButton (MessageBox::No);
    message_box.setDefaultButton (retry_button);
    message_box.setEscapeButton (no_button);

    if (message_box.exec () == -1)
      {
        return LockFileAction::Stop;
      }
    auto clicked_button = message_box.clickedButton ();
    if (remove_button == clicked_button)
      {
        return LockFileAction::RemoveLockFile;
      }
    if (retry_button == clicked_button)
      {
        return LockFileAction::Retry;
      }
    return LockFileAction::Stop;
  }
}

int main(int argc, char *argv[])
{
  PerformanceTrace::begin_run ("startup", "initial");
  PerformanceTrace::Phase process_bootstrap {"process.bootstrap"};
  init_random_seed ();

  // make the Qt type magic happen
  Radio::register_types ();
  register_types ();

  // Multiple instances communicate with jt9 via this
  QSharedMemory mem_jt9;

  if (HighDpiScaling::wsjtxEnabled (argc, argv))
    {
      QApplication::setAttribute (Qt::AA_EnableHighDpiScaling);
    }

  auto const env = QProcessEnvironment::systemEnvironment ();

  ExceptionCatchingApplication a(argc, argv);
  bool startup_smoke_test {false};
  bool automated_test {false};
#ifdef WSJT_ENABLE_LIVE_AUDIO_TEST
  bool live_audio_test {false};
  bool jtty_live_audio_test {false};
  bool jtty_tx_loopback_test {false};
  bool ft8_tx_loopback_test {false};
#endif
  try
    {
      // LOG_INfO ("+++++++++++++++++++++++++++ Resources ++++++++++++++++++++++++++++");
      // {
      //   QDirIterator resources_iter {":/", QDirIterator::Subdirectories};
      //   while (resources_iter.hasNext ())
      //     {
      //       LOG_INFO (resources_iter.next ());
      //     }
      // }
      // LOG_INFO ("--------------------------- Resources ----------------------------");

      QLocale locale;              // get the current system locale

      // reset the C+ & C global locales to the classic C locale
      std::locale::global (std::locale::classic ());

      // Override programs executable basename as application name.
      a.setApplicationName ("WSJT-X");
      a.setApplicationVersion (version ());

      QCommandLineParser parser;
      parser.setApplicationDescription ("\n" PROJECT_DESCRIPTION);
      auto help_option = parser.addHelpOption ();
      auto version_option = parser.addVersionOption ();

      // support for multiple instances running from a single installation
      QCommandLineOption rig_option (QStringList {} << "r" << "rig-name"
                                     , "Where <rig-name> is for multi-instance support."
                                     , "rig-name");
      parser.addOption (rig_option);

      QCommandLineOption n1mm_tcp_port_option (QStringList {} << "p" << "n1mm-tcp-port" << "n1mm_tcp_port"
                                        , "N1MM Logger+ TCP port number."
                                        , "TCP port");
      parser.addOption (n1mm_tcp_port_option);

      QCommandLineOption mode_option (QStringList {} << "mode"
                                     , "Startup mode (ft8, ft4, jtty)."
                                     , "mode");
      parser.addOption (mode_option);

      // support for start up configuration
      QCommandLineOption cfg_option (QStringList {} << "c" << "config"
                                     , "Where <configuration> is an existing one."
                                     , "configuration");
      parser.addOption (cfg_option);

      // support for UI language override (useful on Windows)
      QCommandLineOption lang_option (QStringList {} << "l" << "language"
                                     , "Where <language> is <lang-code>[-<country-code>]."
                                     , "language");
      parser.addOption (lang_option);

      QCommandLineOption test_option (QStringList {} << "test-mode"
                                      , "Writable files in test location.  Use with caution, for testing only.");
      parser.addOption (test_option);

      QCommandLineOption startup_smoke_test_option (
        QStringList {} << "startup-smoke-test",
        "Start the application, process initial GUI events, and exit.");
      parser.addOption (startup_smoke_test_option);

#ifdef WSJT_ENABLE_LIVE_AUDIO_TEST
      QCommandLineOption live_audio_test_option (
        QStringList {} << "live-audio-test",
        "Feed a WAV fixture through the live receive path.", "wav-path");
      parser.addOption (live_audio_test_option);
      QCommandLineOption live_audio_expected_option (
        QStringList {} << "live-audio-expected",
        "Expected decoder output used by --live-audio-test.", "expected-path");
      parser.addOption (live_audio_expected_option);
      QCommandLineOption live_audio_data_dir_option (
        QStringList {} << "live-audio-data-dir",
        "Shipped decoder data used by --live-audio-test.", "directory");
      parser.addOption (live_audio_data_dir_option);
      QCommandLineOption jtty_live_audio_test_option (
        QStringList {} << "jtty-live-audio-test",
        "Feed a WAV fixture through the live JTTY receive path.", "wav-path");
      parser.addOption (jtty_live_audio_test_option);
      QCommandLineOption jtty_live_audio_expected_option (
        QStringList {} << "jtty-live-audio-expected",
        "Expected message used by --jtty-live-audio-test.", "text-path");
      parser.addOption (jtty_live_audio_expected_option);
      QCommandLineOption jtty_tx_loopback_test_option (
        QStringList {} << "jtty-tx-loopback-test",
        "Capture two gaplessly queued JTTY messages to a WAV file.", "wav-path");
      parser.addOption (jtty_tx_loopback_test_option);
      QCommandLineOption ft8_tx_loopback_test_option (
        QStringList {} << "ft8-tx-loopback-test",
        "Capture one period-aligned FT8 transmission to a WAV file.", "wav-path");
      parser.addOption (ft8_tx_loopback_test_option);
#endif

      if (!parser.parse (a.arguments ()))
        {
          MessageBox::critical_message (nullptr, "Command line error", parser.errorText ());
          return -1;
        }
      else
        {
          if (parser.isSet (help_option))
            {
              MessageBox::information_message (nullptr, "Command line help", parser.helpText ());
              return 0;
            }
          else if (parser.isSet (version_option))
            {
              MessageBox::information_message (nullptr, "Application version", a.applicationVersion ());
              return 0;
            }
        }

      startup_smoke_test = parser.isSet (startup_smoke_test_option);
#ifdef WSJT_ENABLE_LIVE_AUDIO_TEST
      live_audio_test = parser.isSet (live_audio_test_option);
      jtty_live_audio_test = parser.isSet (jtty_live_audio_test_option);
      jtty_tx_loopback_test = parser.isSet (jtty_tx_loopback_test_option);
      ft8_tx_loopback_test = parser.isSet (ft8_tx_loopback_test_option);
      if (live_audio_test != parser.isSet (live_audio_expected_option)
          || live_audio_test != parser.isSet (live_audio_data_dir_option))
        {
          std::cerr << "--live-audio-test, --live-audio-expected, and "
                       "--live-audio-data-dir must be used together"
                    << std::endl;
          return EXIT_FAILURE;
        }
      if (jtty_live_audio_test != parser.isSet (jtty_live_audio_expected_option))
        {
          std::cerr << "--jtty-live-audio-test and --jtty-live-audio-expected "
                       "must be used together" << std::endl;
          return EXIT_FAILURE;
        }
      if ((startup_smoke_test ? 1 : 0) + (live_audio_test ? 1 : 0)
          + (jtty_live_audio_test ? 1 : 0)
          + (jtty_tx_loopback_test ? 1 : 0)
          + (ft8_tx_loopback_test ? 1 : 0) > 1)
        {
          std::cerr << "Startup, live-audio, and TX loopback tests are mutually exclusive"
                    << std::endl;
          return EXIT_FAILURE;
        }
      if (live_audio_test)
        {
          QDir const decoderDataDir {parser.value (live_audio_data_dir_option)};
          QFileInfo const allCallsigns {
            decoderDataDir.absoluteFilePath ("ALLCALL7.TXT")};
          if (!allCallsigns.isFile () || !allCallsigns.isReadable ())
            {
              std::cerr << "--live-audio-data-dir does not contain a readable "
                           "ALLCALL7.TXT"
                        << std::endl;
              return EXIT_FAILURE;
            }
        }
      automated_test = startup_smoke_test || live_audio_test
        || jtty_live_audio_test || jtty_tx_loopback_test || ft8_tx_loopback_test;
#else
      automated_test = startup_smoke_test;
#endif
      auto const smoke_phase = [startup_smoke_test] (char const *phase) {
        if (startup_smoke_test)
          {
            std::cerr << "WSJT-X startup smoke: " << phase << std::endl;
          }
      };
      smoke_phase ("command line accepted");
      QStandardPaths::setTestModeEnabled (parser.isSet (test_option) || automated_test);

      // support for multiple instances running from a single installation
      bool multiple {false};
      if (parser.isSet (rig_option) || parser.isSet (test_option) || automated_test)
        {
          auto temp_name = parser.value (rig_option);
          if (!temp_name.isEmpty ())
            {
              if (temp_name.contains (QRegularExpression {R"([\\/,])"}))
                {
                  std::cerr << "Invalid rig name - \\ & / not allowed" << std::endl;
                  parser.showHelp (-1);
                }
                
              a.setApplicationName (a.applicationName () + " - " + temp_name);
            }

          if (parser.isSet (test_option) || automated_test)
            {
              a.setApplicationName (a.applicationName () + " - test");
            }

          multiple = true;
        }

      // now we have the application name we can open the logging and settings
      WSJTXLogging lg;
      LOG_INFO (program_title (revision ()) << " - Program startup");
      process_bootstrap.finish ();
      PerformanceTrace::milestone ("logging.ready");
      PerformanceTrace::Phase process_prepare {"process.prepare"};
      MultiSettings multi_settings {parser.value (cfg_option)};

      // find the temporary files path
      QDir temp_dir {QStandardPaths::writableLocation (QStandardPaths::TempLocation)};
      Q_ASSERT (temp_dir.exists ()); // sanity check

      // disallow multiple instances with same instance key
      auto const instance_lock_path = temp_dir.absoluteFilePath (a.applicationName () + ".lock");
      QLockFile instance_lock {instance_lock_path};
      instance_lock.setStaleLockTime (0);
      while (!instance_lock.tryLock ())
        {
          if (QLockFile::LockFailedError == instance_lock.error ())
            {
              switch (query_lock_file_recovery (instance_lock, instance_lock_path))
                {
                case LockFileAction::RemoveLockFile:
                  {
                    auto const removal = remove_stale_lock_file (instance_lock, instance_lock_path);
                    if (!removal.removed)
                    {
                      auto details = lock_file_details (instance_lock, instance_lock_path);
                      if (!removal.error.isEmpty ())
                        {
                          details += '\n';
                          details += removal.error;
                        }
                      MessageBox::warning_message (nullptr
                                                   , a.translate ("main", "Unable to remove stale lock file")
                                                   , a.translate ("main", "Close any running WSJT-X instance or remove the lock file manually after confirming WSJT-X is not running.")
                                                   , details);
                    }
                  }
                  break;

                case LockFileAction::Retry:
                  break;

                case LockFileAction::Stop:
                  throw std::runtime_error {"Multiple instances must have unique rig names"};
                }
            }
          else
            {
              throw std::runtime_error {"Failed to access lock file"};
            }
        }

      // load UI translations
      L10nLoader l10n {&a, locale, parser.value (lang_option)};

      // Create a unique writeable temporary directory in a suitable location
      bool temp_ok {false};
      QString unique_directory {ExceptionCatchingApplication::applicationName ()};
      do
        {
          if (!temp_dir.mkpath (unique_directory)
              || !temp_dir.cd (unique_directory))
            {
              MessageBox::critical_message (nullptr,
                                            a.translate ("main", "Failed to create a temporary directory"),
                                            a.translate ("main", "Path: \"%1\"").arg (temp_dir.absolutePath ()));
              throw std::runtime_error {"Failed to create a temporary directory"};
            }
          if (!temp_dir.isReadable () || !(temp_ok = QTemporaryFile {temp_dir.absoluteFilePath ("test")}.open ()))
            {
              auto button =  MessageBox::critical_message (nullptr,
                                                           a.translate ("main", "Failed to create a usable temporary directory"),
                                                           a.translate ("main", "Another application may be locking the directory"),
                                                           a.translate ("main", "Path: \"%1\"").arg (temp_dir.absolutePath ()),
                                                           MessageBox::Retry | MessageBox::Cancel);
              if (MessageBox::Cancel == button)
                {
                  throw std::runtime_error {"Failed to create a usable temporary directory"};
                }
              temp_dir.cdUp ();  // revert to parent as this one is no good
            }
        }
      while (!temp_ok);

      SplashScreen splash;
      {
        // change this key if you want to force a new splash screen
        // for a new version, the user will be able to re-disable it
        // if they wish
        QString splash_flag_name {"Splash_v1.7"};
#if defined(WSJT_TSAN_TEST_PROFILE)
        // GCC TSan cannot track Qt's instrumented SVG setjmp/longjmp rasterizer.
        auto const skip_splash = automated_test;
#else
        auto const skip_splash = false;
#endif
        if (!skip_splash
            && multi_settings.common_value (splash_flag_name, true).toBool ())
          {
            QObject::connect (&splash, &SplashScreen::disabled, [&, splash_flag_name] {
                multi_settings.set_common_value (splash_flag_name, false);
                splash.close ();
              });
            splash.show ();
            a.processEvents ();
          }
      }

      // create writeable data directory if not already there
      auto writeable_data_dir = QDir {QStandardPaths::writableLocation (QStandardPaths::DataLocation)};
      if (!writeable_data_dir.mkpath ("."))
        {
          MessageBox::critical_message (nullptr, a.translate ("main", "Failed to create data directory"),
                                        a.translate ("main", "path: \"%1\"").arg (writeable_data_dir.absolutePath ()));
          throw std::runtime_error {"Failed to create data directory"};
        }

      // set up SQLite database
      if (!QSqlDatabase::drivers ().contains ("QSQLITE"))
        {
          throw std::runtime_error {"Failed to find SQLite Qt driver"};
        }
      auto db = QSqlDatabase::addDatabase ("QSQLITE");
      db.setDatabaseName (writeable_data_dir.absoluteFilePath ("db.sqlite"));
      if (!db.open ())
        {
          throw std::runtime_error {("Database Error: " + db.lastError ().text ()).toStdString ()};
        }
      smoke_phase ("SQLite opened");
      // better performance traded for a risk of d/b corruption
      // on system crash or application crash
      // db.exec ("PRAGMA synchronous=OFF"); // system crash risk
      // db.exec ("PRAGMA journal_mode=MEMORY"); // application crash risk
      db.exec ("PRAGMA locking_mode=EXCLUSIVE");
      process_prepare.finish ();

      int result;
      bool startup_smoke_ready {false};
      auto const& original_style_sheet = a.styleSheet ();
      do
        {
          PerformanceTrace::Phase runtime_prepare {"runtime.prepare"};
          // dump settings
          auto sys_lg = sys::get ();
          if (auto rec = sys_lg.open_record
              (
               boost::log::keywords::severity = boost::log::trivial::trace)
              )
            {
              boost::log::record_ostream strm (rec);
              strm << "++++++++++++++++++++++++++++ Settings ++++++++++++++++++++++++++++\n";
              for (auto const& key: multi_settings.settings ()->allKeys ())
                {
                  if (!key.contains (QRegularExpression {"^MultiSettings/[^/]*/"}))
                    {
                      auto const& value = multi_settings.settings ()->value (key);
                      if (value.canConvert<QVariantList> ())
                        {
                          auto const sequence = value.value<QSequentialIterable> ();
                          strm << key << ":\n";
                          for (auto const& item: sequence)
                            {
                              strm << "\t";
                              safe_stream_QVariant (strm, item);
                              strm << '\n';
                            }
                        }
                      else
                        {
                          strm << key << ": ";
                          safe_stream_QVariant (strm, value);
                          strm << '\n';
                        }
                    }
                }
              strm << "---------------------------- Settings ----------------------------\n";
              strm.flush ();
              sys_lg.push_record (boost::move (rec));
            }

          // Create and initialize shared memory segment
          // Multiple instances: use rig_name as shared memory key
          mem_jt9.setKey(a.applicationName ());

          // try and shut down any orphaned jt9 process
          for (int i = 3; i; --i) // three tries to close old jt9
            {
              if (mem_jt9.attach ()) // shared memory presence implies
                                     // orphaned jt9 sub-process
                {
                  if (DecoderIpc::hasShutdownControlSize (mem_jt9.size ()))
                    {
                      DecoderIpc::shutdownControl (mem_jt9.data ());
                    }
                  mem_jt9.detach (); // start again
                }
              else
                {
                  break;        // good to go
                }
              QThread::sleep (1); // wait for jt9 to end
            }
          if (!mem_jt9.attach ())
            {
              if (!mem_jt9.create (sizeof (shared_dec_data_t)))
              {
                auto const shared_memory_error = mem_jt9.error ();
                auto const shared_memory_error_text = mem_jt9.errorString ();
                std::cerr << "WSJT-X startup: shared memory creation failed"
                          << " (error " << static_cast<int> (shared_memory_error) << "): "
                          << shared_memory_error_text.toStdString () << std::endl;
                if (!automated_test)
                  {
                    splash.hide ();
                    MessageBox::critical_message (
                      nullptr, a.translate ("main", "Shared memory error"),
                      a.translate ("main", "Unable to create shared memory segment"));
                  }
                throw std::runtime_error {"Shared memory error"};
              }
              LOG_INFO ("shmem size: " << mem_jt9.size ());
            }
          else
            {
              std::cerr << "WSJT-X startup: orphaned jt9 shared memory segment remained after "
                           "shutdown attempts"
                        << std::endl;
              if (!automated_test)
                {
                  splash.hide ();
                  MessageBox::critical_message (
                    nullptr, a.translate ("main", "Sub-process error"),
                    a.translate ("main", "Failed to close orphaned jt9 process"));
                }
              throw std::runtime_error {"Sub-process error"};
            }
          auto * shared = reinterpret_cast<shared_dec_data_t *> (mem_jt9.data ());
          DecoderIpc::initialize (*shared);

          unsigned downSampleFactor;
          {
            SettingsGroup {multi_settings.settings (), "Tune"};

            // deal with Windows Vista and earlier input audio rate
            // converter problems
            downSampleFactor = multi_settings.settings()->value(
                "Audio/DisableInputResampling",
#if defined (Q_OS_WIN)
                                                                  // default to true for
                (QOperatingSystemVersion::current() <
                 QOperatingSystemVersion(QOperatingSystemVersion::Windows, 6))
                    ? true
                    : false
#else
                                                                  false
#endif
                                                                  ).toBool () ? 1u : 4u;

#ifdef WSJT_ENABLE_LIVE_AUDIO_TEST
            if (live_audio_test)
              {
                BWFFile fixture {QAudioFormat {}, parser.value (live_audio_test_option)};
                if (fixture.open (BWFFile::ReadOnly))
                  {
                    downSampleFactor = fixture.format ().sampleRate () == 48000
                      ? 4u : 1u;
                  }
              }
            if (jtty_live_audio_test)
              {
                BWFFile fixture {QAudioFormat {},
                                 parser.value (jtty_live_audio_test_option)};
                if (fixture.open (BWFFile::ReadOnly))
                  {
                    downSampleFactor = fixture.format ().sampleRate () == 48000
                      ? 4u : 1u;
                  }
              }
#endif

          }

          QDir::setCurrent(qApp->applicationDirPath()); //This helps to find the SF executables
          runtime_prepare.finish ();

          // run the application UI
          smoke_phase ("constructing MainWindow");
          PerformanceTrace::Phase main_window_construct {"mainwindow.construct"};
#ifdef WSJT_ENABLE_LIVE_AUDIO_TEST
          FixtureAudioInput * fixture_input {nullptr};
          FixtureSoundOutput * fixture_output {nullptr};
          std::unique_ptr<AudioInputSource> audio_input;
          std::unique_ptr<SoundOutput> sound_output;
          if (live_audio_test || jtty_live_audio_test)
            {
              std::unique_ptr<FixtureAudioInput> fixture {
                new FixtureAudioInput {
                  parser.value (live_audio_test
                                ? live_audio_test_option
                                : jtty_live_audio_test_option),
                  live_audio_test ? FixtureAudioInput::Profile::Ft8
                                  : FixtureAudioInput::Profile::Jtty}};
              fixture_input = fixture.get ();
              audio_input = std::move (fixture);
            }
          if (jtty_tx_loopback_test || ft8_tx_loopback_test)
            {
              std::unique_ptr<FixtureSoundOutput> fixture {
                new FixtureSoundOutput {
                  parser.value (jtty_tx_loopback_test
                                ? jtty_tx_loopback_test_option
                                : ft8_tx_loopback_test_option),
                  ft8_tx_loopback_test
                    ? FixtureSoundOutput::Profile::Ft8Period
                    : FixtureSoundOutput::Profile::JttyStrict}};
              fixture_output = fixture.get ();
              sound_output = std::move (fixture);
            }
#else
          std::unique_ptr<AudioInputSource> audio_input;
#endif
          MainWindow w(temp_dir, multiple, &multi_settings, &mem_jt9, downSampleFactor, &splash, env,
                       automated_test, std::move (audio_input),
#ifdef WSJT_ENABLE_LIVE_AUDIO_TEST
                       std::move (sound_output),
#else
                       {},
#endif
#ifdef WSJT_ENABLE_LIVE_AUDIO_TEST
                       live_audio_test ? parser.value (live_audio_data_dir_option) : QString {}
#else
                       QString {}
#endif
                       );
          main_window_construct.finish ();
          smoke_phase ("MainWindow constructed");
#ifdef Q_OS_WIN
          quint16 mmtty_port = 0;
          if (parser.isSet(n1mm_tcp_port_option)) {
              mmtty_port = parser.value(n1mm_tcp_port_option).toUShort();
          }
          
          if (mmtty_port > 0) {
              LOG_INFO("Starting JTTY/N1MM Logger interface on port: " << mmtty_port);
              w.initMMTTY(mmtty_port);
          } else {
              LOG_INFO("JTTY/N1MM Logger interface not enabled (no port or matching rig name provided).");
          }
#endif
          if (parser.isSet(mode_option)) {
              bool lock_mode = parser.isSet(n1mm_tcp_port_option);
              w.set_mode_from_command_line(parser.value(mode_option), lock_mode);
          }

          w.show();
          PerformanceTrace::milestone ("ui.show_returned");
          smoke_phase ("MainWindow shown");
#ifdef WSJT_ENABLE_LIVE_AUDIO_TEST
          std::unique_ptr<LiveAudioTestController> live_audio_controller;
          std::unique_ptr<JttyTxLoopbackTestController> jtty_tx_controller;
          std::unique_ptr<Ft8TxLoopbackTestController> ft8_tx_controller;
          if (live_audio_test || jtty_live_audio_test)
            {
              a.setQuitOnLastWindowClosed (false);
              live_audio_controller.reset (new LiveAudioTestController {
                &w, fixture_input,
                parser.value (live_audio_test
                              ? live_audio_expected_option
                              : jtty_live_audio_expected_option),
                live_audio_test ? LiveAudioTestController::Mode::Ft8
                                : LiveAudioTestController::Mode::Jtty});
              auto * controller = live_audio_controller.get ();
              QTimer::singleShot (0, live_audio_controller.get (),
                                  [controller] {
                                    controller->begin ();
                                  });
            }
          if (jtty_tx_loopback_test)
            {
              a.setQuitOnLastWindowClosed (false);
              jtty_tx_controller.reset (new JttyTxLoopbackTestController {
                &w, fixture_output,
                parser.value (jtty_tx_loopback_test_option)});
              auto * controller = jtty_tx_controller.get ();
              QTimer::singleShot (0, jtty_tx_controller.get (),
                                  [controller] {
                                    controller->begin ();
                                  });
            }
          if (ft8_tx_loopback_test)
            {
              a.setQuitOnLastWindowClosed (false);
              ft8_tx_controller.reset (new Ft8TxLoopbackTestController {
                &w, fixture_output,
                parser.value (ft8_tx_loopback_test_option)});
              auto * controller = ft8_tx_controller.get ();
              QTimer::singleShot (0, ft8_tx_controller.get (),
                                  [controller] {
                                    controller->begin ();
                                  });
            }
#endif
          if (startup_smoke_test)
            {
              QTimer::singleShot (1000, &w, [&a, &w, &smoke_phase, &startup_smoke_ready] {
                smoke_phase ("event loop reached");
                if (auto *modal = QApplication::activeModalWidget ())
                  {
                    report_unexpected_modal (*modal);
                    modal->close ();
                    w.close ();
                    a.exit (EXIT_FAILURE);
                    return;
                  }
                if (w.close ())
                  {
                    smoke_phase ("close accepted");
                    startup_smoke_ready = true;
                    a.quit ();
                  }
                else
                  {
                    std::cerr << "WSJT-X startup smoke: main window rejected close" << std::endl;
                    a.exit (EXIT_FAILURE);
                  }
              });
            }
          splash.raise ();
          QObject::connect (&a, SIGNAL (lastWindowClosed()), &a, SLOT (quit()));
          result = a.exec();
          PerformanceTrace::milestone ("event_loop.exit");
          if (startup_smoke_test && !startup_smoke_ready)
            {
              std::cerr << "WSJT-X startup smoke: application exited before readiness" << std::endl;
              result = EXIT_FAILURE;
            }
#ifdef WSJT_ENABLE_LIVE_AUDIO_TEST
          if ((live_audio_test || jtty_live_audio_test)
              && (!live_audio_controller || !live_audio_controller->succeeded ()))
            {
              result = EXIT_FAILURE;
            }
          if (jtty_tx_loopback_test
              && (!jtty_tx_controller || !jtty_tx_controller->succeeded ()))
            {
              result = EXIT_FAILURE;
            }
          if (ft8_tx_loopback_test
              && (!ft8_tx_controller || !ft8_tx_controller->succeeded ()))
            {
              result = EXIT_FAILURE;
            }
#endif

          // ensure config switches start with the right style sheet
          a.setStyleSheet (original_style_sheet);
        }
      while (!multi_settings.exit () && !result && !automated_test);

      // clean up lazily initialized resources
      {
        int nfft {-1};
        int ndim {1};
        int isign {1};
        int iform {1};
        // free FFT plan resources
        four2a_ (nullptr, &nfft, &ndim, &isign, &iform, 0);
      }
      fftwf_forget_wisdom ();
      fftwf_cleanup ();

      temp_dir.removeRecursively (); // clean up temp files
      if (startup_smoke_test && !result)
        {
          smoke_phase ("cleanup complete");
          std::cout << "WSJT-X startup smoke test passed" << std::endl;
        }
      return result;
    }
  catch (std::exception const& e)
    {
      if (!automated_test)
        {
          MessageBox::critical_message (nullptr, "Fatal error", e.what ());
        }
      std::cerr << "Error: " << e.what () << '\n';
    }
  catch (...)
    {
      if (!automated_test)
        {
          MessageBox::critical_message (nullptr, "Unexpected fatal error");
        }
      std::cerr << "Unexpected fatal error\n";
      throw;			// hoping the runtime might tell us more about the exception
    }
  return -1;
}
