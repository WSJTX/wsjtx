#include <QtTest>
#include <vector>

#include <QTemporaryDir>

#include "qmap/getfile.h"
#include "qmap/qmap_runtime_config.h"
#include "qmap/spectrum_columns.h"

class TestQMapSampleRates final : public QObject
{
  Q_OBJECT

private slots:
  void acceptsOnlyTheSelectableRates_data();
  void acceptsOnlyTheSelectableRates();
  void fftLengthKeepsBinWidthNearThreeHertz_data();
  void fftLengthKeepsBinWidthNearThreeHertz();
  void startsAtTheLegacyRate();
  void bringsASavedSpanWithinTheRateLimit_data();
  void bringsASavedSpanWithinTheRateLimit();
  void deducesFileRateFromSize_data();
  void deducesFileRateFromSize();
  void columnsStayInsideTheSpectrum();
  void columnsTagStrongSignals();
};

void TestQMapSampleRates::acceptsOnlyTheSelectableRates_data()
{
  QTest::addColumn<int> ("rate");
  QTest::addColumn<bool> ("supported");
  QTest::newRow ("96 kHz") << 96000 << true;
  QTest::newRow ("128 kHz") << 128000 << true;
  QTest::newRow ("192 kHz") << 192000 << true;
  QTest::newRow ("256 kHz") << 256000 << true;
  QTest::newRow ("48 kHz") << 48000 << false;
  QTest::newRow ("95238 Hz") << 95238 << false;
  QTest::newRow ("384 kHz") << 384000 << false;
  QTest::newRow ("zero") << 0 << false;
}

void TestQMapSampleRates::acceptsOnlyTheSelectableRates()
{
  QFETCH (int, rate);
  QFETCH (bool, supported);
  QCOMPARE (qmap_runtime::isSupportedRate (rate), supported);
}

void TestQMapSampleRates::fftLengthKeepsBinWidthNearThreeHertz_data()
{
  QTest::addColumn<int> ("rate");
  QTest::addColumn<int> ("nfft");
  QTest::newRow ("96 kHz") << 96000 << 32768;
  QTest::newRow ("128 kHz") << 128000 << 65536;
  QTest::newRow ("192 kHz") << 192000 << 65536;
  QTest::newRow ("256 kHz") << 256000 << 131072;
}

void TestQMapSampleRates::fftLengthKeepsBinWidthNearThreeHertz()
{
  QFETCH (int, rate);
  QFETCH (int, nfft);
  QCOMPARE (qmap_runtime::nfftForRate (rate), nfft);
  QVERIFY (nfft <= qmap_runtime::MAX_NFFT);
  QVERIFY (rate <= qmap_runtime::MAX_RATE_HZ);
  double const binHz = double (rate) / nfft;
  QVERIFY2 (binHz > 1.9 && binHz < 3.0, "bin width should stay within 2-3 Hz");
}

void TestQMapSampleRates::startsAtTheLegacyRate()
{
  // Until main() selects another rate, the decoder sizes are the original 96 kHz ones.
  QCOMPARE (qmap_runtime::activeRateHz (), 96000);
  QCOMPARE (qmap_runtime::activeNfft (), 32768);
  QCOMPARE (qmap_runtime::activeNsmax (), 60 * 96000);
}

void TestQMapSampleRates::bringsASavedSpanWithinTheRateLimit_data()
{
  QTest::addColumn<int> ("savedSpan");
  QTest::addColumn<int> ("rate");
  QTest::addColumn<int> ("span");
  // The waterfall span is saved in kHz and read back at the next start, possibly at another rate.
  QTest::newRow ("saved at 192 kHz, now 96 kHz") << 180 << 96000 << 90;
  QTest::newRow ("saved at 256 kHz, now 128 kHz") << 240 << 128000 << 120;
  QTest::newRow ("still fits") << 60 << 96000 << 60;
  QTest::newRow ("widest at 192 kHz") << 180 << 192000 << 180;
  QTest::newRow ("widest at 256 kHz") << 240 << 256000 << 240;
  QTest::newRow ("too narrow") << 1 << 96000 << qmap_runtime::MIN_SPAN_KHZ;
  QTest::newRow ("nonsense") << 100000 << 192000 << 180;
}

void TestQMapSampleRates::bringsASavedSpanWithinTheRateLimit()
{
  QFETCH (int, savedSpan);
  QFETCH (int, rate);
  QFETCH (int, span);
  QCOMPARE (qmap_runtime::clampSpanKHz (savedSpan, rate), span);
}

void TestQMapSampleRates::deducesFileRateFromSize_data()
{
  QTest::addColumn<qint64> ("size");
  QTest::addColumn<int> ("rate");
  // An .iq file is 8 bytes of centre frequency, 60 s (older files: 56 s) of 4-byte I/Q samples,
  // and sometimes two 4-byte Tx counters.
  for (int rate : {96000, 128000, 192000, 256000}) {
    for (qint64 seconds : {60, 56}) {
      qint64 const body = 8 + 4 * seconds * rate;
      QString const tag = QString::number (rate) + " Hz, " + QString::number (seconds) + " s";
      QTest::newRow (qPrintable (tag)) << body << rate;
      QTest::newRow (qPrintable (tag + ", with Tx counters")) << body + 8 << rate;
    }
  }
  QTest::newRow ("a 57 s file fits no rate") << (8 + 57 * 4LL * 96000) << 0;
  QTest::newRow ("48 kHz, 60 s") << (8 + 240LL * 48000) << 0;
  QTest::newRow ("empty") << qint64 (0) << 0;
}

void TestQMapSampleRates::deducesFileRateFromSize()
{
  QFETCH (qint64, size);
  QFETCH (int, rate);
  QTemporaryDir dir;
  QVERIFY (dir.isValid ());
  QString const name = dir.filePath ("000001_1200.iq");
  QFile f {name};
  QVERIFY (f.open (QIODevice::WriteOnly));
  QVERIFY (f.resize (size));            // sparse: the content does not matter
  f.close ();
  QCOMPARE (iqFileRate (name), rate);
}

void TestQMapSampleRates::columnsStayInsideTheSpectrum()
{
  // 256 kHz, 240 kHz span across 1000 columns: 123 bins per column, so the old fixed 2048 columns
  // would read ~252k bins from the 131072-bin spectrum.  Sentinels around the arrays catch any read
  // outside them.
  int const nfft = 131072, nbpp = 123, cols = 1000;
  float const sentinel = 777.f;
  std::vector<float> buf (nfft + 2 * 4096, sentinel);
  float * splot = buf.data () + 4096;
  for (int i = 0; i < nfft; ++i) splot[i] = 1.f;
  std::vector<unsigned char> strong (1024 + 8, 1);       // bytes past entry 1023 are tagged strong
  std::fill (strong.begin (), strong.begin () + 1024, 0);

  for (int i0 : {nfft / 2 - cols * nbpp / 2, -50000, nfft - 1000, nfft + 5000}) {
    float swide[2048];
    qmap_runtime::reduceToColumns (splot, nfft, i0, nbpp, cols, strong.data (), swide, 2048);
    for (int j = 0; j < 2048; ++j) {
      QVERIFY2 (swide[j] != sentinel && swide[j] != -sentinel, "read outside the spectrum");
      QVERIFY2 (swide[j] >= 0.f, "tagged from beyond lstrong");
      if (j >= cols) QCOMPARE (swide[j], 0.f);
    }
  }
  float swide[2048];
  qmap_runtime::reduceToColumns (splot, nfft, nfft / 2 - cols * nbpp / 2, nbpp, cols,
                                 strong.data (), swide, 2048);
  QCOMPARE (swide[0], 1.f);
  QCOMPARE (swide[cols - 1], 1.f);
}

void TestQMapSampleRates::columnsTagStrongSignals()
{
  int const nfft = 32768;
  std::vector<float> splot (nfft, 2.f);
  std::vector<unsigned char> strong (1024, 0);
  strong[1 + 16000 / 32] = 1;
  float swide[16];
  qmap_runtime::reduceToColumns (splot.data (), nfft, 15990, 10, 6, strong.data (), swide, 16);
  // the column ends at bin 16000 -> entry 501, as do the next three (32 bins per entry)
  for (int j = 0; j < 4; ++j) QCOMPARE (swide[j], -2.f);
  QCOMPARE (swide[4], 2.f);
  QCOMPARE (swide[5], 2.f);
}

QTEST_GUILESS_MAIN (TestQMapSampleRates)

#include "test_qmap_sample_rates.moc"
