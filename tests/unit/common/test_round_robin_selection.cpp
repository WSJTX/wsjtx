#include <QtTest>
#include <QComboBox>
#include <QLineEdit>

#include "widgets/RoundRobinSelection.hpp"

namespace
{
  QString selectedValue (QComboBox const& combo)
  {
    return QString::fromStdString (
      BeaconTx::formatRoundRobinPolicy (RoundRobinSelection::policy (combo)));
  }
}

class TestRoundRobinSelection final : public QObject
{
  Q_OBJECT

private slots:
  void presetsHaveCanonicalIdentity ()
  {
    QComboBox combo;
    combo.setEditable (true);
    RoundRobinSelection::initialize (combo, "Localized random");
    QCOMPARE (combo.count (), 21);
    QCOMPARE (combo.currentText (), QString {"Localized random"});
    QCOMPARE (selectedValue (combo), QString {"random"});
    for (int index = 1; index < combo.count (); ++index)
      {
        combo.setCurrentIndex (index);
        QCOMPARE (selectedValue (combo), combo.currentData ().toString ());
        QVERIFY (RoundRobinSelection::policy (combo).kind == BeaconTx::RoundRobinPolicy::Kind::Fixed);
      }
    combo.setItemText (1, "Localized fixed preset");
    combo.setCurrentIndex (1);
    QCOMPARE (selectedValue (combo), QString {"1/2"});
  }

  void editorOverridesSelectedPreset ()
  {
    QComboBox combo;
    combo.setEditable (true);
    RoundRobinSelection::initialize (combo, "Random");
    QString observed;
    connect (&combo, &QComboBox::currentTextChanged, this,
             [&] {observed = selectedValue (combo);});
    combo.setEditText ("7/12");
    QCOMPARE (combo.currentIndex (), 0);
    QCOMPARE (observed, QString {"7/12"});
    combo.setCurrentIndex (1);
    combo.setEditText ("2/4");
    QCOMPARE (observed, QString {"2/4"});
    RoundRobinSelection::setPolicy (combo, BeaconTx::RoundRobinPolicy::fixed (0, 2));
    QCOMPARE (combo.currentText (), QString {"1/2"});
    QCOMPARE (observed, QString {"1/2"});
    combo.setEditText ("invalid");
    QCOMPARE (observed, QString {"random"});
    combo.setCurrentIndex (0);
    QCOMPARE (observed, QString {"random"});
  }

  void committedCustomEntryDoesNotNeedPresetData ()
  {
    QComboBox combo;
    combo.setEditable (true);
    RoundRobinSelection::initialize (combo, "Random");
    combo.setEditText ("7/12");
    QTest::keyClick (combo.lineEdit (), Qt::Key_Return);
    QVERIFY (combo.currentIndex () > 0);
    QVERIFY (!combo.currentData ().isValid ());
    QCOMPARE (selectedValue (combo), QString {"7/12"});
  }

  void selectionRestoration_data ()
  {
    QTest::addColumn<QString> ("stored");
    QTest::addColumn<QString> ("expected");
    QTest::newRow ("custom") << "7/12" << "7/12";
    QTest::newRow ("preset") << "2/4" << "2/4";
    QTest::newRow ("whitespace") << " 2 / 4 " << "2/4";
    QTest::newRow ("canonical-random") << "random" << "random";
    QTest::newRow ("legacy-random-label") << "Localized random" << "random";
    QTest::newRow ("invalid") << "13/12" << "random";
  }

  void selectionRestoration ()
  {
    QFETCH (QString, stored);
    QFETCH (QString, expected);
    QComboBox combo;
    combo.setEditable (true);
    RoundRobinSelection::initialize (combo, "Localized random");
    RoundRobinSelection::setPolicy (combo, BeaconTx::parseRoundRobinPolicy (
      stored.toStdString ()));
    QCOMPARE (selectedValue (combo), expected);
    QCOMPARE (combo.currentText (), expected == "random" ? QString {"Localized random"} : expected);
  }
};

QTEST_MAIN (TestRoundRobinSelection)

#include "test_round_robin_selection.moc"
