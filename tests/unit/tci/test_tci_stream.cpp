#include <cstddef>

#include <QtTest>

#include "Transceiver/TCIStream.hpp"

class TestTciStream final
  : public QObject
{
  Q_OBJECT

private slots:
  void preservesWireHeaderLayout ()
  {
    QCOMPARE (sizeof (TciStream::Header), size_t {64});
    QCOMPARE (offsetof (TciStream::Header, length), size_t {20});
    QCOMPARE (offsetof (TciStream::Header, type), size_t {24});
    QCOMPARE (offsetof (TciStream::Header, channels), size_t {28});
  }

  void preparesExactZeroedFloatFrame ()
  {
    QByteArray frame (128, '\x7f');

    QVERIFY (TciStream::prepare_float_frame (&frame, 2048u));
    QCOMPARE (frame.size (), 64 + 2048 * int (sizeof (float)));
    for (auto byte : frame)
      {
        QCOMPARE (byte, char {0});
      }
    QVERIFY (TciStream::has_complete_float_payload (frame, 2048u));
    QVERIFY (!TciStream::has_complete_float_payload (frame.left (frame.size () - 1), 2048u));
  }

  void rejectsOversizedFloatFrame ()
  {
    QByteArray frame;
    QVERIFY (!TciStream::prepare_float_frame (&frame, TciStream::MaxFloatSamples + 1u));
    QVERIFY (!TciStream::checked_float_frame_size (1u, nullptr));
  }

  void recognizesChannelHeaderVersions_data ()
  {
    QTest::addColumn<QString> ("version");
    QTest::addColumn<bool> ("expected");

    QTest::newRow ("pre-channel header") << QString {"1.8"} << false;
    QTest::newRow ("first channel header") << QString {"1.9"} << true;
    QTest::newRow ("patch version") << QString {"1.9.1"} << true;
    QTest::newRow ("current protocol") << QString {"2.0"} << true;
    QTest::newRow ("invalid") << QString {"ExpertSDR3"} << false;
    QTest::newRow ("missing") << QString {} << false;
  }

  void recognizesChannelHeaderVersions ()
  {
    QFETCH (QString, version);
    QFETCH (bool, expected);

    QCOMPARE (TciStream::protocol_has_channels (version), expected);
  }

  void validatesChronoParameters_data ()
  {
    QTest::addColumn<bool> ("modern");
    QTest::addColumn<quint32> ("header_channels");
    QTest::addColumn<quint32> ("length");
    QTest::addColumn<quint32> ("format");
    QTest::addColumn<quint32> ("codec");
    QTest::addColumn<quint32> ("sample_rate");
    QTest::addColumn<bool> ("expected_valid");
    QTest::addColumn<quint32> ("expected_channels");

    QTest::newRow ("legacy ignores reserved")
      << false << quint32 {0xdeadbeefu} << quint32 {2048}
      << TciStream::Float32Format << TciStream::UncompressedCodec << quint32 {48000}
      << true << quint32 {2};
    QTest::newRow ("modern mono")
      << true << quint32 {1} << quint32 {3}
      << TciStream::Float32Format << TciStream::UncompressedCodec << quint32 {48000}
      << true << quint32 {1};
    QTest::newRow ("modern stereo")
      << true << quint32 {2} << quint32 {2048}
      << TciStream::Float32Format << TciStream::UncompressedCodec << quint32 {48000}
      << true << quint32 {2};
    QTest::newRow ("zero-length mono")
      << true << quint32 {1} << quint32 {0}
      << TciStream::Float32Format << TciStream::UncompressedCodec << quint32 {48000}
      << true << quint32 {1};
    QTest::newRow ("odd stereo")
      << true << quint32 {2} << quint32 {3}
      << TciStream::Float32Format << TciStream::UncompressedCodec << quint32 {48000}
      << false << quint32 {0};
    QTest::newRow ("invalid modern channels")
      << true << quint32 {0} << quint32 {2048}
      << TciStream::Float32Format << TciStream::UncompressedCodec << quint32 {48000}
      << false << quint32 {0};
    QTest::newRow ("unsupported format")
      << true << quint32 {2} << quint32 {2048}
      << quint32 {0} << TciStream::UncompressedCodec << quint32 {48000}
      << false << quint32 {0};
    QTest::newRow ("unsupported codec")
      << true << quint32 {2} << quint32 {2048}
      << TciStream::Float32Format << quint32 {1} << quint32 {48000}
      << false << quint32 {0};
    QTest::newRow ("unsupported sample rate")
      << true << quint32 {2} << quint32 {2048}
      << TciStream::Float32Format << TciStream::UncompressedCodec << quint32 {24000}
      << false << quint32 {0};
    QTest::newRow ("oversized")
      << true << quint32 {1} << TciStream::MaxFloatSamples + 1u
      << TciStream::Float32Format << TciStream::UncompressedCodec << quint32 {48000}
      << false << quint32 {0};
  }

  void validatesChronoParameters ()
  {
    QFETCH (bool, modern);
    QFETCH (quint32, header_channels);
    QFETCH (quint32, length);
    QFETCH (quint32, format);
    QFETCH (quint32, codec);
    QFETCH (quint32, sample_rate);
    QFETCH (bool, expected_valid);
    QFETCH (quint32, expected_channels);

    TciStream::Header chrono {};
    chrono.sampleRate = sample_rate;
    chrono.format = format;
    chrono.codec = codec;
    chrono.length = length;
    chrono.channels = header_channels;
    quint32 channels {99u};

    QCOMPARE (TciStream::valid_tx_chrono (chrono, modern, 48000u, &channels), expected_valid);
    if (expected_valid)
      {
        QCOMPARE (channels, expected_channels);
      }
  }

  void buildsConformingStereoResponse ()
  {
    TciStream::Header chrono {};
    chrono.receiver = 1u;
    chrono.sampleRate = 48000u;
    chrono.length = 2048u;
    QByteArray response;

    QVERIFY (TciStream::prepare_tx_audio_frame (&response, chrono, 2u));
    QCOMPARE (response.size (), 8256);
    auto const * header = TciStream::header (response);
    QCOMPARE (header->receiver, quint32 {1});
    QCOMPARE (header->sampleRate, quint32 {48000});
    QCOMPARE (header->format, TciStream::Float32Format);
    QCOMPARE (header->codec, TciStream::UncompressedCodec);
    QCOMPARE (header->length, quint32 {2048});
    QCOMPARE (header->type, quint32 {TciStream::TxAudioStream});
    QCOMPARE (header->channels, quint32 {2});
    for (auto reserved : header->reserved)
      {
        QCOMPARE (reserved, quint32 {0});
      }
    for (quint32 i = 0u; i < header->length; ++i)
      {
        QCOMPARE (TciStream::float_payload (response)[i], 0.0f);
      }
  }

  void rejectsInvalidResponseChannelShape ()
  {
    TciStream::Header chrono {};
    chrono.length = 3u;
    QByteArray response;

    QVERIFY (!TciStream::prepare_tx_audio_frame (&response, chrono, 0u));
    QVERIFY (!TciStream::prepare_tx_audio_frame (&response, chrono, 2u));
    QVERIFY (TciStream::prepare_tx_audio_frame (&response, chrono, 1u));
    QCOMPARE (response.size (), 64 + 3 * int (sizeof (float)));
  }

  void packsMonoAndStereoChannelFrames ()
  {
    float mono[2] {};
    auto * mono_end = TciStream::write_channel_frame (0.25f, 1u, mono);
    mono_end = TciStream::write_channel_frame (-0.5f, 1u, mono_end);
    QCOMPARE (mono_end, mono + 2);
    QCOMPARE (mono[0], 0.25f);
    QCOMPARE (mono[1], -0.5f);

    float stereo[4] {};
    auto * stereo_end = TciStream::write_channel_frame (0.25f, 2u, stereo);
    stereo_end = TciStream::write_channel_frame (-0.5f, 2u, stereo_end);
    QCOMPARE (stereo_end, stereo + 4);
    QCOMPARE (stereo[0], 0.25f);
    QCOMPARE (stereo[1], 0.25f);
    QCOMPARE (stereo[2], -0.5f);
    QCOMPARE (stereo[3], -0.5f);
  }
};

QTEST_MAIN (TestTciStream)

#include "test_tci_stream.moc"
