#include <QtTest>

#include "Transceiver/HRDMessage.hpp"

namespace
{
  quint32 u32le (QByteArray const& bytes, int offset)
  {
    return static_cast<quint32> (static_cast<quint8> (bytes[offset]))
      | (static_cast<quint32> (static_cast<quint8> (bytes[offset + 1])) << 8)
      | (static_cast<quint32> (static_cast<quint8> (bytes[offset + 2])) << 16)
      | (static_cast<quint32> (static_cast<quint8> (bytes[offset + 3])) << 24);
  }

  void write_u32le (QByteArray * bytes, int offset, quint32 value)
  {
    (*bytes)[offset] = static_cast<char> (value & 0xffu);
    (*bytes)[offset + 1] = static_cast<char> ((value >> 8) & 0xffu);
    (*bytes)[offset + 2] = static_cast<char> ((value >> 16) & 0xffu);
    (*bytes)[offset + 3] = static_cast<char> ((value >> 24) & 0xffu);
  }
}

class TestHRDMessage final
  : public QObject
{
  Q_OBJECT

private slots:
  void buildsBinaryMessage ()
  {
    auto const message = HRDMessage::make ("get radio");

    QCOMPARE (u32le (message, 0), static_cast<quint32> (message.size ()));
    QCOMPARE (u32le (message, 4), quint32 (0x1234ABCD));
    QCOMPARE (u32le (message, 8), quint32 (0xABCD1234));
    QCOMPARE (u32le (message, 12), quint32 (0));
    QCOMPARE (message.size (), HRDMessage::header_size () + (QString {"get radio"}.size () + 1) * 2);
    QCOMPARE (static_cast<quint8> (message[message.size () - 2]), quint8 (0));
    QCOMPARE (static_cast<quint8> (message[message.size () - 1]), quint8 (0));
  }

  void parsesValidMessage ()
  {
    auto const parsed = HRDMessage::parse (HRDMessage::make ("FTDX-3000"));

    QVERIFY (parsed.valid);
    QVERIFY (parsed.complete);
    QCOMPARE (parsed.payload, QString {"FTDX-3000"});
  }

  void preservesUtf16Payload ()
  {
    QString payload {"Rig "};
    payload.append (QChar {0x00d8});

    auto const parsed = HRDMessage::parse (HRDMessage::make (payload));

    QVERIFY (parsed.valid);
    QVERIFY (parsed.complete);
    QCOMPARE (parsed.payload, payload);
  }

  void usesDeclaredSizeForPayload ()
  {
    auto message = HRDMessage::make ("OK");
    message.append ("ignored");

    auto const parsed = HRDMessage::parse (message);

    QVERIFY (parsed.valid);
    QVERIFY (parsed.complete);
    QCOMPARE (parsed.payload, QString {"OK"});
    QCOMPARE (parsed.required_size, HRDMessage::make ("OK").size ());
  }

  void roundTripsEmptyPayload ()
  {
    auto const parsed = HRDMessage::parse (HRDMessage::make (""));

    QVERIFY (parsed.valid);
    QVERIFY (parsed.complete);
    QCOMPARE (parsed.payload, QString {});
  }

  void reportsPartialHeader ()
  {
    auto const parsed = HRDMessage::parse (HRDMessage::make ("OK").left (8));

    QVERIFY (parsed.valid);
    QVERIFY (!parsed.complete);
    QCOMPARE (parsed.required_size, HRDMessage::header_size ());
  }

  void reportsPartialBody ()
  {
    auto const message = HRDMessage::make ("OK");
    auto const parsed = HRDMessage::parse (message.left (message.size () - 1));

    QVERIFY (parsed.valid);
    QVERIFY (!parsed.complete);
    QCOMPARE (parsed.required_size, message.size ());
  }

  void rejectsInvalidMagic1 ()
  {
    auto message = HRDMessage::make ("OK");
    write_u32le (&message, 4, 0);

    auto const parsed = HRDMessage::parse (message);

    QVERIFY (!parsed.valid);
  }

  void rejectsInvalidMagic2 ()
  {
    auto message = HRDMessage::make ("OK");
    write_u32le (&message, 8, 0);

    auto const parsed = HRDMessage::parse (message);

    QVERIFY (!parsed.valid);
  }

  void rejectsHeaderSize ()
  {
    auto message = HRDMessage::make ("OK");
    write_u32le (&message, 0, HRDMessage::header_size () - 1);

    auto const parsed = HRDMessage::parse (message);

    QVERIFY (!parsed.valid);
  }

  void rejectsOversizedMessage ()
  {
    auto message = HRDMessage::make ("OK");
    write_u32le (&message, 0, HRDMessage::max_message_size () + 1);

    auto const parsed = HRDMessage::parse (message);

    QVERIFY (!parsed.valid);
  }

  void rejectsOddUtf16Payload ()
  {
    auto message = HRDMessage::make ("OK");
    write_u32le (&message, 0, message.size () - 1);

    auto const parsed = HRDMessage::parse (message);

    QVERIFY (!parsed.valid);
  }

  void rejectsMissingTerminator ()
  {
    auto message = HRDMessage::make ("OK");
    message[message.size () - 2] = 'X';

    auto const parsed = HRDMessage::parse (message);

    QVERIFY (!parsed.valid);
  }
};

QTEST_MAIN (TestHRDMessage)

#include "test_hrd_message.moc"
