#include <QtTest>

#include "Transceiver/HamlibMode.hpp"

class TestHamlibMode : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void fromHamlib_data ()
  {
    QTest::addColumn<rmode_t> ("hamlib_mode");
    QTest::addColumn<Transceiver::MODE> ("transceiver_mode");

    QTest::newRow ("PKTUSB") << rmode_t {RIG_MODE_PKTUSB} << Transceiver::DIG_U;
    QTest::newRow ("USBD1") << rmode_t {RIG_MODE_USBD1} << Transceiver::DIG_U;
    QTest::newRow ("USBD2") << rmode_t {RIG_MODE_USBD2} << Transceiver::DIG_U;
    QTest::newRow ("USBD3") << rmode_t {RIG_MODE_USBD3} << Transceiver::DIG_U;
    QTest::newRow ("PKTLSB") << rmode_t {RIG_MODE_PKTLSB} << Transceiver::DIG_L;
    QTest::newRow ("LSBD1") << rmode_t {RIG_MODE_LSBD1} << Transceiver::DIG_L;
    QTest::newRow ("LSBD2") << rmode_t {RIG_MODE_LSBD2} << Transceiver::DIG_L;
    QTest::newRow ("LSBD3") << rmode_t {RIG_MODE_LSBD3} << Transceiver::DIG_L;
  }

  Q_SLOT void fromHamlib ()
  {
    QFETCH (rmode_t, hamlib_mode);
    QFETCH (Transceiver::MODE, transceiver_mode);

    QCOMPARE (HamlibMode::from_hamlib (hamlib_mode), transceiver_mode);
  }

  Q_SLOT void toHamlib ()
  {
    QCOMPARE (HamlibMode::to_hamlib (Transceiver::DIG_U), RIG_MODE_PKTUSB);
    QCOMPARE (HamlibMode::to_hamlib (Transceiver::DIG_L), RIG_MODE_PKTLSB);
  }

  Q_SLOT void satisfiesRequest_data ()
  {
    QTest::addColumn<rmode_t> ("requested");
    QTest::addColumn<rmode_t> ("current");
    QTest::addColumn<bool> ("satisfied");

    QTest::newRow ("exact-PKTUSB") << rmode_t {RIG_MODE_PKTUSB} << rmode_t {RIG_MODE_PKTUSB} << true;
    QTest::newRow ("PKTUSB-USBD1") << rmode_t {RIG_MODE_PKTUSB} << rmode_t {RIG_MODE_USBD1} << true;
    QTest::newRow ("PKTUSB-USBD2") << rmode_t {RIG_MODE_PKTUSB} << rmode_t {RIG_MODE_USBD2} << true;
    QTest::newRow ("PKTUSB-USBD3") << rmode_t {RIG_MODE_PKTUSB} << rmode_t {RIG_MODE_USBD3} << true;
    QTest::newRow ("PKTLSB-LSBD1") << rmode_t {RIG_MODE_PKTLSB} << rmode_t {RIG_MODE_LSBD1} << true;
    QTest::newRow ("PKTLSB-LSBD2") << rmode_t {RIG_MODE_PKTLSB} << rmode_t {RIG_MODE_LSBD2} << true;
    QTest::newRow ("PKTLSB-LSBD3") << rmode_t {RIG_MODE_PKTLSB} << rmode_t {RIG_MODE_LSBD3} << true;
    QTest::newRow ("PKTUSB-USB") << rmode_t {RIG_MODE_PKTUSB} << rmode_t {RIG_MODE_USB} << false;
    QTest::newRow ("USB-USBD2") << rmode_t {RIG_MODE_USB} << rmode_t {RIG_MODE_USBD2} << false;
    QTest::newRow ("USBD1-USBD2") << rmode_t {RIG_MODE_USBD1} << rmode_t {RIG_MODE_USBD2} << false;
    QTest::newRow ("PKTUSB-LSBD2") << rmode_t {RIG_MODE_PKTUSB} << rmode_t {RIG_MODE_LSBD2} << false;
    QTest::newRow ("PKTLSB-USBD2") << rmode_t {RIG_MODE_PKTLSB} << rmode_t {RIG_MODE_USBD2} << false;
  }

  Q_SLOT void satisfiesRequest ()
  {
    QFETCH (rmode_t, requested);
    QFETCH (rmode_t, current);
    QFETCH (bool, satisfied);

    QCOMPARE (HamlibMode::satisfies_request (requested, current), satisfied);
  }

  Q_SLOT void changeRequired ()
  {
    QVERIFY (!HamlibMode::change_required (Transceiver::UNK, RIG_MODE_USBD2));
    QVERIFY (!HamlibMode::change_required (Transceiver::DIG_U, RIG_MODE_USBD2));
    QVERIFY (HamlibMode::change_required (Transceiver::DIG_U, RIG_MODE_USB));
    QVERIFY (HamlibMode::change_required (Transceiver::USB, RIG_MODE_USBD2));
  }
};

QTEST_MAIN (TestHamlibMode)
#include "test_hamlib_mode.moc"
