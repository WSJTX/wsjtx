// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>

#include <QtTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include "L10nLoader.hpp"
#include "Logger.hpp"

class TestL10nLoader : public QObject
{
  Q_OBJECT

private:
  QString original_directory_;
  std::unique_ptr<QTemporaryDir> directory_;

  QString message (char const * source) const
  {
    return QCoreApplication::translate ("L10nTest", source);
  }

  QApplication * application () const
  {
    return qobject_cast<QApplication *> (QCoreApplication::instance ());
  }

private slots:
  void initTestCase ()
  {
    Logger::disable ();
  }

  void init ()
  {
    original_directory_ = QDir::currentPath ();
    directory_.reset (new QTemporaryDir);
    QVERIFY (directory_->isValid ());
    QVERIFY (QDir::setCurrent (directory_->path ()));
  }

  void cleanup ()
  {
    QVERIFY (QDir::setCurrent (original_directory_));
    directory_.reset ();
    QCOMPARE (message ("Shared message"), QString {"Shared message"});
    QCOMPARE (QCoreApplication::translate ("L10nQtTest", "Qt message"), QString {"Qt message"});
  }

  void automatic_language ()
  {
    L10nLoader loader {application (), QLocale {"hu_HU"}};
    QCOMPARE (message ("Shared message"), QString {"Hungarian interface"});
    QCOMPARE (QCoreApplication::translate ("L10nQtTest", "Qt message"), QString {"Hungarian Qt"});
  }

  void automatic_regional_language ()
  {
    L10nLoader loader {application (), QLocale {"de_AT"}};
    QCOMPARE (message ("Shared message"), QString {"Austrian interface"});
    QCOMPARE (message ("Base only"), QString {"German base"});
  }

  void system_language_preferences_data ()
  {
    QTest::addColumn<QString> ("languages");
    QTest::addColumn<QString> ("expected_interface");
    QTest::addColumn<QString> ("expected_qt");
    QTest::newRow ("secondary language") << "fr:de" << "German interface" << "German Qt";
    QTest::newRow ("regional fallback") << "en_US:en_GB" << "British interface" << "Qt message";
  }

  void system_language_preferences ()
  {
#if defined (Q_OS_LINUX)
    QFETCH (QString, languages);
    QFETCH (QString, expected_interface);
    QFETCH (QString, expected_qt);
    if (qEnvironmentVariableIsSet ("WSJTX_L10N_SYSTEM_CHILD"))
      {
        L10nLoader loader {application (), QLocale::system ()};
        QCOMPARE (message ("Shared message"), expected_interface);
        QCOMPARE (QCoreApplication::translate ("L10nQtTest", "Qt message"), expected_qt);
        return;
      }

    // A fresh process initializes Qt's cached system locale from this environment.
    auto environment = QProcessEnvironment::systemEnvironment ();
    environment.remove ("LC_ALL");
    environment.remove ("LC_MESSAGES");
    environment.insert ("LANG", "en_US.UTF-8");
    environment.insert ("LANGUAGE", languages);
    environment.insert ("QT_QPA_PLATFORM", "offscreen");
    environment.insert ("WSJTX_L10N_SYSTEM_CHILD", "1");
    QProcess child;
    child.setProcessEnvironment (environment);
    child.start (QCoreApplication::applicationFilePath (),
                 {QString {"system_language_preferences:%1"}.arg (QTest::currentDataTag ())});
    QVERIFY (child.waitForStarted ());
    QVERIFY (child.waitForFinished (10000));
    auto const output = child.readAllStandardOutput () + child.readAllStandardError ();
    QVERIFY2 (child.exitStatus () == QProcess::NormalExit && child.exitCode () == 0, output.constData ());
#else
    QSKIP ("Process-local system language preferences are simulated with Linux LANGUAGE");
#endif
  }

  void english_override_data ()
  {
    QTest::addColumn<QString> ("language");
    QTest::newRow ("English") << "en";
    QTest::newRow ("English underscore") << "en_US";
    QTest::newRow ("English hyphen") << "en-US";
  }

  void english_override ()
  {
    QFETCH (QString, language);
    for (auto const& name : {"hu", "en", "en_US"})
      {
        QVERIFY (QFile::copy (":/Translations/local_de.qm", QString {"wsjtx_%1.qm"}.arg (name)));
      }
    L10nLoader loader {application (), QLocale {"hu_HU"}, language};
    QCOMPARE (message ("Shared message"), QString {"Shared message"});
    QCOMPARE (message ("Hungarian only"), QString {"Hungarian only"});
    QCOMPARE (QCoreApplication::translate ("L10nQtTest", "Qt message"), QString {"Qt message"});
  }

  void explicit_language_isolation ()
  {
    QVERIFY (QFile::copy (":/Translations/wsjtx_hu.qm", "wsjtx_hu.qm"));
    auto const default_locale = QLocale ();
    L10nLoader loader {application (), QLocale {"hu_HU"}, "de"};
    QCOMPARE (message ("Shared message"), QString {"German interface"});
    QCOMPARE (message ("Hungarian only"), QString {"Hungarian only"});
    QCOMPARE (QCoreApplication::translate ("L10nQtTest", "Qt message"), QString {"German Qt"});
    QCOMPARE (QLocale (), default_locale);
  }

  void regional_language_data ()
  {
    QTest::addColumn<QString> ("language");
    QTest::newRow ("underscore") << "de_AT";
    QTest::newRow ("hyphen") << "de-AT";
  }

  void regional_language ()
  {
    QFETCH (QString, language);
    L10nLoader loader {application (), QLocale {"hu_HU"}, language};
    QCOMPARE (message ("Shared message"), QString {"Austrian interface"});
    QCOMPARE (message ("Regional only"), QString {"Austrian region"});
    QCOMPARE (message ("Base only"), QString {"German base"});
    QCOMPARE (QCoreApplication::translate ("L10nQtTest", "Qt message"), QString {"German Qt"});
  }

  void regional_english ()
  {
    L10nLoader loader {application (), QLocale {"hu_HU"}, "en_GB"};
    QCOMPARE (message ("Shared message"), QString {"British interface"});
    QCOMPARE (message ("Hungarian only"), QString {"Hungarian only"});
    QCOMPARE (QCoreApplication::translate ("L10nQtTest", "Qt message"), QString {"Qt message"});
  }

  void local_catalog_override ()
  {
    QVERIFY (QFile::copy (":/Translations/local_de.qm", "wsjtx_de.qm"));
    L10nLoader loader {application (), QLocale {"hu_HU"}, "de"};
    QCOMPARE (message ("Shared message"), QString {"Local German interface"});
    QCOMPARE (message ("Base only"), QString {"German base"});
    QCOMPARE (message ("Hungarian only"), QString {"Hungarian only"});
  }

  void local_regional_catalog_override ()
  {
    QVERIFY (QFile::copy (":/Translations/local_de.qm", "wsjtx_de_AT.qm"));
    QVERIFY (QFile::copy (":/Translations/wsjtx_hu.qm", "wsjtx_hu.qm"));
    L10nLoader loader {application (), QLocale {"hu_HU"}, "de_AT"};
    QCOMPARE (message ("Shared message"), QString {"Local German interface"});
    QCOMPARE (message ("Base only"), QString {"German base"});
    QCOMPARE (message ("Regional only"), QString {"Austrian region"});
    QCOMPARE (message ("Hungarian only"), QString {"Hungarian only"});
  }

  void missing_language_falls_back_to_source ()
  {
    L10nLoader loader {application (), QLocale {"hu_HU"}, "fr"};
    QCOMPARE (message ("Shared message"), QString {"Shared message"});
    QCOMPARE (QCoreApplication::translate ("L10nQtTest", "Qt message"), QString {"Qt message"});
  }

  void bundled_language_list ()
  {
    QCOMPARE (L10nLoader::available_languages (), QStringList ({"de", "de_AT", "en", "en_GB", "hu"}));
  }
};

QTEST_MAIN (TestL10nLoader)

#include "test_l10n_loader.moc"
