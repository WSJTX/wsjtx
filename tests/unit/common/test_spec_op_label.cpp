#include <QtTest>

#include "widgets/SpecOpLabel.h"

Q_DECLARE_METATYPE (SpecialOperatingActivity)

class TestSpecOpLabel : public QObject
{
  Q_OBJECT

private slots:
  void formatsLabel_data ();
  void formatsLabel ();
  void superFoxPreference ();
};

void TestSpecOpLabel::formatsLabel_data ()
{
  QTest::addColumn<SpecialOperatingActivity> ("specialOperation");
  QTest::addColumn<bool> ("ncccSprint");
  QTest::addColumn<QString> ("expected");

  QTest::newRow ("na-vhf") << SpecialOperatingActivity::NA_VHF << false << QString {"NA VHF"};
  QTest::newRow ("nccc-sprint") << SpecialOperatingActivity::NA_VHF << true << QString {"NCCC Sprint"};
  QTest::newRow ("eu-vhf") << SpecialOperatingActivity::EU_VHF << false << QString {"EU VHF"};
  QTest::newRow ("field-day") << SpecialOperatingActivity::FIELD_DAY << false << QString {"Field Day"};
  QTest::newRow ("rtty") << SpecialOperatingActivity::RTTY << false << QString {"FT RU"};
  QTest::newRow ("ww-digi") << SpecialOperatingActivity::WW_DIGI << false << QString {"WW Digi"};
  QTest::newRow ("arrl-digi") << SpecialOperatingActivity::ARRL_DIGI << false << QString {"ARRL Digi"};
  QTest::newRow ("q65-pileup") << SpecialOperatingActivity::Q65_PILEUP << false << QString {"Q65 Pileup"};
  QTest::newRow ("nccc-ignored-outside-na-vhf") << SpecialOperatingActivity::EU_VHF << true << QString {"EU VHF"};
  QTest::newRow ("none") << SpecialOperatingActivity::NONE << false << QString {};
  QTest::newRow ("fox") << SpecialOperatingActivity::FOX << false << QString {"Fox"};
  QTest::newRow ("hound") << SpecialOperatingActivity::HOUND << false << QString {"Hound"};
}

void TestSpecOpLabel::formatsLabel ()
{
  QFETCH (SpecialOperatingActivity, specialOperation);
  QFETCH (bool, ncccSprint);
  QFETCH (QString, expected);

  QCOMPARE (SpecOpLabel::label (specialOperation, ncccSprint), expected);
}

void TestSpecOpLabel::superFoxPreference ()
{
  QCOMPARE(SpecOpLabel::label(SpecialOperatingActivity::FOX, false, true), QString("Super Fox"));
  QCOMPARE(SpecOpLabel::label(SpecialOperatingActivity::HOUND, false, true), QString("Super Hound"));
  QCOMPARE(SpecOpLabel::label(SpecialOperatingActivity::NONE, false, true), QString{});
  QCOMPARE(SpecOpLabel::label(SpecialOperatingActivity::NA_VHF, true, true), QString("NCCC Sprint"));
}

QTEST_MAIN (TestSpecOpLabel)
#include "test_spec_op_label.moc"
