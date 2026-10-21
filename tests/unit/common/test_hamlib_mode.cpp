#include "wsjtx_config.h"

#include <memory>
#include <hamlib/rig.h>
#include <QScopeGuard>
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

#if HAVE_HAMLIB_CACHING
  Q_SLOT void uncachedModeAfterQsy_data ()
  {
    QTest::addColumn<rmode_t> ("recalled_mode");
    QTest::addColumn<Transceiver::MODE> ("requested_mode");
    QTest::addColumn<bool> ("change_required");

    QTest::newRow ("USB-recalled-as-LSB") << rmode_t {RIG_MODE_LSB} << Transceiver::USB << true;
    QTest::newRow ("DATA2-recalled") << rmode_t {RIG_MODE_USBD2} << Transceiver::DIG_U << false;
    QTest::newRow ("LSB-DATA3-recalled") << rmode_t {RIG_MODE_LSBD3} << Transceiver::DIG_L << false;
  }

  Q_SLOT void uncachedModeAfterQsy ()
  {
    QFETCH (rmode_t, recalled_mode);
    QFETCH (Transceiver::MODE, requested_mode);
    QFETCH (bool, change_required);

    auto rig = std::unique_ptr<RIG, decltype (&rig_cleanup)> {rig_init (RIG_MODEL_DUMMY), rig_cleanup};
    QVERIFY (rig);
    QCOMPARE (rig_open (rig.get ()), RIG_OK);
    auto const close_rig = qScopeGuard ([&] { rig_close (rig.get ()); });
    QCOMPARE (rig_set_mode (rig.get (), RIG_VFO_A, RIG_MODE_USB, 2400), RIG_OK);
    QCOMPARE (rig_set_cache_timeout_ms (rig.get (), HAMLIB_CACHE_ALL, HAMLIB_CACHE_ALWAYS), RIG_OK);

    // Simulate a band recall at the backend without updating the public mode cache.
    QCOMPARE (rig->caps->set_mode (rig.get (), RIG_VFO_A, recalled_mode, 1800), RIG_OK);
    rmode_t mode;
    pbwidth_t width;
    QCOMPARE (rig_get_mode (rig.get (), RIG_VFO_A, &mode, &width), RIG_OK);
    QCOMPARE (mode, rmode_t {RIG_MODE_USB});

    QCOMPARE (HamlibMode::get_uncached_mode (rig.get (), RIG_VFO_A, &mode, &width), RIG_OK);
    QCOMPARE (mode, recalled_mode);
    QCOMPARE (width, pbwidth_t {1800});
    QCOMPARE (HamlibMode::change_required (requested_mode, mode), change_required);
    QCOMPARE (rig_get_cache_timeout_ms (rig.get (), HAMLIB_CACHE_ALL), HAMLIB_CACHE_ALWAYS);
  }

  Q_SLOT void uncachedModeRestoresTimeoutOnError ()
  {
    auto rig = std::unique_ptr<RIG, decltype (&rig_cleanup)> {rig_init (RIG_MODEL_DUMMY), rig_cleanup};
    QVERIFY (rig);
    QCOMPARE (rig_open (rig.get ()), RIG_OK);
    auto const close_rig = qScopeGuard ([&] { rig_close (rig.get ()); });
    auto const original_caps = rig->caps;
    auto caps = *original_caps;
    caps.get_mode = [] (RIG *, vfo_t, rmode_t *, pbwidth_t *) { return -RIG_EIO; };
    rig->caps = &caps;
    auto const restore_caps = qScopeGuard ([&] { rig->caps = original_caps; });
    QCOMPARE (rig_set_cache_timeout_ms (rig.get (), HAMLIB_CACHE_ALL, 1234), RIG_OK);

    rmode_t mode;
    pbwidth_t width;
    QCOMPARE (HamlibMode::get_uncached_mode (rig.get (), RIG_VFO_A, &mode, &width), -RIG_EIO);
    QCOMPARE (rig_get_cache_timeout_ms (rig.get (), HAMLIB_CACHE_ALL), 1234);
  }
#endif
};

QTEST_MAIN (TestHamlibMode)
#include "test_hamlib_mode.moc"
