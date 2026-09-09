#include <QtTest>
#include <QCheckBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTemporaryDir>

#include "widgets/activeStations.h"

class TestActiveStations : public QObject
{
  Q_OBJECT

private slots:
  void transitions();
  void clickRouting();
};

void TestActiveStations::transitions()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QSettings settings(directory.filePath("settings.ini"), QSettings::IniFormat);
  ActiveStations widget(&settings, QFont{});
  using Mode = ActiveStations::DisplayMode;
  auto const modes = {Mode::Standard, Mode::Q65, Mode::Q65Pileup, Mode::Fox};
  auto ready = widget.findChild<QCheckBox *>("cbReadyOnly");
  auto wanted = widget.findChild<QCheckBox *>("cbWantedOnly");
  auto label = widget.findChild<QLabel *>("label");
  auto header = widget.findChild<QLabel *>("header_label2");
  QVERIFY(ready && wanted && label && header);

  for (auto previous : modes) {
    for (auto mode : modes) {
      widget.setupUi(previous);
      widget.setupUi(mode);
      bool const fox = mode == Mode::Fox;
      bool const pileup = mode == Mode::Q65Pileup;
      QCOMPARE(ready->isHidden(), fox || pileup);
      QCOMPARE(wanted->isHidden(), pileup);
      QCOMPARE(wanted->text(), fox ? QString("My call only") : QString("Wanted only"));
      QCOMPARE(label->isHidden(), fox || pileup);
      QCOMPARE(label->text(), mode == Mode::Q65 ? QString("QSOs:") : QString("Rate:"));
      QCOMPARE(widget.findChild<QWidget *>("score")->isHidden(), mode != Mode::Standard);
      QCOMPARE(widget.findChild<QWidget *>("sbMaxAge")->isHidden(), fox || pileup);
      QCOMPARE(header->text().contains("Fsked"), mode == Mode::Q65);
      QCOMPARE(header->text().contains("Age(h)"), pileup);
      QCOMPARE(header->text().contains("Message"), fox);
    }
  }
}

void TestActiveStations::clickRouting()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  QSettings settings(directory.filePath("settings.ini"), QSettings::IniFormat);
  ActiveStations widget(&settings, QFont{});
  QSignalSpy station(&widget, SIGNAL(callSandP(int)));
  QSignalSpy hound(&widget, SIGNAL(queueActiveWindowHound(QString)));
  QVERIFY(station.isValid());
  QVERIFY(hound.isValid());
  auto editor = widget.findChild<QPlainTextEdit *>("RecentStationsPlainTextEdit");
  QVERIFY(editor);
  using Mode = ActiveStations::DisplayMode;
  for (auto mode : {Mode::Fox, Mode::Q65, Mode::Q65Pileup, Mode::Standard}) {
    widget.displayRecentStations(mode, " 2. K1ABC FN42\n");
    widget.findChild<QLabel *>("header_label2")->setText("Translated heading");
    widget.findChild<QCheckBox *>("cbWantedOnly")->setText("Translated filter");
    widget.setClickOK(false);
    editor->moveCursor(QTextCursor::Start);
    station.clear();
    hound.clear();
    QVERIFY(QMetaObject::invokeMethod(&widget, "on_textEdit_clicked", Qt::DirectConnection));
    QCOMPARE(station.count() + hound.count(), 0);
    widget.setClickOK(true);
    QVERIFY(QMetaObject::invokeMethod(&widget, "on_textEdit_clicked", Qt::DirectConnection));
    QCOMPARE(hound.count(), mode == Mode::Fox ? 1 : 0);
    QCOMPARE(station.count(), mode == Mode::Fox ? 0 : 1);
    if (mode == Mode::Fox) QCOMPARE(hound.at(0).at(0).toString(), QString(" 2. K1ABC FN42"));
    else QCOMPARE(station.at(0).at(0).toInt(), 2);
  }
}

QTEST_MAIN(TestActiveStations)
#include "test_active_stations.moc"
