#include <QtTest>
#include <cmath>
#include <memory>
#include <vector>

#include "commons.h"
#include "widgets/JttySpectrum.hpp"

extern "C" {
  void symspec_ (struct dec_data *, int *, int *, int *, bool *, int *,
                 float *, float *, float *, int *, int *, float *, int *);
}

namespace {
std::vector<qint16> fixture (int count)
{
  std::vector<qint16> result (count);
  quint32 random = 17;
  for (int i = 0; i < count; ++i) {
    random = 1664525u * random + 1013904223u;
    result[i] = 1000 * std::sin (2 * 3.141592653589793 * 1234 * i / 12000)
        + int (random >> 24) - 128;
  }
  return result;
}

constexpr float spectrumToleranceScale = 5.e-4f;

float spectrumTolerance (float actual, float expected)
{
  auto const magnitude = std::max ({1.f, std::abs (actual), std::abs (expected)});
  return spectrumToleranceScale * magnitude;
}

bool spectrumNearlyEqual (float actual, float expected)
{
  return std::isfinite (actual) && std::isfinite (expected)
      && std::abs (actual - expected) <= spectrumTolerance (actual, expected);
}

QString spectrumMismatch (char const * series, int bin, float actual, float expected)
{
  return QStringLiteral ("%1[%2]: actual=%3 expected=%4 difference=%5 tolerance=%6")
    .arg (QString::fromLatin1 (series)).arg (bin)
    .arg (actual, 0, 'g', 9).arg (expected, 0, 'g', 9)
    .arg (std::abs (actual - expected), 0, 'g', 9)
    .arg (spectrumTolerance (actual, expected), 0, 'g', 9);
}

QString spectrumFrameMismatch (JttySpectrumFrame const& actual, JttySpectrumFrame const& expected)
{
  auto const actualSeries = {&actual.bins, &actual.cumulative, &actual.linearAverage};
  auto const expectedSeries = {&expected.bins, &expected.cumulative, &expected.linearAverage};
  char const * names[] = {"bins", "cumulative", "linearAverage"};
  auto reference = expectedSeries.begin ();
  int series = 0;
  for (auto const * values : actualSeries) {
    for (int bin = 0; bin < JttySpectrumFrame::BinCount; ++bin) {
      if (!spectrumNearlyEqual ((*values)[bin], (**reference)[bin])) {
        return spectrumMismatch (names[series], bin, (*values)[bin], (**reference)[bin]);
      }
    }
    ++reference;
    ++series;
  }
  return {};
}
}

class TestJttySpectrum final : public QObject
{
  Q_OBJECT
private slots:
  void agreesWithTimedSpectrumMath_data ()
  {
    QTest::addColumn<bool> ("windowed");
    QTest::newRow ("rectangular") << false;
    QTest::newRow ("low-sidelobes") << true;
  }

  void agreesWithTimedSpectrumMath ()
  {
    QFETCH (bool, windowed);
    auto shared = std::make_unique<dec_data_t> ();
    auto const pcm = fixture (24 * JttySpectrum::Hop);
    std::copy (pcm.begin (), pcm.end (), shared->d2);
    JttySpectrum spectrum;
    spectrum.begin (1, 0, 0);
    int nsps = 6912;
    int gain = 3;
    int smoothing = 2;
    int symbols = 0;
    int eighths = 0;
    int percentile = 0;
    for (int end = JttySpectrum::Hop; end <= int (pcm.size ()); end += JttySpectrum::Hop) {
      std::array<float, NSMAX> bins {};
      float db = 0, peak = 0, width = 0;
      symspec_ (shared.get (), &end, &nsps, &gain, &windowed, &smoothing,
                &db, bins.data (), &width, &symbols, &eighths, &peak, &percentile);
      bool emitted = false;
      QVERIFY (spectrum.process (end - JttySpectrum::Hop, pcm.data () + end - JttySpectrum::Hop,
          JttySpectrum::Hop, gain, windowed, smoothing, [&] (JttySpectrumFrame const& frame) {
        emitted = true;
        QCOMPARE (frame.row.endSample, qint64 (end));
        QCOMPARE (frame.row.utcEndMs, qint64 (end / 12));
        QVERIFY (std::abs (frame.powerDb - db) < 0.002f);
        QVERIFY (std::abs (frame.maxPowerDb - peak) < 0.002f);
        for (int i = 0; i < NSMAX; ++i) {
          QVERIFY2 (spectrumNearlyEqual (frame.bins[i], bins[i]),
                    qPrintable (spectrumMismatch ("bins", i, frame.bins[i], bins[i])));
          QVERIFY2 (spectrumNearlyEqual (frame.cumulative[i], shared->savg[i]),
                    qPrintable (spectrumMismatch ("cumulative", i, frame.cumulative[i],
                                                  shared->savg[i])));
        }
      }));
      QVERIFY (emitted);
    }
  }

  void packetizationAndLargeCoordinatesPreserveSpectrum ()
  {
    auto const pcm = fixture (181 * 12000);
    qint64 const origin = (qint64 (1) << 33) + 117;
    qint64 const utc = 179000;
    JttySpectrum whole;
    JttySpectrum packets;
    whole.begin (7, origin, utc);
    packets.begin (7, origin, utc);
    QVector<JttySpectrumFrame> expected;
    expected.reserve (int (pcm.size ()) / JttySpectrum::Hop);
    QVERIFY (whole.process (origin, pcm.data (), int (pcm.size ()), 0, true, 1,
        [&] (JttySpectrumFrame const& frame) { expected.append (frame); }));
    int frameIndex = 0;
    int offset = 0;
    while (offset < int (pcm.size ())) {
      int const size = std::min (1 + (offset * 17LL % 4001), qint64 (pcm.size () - offset));
      QVERIFY (packets.process (origin + offset, pcm.data () + offset, size, 0, true, 1,
          [&] (JttySpectrumFrame const& frame) {
        QVERIFY (frameIndex < expected.size ());
        auto const mismatch = spectrumFrameMismatch (frame, expected.at (frameIndex));
        QVERIFY2 (mismatch.isEmpty (), qPrintable (mismatch));
        QCOMPARE (frame.row.reception, quint64 (7));
        QCOMPARE (frame.row.beginSample, origin + qint64 (frameIndex) * JttySpectrum::Hop);
        QCOMPARE (frame.row.utcEndMs, utc + qint64 (frameIndex + 1) * JttySpectrum::Hop / 12);
        ++frameIndex;
      }));
      offset += size;
    }
    QCOMPARE (frameIndex, expected.size ());
    QVERIFY (!packets.process (origin, pcm.data (), 100, 0, true, 1, [] (JttySpectrumFrame const&) {}));
  }

  void explicitReceptionResetsTailAndAverage ()
  {
    auto const pcm = fixture (12 * JttySpectrum::Hop);
    JttySpectrum reused;
    reused.begin (1, 0, 0);
    QVERIFY (reused.process (0, pcm.data (), int (pcm.size ()) - 13, 0, true, 1,
                            [] (JttySpectrumFrame const&) {}));
    reused.begin (2, 999, 15000);
    JttySpectrum fresh;
    fresh.begin (2, 999, 15000);
    QVector<JttySpectrumFrame> expected;
    expected.reserve (int (pcm.size ()) / JttySpectrum::Hop);
    QVERIFY (fresh.process (999, pcm.data (), int (pcm.size ()), 0, true, 1,
        [&] (JttySpectrumFrame const& frame) { expected.append (frame); }));
    int index = 0;
    QVERIFY (reused.process (999, pcm.data (), int (pcm.size ()), 0, true, 1,
        [&] (JttySpectrumFrame const& frame) {
      QVERIFY (index < expected.size ());
      auto const mismatch = spectrumFrameMismatch (frame, expected.at (index++));
      QVERIFY2 (mismatch.isEmpty (), qPrintable (mismatch));
    }));
    QCOMPARE (index, expected.size ());
  }

  void averagesFollowNewSignalsAfterLongReception ()
  {
    JttySpectrum spectrum;
    spectrum.begin (3, 0, 0);
    std::array<qint16, JttySpectrum::Hop> pcm;
    JttySpectrumFrame latest;
    qint64 nextSample = 0;
    auto receiveTone = [&] (int frequency, int frames) {
      for (int i = 0; i < int (pcm.size ()); ++i) {
        pcm[i] = 1000 * std::sin (2 * 3.141592653589793 * frequency * i / 12000);
      }
      for (int i = 0; i < frames; ++i) {
        QVERIFY (spectrum.process (nextSample, pcm.data (), int (pcm.size ()), 0, true, 0,
            [&] (JttySpectrumFrame const& frame) { latest = frame; }));
        nextSample += pcm.size ();
      }
    };
    int const oldBin = int (1500 / JttySpectrumFrame::BinWidth);
    int const newBin = int (2250 / JttySpectrumFrame::BinWidth);
    receiveTone (1500, 720 * 12000 / JttySpectrum::Hop);
    float const oldPower = latest.cumulative[oldBin];
    QVERIFY (oldPower > 0);
    receiveTone (2250, 180 * 12000 / JttySpectrum::Hop);
    QVERIFY (latest.cumulative[oldBin] < 0.5f * oldPower);
    QVERIFY (latest.cumulative[newBin] > 1.5f * latest.cumulative[oldBin]);
    QVERIFY (latest.linearAverage[newBin] > latest.linearAverage[oldBin]);
    QCOMPARE (latest.row.reception, quint64 (3));
    QCOMPARE (latest.row.endSample, nextSample);
    QCOMPARE (latest.row.utcEndMs, nextSample / 12);
  }
};

QTEST_APPLESS_MAIN (TestJttySpectrum)
#include "test_jtty_spectrum.moc"
