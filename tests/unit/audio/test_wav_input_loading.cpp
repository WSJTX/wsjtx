#include <QtTest>

#include <algorithm>

#include <QFile>
#include <QTemporaryFile>

#include "Audio/WavInputLoader.hpp"

namespace
{
QByteArray le16 (quint16 value)
{
  QByteArray bytes (2, '\0');
  bytes[0] = static_cast<char> (value & 0xffu);
  bytes[1] = static_cast<char> ((value >> 8) & 0xffu);
  return bytes;
}

QByteArray le32 (quint32 value)
{
  QByteArray bytes (4, '\0');
  bytes[0] = static_cast<char> (value & 0xffu);
  bytes[1] = static_cast<char> ((value >> 8) & 0xffu);
  bytes[2] = static_cast<char> ((value >> 16) & 0xffu);
  bytes[3] = static_cast<char> ((value >> 24) & 0xffu);
  return bytes;
}

QByteArray wave_file (quint32 sample_rate, quint16 bits_per_sample,
                      QByteArray const& samples)
{
  auto const bytes_per_frame = static_cast<quint16> (bits_per_sample / 8u);
  QByteArray format;
  format.append (le16 (1));
  format.append (le16 (1));
  format.append (le32 (sample_rate));
  format.append (le32 (sample_rate * bytes_per_frame));
  format.append (le16 (bytes_per_frame));
  format.append (le16 (bits_per_sample));

  QByteArray payload {"WAVE", 4};
  payload.append ("fmt ", 4);
  payload.append (le32 (static_cast<quint32> (format.size ())));
  payload.append (format);
  payload.append ("data", 4);
  payload.append (le32 (static_cast<quint32> (samples.size ())));
  payload.append (samples);
  if (samples.size () & 1)
    {
      payload.append ('\0');
    }

  QByteArray result {"RIFF", 4};
  result.append (le32 (static_cast<quint32> (payload.size ())));
  result.append (payload);
  return result;
}

QString write_temp_file (QByteArray const& contents)
{
  QTemporaryFile file;
  file.setAutoRemove (false);
  if (!file.open () || file.write (contents) != contents.size ())
    {
      return {};
    }
  auto const name = file.fileName ();
  file.close ();
  return name;
}

QByteArray signed_samples (int count)
{
  QByteArray samples;
  samples.reserve (count * 2);
  for (int index = 0; index < count; ++index)
    {
      samples.append (le16 (static_cast<quint16> (1000 + index % 100)));
    }
  return samples;
}
}

class TestWavInputLoading : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void preserves_short_input_and_zero_fills_tail ()
  {
    QByteArray samples;
    samples.append (le16 (1234));
    samples.append (le16 (static_cast<quint16> (-2345)));
    samples.append (le16 (32767));
    auto const name = write_temp_file (wave_file (12000, 16, samples));
    QVERIFY (!name.isEmpty ());

    auto const result = Radio::load_wav_input (name, 8);
    QVERIFY (result.valid);
    QCOMPARE (result.frames, 3);
    QCOMPARE (result.samples.size (), std::size_t {8});
    QCOMPARE (result.samples[0], short {1234});
    QCOMPARE (result.samples[1], short {-2345});
    QCOMPARE (result.samples[2], short {32767});
    QVERIFY (std::all_of (result.samples.cbegin () + result.frames,
                          result.samples.cend (), [] (short sample) {return !sample;}));
    QFile::remove (name);
  }

  Q_SLOT void truncates_oversized_input_to_capacity ()
  {
    int constexpr sample_limit = 13000;
    auto const name = write_temp_file (
        wave_file (12000, 16, signed_samples (sample_limit + 1000)));
    QVERIFY (!name.isEmpty ());

    auto const result = Radio::load_wav_input (name, sample_limit);
    QVERIFY (result.valid);
    QCOMPARE (result.frames, sample_limit);
    QCOMPARE (result.samples.size (), std::size_t {sample_limit});
    QCOMPARE (result.samples.back (), short {1000 + (sample_limit - 1) % 100});
    QFile::remove (name);
  }

  Q_SLOT void resamples_legacy_input_data ()
  {
    QTest::addColumn<int> ("sampleSize");
    QTest::addColumn<QByteArray> ("samples");

    QTest::newRow ("unsigned-8-bit") << 8 << QByteArray (11025, static_cast<char> (200));
    QTest::newRow ("signed-16-bit") << 16 << signed_samples (11025);
  }

  Q_SLOT void resamples_legacy_input ()
  {
    QFETCH (int, sampleSize);
    QFETCH (QByteArray, samples);
    int constexpr sample_limit = 13000;
    auto const name = write_temp_file (
        wave_file (11025, static_cast<quint16> (sampleSize), samples));
    QVERIFY (!name.isEmpty ());

    auto const result = Radio::load_wav_input (name, sample_limit);
    QVERIFY (result.valid);
    QCOMPARE (result.frames, 12000);
    QCOMPARE (result.samples.size (), std::size_t {sample_limit});
    QVERIFY (std::any_of (result.samples.cbegin (), result.samples.cbegin () + result.frames,
                          [] (short sample) {return sample != 0;}));
    QVERIFY (std::all_of (result.samples.cbegin () + result.frames, result.samples.cend (),
                          [] (short sample) {return sample == 0;}));
    QFile::remove (name);
  }
};

QTEST_GUILESS_MAIN (TestWavInputLoading)

#include "test_wav_input_loading.moc"
