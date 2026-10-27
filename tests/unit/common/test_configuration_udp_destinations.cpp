#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTimer>
#include <memory>

#include "Configuration.hpp"
#include "MetaDataRegistry.hpp"
#include "widgets/itoneAndicw.h"
#include "widgets/SettingsDialogLayout.hpp"

int volatile itone[MAX_NUM_SYMBOLS] {};
float gran () { return 0; }

class TestConfigurationUdpDestinations final : public QObject
{
  Q_OBJECT

  QTemporaryDir directory_;
  QNetworkAccessManager network_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Configuration> configuration_;
  QDialog * dialog_ {};
  QLineEdit * primary_ {};
  QLineEdit * secondary_ {};
  QLabel * primary_error_ {};
  QLabel * secondary_error_ {};

  bool create_configuration ()
  {
    configuration_.reset (new Configuration {&network_, QDir {directory_.path ()}, settings_.get (), nullptr});
    for (auto widget : QApplication::topLevelWidgets ())
      if (widget->objectName () == "configuration_dialog") dialog_ = qobject_cast<QDialog *> (widget);
    if (!dialog_) return false;
    primary_ = dialog_->findChild<QLineEdit *> ("udp_server_line_edit");
    secondary_ = dialog_->findChild<QLineEdit *> ("n1mm_server_name_line_edit");
    primary_error_ = dialog_->findChild<QLabel *> ("udp_server_error_label");
    secondary_error_ = dialog_->findChild<QLabel *> ("n1mm_server_error_label");
    if (!primary_ || !secondary_ || !primary_error_ || !secondary_error_) return false;
    for (auto name : {"sound_input_combo_box", "sound_input_channel_combo_box",
                      "sound_output_combo_box", "sound_output_channel_combo_box"})
      {
        auto combo = dialog_->findChild<QComboBox *> (name);
        if (!combo) return false;
        if (combo->currentIndex () < 0) combo->addItem ("Test audio");
      }
    return true;
  }

  static void forbidden_destinations_data ()
  {
    QTest::addColumn<QString> ("destination");
    QTest::newRow ("root-domain") << QString {"pskreporter.info"};
    QTest::newRow ("report-host") << QString {"report.pskreporter.info"};
    QTest::newRow ("subdomain") << QString {"other.pskreporter.info"};
    QTest::newRow ("case-whitespace-dot") << QString {"  REPORT.PSKREPORTER.INFO.  "};
    QTest::newRow ("ipv4") << QString {"15.204.212.142"};
    QTest::newRow ("ipv4-whitespace") << QString {"  15.204.212.142  "};
    QTest::newRow ("mapped-ipv4") << QString {"::ffff:15.204.212.142"};
    QTest::newRow ("mapped-hex") << QString {"::ffff:0fcc:d48e"};
    QTest::newRow ("mapped-expanded") << QString {"0:0:0:0:0:ffff:0fcc:d48e"};
  }

  static QStringList valid_destinations ()
  {
    return {"", "127.0.0.1", "192.168.1.2", "239.255.0.1", "::1", "ff02::1",
            "localhost", "companion.example.org", "notpskreporter.info",
            "pskreporter.info.example.org", "15.204.212.143"};
  }

private Q_SLOTS:
  void initTestCase ()
  {
    register_types ();
    QStandardPaths::setTestModeEnabled (true);
    QCoreApplication::setApplicationName ("test_configuration_udp_destinations");
    QVERIFY (directory_.isValid ());
  }

  void init ()
  {
    settings_.reset (new QSettings {directory_.filePath ("settings.ini"), QSettings::IniFormat});
    settings_->clear ();
    settings_->setValue ("Configuration/UDPServer", "127.0.0.1");
    settings_->setValue ("Configuration/N1MMServer", "127.0.0.1");
    dialog_ = nullptr;
    primary_ = secondary_ = nullptr;
    primary_error_ = secondary_error_ = nullptr;
  }

  void cleanup ()
  {
    configuration_.reset ();
    settings_.reset ();
  }

  void clears_saved_destinations_before_use_data () { forbidden_destinations_data (); }

  void clears_saved_destinations_before_use ()
  {
    QFETCH (QString, destination);
    settings_->setValue ("Configuration/UDPServer", destination);
    settings_->setValue ("Configuration/N1MMServer", destination);
    settings_->setValue ("Configuration/UDPServerPort", 2238);
    settings_->setValue ("Configuration/N1MMServerPort", 2334);
    settings_->setValue ("Configuration/BroadcastToN1MM", true);
    settings_->setValue ("Configuration/PSKReporter", true);
    QVERIFY (create_configuration ());
    QVERIFY (configuration_->udp_server_name ().isEmpty ());
    QVERIFY (configuration_->n1mm_server_name ().isEmpty ());
    QVERIFY (primary_->text ().isEmpty ());
    QVERIFY (secondary_->text ().isEmpty ());
    QVERIFY (primary_error_->isHidden ());
    QVERIFY (secondary_error_->isHidden ());
    QVERIFY (!QApplication::activeModalWidget ());
    for (auto key : {"Configuration/UDPServer", "Configuration/N1MMServer"})
      {
        QVERIFY (settings_->contains (key));
        QVERIFY (settings_->value (key).toString ().isEmpty ());
      }
    QCOMPARE (configuration_->udp_server_port (), quint16 {2238});
    QCOMPARE (configuration_->n1mm_server_port (), quint16 {2334});
    QVERIFY (configuration_->broadcast_to_n1mm ());
    QCOMPARE (settings_->value ("Configuration/PSKReporter").toBool (), true);
    configuration_.reset ();
    QVERIFY (create_configuration ());
    QVERIFY (configuration_->udp_server_name ().isEmpty ());
    QVERIFY (configuration_->n1mm_server_name ().isEmpty ());
  }

  void preserves_valid_saved_destinations_data ()
  {
    QTest::addColumn<QString> ("destination");
    for (auto const& destination : valid_destinations ())
      QTest::newRow (destination.isEmpty () ? "empty" : qPrintable (destination)) << destination;
  }

  void preserves_valid_saved_destinations ()
  {
    QFETCH (QString, destination);
    settings_->setValue ("Configuration/UDPServer", destination);
    settings_->setValue ("Configuration/N1MMServer", destination);
    QVERIFY (create_configuration ());
    QCOMPARE (configuration_->udp_server_name (), destination);
    QCOMPARE (configuration_->n1mm_server_name (), destination);
    QCOMPARE (primary_->text (), destination);
    QCOMPARE (secondary_->text (), destination);
    QCOMPARE (settings_->value ("Configuration/UDPServer").toString (), destination);
    QCOMPARE (settings_->value ("Configuration/N1MMServer").toString (), destination);
  }

  void cleans_only_forbidden_saved_destination_data ()
  {
    QTest::addColumn<bool> ("primary_invalid");
    QTest::newRow ("primary-only") << true;
    QTest::newRow ("secondary-only") << false;
  }

  void cleans_only_forbidden_saved_destination ()
  {
    QFETCH (bool, primary_invalid);
    auto const primary = primary_invalid ? QString {} : QString {"192.0.2.25"};
    auto const secondary = primary_invalid ? QString {"192.0.2.26"} : QString {};
    settings_->setValue ("Configuration/UDPServer", primary_invalid ? "report.pskreporter.info" : "192.0.2.25");
    settings_->setValue ("Configuration/N1MMServer", primary_invalid ? "192.0.2.26" : "15.204.212.142");
    settings_->setValue ("Configuration/UDPServerPort", 2238);
    settings_->setValue ("Configuration/N1MMServerPort", 2334);
    settings_->setValue ("Configuration/BroadcastToN1MM", true);
    settings_->setValue ("Configuration/AcceptUDPRequests", true);
    settings_->setValue ("Configuration/PSKReporter", true);
    settings_->setValue ("Configuration/UnrelatedSetting", "preserved");
    settings_->sync ();
    QCOMPARE (settings_->status (), QSettings::NoError);

    QVERIFY (create_configuration ());
    QCOMPARE (configuration_->udp_server_name (), primary);
    QCOMPARE (configuration_->n1mm_server_name (), secondary);
    QCOMPARE (configuration_->udp_server_port (), quint16 {2238});
    QCOMPARE (configuration_->n1mm_server_port (), quint16 {2334});
    QVERIFY (configuration_->broadcast_to_n1mm ());

    // Read a separate file to avoid QSettings' shared in-process cache.
    QCoreApplication::sendPostedEvents ();
    QApplication::processEvents ();
    QTemporaryDir snapshot_directory;
    QVERIFY (snapshot_directory.isValid ());
    auto const snapshot_path = snapshot_directory.filePath ("settings.ini");
    QVERIFY (QFile::copy (settings_->fileName (), snapshot_path));
    QSettings persisted {snapshot_path, QSettings::IniFormat};
    QVERIFY (persisted.contains ("Configuration/UDPServer"));
    QVERIFY (persisted.contains ("Configuration/N1MMServer"));
    QCOMPARE (persisted.value ("Configuration/UDPServer").toString (), primary);
    QCOMPARE (persisted.value ("Configuration/N1MMServer").toString (), secondary);
    QCOMPARE (persisted.value ("Configuration/UDPServerPort").toInt (), 2238);
    QCOMPARE (persisted.value ("Configuration/N1MMServerPort").toInt (), 2334);
    QVERIFY (persisted.value ("Configuration/BroadcastToN1MM").toBool ());
    QVERIFY (persisted.value ("Configuration/AcceptUDPRequests").toBool ());
    QVERIFY (persisted.value ("Configuration/PSKReporter").toBool ());
    QCOMPARE (persisted.value ("Configuration/UnrelatedSetting").toString (), QString {"preserved"});
  }

  void rejects_forbidden_edits_data () { forbidden_destinations_data (); }

  void rejects_forbidden_edits ()
  {
    QFETCH (QString, destination);
    QVERIFY (create_configuration ());
    auto enabled = dialog_->findChild<QCheckBox *> ("enable_n1mm_broadcast_check_box");
    QVERIFY (enabled);
    QVERIFY (!enabled->isChecked ());
    QSignalSpy changes {configuration_.get (), &Configuration::udp_server_changed};
    for (auto field : {primary_, secondary_})
      {
        auto error = field == primary_ ? primary_error_ : secondary_error_;
        field->setText (destination);
        dialog_->accept ();
        QCOMPARE (dialog_->result (), int {QDialog::Rejected});
        QVERIFY (!error->isHidden ());
        QVERIFY (!error->text ().isEmpty ());
        QCOMPARE (configuration_->udp_server_name (), QString {"127.0.0.1"});
        QCOMPARE (configuration_->n1mm_server_name (), QString {"127.0.0.1"});
        QCOMPARE (changes.count (), 0);
        QCOMPARE (settings_->value ("Configuration/UDPServer").toString (), QString {"127.0.0.1"});
        QCOMPARE (settings_->value ("Configuration/N1MMServer").toString (), QString {"127.0.0.1"});
        dialog_->reject ();
        QCOMPARE (field->text (), QString {"127.0.0.1"});
        QVERIFY (error->isHidden ());
      }
  }

  void accepts_valid_edits ()
  {
    QVERIFY (create_configuration ());
    for (auto const& destination : valid_destinations ())
      {
        qInfo () << "Valid destination:" << destination;
        dialog_->setResult (QDialog::Rejected);
        primary_->setText (destination);
        secondary_->setText (destination);
        dialog_->accept ();
        QCOMPARE (dialog_->result (), int {QDialog::Accepted});
        QCOMPARE (primary_->text (), destination);
        QCOMPARE (secondary_->text (), destination);
        QCOMPARE (configuration_->udp_server_name (), destination);
        QCOMPARE (configuration_->n1mm_server_name (), destination);
        QCOMPARE (settings_->value ("Configuration/UDPServer").toString (), destination);
        QCOMPARE (settings_->value ("Configuration/N1MMServer").toString (), destination);
        QVERIFY (primary_error_->isHidden ());
        QVERIFY (secondary_error_->isHidden ());
      }
  }

  void visible_validation_keeps_error_and_focus_data ()
  {
    QTest::addColumn<bool> ("primary_invalid");
    QTest::newRow ("primary") << true;
    QTest::newRow ("secondary") << false;
  }

  void visible_validation_keeps_error_and_focus ()
  {
    QFETCH (bool, primary_invalid);
    QVERIFY (create_configuration ());
    auto tabs = dialog_->findChild<QTabWidget *> ("configuration_tabs");
    auto reporting = dialog_->findChild<QWidget *> ("reporting_tab");
    auto buttons = dialog_->findChild<QDialogButtonBox *> ("configuration_dialog_button_box");
    QVERIFY (tabs);
    QVERIFY (reporting);
    QVERIFY (buttons);
    auto ok = buttons->button (QDialogButtonBox::Ok);
    QVERIFY (ok);
    auto field = primary_invalid ? primary_ : secondary_;
    auto error = primary_invalid ? primary_error_ : secondary_error_;
    auto scroll = SettingsDialogLayout::pageScrollArea (reporting);
    QVERIFY (scroll);

    bool unexpected_modal = false;
    QTimer modal_watchdog;
    connect (&modal_watchdog, &QTimer::timeout, this, [&] {
        auto modal = qobject_cast<QDialog *> (QApplication::activeModalWidget ());
        if (modal && modal != dialog_)
          {
            unexpected_modal = true;
            modal->reject ();
          }
      });
    modal_watchdog.start (10);

    tabs->setCurrentWidget (reporting);
    dialog_->resize (683, 439);
    dialog_->show ();
    dialog_->activateWindow ();
    field->setFocus ();
    QTRY_COMPARE (QApplication::focusWidget (), field);
    field->selectAll ();
    QTest::keyClicks (field, "report.pskreporter.info");
    QVERIFY (error->isVisible ());
    QCOMPARE (QApplication::focusWidget (), field);
    ok->setFocus ();
    QTRY_VERIFY (error->isVisible ());
    QVERIFY (!unexpected_modal);

    tabs->setCurrentIndex (0);
    QTest::mouseClick (ok, Qt::LeftButton);
    QTRY_COMPARE (tabs->currentWidget (), reporting);
    QTRY_COMPARE (QApplication::focusWidget (), field);
    QVERIFY (dialog_->isVisible ());
    QCOMPARE (dialog_->result (), int {QDialog::Rejected});
    QVERIFY (error->isVisible ());
    auto error_is_fully_visible = [&] {
        return scroll->viewport ()->rect ().contains (
          QRect {error->mapTo (scroll->viewport (), QPoint {}), error->size ()});
      };
    auto visibility_details = [&] {
      QString details;
      QDebug {&details} << "viewport" << scroll->viewport ()->rect ()
                       << "error" << QRect {error->mapTo (scroll->viewport (), QPoint {}), error->size ()}
                       << "required height" << error->heightForWidth (error->width ())
                       << "horizontal scroll" << scroll->horizontalScrollBar ()->value ()
                       << scroll->horizontalScrollBar ()->maximum ()
                       << "vertical scroll" << scroll->verticalScrollBar ()->value ()
                       << scroll->verticalScrollBar ()->maximum ();
      return details;
    };
    QTRY_VERIFY2 (error_is_fully_visible (), qPrintable (visibility_details ()));
    QVERIFY2 (error->height () >= error->heightForWidth (error->width ()), qPrintable (visibility_details ()));
    QVERIFY (!unexpected_modal);
    QVERIFY (!QApplication::activeModalWidget ());
    QVERIFY (!field->accessibleDescription ().isEmpty ());
    dialog_->reject ();
    QVERIFY (!dialog_->isVisible ());
    QVERIFY (!unexpected_modal);
    QCOMPARE (configuration_->udp_server_name (), QString {"127.0.0.1"});
    QCOMPARE (configuration_->n1mm_server_name (), QString {"127.0.0.1"});
  }

  void correction_clears_error_and_commits ()
  {
    QVERIFY (create_configuration ());
    for (auto field : {primary_, secondary_})
      {
        auto error = field == primary_ ? primary_error_ : secondary_error_;
        field->setText ("report.pskreporter.info");
        QVERIFY (!error->isHidden ());
        field->clear ();
        QVERIFY (error->isHidden ());
      }
    dialog_->accept ();
    QCOMPARE (dialog_->result (), int {QDialog::Accepted});
    QVERIFY (configuration_->udp_server_name ().isEmpty ());
    QVERIFY (configuration_->n1mm_server_name ().isEmpty ());
    QVERIFY (settings_->value ("Configuration/UDPServer").toString ().isEmpty ());
    QVERIFY (settings_->value ("Configuration/N1MMServer").toString ().isEmpty ());
  }

  void cancel_discards_forbidden_edits ()
  {
    QVERIFY (create_configuration ());
    primary_->setText ("report.pskreporter.info");
    secondary_->setText ("15.204.212.142");
    dialog_->reject ();
    QCOMPARE (dialog_->result (), int {QDialog::Rejected});
    QCOMPARE (configuration_->udp_server_name (), QString {"127.0.0.1"});
    QCOMPARE (configuration_->n1mm_server_name (), QString {"127.0.0.1"});
    QCOMPARE (primary_->text (), QString {"127.0.0.1"});
    QCOMPARE (secondary_->text (), QString {"127.0.0.1"});
    QVERIFY (primary_error_->isHidden ());
    QVERIFY (secondary_error_->isHidden ());
  }
};

QTEST_MAIN (TestConfigurationUdpDestinations)
#include "test_configuration_udp_destinations.moc"
