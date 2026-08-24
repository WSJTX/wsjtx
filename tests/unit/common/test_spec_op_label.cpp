#include <QtTest>

#include "widgets/SpecOpLabel.h"

Q_DECLARE_METATYPE (SpecialOperatingActivity)

class TestSpecOpLabel : public QObject
{
  Q_OBJECT

private slots:
  void formatsLabel_data ();
  void formatsLabel ();
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
  QTest::newRow ("fox") << SpecialOperatingActivity::FOX << false << QString {};
  QTest::newRow ("hound") << SpecialOperatingActivity::HOUND << false << QString {};
}

void TestSpecOpLabel::formatsLabel ()
{
  QFETCH (SpecialOperatingActivity, specialOperation);
  QFETCH (bool, ncccSprint);
  QFETCH (QString, expected);

  QCOMPARE (SpecOpLabel::label (specialOperation, ncccSprint), expected);
}

QTEST_MAIN (TestSpecOpLabel)
#include "test_spec_op_label.moc"
