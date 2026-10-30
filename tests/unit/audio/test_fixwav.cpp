#include <QtTest>

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

namespace
{
  constexpr int legacyCapacityBytes = 2 * 120 * 12000;

  QByteArray wav (qint32 declaredBytes, int actualBytes)
  {
    QByteArray result (44 + actualBytes, '\0');
    auto put16 = [&] (int offset, quint16 value) {
      qToLittleEndian (value, result.data () + offset);
    };
    auto put32 = [&] (int offset, quint32 value) {
      qToLittleEndian (value, result.data () + offset);
    };
    result.replace (0, 4, "RIFF");
    put32 (4, 36u + quint32 (declaredBytes));
    result.replace (8, 8, "WAVEfmt ");
    put32 (16, 16);
    put16 (20, 1);
    put16 (22, 1);
    put32 (24, 12000);
    put32 (28, 24000);
    put16 (32, 2);
    put16 (34, 16);
    result.replace (36, 4, "data");
    put32 (40, quint32 (declaredBytes));
    if (actualBytes >= 8) {
      put16 (44, 8); // 20m, FT8, submode A, 60 seconds
      put16 (46, 12);
      put16 (48, 1);
      put16 (50, 5);
    }
    return result;
  }
}

class TestFixwav : public QObject
{
  Q_OBJECT

private slots:
  void input_data ()
  {
    QTest::addColumn<QByteArray> ("contents");
    QTest::addColumn<QByteArray> ("diagnostic");
    QTest::newRow ("minimum") << wav (8, 8) << QByteArray {};
    QTest::newRow ("legacy-capacity") << wav (legacyCapacityBytes, legacyCapacityBytes)
                                     << QByteArray {};
    QTest::newRow ("beyond-legacy-capacity") << wav (legacyCapacityBytes + 2, legacyCapacityBytes + 2)
                                            << QByteArray {};
    QTest::newRow ("huge-count") << wav (2147483647, 8) << QByteArray {};
    QTest::newRow ("negative") << wav (-2, 8) << QByteArray {"Invalid WAV data size."};
    QTest::newRow ("empty") << wav (0, 0) << QByteArray {"Invalid WAV data size."};
    QTest::newRow ("short-metadata") << wav (6, 6) << QByteArray {"Invalid WAV data size."};
    QTest::newRow ("odd") << wav (9, 9) << QByteArray {};
    QTest::newRow ("short-header") << wav (8, 8).left (43)
                                 << QByteArray {"Cannot read WAV header."};
    QTest::newRow ("truncated-audio") << wav (16, 8) << QByteArray {};
    QTest::newRow ("truncated-metadata") << wav (16, 7)
                                  << QByteArray {"Cannot read WAV samples."};
  }

  void input ()
  {
    QFETCH (QByteArray, contents);
    QFETCH (QByteArray, diagnostic);
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    QFile file {dir.filePath ("input.wav")};
    QVERIFY (file.open (QIODevice::WriteOnly));
    QCOMPARE (file.write (contents), qint64 (contents.size ()));
    file.close ();

    QProcess process;
    process.setWorkingDirectory (dir.path ());
    process.start (FIXWAV_EXECUTABLE, {"input.wav"});
    QVERIFY (process.waitForStarted ());
    process.write ("N\n");
    process.closeWriteChannel ();
    QVERIFY (process.waitForFinished (5000));
    QCOMPARE (process.exitStatus (), QProcess::NormalExit);
    auto const errors = process.readAllStandardError ();
    if (diagnostic.isEmpty ()) {
      QCOMPARE (process.exitCode (), 0);
      QVERIFY (process.readAllStandardOutput ().contains ("Band:"));
    } else {
      QVERIFY (process.exitCode () != 0);
      QVERIFY2 (errors.contains (diagnostic), errors.constData ());
      QVERIFY (!process.readAllStandardOutput ().contains ("Do you want"));
    }
    QVERIFY (file.open (QIODevice::ReadOnly));
    QCOMPARE (file.readAll (), contents);
  }

  void rejects_unsupported_layout_data ()
  {
    QTest::addColumn<int> ("offset");
    QTest::addColumn<QByteArray> ("replacement");
    QTest::newRow ("not-riff") << 0 << QByteArray {"RIFX"};
    QTest::newRow ("not-wave") << 8 << QByteArray {"AVI "};
    QTest::newRow ("not-fmt") << 12 << QByteArray {"JUNK"};
    QTest::newRow ("extended-fmt") << 16 << QByteArray (1, char (18));
    QTest::newRow ("non-pcm") << 20 << QByteArray (1, char (3));
    QTest::newRow ("stereo") << 22 << QByteArray (1, char (2));
    QTest::newRow ("block-alignment") << 32 << QByteArray (1, char (4));
    QTest::newRow ("eight-bit") << 34 << QByteArray (1, char (8));
    QTest::newRow ("intervening-chunk") << 36 << QByteArray {"LIST"};
  }

  void rejects_unsupported_layout ()
  {
    QFETCH (int, offset);
    QFETCH (QByteArray, replacement);
    auto contents = wav (8, 8);
    contents.replace (offset, replacement.size (), replacement);
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    QFile file {dir.filePath ("input.wav")};
    QVERIFY (file.open (QIODevice::WriteOnly));
    QCOMPARE (file.write (contents), qint64 (contents.size ()));
    file.close ();
    QProcess process;
    process.setWorkingDirectory (dir.path ());
    process.start (FIXWAV_EXECUTABLE, {"14.0", "FT8", "B", "60", "input.wav"});
    QVERIFY (process.waitForStarted ());
    process.write ("Y\n");
    process.closeWriteChannel ();
    QVERIFY (process.waitForFinished (5000));
    QCOMPARE (process.exitStatus (), QProcess::NormalExit);
    QVERIFY (process.exitCode () != 0);
    QVERIFY (process.readAllStandardError ().contains ("Unsupported WAV layout"));
    QVERIFY (!process.readAllStandardOutput ().contains ("Do you want"));
    QVERIFY (file.open (QIODevice::ReadOnly));
    QCOMPARE (file.readAll (), contents);
  }

  void inspection_without_metadata_never_prompts_or_writes ()
  {
    auto contents = wav (8, 8);
    contents.replace (44, 8, QByteArray (8, '\0'));
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    QFile file {dir.filePath ("input.wav")};
    QVERIFY (file.open (QIODevice::WriteOnly));
    QCOMPARE (file.write (contents), qint64 (contents.size ()));
    file.close ();
    QProcess process;
    process.setWorkingDirectory (dir.path ());
    process.start (FIXWAV_EXECUTABLE, {"input.wav"});
    QVERIFY (process.waitForStarted ());
    process.write ("Y\n");
    process.closeWriteChannel ();
    QVERIFY (process.waitForFinished (5000));
    QCOMPARE (process.exitStatus (), QProcess::NormalExit);
    QCOMPARE (process.exitCode (), 0);
    auto const output = process.readAllStandardOutput ();
    QVERIFY (output.contains ("File has no valid WSJT-X params."));
    QVERIFY (!output.contains ("Do you want"));
    QVERIFY (file.open (QIODevice::ReadOnly));
    QCOMPARE (file.readAll (), contents);
  }

  void rewrite_preserves_other_bytes_data ()
  {
    QTest::addColumn<qint32> ("declaredBytes");
    QTest::addColumn<int> ("actualBytes");
    QTest::newRow ("minimum") << qint32 (8) << 8;
    QTest::newRow ("odd") << qint32 (9) << 9;
    QTest::newRow ("beyond-legacy-capacity") << qint32 (legacyCapacityBytes + 2)
                                            << legacyCapacityBytes + 2;
    QTest::newRow ("truncated-audio") << qint32 (2147483647) << 31;
    QTest::newRow ("trailing-data") << qint32 (8) << 31;
  }

  void rewrite_preserves_other_bytes ()
  {
    QFETCH (qint32, declaredBytes);
    QFETCH (int, actualBytes);
    QTemporaryDir dir;
    QVERIFY (dir.isValid ());
    auto contents = wav (declaredBytes, actualBytes);
    for (int i = 52; i < contents.size (); ++i) contents[i] = char (1 + i % 127);
    QFile file {dir.filePath ("input.wav")};
    QVERIFY (file.open (QIODevice::WriteOnly));
    QCOMPARE (file.write (contents), qint64 (contents.size ()));
    file.close ();
    QProcess process;
    process.setWorkingDirectory (dir.path ());
    process.start (FIXWAV_EXECUTABLE, {"14.0", "FT8", "B", "60", "input.wav"});
    QVERIFY (process.waitForStarted ());
    process.write ("Y\n");
    process.closeWriteChannel ();
    QVERIFY (process.waitForFinished (5000));
    QCOMPARE (process.exitStatus (), QProcess::NormalExit);
    QCOMPARE (process.exitCode (), 0);
    qToLittleEndian (quint16 (2), contents.data () + 48);
    QVERIFY (file.open (QIODevice::ReadOnly));
    QCOMPARE (file.readAll (), contents);
  }
};

QTEST_GUILESS_MAIN (TestFixwav)
#include "test_fixwav.moc"
