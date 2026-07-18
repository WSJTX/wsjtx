#include <QtTest>

#include <QFile>
#include <QTemporaryFile>
#include <QtMultimedia/QAudioFormat>

#include "Audio/BWFFile.hpp"
#include "Audio/WavFile.hpp"

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

  QByteArray chunk (char const * id, QByteArray const& payload)
  {
    QByteArray out;
    out.append (id, 4);
    out.append (le32 (static_cast<quint32> (payload.size ())));
    out.append (payload);
    if (payload.size () & 1)
      {
        out.append ('\0');
      }
    return out;
  }

  QByteArray wave_file (QList<QByteArray> const& chunks)
  {
    QByteArray payload {"WAVE", 4};
    for (auto const& current : chunks)
      {
        payload.append (current);
      }

    QByteArray out;
    out.append ("RIFF", 4);
    out.append (le32 (static_cast<quint32> (payload.size ())));
    out.append (payload);
    return out;
  }

  QByteArray fmt_chunk (quint16 channels = 1, quint32 sample_rate = 12000,
                        quint16 bits_per_sample = 16, quint16 block_align = 0,
                        quint32 byte_rate = 0)
  {
    if (!block_align)
      {
        block_align = static_cast<quint16> (channels * (bits_per_sample / 8u));
      }
    if (!byte_rate)
      {
        byte_rate = sample_rate * block_align;
      }

    QByteArray payload;
    payload.append (le16 (1));
    payload.append (le16 (channels));
    payload.append (le32 (sample_rate));
    payload.append (le32 (byte_rate));
    payload.append (le16 (block_align));
    payload.append (le16 (bits_per_sample));
    return chunk ("fmt ", payload);
  }

  QByteArray data_chunk (QByteArray const& data = {})
  {
    return chunk ("data", data);
  }

  QByteArray info_list_chunk ()
  {
    QByteArray payload {"INFO", 4};
    payload.append (chunk ("INAM", QByteArray {"wsjtx", 5}));
    return chunk ("LIST", payload);
  }

  QByteArray info_list_chunk_with_terminal_pad_outside_size ()
  {
    QByteArray payload {"INFO", 4};
    payload.append ("ISRC", 4);
    payload.append (le32 (5));
    payload.append ("K1JT", 5);

    QByteArray out;
    out.append ("LIST", 4);
    out.append (le32 (static_cast<quint32> (payload.size ())));
    out.append (payload);
    out.append ('\0');
    return out;
  }

  QByteArray bare_bext_payload ()
  {
    return QByteArray (604, '\0');
  }

  QAudioFormat default_format ()
  {
    QAudioFormat format;
    format.setByteOrder (QAudioFormat::LittleEndian);
    format.setChannelCount (1);
    format.setCodec ("audio/pcm");
    format.setSampleRate (12000);
    format.setSampleSize (16);
    format.setSampleType (QAudioFormat::SignedInt);
    return format;
  }

  QString write_temp_file (QByteArray const& data)
  {
    QTemporaryFile file;
    file.setAutoRemove (false);
    if (!file.open ())
      {
        return {};
      }

    if (file.write (data) != data.size ())
      {
        return {};
      }

    auto const name = file.fileName ();
    file.close ();
    return name;
  }
}

class TestBWFFile
  : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void opens_valid_pcm_wave ()
  {
    auto const name = write_temp_file (wave_file ({fmt_chunk (), data_chunk ()}));
    QVERIFY (!name.isEmpty ());

    BWFFile file {default_format (), name};
    QVERIFY (file.open (QIODevice::ReadOnly));
    QCOMPARE (file.format ().channelCount (), 1);
    QCOMPARE (file.format ().sampleRate (), 12000);
    file.close ();
    QFile::remove (name);
  }

  Q_SLOT void loads_supported_decode_wave ()
  {
    QByteArray samples;
    samples.append (le16 (0x1234));
    samples.append (le16 (0x5678));
    samples.append (le16 (0x9abc));
    auto const name = write_temp_file (wave_file ({fmt_chunk (), data_chunk (samples)}));
    QVERIFY (!name.isEmpty ());

    auto const result = Radio::WavFile::load (name, 2);
    QVERIFY2 (result.isValid (), qPrintable (result.error));
    QCOMPARE (result.frames, 2);
    QCOMPARE (result.samples, samples.left (4));
    QFile::remove (name);
  }

  Q_SLOT void loads_legacy_decode_waves ()
  {
    auto const eight_bit_name = write_temp_file (
        wave_file ({fmt_chunk (1, 11025, 8), data_chunk (QByteArray::fromHex ("7f80"))}));
    QVERIFY (!eight_bit_name.isEmpty ());
    auto const eight_bit = Radio::WavFile::load (eight_bit_name, 2);
    QVERIFY2 (eight_bit.isValid (), qPrintable (eight_bit.error));
    QCOMPARE (eight_bit.frames, 2);
    QCOMPARE (eight_bit.samples, QByteArray::fromHex ("7f80"));
    QFile::remove (eight_bit_name);

    auto const sixteen_bit_name = write_temp_file (
        wave_file ({fmt_chunk (1, 11025, 16), data_chunk (QByteArray::fromHex ("01000200"))}));
    QVERIFY (!sixteen_bit_name.isEmpty ());
    auto const sixteen_bit = Radio::WavFile::load (sixteen_bit_name, 2);
    QVERIFY2 (sixteen_bit.isValid (), qPrintable (sixteen_bit.error));
    QCOMPARE (sixteen_bit.frames, 2);
    QCOMPARE (sixteen_bit.samples, QByteArray::fromHex ("01000200"));
    QFile::remove (sixteen_bit_name);
  }

  Q_SLOT void rejects_multichannel_decode_wave ()
  {
    auto const name = write_temp_file (
        wave_file ({fmt_chunk (2000, 12000, 16), data_chunk (QByteArray::fromHex ("0000"))}));
    QVERIFY (!name.isEmpty ());

    BWFFile parsed {QAudioFormat {}, name};
    QVERIFY (parsed.open (QIODevice::ReadOnly));
    QCOMPARE (parsed.format ().channelCount (), 2000);
    parsed.close ();

    auto const result = Radio::WavFile::load (name, 36000);
    QVERIFY (!result.isValid ());
    QVERIFY (result.samples.isEmpty ());
    QCOMPARE (result.frames, 0);
    QFile::remove (name);
  }

  Q_SLOT void discards_partial_decode_frame ()
  {
    auto const name = write_temp_file (
        wave_file ({fmt_chunk (), data_chunk (QByteArray::fromHex ("010002"))}));
    QVERIFY (!name.isEmpty ());

    auto const result = Radio::WavFile::load (name, 2);
    QVERIFY2 (result.isValid (), qPrintable (result.error));
    QCOMPARE (result.frames, 1);
    QCOMPARE (result.samples, QByteArray::fromHex ("0100"));
    QFile::remove (name);
  }

  Q_SLOT void opens_valid_list_info_chunk ()
  {
    auto const name = write_temp_file (wave_file ({fmt_chunk (), info_list_chunk (), data_chunk ()}));
    QVERIFY (!name.isEmpty ());
    BWFFile::InfoDictionary::key_type const inam {{'I', 'N', 'A', 'M'}};
    QByteArray const expected_name {"wsjtx", 5};

    BWFFile file {default_format (), name};
    QVERIFY (file.open (QIODevice::ReadOnly));
    QVERIFY (file.list_info ().contains (inam));
    QCOMPARE (file.list_info ().value (inam), expected_name);
    file.close ();
    QFile::remove (name);
  }

  Q_SLOT void opens_list_info_with_terminal_subchunk_pad ()
  {
    auto const name = write_temp_file (wave_file ({fmt_chunk (), data_chunk (),
          info_list_chunk_with_terminal_pad_outside_size ()}));
    QVERIFY (!name.isEmpty ());
    BWFFile::InfoDictionary::key_type const isrc {{'I', 'S', 'R', 'C'}};

    BWFFile file {default_format (), name};
    QVERIFY (file.open (QIODevice::ReadOnly));
    QVERIFY (file.list_info ().contains (isrc));
    QCOMPARE (file.list_info ().value (isrc), QByteArray ("K1JT", 5));
    file.close ();
    QFile::remove (name);
  }

  Q_SLOT void bext_version_without_chunk_defaults_to_v0 ()
  {
    auto const name = write_temp_file (wave_file ({fmt_chunk (), data_chunk ()}));
    QVERIFY (!name.isEmpty ());

    BWFFile file {default_format (), name};
    BWFFile const& const_file = file;
    QVERIFY (file.open (QIODevice::ReadOnly));
    QCOMPARE (static_cast<int> (const_file.bext_version ()),
              static_cast<int> (BWFFile::BextVersion::v_0));
    file.close ();
    QFile::remove (name);
  }

  Q_SLOT void bext_fixed_field_getter_stays_bounded ()
  {
    QByteArray bext = bare_bext_payload ();
    bext.replace (0, 256, QByteArray (256, 'B'));
    auto const name = write_temp_file (wave_file ({fmt_chunk (), chunk ("bext", bext), data_chunk ()}));
    QVERIFY (!name.isEmpty ());

    BWFFile file {default_format (), name};
    QVERIFY (file.open (QIODevice::ReadOnly));
    QCOMPARE (file.bext_description (), QByteArray (256, 'B'));
    file.close ();
    QFile::remove (name);
  }

  Q_SLOT void bext_origination_date_time_stays_bounded ()
  {
    QByteArray bext = bare_bext_payload ();
    bext.replace (320, 10, QByteArray {"2026-04-06", 10});
    bext.replace (330, 10, QByteArray {"12-34-56ZZ", 10});
    auto const name = write_temp_file (wave_file ({fmt_chunk (), chunk ("bext", bext), data_chunk ()}));
    QVERIFY (!name.isEmpty ());

    BWFFile file {default_format (), name};
    QVERIFY (file.open (QIODevice::ReadOnly));
    QCOMPARE (file.bext_origination_date_time ().date (), QDate (2026, 4, 6));
    QCOMPARE (file.bext_origination_date_time ().time (), QTime (12, 34, 56));
    file.close ();
    QFile::remove (name);
  }

  Q_SLOT void bext_origination_date_time_setter_handles_invalid_value ()
  {
    QTemporaryFile temp;
    temp.setAutoRemove (false);
    QVERIFY (temp.open ());
    auto const name = temp.fileName ();
    temp.close ();

    {
      BWFFile file {default_format (), name};
      QVERIFY (file.open (QIODevice::WriteOnly));
      file.bext_origination_date_time ({});
      file.close ();
    }

    {
      BWFFile file {default_format (), name};
      QVERIFY (file.open (QIODevice::ReadOnly));
      QVERIFY (!file.bext_origination_date_time ().isValid ());
      file.close ();
    }

    QFile::remove (name);
  }

  Q_SLOT void bext_setter_truncates_without_overread ()
  {
    QTemporaryFile temp;
    temp.setAutoRemove (false);
    QVERIFY (temp.open ());
    auto const name = temp.fileName ();
    temp.close ();

    {
      BWFFile file {default_format (), name};
      QVERIFY (file.open (QIODevice::WriteOnly));
      file.bext_description (QByteArray (300, 'A'));
      file.close ();
    }

    {
      BWFFile file {default_format (), name};
      QVERIFY (file.open (QIODevice::ReadOnly));
      QCOMPARE (file.bext_description (), QByteArray (256, 'A'));
      file.close ();
    }

    QFile::remove (name);
  }

  Q_SLOT void rejects_truncated_bext_chunk ()
  {
    QByteArray bad_bext;
    bad_bext.append ("bext", 4);
    bad_bext.append (le32 (700));
    bad_bext.append (QByteArray (16, '\0'));
    auto const name = write_temp_file (wave_file ({fmt_chunk (), bad_bext, data_chunk ()}));
    QVERIFY (!name.isEmpty ());

    BWFFile file {default_format (), name};
    QVERIFY (!file.open (QIODevice::ReadOnly));
    QFile::remove (name);
  }

  Q_SLOT void rejects_list_info_subchunk_overrun ()
  {
    QByteArray payload {"INFO", 4};
    payload.append ("INAM", 4);
    payload.append (le32 (8));
    payload.append ("bad", 3);
    auto const name = write_temp_file (wave_file ({fmt_chunk (), chunk ("LIST", payload), data_chunk ()}));
    QVERIFY (!name.isEmpty ());

    BWFFile file {default_format (), name};
    QVERIFY (!file.open (QIODevice::ReadOnly));
    QFile::remove (name);
  }

  Q_SLOT void rejects_short_fmt_chunk ()
  {
    QByteArray payload;
    payload.append (le16 (1));
    payload.append (le16 (1));
    auto const name = write_temp_file (wave_file ({chunk ("fmt ", payload), data_chunk ()}));
    QVERIFY (!name.isEmpty ());

    BWFFile file {default_format (), name};
    QVERIFY (!file.open (QIODevice::ReadOnly));
    QFile::remove (name);
  }

  Q_SLOT void rejects_zero_channel_fmt ()
  {
    auto const name = write_temp_file (wave_file ({fmt_chunk (0), data_chunk ()}));
    QVERIFY (!name.isEmpty ());

    BWFFile file {default_format (), name};
    QVERIFY (!file.open (QIODevice::ReadOnly));
    QFile::remove (name);
  }

  Q_SLOT void tolerates_inconsistent_fmt_rates ()
  {
    auto const name = write_temp_file (wave_file ({fmt_chunk (1, 12000, 16, 4, 48000), data_chunk ()}));
    QVERIFY (!name.isEmpty ());

    BWFFile file {default_format (), name};
    QVERIFY (file.open (QIODevice::ReadOnly));
    QCOMPARE (file.format ().sampleRate (), 12000);
    QCOMPARE (file.format ().sampleSize (), 16);
    file.close ();
    QFile::remove (name);
  }
};

QTEST_MAIN (TestBWFFile)

#include "test_bwf_file.moc"
