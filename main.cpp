#include <iostream>
#include <exception>
#include <stdexcept>
#include <string>
#include <iterator>
#include <algorithm>
#include <ios>
#include <locale>
#include <fftw3.h>

#include <QSharedMemory>
#include <QProcessEnvironment>
#include <QTemporaryFile>
#include <QDateTime>
#include <QLocale>
#include <QTranslator>
#include <QRegularExpression>
#include <QObject>
#include <QSettings>
#include <QSysInfo>
#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>
#include <QStringList>
#include <QLockFile>
#include <QSplashScreen>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QByteArray>
#include <QBitArray>
#include <QMetaType>

#include "ExceptionCatchingApplication.hpp"
#include "Logger.hpp"
#include "revision_utils.hpp"
#include "MetaDataRegistry.hpp"
#include "qt_helpers.hpp"
#include "L10nLoader.hpp"
#include "SettingsGroup.hpp"
//#include "TraceFile.hpp"
#include "WSJTXLogging.hpp"
#include "MultiSettings.hpp"
#include "widgets/mainwindow.h"
#include "commons.h"
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
}

int main(int argc, char *argv[])
{
  init_random_seed ();

  // make the Qt type magic happen
  Radio::register_types ();
  register_types ();

  // Multiple instances communicate with jt9 via this
  QSharedMemory mem_jt9;

  auto const env = QProcessEnvironment::systemEnvironment ();

  ExceptionCatchingApplication a(argc, argv);
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

      QStandardPaths::setTestModeEnabled (parser.isSet (test_option));

      // support for multiple instances running from a single installation
      bool multiple {false};
      if (parser.isSet (rig_option) || parser.isSet (test_option))
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

          if (parser.isSet (test_option))
            {
              a.setApplicationName (a.applicationName () + " - test");
            }

          multiple = true;
        }

      // now we have the application name we can open the logging and settings
      WSJTXLogging lg;
      LOG_INFO (program_title (revision ()) << " - Program startup");
      MultiSettings multi_settings {parser.value (cfg_option)};

      // find the temporary files path
      QDir temp_dir {QStandardPaths::writableLocation (QStandardPaths::TempLocation)};
      Q_ASSERT (temp_dir.exists ()); // sanity check

      // disallow multiple instances with same instance key
      QLockFile instance_lock {temp_dir.absoluteFilePath (a.applicationName () + ".lock")};
      instance_lock.setStaleLockTime (0);
      while (!instance_lock.tryLock ())
        {
          if (QLockFile::LockFailedError == instance_lock.error ())
            {
              auto button = MessageBox::query_message (nullptr
                                                       , "Another instance may be running"
                                                       , "try to remove stale lock file?"
                                                       , QString {}
                                                       , MessageBox::Yes | MessageBox::Retry | MessageBox::No
                                                       , MessageBox::Yes);
              switch (button)
                {
                case MessageBox::Yes:
                  instance_lock.removeStaleLockFile ();
                  break;

                case MessageBox::Retry:
                  break;

                default:
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
        if (multi_settings.common_value (splash_flag_name, true).toBool ())
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

      // better performance traded for a risk of d/b corruption
      // on system crash or application crash
      // db.exec ("PRAGMA synchronous=OFF"); // system crash risk
      // db.exec ("PRAGMA journal_mode=MEMORY"); // application crash risk
      db.exec ("PRAGMA locking_mode=EXCLUSIVE");

      int result;
      auto const& original_style_sheet = a.styleSheet ();
      do
        {
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
                  dec_data_t * dd = reinterpret_cast<dec_data_t *> (mem_jt9.data());
                  mem_jt9.lock ();
                  dd->ipc[1] = 999; // tell jt9 to shut down
                  mem_jt9.unlock ();
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
              if (!mem_jt9.create (sizeof (dec_data)))
              {
                splash.hide ();
                MessageBox::critical_message (nullptr, a.translate ("main", "Shared memory error"),
                                              a.translate ("main", "Unable to create shared memory segment"));
                throw std::runtime_error {"Shared memory error"};
              }
              LOG_INFO ("shmem size: " << mem_jt9.size ());
            }
          else
            {
              splash.hide ();
              MessageBox::critical_message (nullptr, a.translate ("main", "Sub-process error"),
                                            a.translate ("main", "Failed to close orphaned jt9 process"));
              throw std::runtime_error {"Sub-process error"};
            }
          mem_jt9.lock ();
          memset(mem_jt9.data(),0,sizeof(struct dec_data)); //Zero all decoding params in shared memory
          mem_jt9.unlock ();

          unsigned downSampleFactor;
          {
            SettingsGroup {multi_settings.settings (), "Tune"};

            // deal with Windows Vista and earlier input audio rate
            // converter problems
            downSampleFactor = multi_settings.settings ()->value ("Audio/DisableInputResampling",
#if defined (Q_OS_WIN)
                                                                  // default to true for
                                                                  // Windows Vista and older
                                                                  QSysInfo::WV_VISTA >= QSysInfo::WindowsVersion ? true : false
#else
                                                                  false
#endif
                                                                  ).toBool () ? 1u : 4u;
          }

          // run the application UI
          MainWindow w(temp_dir, multiple, &multi_settings, &mem_jt9, downSampleFactor, &splash, env);
          w.show();
          splash.raise ();
          QObject::connect (&a, SIGNAL (lastWindowClosed()), &a, SLOT (quit()));
          result = a.exec();

          // ensure config switches start with the right style sheet
          a.setStyleSheet (original_style_sheet);
        }
      while (!result && !multi_settings.exit ());

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
      return result;
    }
  catch (std::exception const& e)
    {
      MessageBox::critical_message (nullptr, "Fatal error", e.what ());
      std::cerr << "Error: " << e.what () << '\n';
    }
  catch (...)
    {
      MessageBox::critical_message (nullptr, "Unexpected fatal error");
      std::cerr << "Unexpected fatal error\n";
      throw;			// hoping the runtime might tell us more about the exception
    }
  return -1;
}
