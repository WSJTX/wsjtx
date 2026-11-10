#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <memory>

#include "Configuration.hpp"
#include "MetaDataRegistry.hpp"
#include "models/DecodeHighlightingModel.hpp"
#include "models/IARURegions.hpp"
#include "widgets/itoneAndicw.h"

int volatile itone[MAX_NUM_SYMBOLS] {};
float gran () { return 0; }

class TestConfigurationPreferences final : public QObject
{
  Q_OBJECT

  QTemporaryDir directory_;
  QNetworkAccessManager network_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Configuration> configuration_;
  QDialog * dialog_ {};
  QComboBox * language_ {};

  void loadConfiguration ()
  {
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

private Q_SLOTS:
  void initTestCase ()
  {
    register_types ();
    QStandardPaths::setTestModeEnabled (true);
    QCoreApplication::setApplicationName ("test_configuration_preferences");
    QVERIFY (directory_.isValid ());
  }

  void init ()
  {
    settings_.reset (new QSettings {directory_.filePath ("settings.ini"), QSettings::IniFormat});
    settings_->clear ();
    loadConfiguration ();
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

  void bestSpDefaultsAndAcceptCancel ()
  {
    QVERIFY (configuration_->best_sp_new_dxcc ());
    QVERIFY (configuration_->best_sp_new_call_on_band ());
    auto dxcc = dialog_->findChild<QCheckBox *> ("best_sp_new_dxcc_check_box");
    auto call = dialog_->findChild<QCheckBox *> ("best_sp_new_call_on_band_check_box");
    QVERIFY (dxcc && call);
    dxcc->setChecked (false);
    call->setChecked (false);
    dialog_->reject ();
    QVERIFY (configuration_->best_sp_new_dxcc ());
    QVERIFY (configuration_->best_sp_new_call_on_band ());
    QVERIFY (dxcc->isChecked () && call->isChecked ());
    dxcc->setChecked (false);
    call->setChecked (false);
    dialog_->accept ();
    QVERIFY (!configuration_->best_sp_new_dxcc ());
    QVERIFY (!configuration_->best_sp_new_call_on_band ());
    QVERIFY (!settings_->value ("Configuration/BestSPNewDXCC").toBool ());
    QVERIFY (!settings_->value ("Configuration/BestSPNewCallOnBand").toBool ());
  }

  void bestSpPartialProfileWithoutHighlightingUsesNewDefaults ()
  {
    configuration_.reset ();
    settings_->clear ();
    settings_->setValue ("Configuration/MyCall", QStringLiteral ("K1ABC"));
    QVERIFY (!settings_->contains ("Configuration/DecodeHighlighting"));
    loadConfiguration ();
    QVERIFY (configuration_->best_sp_new_dxcc ());
    QVERIFY (configuration_->best_sp_new_call_on_band ());
    QVERIFY (settings_->value ("Configuration/BestSPNewDXCC").toBool ());
    QVERIFY (settings_->value ("Configuration/BestSPNewCallOnBand").toBool ());
  }

  void bestSpMigrationIsIndependentOfLaterColors_data ()
  {
    QTest::addColumn<bool> ("dxcc");
    QTest::addColumn<bool> ("call");
    for (bool dxcc : {false, true})
      for (bool call : {false, true})
        QTest::newRow (qPrintable (QString {"%1-%2"}.arg (dxcc).arg (call))) << dxcc << call;
  }

  void bestSpMigrationIsIndependentOfLaterColors ()
  {
    QFETCH (bool, dxcc);
    QFETCH (bool, call);
    configuration_.reset ();
    settings_->clear ();
    auto highlights = DecodeHighlightingModel::default_items ();
    for (auto& item : highlights)
      {
        if (item.type_ == DecodeHighlightingModel::Highlight::DXCC) item.enabled_ = dxcc;
        if (item.type_ == DecodeHighlightingModel::Highlight::CallBand) item.enabled_ = call;
      }
    settings_->setValue ("Configuration/DecodeHighlighting", QVariant::fromValue (highlights));
    loadConfiguration ();
    QCOMPARE (configuration_->best_sp_new_dxcc (), dxcc);
    QCOMPARE (configuration_->best_sp_new_call_on_band (), call);
    QCOMPARE (settings_->value ("Configuration/BestSPNewDXCC").toBool (), dxcc);
    QCOMPARE (settings_->value ("Configuration/BestSPNewCallOnBand").toBool (), call);
    configuration_.reset ();
    for (auto& item : highlights) item.enabled_ = !item.enabled_;
    settings_->setValue ("Configuration/DecodeHighlighting", QVariant::fromValue (highlights));
    settings_->setValue ("Configuration/DXCCEntity", false);
    loadConfiguration ();
    QCOMPARE (configuration_->best_sp_new_dxcc (), dxcc);
    QCOMPARE (configuration_->best_sp_new_call_on_band (), call);
  }

  void translatedRegionAcceptancePreservesKeys ()
  {
    class Translator final : public QTranslator
    {
      QString translate (char const * context, char const *, char const *, int) const override
      {
        return QByteArray {context} == "IARURegions" ? QStringLiteral ("Duplicate label") : QString {};
      }
    } translator;
    configuration_.reset ();
    QCoreApplication::installTranslator (&translator);
    loadConfiguration ();
    auto combo = dialog_->findChild<QComboBox *> ("region_combo_box");
    QVERIFY (combo);
    QCOMPARE (combo->itemText (1), combo->itemText (2));
    auto const r2 = combo->findData (QVariant::fromValue (IARURegions::R2), Qt::EditRole);
    combo->setCurrentIndex (r2);
    dialog_->accept ();
    QCOMPARE (configuration_->region (), IARURegions::R2);
    dialog_->accept ();
    QCOMPARE (configuration_->region (), IARURegions::R2);
    combo->setCurrentIndex (0);
    dialog_->reject ();
    QCOMPARE (configuration_->region (), IARURegions::R2);
    QCOMPARE (combo->currentIndex (), r2);
    QCoreApplication::removeTranslator (&translator);
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

QTEST_MAIN (TestConfigurationPreferences)
#include "test_configuration_preferences.moc"
