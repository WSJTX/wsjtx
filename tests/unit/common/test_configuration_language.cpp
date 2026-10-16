#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

#include "Configuration.hpp"
#include "MetaDataRegistry.hpp"
#include "widgets/itoneAndicw.h"

int volatile itone[MAX_NUM_SYMBOLS] {};
float gran () { return 0; }

class TestConfigurationLanguage final : public QObject
{
  Q_OBJECT

  QTemporaryDir directory_;
  QNetworkAccessManager network_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Configuration> configuration_;
  QDialog * dialog_ {};
  QComboBox * language_ {};

private Q_SLOTS:
  void initTestCase ()
  {
    register_types ();
    QStandardPaths::setTestModeEnabled (true);
    QCoreApplication::setApplicationName ("test_configuration_language");
    QVERIFY (directory_.isValid ());
  }

  void init ()
  {
    settings_.reset (new QSettings {directory_.filePath ("settings.ini"), QSettings::IniFormat});
    settings_->clear ();
    configuration_.reset (new Configuration {&network_, QDir {directory_.path ()}, settings_.get (), nullptr});
    dialog_ = nullptr;
    for (auto widget : QApplication::topLevelWidgets ())
      if (widget->objectName () == "configuration_dialog") dialog_ = qobject_cast<QDialog *> (widget);
    QVERIFY (dialog_);
    language_ = dialog_->findChild<QComboBox *> ("language_combo_box");
    QVERIFY (language_);
    for (auto name : {"sound_input_combo_box", "sound_input_channel_combo_box",
                      "sound_output_combo_box", "sound_output_channel_combo_box"})
      {
        auto combo = dialog_->findChild<QComboBox *> (name);
        QVERIFY (combo);
        if (combo->currentIndex () < 0) combo->addItem ("Test audio");
      }
  }

  void cleanup ()
  {
    configuration_.reset ();
    settings_.reset ();
  }

  void preferenceInitialization ()
  {
    QCOMPARE (language_->currentData ().toString (), QString {});
    configuration_->set_ui_language_preference ("en");
    QCOMPARE (language_->currentData ().toString (), QString {"en"});
    configuration_->set_ui_language_preference ("unavailable");
    QCOMPARE (language_->currentData ().toString (), QString {});
  }

  void cancelDiscardsPendingPreference ()
  {
    configuration_->set_ui_language_preference ("en");
    QSignalSpy changes {configuration_.get (), &Configuration::ui_language_preference_changed};
    language_->setCurrentIndex (0);
    dialog_->reject ();
    QCOMPARE (language_->currentData ().toString (), QString {"en"});
    QCOMPARE (changes.count (), 0);
  }

  void acceptanceEmitsOnlyChangedPreference ()
  {
    QSignalSpy changes {configuration_.get (), &Configuration::ui_language_preference_changed};
    language_->setCurrentIndex (language_->findData (QString {"en"}));
    dialog_->accept ();
    QCOMPARE (changes.count (), 1);
    QCOMPARE (changes.at (0).at (0).toString (), QString {"en"});
    dialog_->accept ();
    QCOMPARE (changes.count (), 1);
    language_->setCurrentIndex (0);
    dialog_->reject ();
    QCOMPARE (language_->currentData ().toString (), QString {"en"});
  }

  void failedValidationDoesNotCommitPreference ()
  {
    configuration_->set_ui_language_preference ("en");
    QSignalSpy changes {configuration_.get (), &Configuration::ui_language_preference_changed};
    language_->setCurrentIndex (0);
    auto latitude = dialog_->findChild<QLineEdit *> ("latitude_line_edit");
    QVERIFY (latitude);
    latitude->setText ("100");
    QTimer::singleShot (0, [] {
        auto modal = qobject_cast<QDialog *> (QApplication::activeModalWidget ());
        if (modal) modal->accept ();
      });
    dialog_->accept ();
    QCOMPARE (changes.count (), 0);
    dialog_->reject ();
    QCOMPARE (language_->currentData ().toString (), QString {"en"});
  }
};

QTEST_MAIN (TestConfigurationLanguage)
#include "test_configuration_language.moc"
