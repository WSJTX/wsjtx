#include <QtTest>

#include "Audio/AudioOutputSink.hpp"
#include "Modulator/Modulator.hpp"
#include "widgets/itoneAndicw.h"

int volatile itone[MAX_NUM_SYMBOLS] {};
float gran () { return 0; }

namespace
{
  class ScriptedOutput final : public AudioOutputSink
  {
  public:
    void setFormat (QAudioDeviceInfo const&, unsigned, int) override {}
    void restart (QIODevice * source) override { m_source = source; }
    void suspend () override {}
    void resume () override {}
    void reset () override { m_source = nullptr; }
    void stop () override { m_source = nullptr; }
    void setAttenuation (qreal) override {}
    void resetAttenuation () override {}
    int bufferSize () const override { return 0; }

    QIODevice * m_source {nullptr};
  };

  TxEvidence::TxRequest cwRequest ()
  {
    TxEvidence::TxRequest request;
    request.mode = QStringLiteral ("JT9");
    request.symbols_length = 1;
    request.frames_per_symbol = 1;
    request.tone_spacing = 0;
    request.synchronize = false;
    request.tr_period_s = 60;
    request.cw_id = {1, 0, 1};
    return request;
  }
}

class TestModulator : public QObject
{
  Q_OBJECT

  QByteArray readFrames (Modulator& modulator, int frames)
  {
    QByteArray pcm (frames * sizeof (qint16), '\0');
    pcm.resize (modulator.readData (pcm.data (), pcm.size ()));
    return pcm;
  }

private slots:
  void fastCwStartsIndependently_data ()
  {
    QTest::addColumn<bool> ("reuseInstance");
    QTest::newRow ("restart") << true;
    QTest::newRow ("second-instance") << false;
  }

  void fastCwStartsIndependently ()
  {
    QFETCH (bool, reuseInstance);
    auto request = cwRequest ();
    request.fast_mode = true;
    request.tr_period_s = 15;

    ScriptedOutput output;
    Modulator original {48000, 15};
    original.startAt (request, &output, 11000);
    QByteArray initialPcm (64, '\0');
    QCOMPARE (original.readDataAt (initialPcm.data (), initialPcm.size (), 11000),
              qint64 {64});
    QVERIFY (initialPcm != QByteArray (initialPcm.size (), '\0'));

    ScriptedOutput independentOutput;
    Modulator independent {48000, 15};
    auto& next = reuseInstance ? original : independent;
    if (reuseInstance)
      {
        original.stop (true);
      }
    next.startAt (request, reuseInstance ? &output : &independentOutput, 11000);
    QByteArray nextPcm (64, '\0');
    QCOMPARE (next.readDataAt (nextPcm.data (), nextPcm.size (), 11000), qint64 {64});
    QCOMPARE (nextPcm, initialPcm);
  }

  void firstCwIdStartsWithQuietRamp_data ()
  {
    QTest::addColumn<qint64> ("startMs");
    QTest::addColumn<int> ("messageFrames");
    QTest::newRow ("unsynchronized") << qint64 {0} << 5;
    QTest::newRow ("late") << qint64 {1001} << 0;
  }

  void firstCwIdStartsWithQuietRamp ()
  {
    QFETCH (qint64, startMs);
    QFETCH (int, messageFrames);
    ScriptedOutput output;
    Modulator modulator {48000, 60};
    modulator.startAt (cwRequest (), &output, startMs);
    if (messageFrames)
      {
        QCOMPARE (readFrames (modulator, messageFrames).size (),
                  messageFrames * int (sizeof (qint16)));
      }
    auto const pcm = readFrames (modulator, 32);
    QCOMPARE (pcm.size (), 32 * int (sizeof (qint16)));
    QCOMPARE (pcm.left (sizeof (qint16)), QByteArray (sizeof (qint16), '\0'));
    QVERIFY (pcm != QByteArray (pcm.size (), '\0'));
  }

  void restartDiscardsPartialCwRamp ()
  {
    ScriptedOutput output;
    Modulator reused {48000, 60};
    reused.startAt (cwRequest (), &output, 1001);
    QCOMPARE (readFrames (reused, 32).size (), 64);
    reused.stop (true);
    reused.startAt (cwRequest (), &output, 1001);
    auto const restartedPcm = readFrames (reused, 64);

    ScriptedOutput cleanOutput;
    Modulator clean {48000, 60};
    clean.startAt (cwRequest (), &cleanOutput, 1001);
    auto const cleanPcm = readFrames (clean, 64);
    QCOMPARE (restartedPcm.size (), 128);
    QCOMPARE (restartedPcm, cleanPcm);
  }
};

QTEST_GUILESS_MAIN (TestModulator)
#include "test_modulator.moc"
