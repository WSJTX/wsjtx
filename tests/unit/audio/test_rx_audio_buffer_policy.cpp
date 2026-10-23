#include <QtTest>

#include "Audio/RxAudioBufferPolicy.hpp"

class TestRxAudioBufferPolicy : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void normal_operation_lets_qt_choose ()
  {
    QCOMPARE (RxAudioBufferPolicy::select (0, false),
              RxAudioBufferPolicy::qt_default_frames);
  }

  Q_SLOT void full_duplex_uses_larger_buffer ()
  {
    QCOMPARE (RxAudioBufferPolicy::select (0, true),
              RxAudioBufferPolicy::full_duplex_frames);
    QVERIFY (RxAudioBufferPolicy::full_duplex_frames >= 48000);
  }

  Q_SLOT void environment_override_wins_in_both_modes ()
  {
    QCOMPARE (RxAudioBufferPolicy::select (19200, false), 19200);
    QCOMPARE (RxAudioBufferPolicy::select (19200, true), 19200);
  }

  Q_SLOT void is_usable_at_compile_time ()
  {
    static_assert (RxAudioBufferPolicy::select (0, true)
                   == RxAudioBufferPolicy::full_duplex_frames, "");
    static_assert (RxAudioBufferPolicy::select (0, false)
                   == RxAudioBufferPolicy::qt_default_frames, "");
  }
};

QTEST_GUILESS_MAIN (TestRxAudioBufferPolicy)

#include "test_rx_audio_buffer_policy.moc"
