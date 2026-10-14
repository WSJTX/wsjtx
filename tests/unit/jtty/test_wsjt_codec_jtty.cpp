// SPDX-License-Identifier: GPL-3.0-or-later
// JTTY receive in wsjt-codec's stream session, end to end: framed audio and
// controls on stdin, JSON lines on stdout, as lib/streaming_io.f90 and
// lib/streaming_jtty.f90 state them, and as the GUI's own live receive
// decodes the same audio. Transmissions are synthesized with the encoder the
// GUI transmits with.

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

#include <fftw3.h>

#include "JttyDecoder.hpp"
#include "widgets/JttyMessages.hpp"
#include "wsjtx_config.h"

extern "C"
{
  void genjtty_ (char * message, int tones[], int * symbols, fortran_charlen_t);
  void gen_jttywave_ (int const tones[], int * symbols, int * nsps, float * bt, float * fsample,
                      float * f0, float cwave[], float wave[], int * icmplx, int * nwave);
}

namespace
{
constexpr int SampleRate = 12000;
constexpr int FrameSamples = 59 * 384;    // one JTTY frame at nsps 384
constexpr int LeadSamples = 6000;         // silence before each synthesized transmission
constexpr int ProcessTimeoutMs = 60000;
// A process that measures JTTY's FFT plans: about a minute on ARM Linux.
constexpr int PlanningTimeoutMs = 240000;
QByteArray const JttyConfigure {R"({"t":"configure","mode":"JTTY"})"};
QByteArray const RjttySettings {R"({"t":"configure","mode":"JTTY","rxfreq":1500,"ntol":50,"nfa":200,"nfb":2800})"};
QByteArray const Ft8Configure {R"({"t":"configure","mode":"FT8","utc":"05:11:15"})"};

QString const Quick {"THE QUICK BROWN FOX JUMPED OVER THE LAZY DOG."};
QString const Quoted {"HE SAID \"73\" OK"};
QString const Gapped {"HI ... THE QUICK BROWN FOX JUMPED OVER THE LAZY DOG."};
QString const Padded {"HELLO BOB <<<<< <<<<< <<<<< <<<<< HOW ARE YOU"};    // live entry pausing

struct Run
{
  bool started {};
  bool timedOut {};
  int exitCode {-1};
  QProcess::ExitStatus exitStatus {QProcess::CrashExit};
  QList<QByteArray> lines;    // stdout, without line ends
  QByteArray standardError;
  QList<QJsonObject> events;    // one per line
  QString error;    // the first protocol violation found in the output
};

void appendLittleEndian16 (QByteArray& bytes, quint16 value)
{
  bytes.append (char (value & 0xffu));
  bytes.append (char ((value >> 8) & 0xffu));
}

void appendLittleEndian32 (QByteArray& bytes, quint32 value)
{
  appendLittleEndian16 (bytes, quint16 (value & 0xffffu));
  appendLittleEndian16 (bytes, quint16 (value >> 16));
}

QByteArray header ()
{
  QByteArray bytes {"WSJT", 4};
  bytes.append (char (0));    // int16 PCM
  bytes.append (char (1));    // mono
  appendLittleEndian16 (bytes, 12);
  return bytes;
}

QByteArray frame (quint8 type, QByteArray const& body)
{
  QByteArray bytes;
  bytes.append (char (type));
  appendLittleEndian32 (bytes, quint32 (body.size ()));
  bytes.append (body);
  return bytes;
}

QByteArray control (QByteArray const& json)
{
  return frame (0x02u, json);
}

QByteArray jtty ()
{
  return control (JttyConfigure);
}

// int16 LE samples [first, first + count) of pcm, as audio frames of
// samplesPerFrame samples (the last one shorter).
QByteArray audio (QByteArray const& pcm, int samplesPerFrame, int first = 0, int count = -1)
{
  int const total = int (pcm.size () / 2);
  if (count < 0 || first + count > total) count = total - first;
  QByteArray bytes;
  for (int offset = 0; offset < count; offset += samplesPerFrame)
    bytes += frame (0x01u, pcm.mid (2 * (first + offset), 2 * qMin (samplesPerFrame, count - offset)));
  return bytes;
}

QByteArray silence (int samples)
{
  return audio (QByteArray (2 * samples, '\0'), 4096);
}

QByteArray wavFile (QString const& path)
{
  QFile file {path};
  return file.open (QIODevice::ReadOnly) ? file.readAll () : QByteArray {};
}

QByteArray wavData (QByteArray const& bytes)
{
  for (int offset = 12; offset + 8 <= bytes.size ();)
    {
      auto const size = qFromLittleEndian<quint32> (reinterpret_cast<uchar const *> (bytes.constData () + offset + 4));
      if (bytes.mid (offset, 4) == "data") return bytes.mid (offset + 8, int (size));
      offset += 8 + int (size);
    }
  return {};
}

// A clean transmission of message at f0, after LeadSamples of silence and
// followed by one frame of silence.
QByteArray synthesize (QString const& message, float f0 = 1500.0f)
{
  QByteArray field (80, ' ');
  auto const bytes = message.toLatin1 ();
  if (bytes.size () > field.size ()) return {};
  std::copy (bytes.cbegin (), bytes.cend (), field.begin ());
  std::array<int, 59 * 16> tones {};
  int symbols {0};
  genjtty_ (field.data (), tones.data (), &symbols, 80);
  int nsps {384};
  float bt {2.0f};
  float fsample {float (SampleRate)};
  int icmplx {0};
  int nwave {symbols * nsps};
  if (nwave <= 0) return {};
  std::vector<float> wave (nwave);
  std::vector<std::complex<float>> unused (nwave);
  gen_jttywave_ (tones.data (), &symbols, &nsps, &bt, &fsample, &f0,
                 reinterpret_cast<float *> (unused.data ()), wave.data (), &icmplx, &nwave);
  QByteArray pcm (2 * (LeadSamples + nwave + FrameSamples), '\0');
  for (int i = 0; i < nwave; ++i)
    {
      auto const sample = qint16 (std::lround (20000.0 * wave[i]));
      pcm[2 * (LeadSamples + i)] = char (sample & 0xff);
      pcm[2 * (LeadSamples + i) + 1] = char ((sample >> 8) & 0xff);
    }
  return pcm;
}

QList<QByteArray> outputLines (QByteArray const& output)
{
  auto lines = output.split ('\n');
  if (!lines.isEmpty () && lines.last ().isEmpty ()) lines.removeLast ();
  for (auto& line : lines)
    if (line.endsWith ('\r')) line.chop (1);
  return lines;
}

QString checkReady (QJsonObject const& event)
{
  return event.value ("protocol").toInt () == 1 && event.value ("modes").toArray ().contains ("JTTY")
    ? QString {} : "the ready line does not offer JTTY";
}

QString checkUpdate (QJsonObject const& event)
{
  if (!event.value ("id").isDouble () || !event.value ("freq").isDouble ()
      || !event.value ("snr").isDouble () || !event.value ("start").isDouble ()
      || !event.value ("latest").isDouble () || !event.value ("text").isString ()
      || !event.value ("gaps").isArray ())
    return "a jtty_update field has the wrong JSON type";
  if (!QStringList {"growing", "complete", "expired", "ended"}.contains (event.value ("state").toString ()))
    return "unknown state";
  if (event.value ("latest").toDouble () < event.value ("start").toDouble ())
    return "latest precedes start";
  auto const text = event.value ("text").toString ();
  if (text.size () > 80 || text != text.simplified () || text.contains ("<<<<<") || text.contains ('~'))
    return "text is not the GUI's display text";
  int previous = -1;
  for (auto const& gap : event.value ("gaps").toArray ())
    {
      if (!gap.isDouble () || gap.toInt () <= previous || text.mid (gap.toInt (), 3) != "...")
        return "a gap offset does not point at a marker";
      previous = gap.toInt () + 2;
    }
  return {};
}

QString checkError (QJsonObject const& event)
{
  if (event.contains ("msg"))
    return event.size () == 3 && event.value ("msg").isString () && !event.value ("msg").toString ().isEmpty ()
      ? QString {} : "an uncoded error is not {v,t,msg}";
  auto const code = event.value ("code").toString ();
  auto const keys = event.keys ();
  if (code == "unknown_schema_version")
    return keys == QStringList {"code", "got", "t", "v"} ? QString {} : "bad unknown_schema_version";
  if (code == "configure_type_error")
    return keys == QStringList {"code", "expected", "got", "key", "t", "v"} ? QString {} : "bad configure_type_error";
  if (code == "configure_range_error")
    return keys == QStringList {"code", "detail", "key", "t", "v"} ? QString {} : "bad configure_range_error";
  return !code.isEmpty () && keys == QStringList {"code", "detail", "t", "v"}
    && event.value ("detail").isString () ? QString {} : "an error is not one of the documented shapes";
}

void checkProtocol (Run& run)
{
  QSet<qint64> terminated;
  int session = 0;
  for (int i = 0; i < run.lines.size () && run.error.isEmpty (); ++i)
    {
      auto const& line = run.lines[i];
      QJsonParseError parseError;
      auto const document = QJsonDocument::fromJson (line, &parseError);
      if (parseError.error != QJsonParseError::NoError || !document.isObject ())
        {
          run.error = "invalid JSON line: " + QString::fromUtf8 (line);
          return;
        }
      auto const event = document.object ();
      run.events.append (event);
      auto const type = event.value ("t").toString ();
      QString problem;
      if (event.value ("v").toInt (-1) != 1 || type.isEmpty ())
        problem = "bad envelope";
      else if (i == 0 && type != "error" && type != "ready")
        problem = "the first line is neither the ready line nor an error";
      else if (type == "ready")
        problem = i == 0 ? checkReady (event) : "ready after the first line";
      else if (type == "session")
        problem = event.value ("session").toInt () == ++session ? "" : "sessions are not numbered 1, 2, 3 ...";
      else if (type == "jtty_update")
        {
          auto const id = qint64 (event.value ("id").toDouble ());
          if (event.value ("session").toInt () != session)
            problem = "an update names a session other than the current one";
          else if (terminated.contains (id))
            problem = "a message has a line after its terminal line";
          else
            problem = checkUpdate (event);
          if (event.value ("state").toString () != "growing") terminated.insert (id);
        }
      else if (type == "error")
        problem = checkError (event);
      else if (type != "decode" && type != "decode_finished")
        problem = "unknown line type";
      if (!problem.isEmpty ()) run.error = problem + ": " + QString::fromUtf8 (line);
    }
}

#ifdef RJTTY_EXECUTABLE
QByteArray runRjtty (QStringList const& arguments, QString const& directory)
{
  QProcess process;
  process.setWorkingDirectory (directory);
  process.start (QString::fromUtf8 (RJTTY_EXECUTABLE), arguments);
  if (!process.waitForFinished (PlanningTimeoutMs) || process.exitCode () != 0) return {};
  return process.readAllStandardOutput ();
}
#endif

// One data directory for every run: the codec keeps its FFTW wisdom there,
// so each FFT size the decoder's search needs is measured once (up to about a
// minute on ARM Linux) and its plan is the same in every later run.
QString const& codecDirectory ()
{
  static QTemporaryDir directory;
  static QString const path {directory.isValid () ? directory.path () : QString {}};
  return path;
}

Run runCodec (QByteArray const& input, QStringList options = {}, int timeoutMs = ProcessTimeoutMs)
{
  Run run;
  auto const& directory = codecDirectory ();
  if (directory.isEmpty ()) return run;
  options << "-a" << directory << "-t" << directory << "--stream";
  QProcess process;
  process.setProcessChannelMode (QProcess::SeparateChannels);
  process.setWorkingDirectory (directory);
  process.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), options);
  run.started = process.waitForStarted (5000);
  if (!run.started) return run;
  process.write (input);
  process.closeWriteChannel ();
  if (!process.waitForFinished (timeoutMs))
    {
      run.timedOut = true;
      process.kill ();
      process.waitForFinished (5000);
    }
  run.exitCode = process.exitCode ();
  run.exitStatus = process.exitStatus ();
  run.lines = outputLines (process.readAllStandardOutput ());
  run.standardError = process.readAllStandardError ();
  checkProtocol (run);
  return run;
}

QList<QJsonObject> ofType (QList<QJsonObject> const& events, QString const& type)
{
  QList<QJsonObject> matching;
  for (auto const& event : events)
    if (event.value ("t").toString () == type) matching.append (event);
  return matching;
}

QList<QJsonObject> withoutErrors (QList<QJsonObject> const& events)
{
  QList<QJsonObject> kept;
  for (auto const& event : events)
    if (event.value ("t").toString () != "error") kept.append (event);
  return kept;
}

// The period modes' output lines, as written.
QList<QByteArray> decodeLines (Run const& run)
{
  QList<QByteArray> lines;
  for (int i = 0; i < run.events.size (); ++i)
    {
      auto const type = run.events[i].value ("t").toString ();
      if (type == "decode" || type == "decode_finished") lines.append (run.lines[i]);
    }
  return lines;
}

// Updates as the GUI has them: no session or gaps, ids counted from the
// first update's.
QList<QJsonObject> comparable (QList<QJsonObject> updates)
{
  qint64 first = -1;
  for (auto& update : updates)
    {
      auto const id = qint64 (update.value ("id").toDouble ());
      if (first < 0) first = id;
      update["id"] = double (id - first);
      for (auto const& key : {"v", "t", "session", "gaps"}) update.remove (key);
    }
  return updates;
}

// The GUI's live receive (MainWindow::pumpJttyReceive) of pcm from its first
// sample, arriving 4096 samples at a time: one decoder step while a whole
// search window is in, the updates taken after each, then the reception's end.
QList<QJsonObject> guiUpdates (QByteArray const& pcm, int low, int high, float center, float tolerance)
{
  static char const * const states[] {"growing", "complete", "expired", "ended"};
  constexpr qint64 window = FrameSamples + FrameSamples / 4;
  QList<QJsonObject> updates;
  Jtty::Decoder decoder;
  if (!decoder.valid ()) return updates;
  auto const take = [&decoder, &updates] {
    for (auto const& update : decoder.takeUpdates ())
      updates.append (QJsonObject {
        {"id", double (update.messageId)}, {"state", states[int (update.terminal)]},
        {"freq", QString::number (double (update.frequency), 'f', 3).toDouble ()}, {"snr", update.snr},
        {"start", QString::number (update.startSeconds, 'f', 6).toDouble ()},
        {"latest", QString::number (update.latestSeconds, 'f', 6).toDouble ()},
        {"text", Jtty::stripJttyFillerText (update.text)}});
  };
  auto const samples = reinterpret_cast<qint16 const *> (pcm.constData ());
  qint64 const total = pcm.size () / 2;
  decoder.begin (1, 0, 0);
  for (qint64 end = 0; end < total;)
    {
      end = qMin (end + 4096, total);
      for (;;)
        {
          auto const search = decoder.nextSearchSample ();
          if (search + window > end) break;
          auto const first = decoder.nextRequiredSample ();
          auto const processed = decoder.process (samples + first, int (search + window - first), first,
                                                  search + window, 1, low, high, center, tolerance);
          take ();
          if (processed <= 0) break;
        }
    }
  decoder.end ();
  take ();
  return comparable (updates);
}

// a and b at half amplitude each, b starting offset samples after a.
QByteArray mixed (QByteArray const& a, QByteArray const& b, int offset)
{
  auto const sample = [] (QByteArray const& pcm, int i) {
    return 2 * i + 1 < pcm.size () ? int (qFromLittleEndian<qint16> (pcm.constData () + 2 * i)) : 0;
  };
  int const total = qMax (int (a.size () / 2), int (b.size () / 2) + offset);
  QByteArray sum (2 * total, '\0');
  for (int i = 0; i < total; ++i)
    qToLittleEndian<qint16> (qint16 ((sample (a, i) + (i >= offset ? sample (b, i - offset) : 0)) / 2),
                             sum.data () + 2 * i);
  return sum;
}

QList<QJsonObject> updatesFor (QList<QJsonObject> const& events, qint64 id)
{
  QList<QJsonObject> matching;
  for (auto const& event : ofType (events, "jtty_update"))
    if (qint64 (event.value ("id").toDouble ()) == id) matching.append (event);
  return matching;
}

// The id of the first message decoded near frequency.
qint64 messageNear (QList<QJsonObject> const& events, double frequency)
{
  for (auto const& event : ofType (events, "jtty_update"))
    if (std::abs (event.value ("freq").toDouble () - frequency) < 5.0)
      return qint64 (event.value ("id").toDouble ());
  return -1;
}

QList<int> gapsOf (QJsonObject const& update)
{
  QList<int> gaps;
  for (auto const& gap : update.value ("gaps").toArray ()) gaps.append (gap.toInt ());
  return gaps;
}

QStringList textsOf (QList<QJsonObject> const& updates)
{
  QStringList texts;
  for (auto const& update : updates) texts << update.value ("text").toString ();
  return texts;
}

QJsonObject codedError (QString const& code, QString const& detail)
{
  return {{"v", 1}, {"t", "error"}, {"code", code}, {"detail", detail}};
}

QJsonObject rangeError (QString const& key)
{
  return {{"v", 1}, {"t", "error"}, {"code", "configure_range_error"}, {"key", key},
          {"detail", key + " must be from 0 to 6000 Hz"}};
}

QJsonObject const OddAudioFrame {codedError ("odd_audio_frame", "audio frame length must contain whole int16 samples")};

#define CHECK_RUN(run, status)                                          \
  do {                                                                  \
    QVERIFY2 ((run).started, "wsjt-codec did not start");               \
    QVERIFY2 (!(run).timedOut, "wsjt-codec timed out");                 \
    QCOMPARE ((run).exitStatus, QProcess::NormalExit);                  \
    QVERIFY2 ((run).error.isEmpty (), qPrintable ((run).error));        \
    QCOMPARE ((run).exitCode, (status));                                \
  } while (false)
}

class TestWsjtCodecJtty
  : public QObject
{
  Q_OBJECT

private:
  QByteArray sampleFile_;
  QByteArray sample_;
  QByteArray ft8_;
  QMap<QString, QByteArray> synthesized_;

  QByteArray const& transmission (QString const& message) const
  {
    static QByteArray const none;
    auto const found = synthesized_.find (message);
    return found == synthesized_.end () ? none : *found;
  }

private Q_SLOTS:
  void initTestCase ()
  {
    sampleFile_ = wavFile (QString::fromUtf8 (JTTY_SAMPLE_WAV));
    sample_ = wavData (sampleFile_);
    QVERIFY2 (sample_.size () > 30 * SampleRate * 2, "the JTTY sample WAV is readable");
    ft8_ = wavData (wavFile (QString::fromUtf8 (FT8_ENGINE_WAV)));
    QCOMPARE (ft8_.size (), 15 * SampleRate * 2);
    for (auto const& message : {Quick, Quoted, Gapped, Padded, QString {"<<<<< <<<<<"}})
      {
        auto const pcm = synthesize (message);
        QVERIFY2 (!pcm.isEmpty (), qPrintable ("the encoder synthesizes " + message));
        synthesized_.insert (message, pcm);
      }
    auto const warm = runCodec (header () + jtty () + audio (transmission (Quick), 4096), {}, PlanningTimeoutMs);
    QVERIFY2 (warm.started && !warm.timedOut && warm.exitCode == 0, "wsjt-codec measures JTTY's FFT plans");
    // The wisdom fits only a planner with the threads wsjt-codec initializes.
    QVERIFY (fftwf_init_threads () != 0);
    QVERIFY2 (fftwf_import_wisdom_from_filename (QFile::encodeName (codecDirectory () + "/jt9_wisdom.dat")) != 0,
              "wsjt-codec saved its FFTW wisdom");
  }

  void readyAndSession ()
  {
    auto const run = runCodec (header () + jtty ());
    CHECK_RUN (run, 0);
    QCOMPARE (run.events.size (), 1);    // a session begins with its first sample

    auto const one = runCodec (header () + jtty () + frame (0x01u, QByteArray (2, '\0')));
    CHECK_RUN (one, 0);
    QCOMPARE (one.events.size (), 2);
    QCOMPARE (one.events[1], (QJsonObject {{"v", 1}, {"t", "session"}, {"session", 1}, {"origin", 0}}));
  }

  // A session begins with its first audio sample, so controls between
  // samples never open empty sessions and no session number is skipped.
  void sessionsBeginAtTheirFirstAudio ()
  {
    auto const discontinuity = control (R"({"t":"discontinuity"})");
    auto const run = runCodec (
      header () + jtty () + discontinuity + discontinuity + frame (0x01u, {})
      + audio (QByteArray (2 * 1000, '\0'), 400) + discontinuity + discontinuity
      + frame (0x01u, QByteArray (1, '\0')) + audio (QByteArray (2 * 500, '\0'), 400)
      + discontinuity + control (R"({"t":"configure","rxfreq":1500})"));
    CHECK_RUN (run, 0);
    QCOMPARE (ofType (run.events, "session"),
              (QList<QJsonObject> {
                {{"v", 1}, {"t", "session"}, {"session", 1}, {"origin", 0}},
                {{"v", 1}, {"t", "session"}, {"session", 2}, {"origin", 1000}}}));
    QCOMPARE (ofType (run.events, "error"), QList<QJsonObject> {OddAudioFrame});

    auto const none = runCodec (header () + jtty () + discontinuity + control (R"({"t":"halt"})")
                                + audio (QByteArray (2 * 1000, '\0'), 400));
    CHECK_RUN (none, 0);
    QCOMPARE (none.events.size (), 1);
  }

  // Configured as rjtty searches, the codec reports every frame rjtty
  // accepts from the in-tree sample: the same text, frequency and sync time.
  void sampleDecodesAsRjttyDoes ()
  {
#ifndef RJTTY_EXECUTABLE
    QSKIP ("rjtty is built only with WSJT_BUILD_UTILS");
#else
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QFile copy {directory.filePath ("sample.wav")};
    QVERIFY (copy.open (QIODevice::WriteOnly) && copy.write (sampleFile_) == sampleFile_.size ());
    copy.close ();
    // `rjtty smin 0 ...` prints each accepted frame as "<nint(f1)>  <text so far>";
    // `rjtty smin 1 ...` prints every candidate, accepted ones in the same order,
    // with its frequency and sync time in fixed columns (rjtty's format 3002).
    QStringList const settings {"384", "1500", "50", "sample.wav"};
    auto const accepted = outputLines (runRjtty (QStringList {"4.6", "0"} + settings, directory.path ()));
    auto const candidates = outputLines (runRjtty (QStringList {"4.6", "1"} + settings, directory.path ()));
    QVERIFY2 (!accepted.isEmpty (), "rjtty decodes the sample");

    struct Reference
    {
      double frequency;
      double tsync;
      QString text;
    };
    QList<Reference> references;
    int next = 0;
    for (auto const& line : accepted)
      {
        auto const frequency = line.left (4).trimmed ().toInt ();
        auto const text = QString::fromLatin1 (line.mid (6));
        for (; next < candidates.size (); ++next)
          {
            auto const& candidate = candidates[next];
            bool frequencyOk {false}, tsyncOk {false};
            auto const f1 = candidate.mid (30, 7).toDouble (&frequencyOk);
            auto const tsync = candidate.mid (44, 9).toDouble (&tsyncOk);
            if (frequencyOk && tsyncOk && qRound (f1) == frequency
                && QString::fromLatin1 (candidate.mid (68)) == text)
              {
                references.append ({f1, tsync, text});
                ++next;
                break;
              }
          }
      }
    QCOMPARE (references.size (), accepted.size ());

    auto const run = runCodec (header () + control (RjttySettings) + audio (sample_, 1000));
    CHECK_RUN (run, 0);
    QVERIFY (ofType (run.events, "error").isEmpty ());
    QList<QJsonObject> frames;
    for (auto const& update : ofType (run.events, "jtty_update"))
      if (update.value ("state").toString () == "growing" || update.value ("state").toString () == "complete")
        frames.append (update);
    QCOMPARE (frames.size (), references.size ());
    QMap<qint64, double> starts;
    for (auto const& reference : references)
      {
        auto const match = std::find_if (frames.begin (), frames.end (), [&reference] (QJsonObject const& update) {
          return update.value ("text").toString () == reference.text
            && std::abs (update.value ("freq").toDouble () - reference.frequency) < 0.06
            && std::abs (update.value ("latest").toDouble () - reference.tsync) < 0.0006;
        });
        QVERIFY2 (match != frames.end (), qPrintable ("rjtty's frame \"" + reference.text + "\" is reported"));
        auto const id = qint64 (match->value ("id").toDouble ());
        if (!starts.contains (id)) starts.insert (id, reference.tsync);
        QVERIFY (std::abs (match->value ("start").toDouble () - starts[id]) < 0.0006);
        frames.erase (match);
      }
#endif
  }

  // The GUI's live receive gives the same updates from the same audio and
  // settings, field for field.
  void decodesAsTheGuiDoes_data ()
  {
    QTest::addColumn<QByteArray> ("pcm");
    QTest::addColumn<int> ("rxfreq");
    QTest::addColumn<int> ("ntol");
    QTest::addColumn<int> ("nfa");
    QTest::addColumn<int> ("nfb");
    auto gapped = transmission (Gapped);
    gapped.replace (2 * (LeadSamples + 4 * FrameSamples), 2 * FrameSamples, QByteArray (2 * FrameSamples, '\0'));
    QTest::newRow ("one transmission") << transmission (Quick) << 1500 << 20 << 200 << 4000;
    QTest::newRow ("a missed frame") << gapped << 1500 << 20 << 200 << 4000;
    QTest::newRow ("two transmissions")
      << mixed (transmission (Quick), synthesize (Quoted, 1650.0f), 2 * FrameSamples) << 1500 << 20 << 200 << 4000;
    QTest::newRow ("the sample") << sample_ << 1500 << 50 << 200 << 2800;
  }

  void decodesAsTheGuiDoes ()
  {
    QFETCH (QByteArray, pcm);
    QFETCH (int, rxfreq);
    QFETCH (int, ntol);
    QFETCH (int, nfa);
    QFETCH (int, nfb);
    QJsonObject settings {{"t", "configure"}, {"mode", "JTTY"}, {"rxfreq", rxfreq}, {"ntol", ntol},
                          {"nfa", nfa}, {"nfb", nfb}};
    auto const run = runCodec (header () + control (QJsonDocument {settings}.toJson (QJsonDocument::Compact))
                               + audio (pcm, 4096));
    CHECK_RUN (run, 0);
    auto const codec = comparable (ofType (run.events, "jtty_update"));
    QVERIFY (!codec.isEmpty ());
    QCOMPARE (codec, guiUpdates (pcm, nfa, nfb, float (rxfreq), float (ntol)));
  }

  // The event stream does not depend on how the audio is framed, on frame
  // types or controls it ignores, or on a configure that restates the
  // settings.
  void framingDoesNotChangeTheDecodes ()
  {
    auto const settings = control (RjttySettings);
    auto const updatesOnly = [] (Run const& r) { return ofType (r.events, "jtty_update"); };
    auto const run = runCodec (header () + settings + audio (sample_, 1000));
    CHECK_RUN (run, 0);
    QVERIFY (!updatesOnly (run).isEmpty ());

    auto const whole = runCodec (header () + settings + audio (sample_, int (sample_.size () / 2)));
    CHECK_RUN (whole, 0);
    QCOMPARE (updatesOnly (whole), updatesOnly (run));
    auto const ragged = runCodec (header () + settings + audio (sample_, 7));
    CHECK_RUN (ragged, 0);
    QCOMPARE (updatesOnly (ragged), updatesOnly (run));

    int const half = int (sample_.size () / 4);
    auto const interleaved = runCodec (
      header () + frame (0x7fu, "ignored") + settings + audio (sample_, 1000, 0, half)
      + settings + control (R"({"t":"a_future_control"})") + frame (0x00u, {})
      + control (R"({"t":"configure","depth":3,"mycall":"K1ABC","utc":"12:00:00"})")
      + audio (sample_, 1000, half));
    CHECK_RUN (interleaved, 0);
    QCOMPARE (ofType (interleaved.events, "session").size (), 1);
    QVERIFY (ofType (interleaved.events, "error").isEmpty ());
    QCOMPARE (updatesOnly (interleaved), updatesOnly (run));
  }

  // configure steers later decoding and never ends the session.
  void configureAppliesToLaterAudio ()
  {
    auto const& pcm = transmission (Quoted);
    auto const run = runCodec (
      header () + control (R"({"t":"configure","mode":"JTTY","rxfreq":2500,"ntol":50,"nfa":1600,"nfb":3000})")
      + audio (pcm, 4096) + control (R"({"t":"configure","rxfreq":1500,"nfa":200})")
      + audio (pcm, 4096));
    CHECK_RUN (run, 0);
    QCOMPARE (ofType (run.events, "session").size (), 1);
    auto const updates = ofType (run.events, "jtty_update");
    QVERIFY (!updates.isEmpty ());
    double const secondCopy = double (pcm.size () / 2) / SampleRate;
    for (auto const& update : updates)
      QVERIFY2 (update.value ("start").toDouble () > secondCopy,
                "the transmission outside the configured search is not decoded");
    QCOMPARE (updates.last ().value ("state").toString (), QString {"complete"});
  }

  // A configure that repeats the mode or changes only trperiod keeps the
  // session but resets the search keys it does not set: here nfb, which
  // had hidden 1500 Hz.
  void repeatedModeResetsUnsetKeys_data ()
  {
    QTest::addColumn<QByteArray> ("repeat");
    QTest::newRow ("mode") << QByteArray {R"({"t":"configure","mode":"JTTY"})"};
    QTest::newRow ("trperiod") << QByteArray {R"({"t":"configure","trperiod":30})"};
  }

  void repeatedModeResetsUnsetKeys ()
  {
    QFETCH (QByteArray, repeat);
    auto const& pcm = transmission (Quick);
    // These settings need FFT sizes the warm-up does not measure.
    auto const hidden = control (R"({"t":"configure","mode":"JTTY","rxfreq":2500,"ntol":50,"nfa":200,"nfb":1000})");
    auto const unseen = runCodec (header () + hidden + audio (pcm, 4096), {}, PlanningTimeoutMs);
    CHECK_RUN (unseen, 0);
    QVERIFY (ofType (unseen.events, "jtty_update").isEmpty ());

    auto const run = runCodec (header () + hidden + audio (pcm, 4096, 0, LeadSamples) + control (repeat)
                               + audio (pcm, 4096, LeadSamples), {}, PlanningTimeoutMs);
    CHECK_RUN (run, 0);
    QCOMPARE (ofType (run.events, "session").size (), 1);
    auto const updates = updatesFor (run.events, messageNear (run.events, 1500.0));
    QVERIFY (!updates.isEmpty ());
    QCOMPARE (updates.last ().value ("state").toString (), QString {"complete"});
    QCOMPARE (updates.last ().value ("text").toString (), Quick);
  }

  // A trperiod change, validated and unused in JTTY, keeps the session.
  void trperiodChangeKeepsTheSession ()
  {
    auto const& pcm = transmission (Quick);
    int const cut = LeadSamples + 4 * FrameSamples + FrameSamples / 4;
    auto const plain = runCodec (header () + jtty () + audio (pcm, 4096));
    CHECK_RUN (plain, 0);
    QVERIFY (!ofType (plain.events, "jtty_update").isEmpty ());
    auto const changed = runCodec (header () + jtty () + audio (pcm, 4096, 0, cut)
                                   + control (R"({"t":"configure","trperiod":30})") + audio (pcm, 4096, cut));
    CHECK_RUN (changed, 0);
    QCOMPARE (ofType (changed.events, "session").size (), 1);
    QCOMPARE (changed.events, plain.events);
  }

  // Entering JTTY, rxfreq carries over and ntol becomes 20 Hz unless the
  // frame sets it; a value the frame sets is not capped as FT8's is. Away
  // from rxfreq, with the default nfa and nfb, the decoder searches only 1200
  // to 1800 Hz.
  void enteringJttySetsItsDefaults_data ()
  {
    QTest::addColumn<QByteArray> ("configures");
    QTest::addColumn<float> ("frequency");
    QTest::addColumn<bool> ("decoded");
    QTest::newRow ("ntol 20") << control (R"({"t":"configure","mode":"FT8"})") + jtty () << 2200.0f << false;
    QTest::newRow ("rxfreq carried over")
      << control (R"({"t":"configure","mode":"FT8","rxfreq":2200})") + jtty () << 2200.0f << true;
    QTest::newRow ("ntol 3000") << control (R"({"t":"configure","mode":"JTTY","ntol":3000})") << 2800.0f << true;
  }

  void enteringJttySetsItsDefaults ()
  {
    QFETCH (QByteArray, configures);
    QFETCH (float, frequency);
    QFETCH (bool, decoded);
    auto const run = runCodec (header () + configures + audio (synthesize (Quick, frequency), 4096));
    CHECK_RUN (run, 0);
    QVERIFY (ofType (run.events, "error").isEmpty ());
    auto const updates = ofType (run.events, "jtty_update");
    QCOMPARE (!updates.isEmpty (), decoded);
    if (decoded)
      {
        QCOMPARE (updatesFor (run.events, messageNear (run.events, frequency)).size (), updates.size ());
        QCOMPARE (updates.last ().value ("state").toString (), QString {"complete"});
        QCOMPARE (updates.last ().value ("text").toString (), Quick);
      }
  }

  // Controls are parsed, and their errors reported, as for the other
  // modes, plus the range of the frequencies. Each declined configure but
  // the rxfreq 2147483647 one carries keys that would hide the transmission
  // at 1500 Hz or leave JTTY, so applying any of them would lose the decode.
  void controlErrorsAreReported ()
  {
    auto const hide = QByteArray {R"("rxfreq":2500,"nfa":1600)"};
    QByteArray oversized {R"({"t":"configure",)" + hide + "}"};
    oversized += QByteArray (1025 - oversized.size (), ' ');
    QByteArray largest {R"({"t":"configure","ntol":20})"};
    largest += QByteArray (1024 - largest.size (), ' ');
    auto const run = runCodec (
      header () + control (R"({"t":"configure","mode":"FT8"})")
      + control (R"({"t":"configure","mode":"JTTY","rxfreq":0,"ntol":6000,"nfa":0,"nfb":6000})")
      + control (R"({"t":"configure","rxfreq":1500,"ntol":20,"nfa":200,"nfb":3000})")
      + control (largest)
      + control (R"({"t":"configure","rxfreq":2500.0,"nfa":1600.0})")
      + control (R"({"t":"configure","ntol":"wide",)" + hide + "}")
      + control (R"({"t":"configure","depth":"deep",)" + hide + "}")
      + control (R"({"t":"configure","version":2,)" + hide + "}")
      + control (R"({"t":"configure","mode":"BOGUS",)" + hide + "}")
      + control (R"({"t":"configure","mode":"FT8","trperiod":0})")
      + control (R"({"t":"configure","mode":8,"trperiod":1801})")
      + control (R"({"t":"configure","ntol":-1,)" + hide + "}")
      + control (R"({"t":"configure","rxfreq":2147483647})")
      + control (R"({"t":"configure","rxfreq":2500,"nfa":1600,"nfb":6001})")
      + control ("not json") + control (R"({"x":1})") + control (R"({"t":7})")
      + control (oversized)
      + audio (transmission (Quoted), 4096));
    CHECK_RUN (run, 0);
    auto const invalidTrperiod = codedError ("invalid_trperiod", "trperiod must be finite and between 0 and 1800 seconds");
    auto const parseError = codedError ("configure_parse_error", "malformed control frame (missing or invalid t field)");
    QCOMPARE (ofType (run.events, "error"), (QList<QJsonObject> {
      {{"v", 1}, {"t", "error"}, {"code", "configure_type_error"}, {"key", "ntol"},
       {"expected", "int"}, {"got", "string"}},
      {{"v", 1}, {"t", "error"}, {"code", "configure_type_error"}, {"key", "depth"},
       {"expected", "int"}, {"got", "string"}},
      {{"v", 1}, {"t", "error"}, {"code", "unknown_schema_version"}, {"got", 2}},
      codedError ("unknown_mode", "configure frame contains an unsupported mode"),
      invalidTrperiod, invalidTrperiod,
      rangeError ("ntol"), rangeError ("rxfreq"), rangeError ("nfb"),
      parseError, parseError, parseError,
      codedError ("control_frame_too_large", "control frame exceeds the configured buffer capacity")}));
    QCOMPARE (ofType (run.events, "session").size (), 1);
    auto const updates = ofType (run.events, "jtty_update");
    QVERIFY2 (!updates.isEmpty (), "no declined configure was applied");
    QCOMPARE (updates.last ().value ("text").toString (), Quoted);
  }

  // The range applies to the values JTTY would decode with, including one
  // carried over from another mode, and only in JTTY.
  void carriedOverValueOutOfRange ()
  {
    auto const run = runCodec (header () + control (R"({"t":"configure","mode":"FT8","rxfreq":9000})")
                               + jtty () + audio (transmission (Quick), 4096));
    CHECK_RUN (run, 0);
    QCOMPARE (ofType (run.events, "error"), QList<QJsonObject> {rangeError ("rxfreq")});
    QVERIFY2 (ofType (run.events, "session").isEmpty (), "the declined configure left FT8");
    QVERIFY (!ofType (run.events, "decode_finished").isEmpty ());
  }

  // An odd-length audio frame is dropped whole. Before any audio it leaves
  // the events as they are without it; in a session it is a hole, which
  // ends the session as a discontinuity does.
  void oddAudioFrameIsDropped ()
  {
    auto const odd = frame (0x01u, QByteArray (2001, '\x55'));
    auto const& quoted = transmission (Quoted);
    auto const reference = runCodec (header () + jtty () + audio (quoted, 4096));
    CHECK_RUN (reference, 0);
    QVERIFY (!ofType (reference.events, "jtty_update").isEmpty ());
    auto const first = runCodec (header () + jtty () + odd + audio (quoted, 4096));
    CHECK_RUN (first, 0);
    QCOMPARE (ofType (first.events, "error"), QList<QJsonObject> {OddAudioFrame});
    QCOMPARE (withoutErrors (first.events), reference.events);

    auto const& quick = transmission (Quick);
    int const cut = LeadSamples + 4 * FrameSamples + FrameSamples / 4;
    auto const discontinued = runCodec (header () + jtty () + audio (quick, 4096, 0, cut)
                                        + control (R"({"t":"discontinuity"})") + audio (quick, 4096, cut));
    CHECK_RUN (discontinued, 0);
    QCOMPARE (ofType (discontinued.events, "session").size (), 2);
    auto const holed = runCodec (header () + jtty () + audio (quick, 4096, 0, cut) + odd
                                 + audio (quick, 4096, cut));
    CHECK_RUN (holed, 0);
    QCOMPARE (ofType (holed.events, "error"), QList<QJsonObject> {OddAudioFrame});
    QCOMPARE (withoutErrors (holed.events), discontinued.events);
    auto const at = holed.events.indexOf (OddAudioFrame);
    QCOMPARE (holed.events.value (at + 1).value ("state").toString (), QString {"ended"});
  }

  // A frame lost in the middle of a message is a gap marker; typed dots are
  // not.
  void gapMarkersAreReported ()
  {
    auto pcm = transmission (Gapped);
    int const frames = (int (pcm.size () / 2) - LeadSamples) / FrameSamples - 1;
    QCOMPARE (frames, 11);    // eleven five-character text frames
    int const dropped = 5;    // characters 20-24, "WN FO"
    pcm.replace (2 * (LeadSamples + (dropped - 1) * FrameSamples), 2 * FrameSamples,
                 QByteArray (2 * FrameSamples, '\0'));

    auto const run = runCodec (header () + jtty () + audio (pcm, 4096));
    CHECK_RUN (run, 0);
    auto const updates = updatesFor (run.events, messageNear (run.events, 1500.0));
    QVERIFY (!updates.isEmpty ());
    QCOMPARE (ofType (run.events, "jtty_update").size (), updates.size ());
    auto const last = updates.last ();
    QCOMPARE (last.value ("state").toString (), QString {"complete"});

    int const lost = 5 * (dropped - 1);
    auto const text = last.value ("text").toString ();
    QCOMPARE (text, Gapped.left (lost) + " ... " + Gapped.mid (lost + 5));
    QCOMPARE (gapsOf (last), QList<int> {lost + 1});
    QCOMPARE (text.indexOf ("..."), 3);    // the typed dots come first, unreported
    for (auto const& update : updates)
      QCOMPARE (gapsOf (update), update.value ("text").toString ().size () > lost
                ? QList<int> {lost + 1} : QList<int> {});
  }

  // Live-entry filler is removed from text, as the GUI removes it; an update
  // that is only filler has empty text and is still reported.
  void fillerIsRemoved ()
  {
    auto const run = runCodec (header () + jtty () + audio (transmission (Padded), 4096));
    CHECK_RUN (run, 0);
    auto const updates = ofType (run.events, "jtty_update");
    QVERIFY (!updates.isEmpty ());
    QCOMPARE (updates.last ().value ("state").toString (), QString {"complete"});
    QCOMPARE (updates.last ().value ("text").toString (), QString {"HELLO BOB HOW ARE YOU"});
    QVERIFY2 (textsOf (updates).contains ("HELLO BOB"), qPrintable (textsOf (updates).join ('|')));

    auto const filler = runCodec (header () + jtty () + audio (transmission ("<<<<< <<<<<"), 4096));
    CHECK_RUN (filler, 0);
    auto const only = ofType (filler.events, "jtty_update");
    QVERIFY (only.size () >= 2);
    QCOMPARE (only.first ().value ("text").toString (), QString {});
    QCOMPARE (only.last ().value ("state").toString (), QString {"complete"});
    QCOMPARE (only.last ().value ("text").toString (), QString {});
  }

  // A message whose last frame never comes expires once the search has
  // moved far enough past its last frame.
  void unfinishedMessageExpires ()
  {
    auto const& pcm = transmission (Quick);
    auto const run = runCodec (header () + jtty () + audio (pcm, 4096, 0, LeadSamples + 4 * FrameSamples)
                               + silence (12 * SampleRate));
    CHECK_RUN (run, 0);
    auto const updates = updatesFor (run.events, messageNear (run.events, 1500.0));
    QVERIFY (!updates.isEmpty ());
    QCOMPARE (updates.last ().value ("state").toString (), QString {"expired"});
    QCOMPARE (updates.last ().value ("text").toString (), QString {"THE QUICK BROWN FOX"});
    QCOMPARE (ofType (run.events, "jtty_update").size (), updates.size ());
  }

  // discontinuity ends the message in flight and begins session 2 at the
  // next audio sample; sample counting continues. The cut falls a search
  // step into the fifth frame, so session 1 holds four whole frames and
  // session 2 the frames after the fifth.
  void discontinuityEndsTheMessage ()
  {
    auto const& pcm = transmission (Quick);
    int const cut = LeadSamples + 4 * FrameSamples + FrameSamples / 4;
    auto const run = runCodec (header () + jtty () + audio (pcm, 4096, 0, cut)
                               + control (R"({"t":"discontinuity"})")
                               + audio (pcm, 4096, cut));
    CHECK_RUN (run, 0);

    auto const sessions = ofType (run.events, "session");
    QCOMPARE (sessions.size (), 2);
    QCOMPARE (sessions[1].value ("origin").toDouble (), double (cut));
    auto const first = messageNear (run.events, 1500.0);
    auto const before = updatesFor (run.events, first);
    QCOMPARE (textsOf (before), (QStringList {"THE Q", "THE QUICK", "THE QUICK BROWN",
                                              "THE QUICK BROWN FOX", "THE QUICK BROWN FOX"}));
    QCOMPARE (before.last ().value ("state").toString (), QString {"ended"});
    for (auto const& update : before) QCOMPARE (update.value ("session").toInt (), 1);
    QVERIFY (run.events.indexOf (before.last ()) < run.events.indexOf (sessions[1]));

    QList<QJsonObject> after;
    for (auto const& update : ofType (run.events, "jtty_update"))
      if (update.value ("session").toInt () == 2) after.append (update);
    QCOMPARE (textsOf (after), (QStringList {"D OVE", "D OVER THE", "D OVER THE LAZY",
                                             "D OVER THE LAZY DOG."}));
    QCOMPARE (after.last ().value ("state").toString (), QString {"complete"});
    for (auto const& update : after)
      {
        QVERIFY (qint64 (update.value ("id").toDouble ()) != first);
        QVERIFY (update.value ("start").toDouble () >= double (cut) / SampleRate);
      }
  }

  // halt and end of input both end the session, flush, and exit 0.
  void haltAndEndOfInputFlush ()
  {
    auto const& pcm = transmission (Quick);
    int const cut = LeadSamples + 4 * FrameSamples;
    auto const partial = header () + jtty () + audio (pcm, 4096, 0, cut);
    auto const endsInFlight = [] (Run const& run) {
      auto const updates = ofType (run.events, "jtty_update");
      return !updates.isEmpty () && run.events.last () == updates.last ()
        && updates.last ().value ("state").toString () == "ended"
        && ofType (run.events, "session").size () == 1;
    };

    auto const halted = runCodec (partial + control (R"({"t":"halt"})")
                                  + audio (pcm, 4096, cut));
    CHECK_RUN (halted, 0);
    QVERIFY2 (endsInFlight (halted), "halt ends the message in flight and reads no further");

    auto const eof = runCodec (partial);
    CHECK_RUN (eof, 0);
    QVERIFY2 (endsInFlight (eof), "end of input ends the message in flight");
    QCOMPARE (eof.events, halted.events);
  }

  // A change to JTTY in the middle of an FT8 period discards the period, as
  // any mode change does. The session's samples count from the stream's
  // first, so its decodes are those of JTTY alone, later by the FT8 audio.
  void jttyAfterPartFt8Period ()
  {
    int const ft8Samples = 6 * SampleRate;
    auto const& pcm = transmission (Quick);
    auto const alone = runCodec (header () + jtty () + audio (pcm, 4096));
    CHECK_RUN (alone, 0);
    auto const run = runCodec (header () + control (Ft8Configure) + frame (0x01u, ft8_.left (2 * ft8Samples))
                               + jtty () + audio (pcm, 4096));
    CHECK_RUN (run, 0);
    QVERIFY (decodeLines (run).isEmpty ());
    auto const sessions = ofType (run.events, "session");
    QCOMPARE (sessions.size (), 1);
    QCOMPARE (sessions[0].value ("origin").toInt (), ft8Samples);
    auto const updates = ofType (run.events, "jtty_update");
    auto const expected = ofType (alone.events, "jtty_update");
    QVERIFY (!expected.isEmpty ());
    QCOMPARE (updates.size (), expected.size ());
    for (int i = 0; i < updates.size (); ++i)
      {
        auto shifted = expected[i];
        for (auto const& key : {"start", "latest"})
          {
            QVERIFY (std::abs (updates[i].value (key).toDouble () - expected[i].value (key).toDouble () - 6.0) < 1e-6);
            shifted[key] = updates[i].value (key);
          }
        QCOMPARE (updates[i], shifted);
      }
  }

  // Leaving JTTY ends the session before anything of the next mode, whose
  // periods start at the change.
  void ft8AfterJtty ()
  {
    auto const& pcm = transmission (Quick);
    int const cut = LeadSamples + 4 * FrameSamples + FrameSamples / 4;
    auto const ft8 = control (Ft8Configure) + frame (0x01u, ft8_);
    auto const alone = runCodec (header () + ft8);
    CHECK_RUN (alone, 0);
    QCOMPARE (ofType (alone.events, "decode_finished").size (), 1);
    auto const run = runCodec (header () + jtty () + audio (pcm, 4096, 0, cut) + ft8);
    CHECK_RUN (run, 0);
    auto const updates = ofType (run.events, "jtty_update");
    QVERIFY (!updates.isEmpty ());
    QCOMPARE (updates.last ().value ("state").toString (), QString {"ended"});
    auto const firstDecode = std::find_if (run.events.cbegin (), run.events.cend (), [] (QJsonObject const& event) {
      return event.value ("t").toString () == "decode" || event.value ("t").toString () == "decode_finished";
    });
    QVERIFY (firstDecode != run.events.cend ());
    QVERIFY (run.events.indexOf (updates.last ()) < int (firstDecode - run.events.cbegin ()));
    QCOMPARE (decodeLines (run), decodeLines (alone));
  }

  // A JTTY session between two FT8 periods leaves the FT8 decodes as they
  // are without it.
  void jttyBetweenFt8Periods ()
  {
    auto const ft8 = control (Ft8Configure) + frame (0x01u, ft8_);
    auto const plain = runCodec (header () + ft8 + ft8);
    CHECK_RUN (plain, 0);
    QCOMPARE (ofType (plain.events, "decode_finished").size (), 2);
    auto const run = runCodec (header () + ft8 + jtty () + audio (transmission (Quick), 4096) + ft8);
    CHECK_RUN (run, 0);
    QCOMPARE (decodeLines (run), decodeLines (plain));
    auto const updates = ofType (run.events, "jtty_update");
    QVERIFY (!updates.isEmpty ());
    QCOMPARE (updates.last ().value ("state").toString (), QString {"complete"});
    QCOMPARE (updates.last ().value ("text").toString (), Quick);
  }

  // A whole FT8 period counts all its samples, beyond the decoder's share.
  void jttyAfterWholeFt8Period ()
  {
    auto const run = runCodec (header () + control (Ft8Configure) + frame (0x01u, ft8_) + jtty ()
                               + audio (transmission (Quick), 4096));
    CHECK_RUN (run, 0);
    QCOMPARE (ofType (run.events, "decode_finished").size (), 1);
    auto const sessions = ofType (run.events, "session");
    QCOMPARE (sessions.size (), 1);
    QCOMPARE (sessions[0].value ("origin").toInt (), 15 * SampleRate);
  }

  // A change of mode discards the period in flight even when trperiod stays.
  void jttyWithFt8sTrperiodDiscardsThePeriod ()
  {
    auto const run = runCodec (header () + control (Ft8Configure) + frame (0x01u, ft8_.left (2 * 6 * SampleRate))
                               + control (R"({"t":"configure","mode":"JTTY","trperiod":15})")
                               + control (R"({"t":"halt"})"));
    CHECK_RUN (run, 0);
    QVERIFY (decodeLines (run).isEmpty ());
  }

  // A declined configure leaves the period modes' time labels as they were.
  void declinedConfigureKeepsTheTimeLabels ()
  {
    auto const ft8 = control (R"({"t":"configure","mode":"FT8","nutc":1200})");
    auto const alone = runCodec (header () + ft8 + frame (0x01u, ft8_));
    CHECK_RUN (alone, 0);
    QVERIFY (!decodeLines (alone).isEmpty ());
    auto const run = runCodec (header () + ft8 + control (R"({"t":"configure","mode":"JTTY","rxfreq":9000})")
                               + frame (0x01u, ft8_));
    CHECK_RUN (run, 0);
    QCOMPARE (ofType (run.events, "error"), QList<QJsonObject> {rangeError ("rxfreq")});
    QCOMPARE (decodeLines (run), decodeLines (alone));
  }

  // The JTTY alphabet includes '"'.
  void quotesAreEscaped ()
  {
    auto const run = runCodec (header () + jtty () + audio (transmission (Quoted), 4096));
    CHECK_RUN (run, 0);
    auto const updates = ofType (run.events, "jtty_update");
    QVERIFY (!updates.isEmpty ());
    QCOMPARE (updates.last ().value ("state").toString (), QString {"complete"});
    QCOMPARE (updates.last ().value ("text").toString (), Quoted);
    bool escaped = false;
    for (auto const& line : run.lines) escaped |= line.contains ("\"text\":\"HE SAID \\\"73\\\" OK\"");
    QVERIFY (escaped);
  }
};

QTEST_GUILESS_MAIN (TestWsjtCodecJtty);

#include "test_wsjt_codec_jtty.moc"
