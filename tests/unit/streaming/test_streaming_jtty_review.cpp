// SPDX-License-Identifier: GPL-3.0-or-later
// JTTY review in wsjt-codec: the jtty_review request, its
// jtty_update lines marked "review", its reviewed line, the 180 s history it
// reads, and its busy lines, end to end through the codec's stdin and stdout.
// The oracle is the GUI's review: Jtty::Decoder driven as
// MainWindow::startJttyReview and MainWindow::pumpJttyReceive drive it
// (widgets/mainwindow_jtty_receive.cpp), on the same samples.

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

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
constexpr int FrameSamples = 59 * 384;
constexpr int LeadSamples = 6000;
constexpr qint64 HistorySamples = 180 * SampleRate;    // JttyReceiveHistory::capacity
constexpr int PlanningTimeoutMs = 240000;

// A codec run's limit: 120 s, or a third of the function timeout a slower
// (sanitizer) build sets.
int processTimeoutMs ()
{
  return qMax (120000, qEnvironmentVariableIntValue ("QTEST_FUNCTION_TIMEOUT") / 3);
}

QString const Quick {"THE QUICK BROWN FOX JUMPED OVER THE LAZY DOG."};
QString const Quoted {"HE SAID \"73\" OK"};

struct Run
{
  bool started {}, timedOut {};
  int exitCode {-1};
  QProcess::ExitStatus exitStatus {QProcess::CrashExit};
  QList<QJsonObject> events;
  QString error;
};

void put16 (QByteArray& bytes, quint16 value)
{
  bytes.append (char (value & 0xffu));
  bytes.append (char (value >> 8));
}

QByteArray header ()
{
  QByteArray bytes {"WSJT", 4};
  bytes.append (char (0));
  bytes.append (char (1));
  put16 (bytes, 12);
  return bytes;
}

QByteArray frame (quint8 type, QByteArray const& body)
{
  QByteArray bytes;
  bytes.append (char (type));
  put16 (bytes, quint16 (body.size () & 0xffff));
  put16 (bytes, quint16 (body.size () >> 16));
  return bytes + body;
}

QByteArray control (QByteArray const& json) { return frame (0x02u, json); }

QByteArray audio (QByteArray const& pcm, int perFrame = 4096)
{
  QByteArray bytes;
  for (int offset = 0; offset < pcm.size (); offset += 2 * perFrame)
    bytes += frame (0x01u, pcm.mid (offset, 2 * perFrame));
  return bytes;
}

QByteArray configure (int rxfreq, int ntol, int nfa, int nfb, bool events = false)
{
  QJsonObject settings {{"t", "configure"}, {"mode", "JTTY"}, {"rxfreq", rxfreq}, {"ntol", ntol},
                        {"nfa", nfa}, {"nfb", nfb}};
  if (events) settings["busy_events"] = true;
  return control (QJsonDocument {settings}.toJson (QJsonDocument::Compact));
}

QByteArray review (QJsonObject request)
{
  request["t"] = "jtty_review";
  return control (QJsonDocument {request}.toJson (QJsonDocument::Compact));
}

// A clean transmission of message at f0 after LeadSamples of silence, then a
// frame of silence (tests/unit/jtty/test_wsjt_codec_jtty.cpp's synthesize).
QByteArray synthesize (QString const& message, float f0)
{
  QByteArray field (80, ' ');
  auto const bytes = message.toLatin1 ();
  std::copy (bytes.cbegin (), bytes.cend (), field.begin ());
  std::array<int, 59 * 16> tones {};
  int symbols {0};
  genjtty_ (field.data (), tones.data (), &symbols, 80);
  int nsps {384};
  float bt {2.0f}, fsample {float (SampleRate)};
  int icmplx {0}, nwave {symbols * nsps};
  std::vector<float> wave (nwave);
  std::vector<std::complex<float>> unused (nwave);
  gen_jttywave_ (tones.data (), &symbols, &nsps, &bt, &fsample, &f0,
                 reinterpret_cast<float *> (unused.data ()), wave.data (), &icmplx, &nwave);
  QByteArray pcm (2 * (LeadSamples + nwave + FrameSamples), '\0');
  for (int i = 0; i < nwave; ++i)
    qToLittleEndian<qint16> (qint16 (std::lround (20000.0 * wave[i])), pcm.data () + 2 * (LeadSamples + i));
  return pcm;
}

// a and b at half amplitude each, b starting offset samples after a.
QByteArray mixed (QByteArray const& a, QByteArray const& b, int offset)
{
  auto const at = [] (QByteArray const& pcm, int i) {
    return 2 * i + 1 < pcm.size () ? int (qFromLittleEndian<qint16> (pcm.constData () + 2 * i)) : 0;
  };
  int const total = qMax (int (a.size () / 2), int (b.size () / 2) + offset);
  QByteArray sum (2 * total, '\0');
  for (int i = 0; i < total; ++i)
    qToLittleEndian<qint16> (qint16 ((at (a, i) + (i >= offset ? at (b, i - offset) : 0)) / 2), sum.data () + 2 * i);
  return sum;
}

QByteArray silence (int samples) { return QByteArray (2 * samples, '\0'); }

QString const& codecDirectory ()
{
  static QTemporaryDir directory;
  static QString const path {directory.isValid () ? directory.path () : QString {}};
  return path;
}

void parse (Run& run, QByteArray const& output)
{
  for (auto line : output.split ('\n'))
    {
      if (line.endsWith ('\r')) line.chop (1);
      if (line.isEmpty ()) continue;
      QJsonParseError error;
      auto const document = QJsonDocument::fromJson (line, &error);
      if (error.error != QJsonParseError::NoError || !document.isObject ())
        {
          if (run.error.isEmpty ()) run.error = "invalid JSON line: " + QString::fromUtf8 (line);
          continue;
        }
      run.events.append (document.object ());
    }
}

// The codec on input; with after, it first waits for a line containing
// marker and only then sends after, so later input arrives while the
// codec works on what came before.
Run runCodec (QByteArray const& input, QByteArray const& marker = {}, QByteArray const& after = {},
              int timeoutMs = processTimeoutMs ())
{
  Run run;
  QProcess process;
  process.setProcessChannelMode (QProcess::SeparateChannels);
  process.setWorkingDirectory (codecDirectory ());
  process.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
                 {"-a", codecDirectory (), "-t", codecDirectory ()});
  run.started = process.waitForStarted (5000);
  if (!run.started) return run;
  process.write (input);
  QByteArray output;
  if (!marker.isEmpty ())
    {
      QElapsedTimer timer;
      timer.start ();
      while (!output.contains (marker) && timer.elapsed () < timeoutMs && process.state () == QProcess::Running)
        if (process.waitForReadyRead (100)) output += process.readAllStandardOutput ();
      process.write (after);
    }
  process.closeWriteChannel ();
  if (!process.waitForFinished (timeoutMs))
    {
      run.timedOut = true;
      process.kill ();
      process.waitForFinished (5000);
    }
  output += process.readAllStandardOutput ();
  run.exitCode = process.exitCode ();
  run.exitStatus = process.exitStatus ();
  parse (run, output);
  return run;
}

QList<QJsonObject> ofType (QList<QJsonObject> const& events, QString const& type)
{
  QList<QJsonObject> matching;
  for (auto const& event : events)
    if (event.value ("t").toString () == type) matching.append (event);
  return matching;
}

// A review's update lines, and the live ones.
QList<QJsonObject> reviewUpdates (QList<QJsonObject> const& events, int id)
{
  QList<QJsonObject> updates;
  for (auto const& event : ofType (events, "jtty_update"))
    if (event.contains ("review") && event.value ("review").toInt () == id) updates.append (event);
  return updates;
}

QList<QJsonObject> liveUpdates (QList<QJsonObject> const& events)
{
  QList<QJsonObject> updates;
  for (auto const& event : ofType (events, "jtty_update"))
    if (!event.contains ("review")) updates.append (event);
  return updates;
}

// Updates as the GUI has them: each id numbered by its first appearance
// (a review's messages take ids from the process's one sequence), and no
// envelope, session, review or gaps.
QList<QJsonObject> comparable (QList<QJsonObject> updates)
{
  QList<qint64> seen;
  for (auto& update : updates)
    {
      auto const id = qint64 (update.value ("id").toDouble ());
      if (!seen.contains (id)) seen.append (id);
      update["id"] = double (seen.indexOf (id));
      for (auto const& key : {"v", "t", "session", "gaps", "review"}) update.remove (key);
    }
  return updates;
}

bool hasReviewed (QList<QJsonObject> const& events, int id)
{
  for (auto const& event : ofType (events, "reviewed"))
    if (event.value ("id").toInt () == id) return true;
  return false;
}

struct Job
{
  QByteArray pcm;    // the session's samples; index 0 is its origin
  qint64 origin;     // the session's first stream sample
  qint64 first, end; // the window, in the session's samples
  bool picked;
};

// The GUI's review: for each job, startJttyReview's begin(span, 0, first) and
// pumpJttyReceive's review branch, one process(...) step at a time, updates
// taken after each, a picked job stopping at a Complete within F Tol; then
// end() and its updates. Times as the codec states them: in stream samples.
QList<QJsonObject> guiReview (QList<Job> const& jobs, int low, int high, float center, float tolerance)
{
  static char const * const states[] {"growing", "complete", "expired", "ended"};
  QList<QJsonObject> updates;
  Jtty::Decoder decoder;
  if (!decoder.valid ()) return updates;
  for (auto const& job : jobs)
    {
      auto const offset = double (job.origin) / SampleRate;
      auto const take = [&] (bool* done) {
        for (auto const& update : decoder.takeUpdates ())
          {
            if (done && job.picked && update.terminal == Jtty::ReceiveTerminal::Complete
                && std::abs (update.frequency - center) < tolerance)
              *done = true;
            updates.append (QJsonObject {
              {"id", double (update.messageId)}, {"state", states[int (update.terminal)]},
              {"freq", QString::number (double (update.frequency), 'f', 3).toDouble ()}, {"snr", update.snr},
              {"start", QString::number (update.startSeconds + offset, 'f', 6).toDouble ()},
              {"latest", QString::number (update.latestSeconds + offset, 'f', 6).toDouble ()},
              {"text", Jtty::stripJttyFillerText (update.text)}});
          }
      };
      decoder.end ();
      decoder.takeUpdates ();
      decoder.begin (1, 0, job.first);
      auto const samples = reinterpret_cast<qint16 const *> (job.pcm.constData ()) + job.first;
      for (;;)
        {
          auto const steps = decoder.process (samples, int (job.end - job.first), job.first, job.end, 1,
                                              low, high, center, tolerance);
          bool done = steps <= 0;
          take (&done);
          if (done) break;
        }
      decoder.end ();
      take (nullptr);
    }
  return comparable (updates);
}

QJsonObject rejected (int id, QString const& reason, QString const& key, QString const& detail)
{
  return {{"v", 1}, {"t", "rejected"}, {"id", id}, {"reason", reason}, {"key", key}, {"detail", detail}};
}

QJsonObject reviewed (int id, bool cancelled)
{
  return {{"v", 1}, {"t", "reviewed"}, {"id", id}, {"cancelled", cancelled}};
}

int indexOf (QList<QJsonObject> const& events, QJsonObject const& event)
{
  return int (events.indexOf (event));
}

}

Q_DECLARE_METATYPE (QList<Job>)

namespace
{
#define CHECK_RUN(run)                                                  \
  do {                                                                  \
    QVERIFY2 ((run).started, "wsjt-codec did not start");               \
    QVERIFY2 (!(run).timedOut, "wsjt-codec timed out");                 \
    QCOMPARE ((run).exitStatus, QProcess::NormalExit);                  \
    QVERIFY2 ((run).error.isEmpty (), qPrintable ((run).error));        \
    QCOMPARE ((run).exitCode, 0);                                       \
  } while (false)
}

class TestStreamingJttyReview
  : public QObject
{
  Q_OBJECT

private:
  QByteArray quick_, pair_;

private Q_SLOTS:
  void initTestCase ()
  {
    QVERIFY (!codecDirectory ().isEmpty ());
    quick_ = synthesize (Quick, 1500.0f);
    // Two stations: the live one at 1500 Hz and another at 1650 Hz.
    pair_ = mixed (quick_, synthesize (Quoted, 1650.0f), 2 * FrameSamples);
    // Measure the decoder's FFT plans once; the GUI model reads the same wisdom.
    auto const warm = runCodec (header () + configure (1650, 20, 200, 4000) + audio (pair_)
                                + review ({{"id", 1}}), {}, {}, qMax (PlanningTimeoutMs, processTimeoutMs ()));
    QVERIFY2 (warm.started && !warm.timedOut && warm.exitCode == 0, "wsjt-codec measures JTTY's FFT plans");
    QVERIFY (fftwf_init_threads () != 0);
    QVERIFY2 (fftwf_import_wisdom_from_filename (QFile::encodeName (codecDirectory () + "/jt9_wisdom.dat")) != 0,
              "wsjt-codec saved its FFTW wisdom");
  }

  // (t1) A review gives the GUI review's updates on the same audio, for
  // every form of the request.
  void reviewDecodesAsTheGuiDoes_data ()
  {
    QTest::addColumn<QByteArray> ("input");
    QTest::addColumn<QJsonObject> ("request");
    QTest::addColumn<QList<Job>> ("jobs");
    QTest::addColumn<int> ("rxfreq");
    QTest::addColumn<int> ("ntol");
    QTest::addColumn<int> ("nfa");
    QTest::addColumn<int> ("nfb");
    QTest::addColumn<int> ("session");    // every update's, or 0 for the jobs' order
    qint64 const pair = pair_.size () / 2;
    auto const live = configure (1500, 20, 200, 4000);
    QTest::newRow ("every session") << header () + live + audio (pair_) << QJsonObject {{"id", 3}, {"rxfreq", 1650}}
      << QList<Job> {{pair_, 0, 0, pair, false}} << 1650 << 20 << 200 << 4000 << 1;
    QTest::newRow ("named session") << header () + live + audio (pair_)
      << QJsonObject {{"id", 4}, {"session", 1}, {"rxfreq", 1650}, {"ntol", 30}, {"nfa", 1000}, {"nfb", 2000}}
      << QList<Job> {{pair_, 0, 0, pair, false}} << 1650 << 30 << 1000 << 2000 << 1;
    qint64 const picked = LeadSamples + 8 * FrameSamples;
    QTest::newRow ("picked") << header () + live + audio (pair_)
      << QJsonObject {{"id", 5}, {"session", 1}, {"sample", picked}, {"rxfreq", 1650}}
      << QList<Job> {{pair_, 0, picked - 60000, pair, true}} << 1650 << 20 << 200 << 4000 << 1;
    // No frequency in the request: the configured ones. The review stops at
    // the 1650 Hz message's complete, before the second copy.
    auto const twice = pair_ + pair_;
    QTest::newRow ("picked near the start") << header () + configure (1650, 20, 300, 3000) + audio (twice)
      << QJsonObject {{"id", 6}, {"session", 1}, {"sample", 30000}}
      << QList<Job> {{twice, 0, 0, qMin<qint64> (2 * pair, 480000), true}} << 1650 << 20 << 300 << 3000 << 1;
    // A picked time whose window's first and last steps decode frames that a
    // window from 4 s before, or 41 s long, would not (found with the model).
    QByteArray four;
    for (int i = 0; i < 4; ++i) four += quick_;
    QTest::newRow ("picked window edges") << header () + live + audio (four)
      << QJsonObject {{"id", 10}, {"session", 1}, {"sample", 142000}, {"rxfreq", 1650}}
      << QList<Job> {{four, 0, 142000 - 60000, 142000 - 60000 + 480000, true}} << 1650 << 20 << 200 << 4000 << 1;
    auto const longer = quick_ + silence (40 * SampleRate);
    QTest::newRow ("picked past 40 s") << header () + live + audio (longer)
      << QJsonObject {{"id", 7}, {"session", 1}, {"sample", 70000}}
      << QList<Job> {{longer, 0, 10000, 10000 + 480000, true}} << 1500 << 20 << 200 << 4000 << 1;
    auto const discontinuity = control (R"({"t":"discontinuity"})");
    qint64 const second = quick_.size () / 2;
    QTest::newRow ("two sessions") << header () + live + audio (quick_) + discontinuity + audio (pair_)
      << QJsonObject {{"id", 8}, {"rxfreq", 1650}}
      << QList<Job> {{quick_, 0, 0, second, false}, {pair_, second, 0, pair, false}} << 1650 << 20 << 200 << 4000 << 0;
    QTest::newRow ("the second session picked") << header () + live + audio (quick_) + discontinuity + audio (pair_)
      << QJsonObject {{"id", 9}, {"session", 2}, {"sample", second + picked}, {"rxfreq", 1650}}
      << QList<Job> {{pair_, second, picked - 60000, pair, true}} << 1650 << 20 << 200 << 4000 << 2;
  }

  void reviewDecodesAsTheGuiDoes ()
  {
    QFETCH (QByteArray, input);
    QFETCH (QJsonObject, request);
    QFETCH (QList<Job>, jobs);
    QFETCH (int, rxfreq);
    QFETCH (int, ntol);
    QFETCH (int, nfa);
    QFETCH (int, nfb);
    QFETCH (int, session);
    auto const id = request.value ("id").toInt ();
    auto const run = runCodec (input + review (request));
    CHECK_RUN (run);
    auto const codec = reviewUpdates (run.events, id);
    QVERIFY2 (!codec.isEmpty (), "the review decodes");
    int last = 0;
    for (auto const& update : codec)
      {
        auto const reviewedSession = update.value ("session").toInt ();
        QVERIFY (update.value ("gaps").isArray ());
        if (session) QCOMPARE (reviewedSession, session);
        else QVERIFY (reviewedSession >= last && reviewedSession >= 1);    // oldest first
        last = reviewedSession;
      }
    if (!session) QCOMPARE (last, jobs.size ());
    auto const gui = guiReview (jobs, nfa, nfb, float (rxfreq), float (ntol));
    qInfo () << "review updates" << codec.size () << "GUI review updates" << gui.size ()
             << "last" << QJsonDocument {codec.last ()}.toJson (QJsonDocument::Compact);
    QCOMPARE (comparable (codec), gui);
    auto const end = run.events.indexOf (reviewed (id, false));
    QVERIFY2 (end >= 0, "the review ends with its reviewed line");
    QVERIFY (run.events.indexOf (codec.last ()) < end);
    QCOMPARE (ofType (run.events, "reviewed").size (), 1);
  }

  // Refusals, each the request's only line.
  void reviewRefusals_data ()
  {
    QTest::addColumn<QByteArray> ("input");
    QTest::addColumn<QByteArray> ("request");
    QTest::addColumn<QJsonObject> ("answer");
    auto const jtty = header () + configure (1500, 20, 200, 4000) + audio (quick_);
    qint64 const end = quick_.size () / 2;
    QTest::newRow ("no id") << jtty << QByteArray {R"({"t":"jtty_review","session":1})"}
      << QJsonObject {{"v", 1}, {"t", "error"}, {"code", "invalid_request_id"},
                      {"detail", "jtty_review needs an integer id from -2147483648 to 2147483647"}};
    QTest::newRow ("unknown key") << jtty << QByteArray {R"({"t":"jtty_review","id":2,"Session":1})"}
      << rejected (2, "bad_request", "Session", "Session is not a jtty_review key");
    QTest::newRow ("sample without session") << jtty << QByteArray {R"({"t":"jtty_review","id":3,"sample":5000})"}
      << rejected (3, "bad_request", "sample", "sample needs session");
    QTest::newRow ("rxfreq out of range") << jtty << QByteArray {R"({"t":"jtty_review","id":4,"rxfreq":6001})"}
      << rejected (4, "bad_request", "rxfreq", "rxfreq must be an integer from 0 to 6000 Hz");
    QTest::newRow ("session not an integer") << jtty << QByteArray {R"({"t":"jtty_review","id":5,"session":"1"})"}
      << rejected (5, "bad_request", "session", "session must be an integer from 1");
    QTest::newRow ("no such session") << jtty << QByteArray {R"({"t":"jtty_review","id":6,"session":2})"}
      << rejected (6, "unavailable", "session", "no audio of that session is kept");
    QTest::newRow ("sample at the first kept") << jtty << QByteArray {R"({"t":"jtty_review","id":7,"session":1,"sample":0})"}
      << rejected (7, "unavailable", "sample", "the audio at that sample is no longer kept");
    QTest::newRow ("sample past the end") << jtty
      << QByteArray {R"({"t":"jtty_review","id":8,"session":1,"sample":)"} + QByteArray::number (end + 1) + "}"
      << rejected (8, "unavailable", "sample", "the audio at that sample is no longer kept");
    QTest::newRow ("not in JTTY") << header () + control (R"({"t":"configure","mode":"FT8"})")
      << QByteArray {R"({"t":"jtty_review","id":9})"}
      << rejected (9, "unavailable", "mode", "a review needs the stream in JTTY");
  }

  void reviewRefusals ()
  {
    QFETCH (QByteArray, input);
    QFETCH (QByteArray, request);
    QFETCH (QJsonObject, answer);
    auto const run = runCodec (input + control (request));
    CHECK_RUN (run);
    QVERIFY2 (run.events.contains (answer), qPrintable (QJsonDocument {answer}.toJson (QJsonDocument::Compact)));
    // The request's only line: one rejected or error line in all.
    QCOMPARE (ofType (run.events, "rejected").size () + ofType (run.events, "error").size (), 1);
    QVERIFY (ofType (run.events, "reviewed").isEmpty ());
    for (auto const& update : ofType (run.events, "jtty_update")) QVERIFY (!update.contains ("review"));
  }

  // The codec keeps the last 180 s of JTTY audio, every session together,
  // as the GUI's history does: of 200 s, the first 20 s are gone.
  void historyKeepsTheLast180Seconds ()
  {
    qint64 const half = 100 * SampleRate;
    auto const input = header () + configure (1500, 20, 200, 4000) + audio (silence (int (half)))
      + control (R"({"t":"discontinuity"})") + audio (silence (int (half)));
    qint64 const firstKept = 2 * half - HistorySamples;
    auto const pick = [] (int id, int session, qint64 sample) {
      return review ({{"id", id}, {"session", session}, {"sample", double (sample)}});
    };
    auto const run = runCodec (input + pick (1, 1, firstKept) + pick (2, 1, firstKept + 1)
                               + pick (3, 2, 2 * half) + pick (4, 2, 2 * half + 1));
    CHECK_RUN (run);
    QVERIFY (run.events.contains (rejected (1, "unavailable", "sample", "the audio at that sample is no longer kept")));
    QVERIFY (run.events.contains (rejected (4, "unavailable", "sample", "the audio at that sample is no longer kept")));
    // Each request ends the one before it; 2 and 3 are not refused.
    QVERIFY (hasReviewed (run.events, 2) && hasReviewed (run.events, 3));
    QCOMPARE (ofType (run.events, "rejected").size (), 2);
  }

  // (t2) Live decoding is the same with a review running beside it: the
  // second half of the audio is sent once the review has started.
  void reviewLeavesLiveDecodingUnchanged ()
  {
    auto const pcm = pair_ + pair_;
    int const half = int (pair_.size ());
    auto const start = header () + configure (1500, 20, 200, 4000, true) + audio (pcm.left (half));
    auto const rest = audio (pcm.mid (half));
    auto const alone = runCodec (start + rest);
    CHECK_RUN (alone);
    auto const run = runCodec (start + review ({{"id", 11}, {"rxfreq", 1650}}), R"("review":11)", rest);
    CHECK_RUN (run);
    QVERIFY (!reviewUpdates (run.events, 11).isEmpty ());
    QVERIFY (!liveUpdates (alone.events).isEmpty ());
    QCOMPARE (comparable (liveUpdates (run.events)), comparable (liveUpdates (alone.events)));
    // The review was still running while the rest decoded: a live update of
    // the second half comes before its reviewed line.
    auto const end = run.events.indexOf (reviewed (11, false));
    QVERIFY (end >= 0);
    bool during = false;
    for (auto const& update : liveUpdates (run.events))
      during = during || (run.events.indexOf (update) < end
                          && update.value ("start").toDouble () * SampleRate > half / 2);
    QVERIFY2 (during, "live decoding went on during the review");
  }

  // (t2b) With busy_events, busy lines marked review bracket the review,
  // and the decode thread says nothing in JTTY.
  void reviewJoinsTheBusyState ()
  {
    auto const run = runCodec (header () + configure (1500, 20, 200, 4000, true) + audio (pair_)
                               + review ({{"id", 12}, {"rxfreq", 1650}}));
    CHECK_RUN (run);
    QJsonObject const on {{"v", 1}, {"t", "busy"}, {"busy", true}, {"mode", "JTTY"}, {"review", 12}};
    QJsonObject const off {{"v", 1}, {"t", "busy"}, {"busy", false}, {"review", 12}};
    auto const begin = indexOf (run.events, on), end = indexOf (run.events, off);
    auto const updates = reviewUpdates (run.events, 12);
    QVERIFY (begin >= 0 && end > begin && !updates.isEmpty ());
    QVERIFY (indexOf (run.events, updates.first ()) > begin && indexOf (run.events, updates.last ()) < end);
    QCOMPARE (indexOf (run.events, reviewed (12, false)), end + 1);
    QCOMPARE (ofType (run.events, "busy").size (), 2);
    // Without busy_events, no busy line.
    auto const quiet = runCodec (header () + configure (1500, 20, 200, 4000) + audio (pair_)
                                 + review ({{"id", 12}, {"rxfreq", 1650}}));
    CHECK_RUN (quiet);
    QVERIFY (ofType (quiet.events, "busy").isEmpty ());
    QVERIFY (quiet.events.contains (reviewed (12, false)));
  }

  // (t2b) A client applying the design's §3.12 rules restarts nothing
  // during a long review: its deadlines run only while a decode-thread busy
  // line (one without review) says true, and none does in JTTY.
  void clientRulesStartNoDeadlineDuringAReview ()
  {
    auto const longer = pair_ + silence (150 * SampleRate);
    auto const run = runCodec (header () + configure (1500, 20, 200, 4000, true) + audio (longer)
                               + review ({{"id", 13}}));
    CHECK_RUN (run);
    bool decodeBusy = false, reviewing = false, sawReview = false;
    for (auto const& event : run.events)
      {
        if (event.value ("t").toString () != "busy") continue;
        if (event.contains ("review")) {
          reviewing = event.value ("busy").toBool ();
          sawReview = sawReview || reviewing;
        } else decodeBusy = event.value ("busy").toBool ();
        QVERIFY2 (!(reviewing && decodeBusy), "a decode deadline would run during the review");
      }
    QVERIFY (sawReview && !reviewing);
    QVERIFY (run.events.contains (reviewed (13, false)));
  }

  // (t2b) A new review ends the running one, as startJttyReview's
  // cancelJttyReview does; leaving JTTY cancels it; decode again stays
  // refused; halt and the end of input let it finish.
  void whatAReviewRefusesAndWhatEndsIt ()
  {
    auto const start = header () + configure (1500, 20, 200, 4000, true) + audio (pair_);
    auto const replaced = runCodec (start + review ({{"id", 21}}) + review ({{"id", 22}, {"rxfreq", 1650}}));
    CHECK_RUN (replaced);
    auto const cancelled = indexOf (replaced.events, reviewed (21, true));
    QVERIFY (cancelled >= 0);
    QVERIFY (indexOf (replaced.events, QJsonObject {{"v", 1}, {"t", "busy"}, {"busy", false}, {"review", 21}}) == cancelled - 1);
    QVERIFY (!reviewUpdates (replaced.events, 22).isEmpty ());
    QVERIFY (indexOf (replaced.events, reviewed (22, false)) > cancelled);

    auto const left = runCodec (start + review ({{"id", 23}}) + control (R"({"t":"configure","mode":"FT8"})"));
    CHECK_RUN (left);
    QVERIFY (left.events.contains (reviewed (23, true)));

    auto const again = runCodec (start + review ({{"id", 24}, {"rxfreq", 1650}})
                                 + control (R"({"t":"configure","nagain_flag":true})"));
    CHECK_RUN (again);
    QCOMPARE (ofType (again.events, "error").size (), 1);
    QCOMPARE (ofType (again.events, "error").first ().value ("code").toString (), QString {"decode_again_unavailable"});
    QVERIFY (again.events.contains (reviewed (24, false)));

    // A malformed request cancels the review in progress too.
    auto const malformed = runCodec (start + review ({{"id", 28}}) + review ({{"id", 29}, {"sesion", 1}}));
    CHECK_RUN (malformed);
    QVERIFY (malformed.events.contains (reviewed (28, true)));
    QVERIFY (malformed.events.contains (rejected (29, "bad_request", "sesion", "sesion is not a jtty_review key")));

    // A request cancels the review in progress even when it is refused.
    auto const refused = runCodec (start + review ({{"id", 26}}) + review ({{"id", 27}, {"session", 9}}));
    CHECK_RUN (refused);
    QVERIFY (refused.events.contains (reviewed (26, true)));
    QVERIFY (refused.events.contains (rejected (27, "unavailable", "session", "no audio of that session is kept")));
    QVERIFY (indexOf (refused.events, reviewed (26, true)) < indexOf (refused.events,
      rejected (27, "unavailable", "session", "no audio of that session is kept")));

    auto const halted = runCodec (start + review ({{"id", 25}, {"rxfreq", 1650}}) + control (R"({"t":"halt"})"));
    CHECK_RUN (halted);
    QVERIFY (!reviewUpdates (halted.events, 25).isEmpty ());
    QVERIFY (halted.events.contains (reviewed (25, false)));
  }

  // A review cancelled mid-message sends none of the ended lines its
  // context still holds, as cancelJttyReview drops them.
  void cancelledReviewDropsItsLastUpdates ()
  {
    // Seconds of review work on an arm64 Mac, so the second request comes
    // while the first runs.
    QByteArray longer;
    for (int i = 0; i < 6; ++i) longer += pair_;
    auto const start = header () + configure (1500, 20, 200, 4000) + audio (longer) + review ({{"id", 41}});
    auto const run = runCodec (start, R"("review":41)", review ({{"id", 42}, {"session", 1}, {"sample", 30000}}));
    CHECK_RUN (run);
    auto const end = indexOf (run.events, reviewed (41, true));
    QVERIFY2 (end >= 0, "the second request cancels the first");
    auto const updates = reviewUpdates (run.events, 41);
    QVERIFY2 (!updates.isEmpty (), "the first review decoded before it was cancelled");
    for (auto const& update : updates)
      {
        QVERIFY (indexOf (run.events, update) < end);
        QVERIFY2 (update.value ("state").toString () != "ended", "a cancelled review's ended line went out");
      }
    QVERIFY (run.events.contains (reviewed (42, false)));
    // None of 41's pending lines comes out as 42's: 42 is the GUI's review
    // of its own window.
    QCOMPARE (comparable (reviewUpdates (run.events, 42)),
              guiReview ({{longer, 0, 0, qMin<qint64> (longer.size () / 2, 480000), true}}, 200, 4000, 1500.0f, 20.0f));
  }

  // A configure that changes live_audio cancels the review in progress
  // (WSJT-X's opening a file does, beginJttyDisk); with live_audio false a
  // review without session covers the sessions since it last went false
  // (WSJT-X's Decode with a file loaded reviews that recording), and a
  // picked one any kept session (WSJT-X reviews a picked live row from its
  // history).
  void fileInputReviewsItsOwnSessions ()
  {
    auto const live = control (R"({"t":"configure","mode":"JTTY","rxfreq":1500,"ntol":20,"nfa":200,"nfb":4000,"live_audio":true})");
    auto const file = control (R"({"t":"configure","live_audio":false})");
    auto const cancelled = runCodec (header () + live + audio (pair_) + review ({{"id", 51}}) + file);
    CHECK_RUN (cancelled);
    QVERIFY2 (cancelled.events.contains (reviewed (51, true)), "a live_audio change cancels the review");

    auto const discontinuity = control (R"({"t":"discontinuity"})");
    qint64 const second = quick_.size () / 2, pair = pair_.size () / 2;
    auto const run = runCodec (header () + live + audio (quick_) + file + discontinuity + audio (pair_)
                               + review ({{"id", 52}, {"rxfreq", 1650}}));
    CHECK_RUN (run);
    auto const updates = reviewUpdates (run.events, 52);
    QVERIFY (!updates.isEmpty ());
    for (auto const& update : updates) QCOMPARE (update.value ("session").toInt (), 2);
    QCOMPARE (comparable (updates), guiReview ({{pair_, second, 0, pair, false}}, 200, 4000, 1650.0f, 20.0f));

    // A live_audio change ends the open session, as WSJT-X's opening a file
    // starts a new one (beginJttyDisk): no discontinuity is needed.
    auto const joined = runCodec (header () + live + audio (quick_) + file + audio (pair_)
                                  + review ({{"id", 54}, {"rxfreq", 1650}}));
    CHECK_RUN (joined);
    QCOMPARE (ofType (joined.events, "session").size (), 2);
    QCOMPARE (ofType (joined.events, "session").last ().value ("origin").toDouble (), double (second));
    QCOMPARE (comparable (reviewUpdates (joined.events, 54)),
              guiReview ({{pair_, second, 0, pair, false}}, 200, 4000, 1650.0f, 20.0f));

    auto const picked = runCodec (header () + live + audio (quick_) + file + discontinuity + audio (pair_)
                                  + review ({{"id", 53}, {"session", 1}, {"sample", 30000}}));
    CHECK_RUN (picked);
    QCOMPARE (comparable (reviewUpdates (picked.events, 53)),
              guiReview ({{quick_, 0, 0, second, true}}, 200, 4000, 1500.0f, 20.0f));
  }

  // A pass on the decode thread that ends while a review runs gets its
  // busy false before the review's reviewed line, as when the frame loop
  // reads or waits for input: the review's steps at frame boundaries let
  // the decode thread drain. The input must not run ahead of the clock
  // (every audio sample's due time is a point the frame loop has read to),
  // so the held pass ends 0.7 s after the last sample is due, the review is
  // sent 0.4 s after it is due, and the row checks only when the hold ended
  // at least 2 s before the review did: five pairs give the review enough
  // audio to outlast that on a faster decoder (about 115 s; CTest reports a
  // skip as skipped).
  void decodeThreadBusyEndsDuringAReview ()
  {
    if ("1" == qEnvironmentVariable ("WSJT_CODEC_TEST_INLINE")) QSKIP ("needs the decode thread");
    QByteArray jttyAudio = pair_ + pair_ + pair_ + pair_ + pair_;
    int const dueMs = int ((15 * SampleRate + jttyAudio.size () / 2) * 1000LL / SampleRate);
    int const holdMs = dueMs + 700;
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment ();
    environment.insert ("WSJT_CODEC_TEST_HOLD_PASS", QString {"50:%1"}.arg (holdMs));
    process.setProcessEnvironment (environment);
    process.setWorkingDirectory (codecDirectory ());
    process.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), {"-a", codecDirectory (), "-t", codecDirectory ()});
    QVERIFY (process.waitForStarted (5000));
    QElapsedTimer timer;
    timer.start ();
    process.write (header () + control (R"({"t":"configure","mode":"FT8","busy_events":true})")
                   + audio (silence (15 * SampleRate)) + configure (1500, 20, 200, 4000) + audio (jttyAudio));
    QByteArray output, errors;
    qint64 holdDoneAt = -1, reviewOnAt = -1, reviewedAt = -1;
    auto const read = [&] {
      if (process.waitForReadyRead (20)) output += process.readAllStandardOutput ();
      errors += process.readAllStandardError ();
      if (holdDoneAt < 0 && errors.contains ("hold done nzhsym=50")) holdDoneAt = timer.elapsed ();
      if (reviewOnAt < 0 && output.contains (R"("review":61})")) reviewOnAt = timer.elapsed ();
      if (reviewedAt < 0 && output.contains (R"("t":"reviewed")")) reviewedAt = timer.elapsed ();
    };
    while (timer.elapsed () < dueMs + 400 && process.state () == QProcess::Running) read ();
    process.write (review ({{"id", 61}, {"rxfreq", 3000}, {"ntol", 3000}, {"nfa", 0}, {"nfb", 6000}}));
    while (reviewedAt < 0 && timer.elapsed () < holdMs + processTimeoutMs () && process.state () == QProcess::Running)
      read ();
    while (holdDoneAt < 0 && timer.elapsed () < holdMs + processTimeoutMs ()) read ();
    process.write (control (R"({"t":"halt"})"));
    process.closeWriteChannel ();
    QVERIFY (process.waitForFinished (processTimeoutMs ()));
    output += process.readAllStandardOutput ();
    qInfo () << "review from" << reviewOnAt << "ms to" << reviewedAt << "ms; the hold ended at" << holdDoneAt << "ms";
    Run run;
    parse (run, output);
    QVERIFY (run.error.isEmpty ());
    // The decode thread's busy false after the review's busy true.
    int off = -1;
    auto const on = run.events.indexOf (QJsonObject {{"v", 1}, {"t", "busy"}, {"busy", true}, {"mode", "JTTY"}, {"review", 61}});
    auto const done = run.events.indexOf (reviewed (61, false));
    for (int i = on + 1; on >= 0 && i < run.events.size () && off < 0; ++i)
      if (run.events[i].value ("t").toString () == "busy" && !run.events[i].contains ("review")
          && !run.events[i].value ("busy").toBool ())
        off = i;
    QVERIFY (on >= 0 && done > on && off >= 0 && reviewOnAt >= 0 && holdDoneAt >= 0);
    if (!(reviewOnAt < holdDoneAt && holdDoneAt + 2000 < reviewedAt))
      QSKIP ("the hold did not end while the review ran: this machine's timing does not exercise the row");
    QVERIFY2 (off < done, "the decode thread's busy false waited for the review to end");
  }

  // A client that ignores the new line type, reviewed, and the lines marked
  // review sees the stream it saw before.
  void clientsIgnoringReviewLinesStillWork ()
  {
    auto const input = header () + configure (1500, 20, 200, 4000) + audio (pair_);
    auto const plain = runCodec (input);
    CHECK_RUN (plain);
    auto const run = runCodec (input + review ({{"id", 31}, {"rxfreq", 1650}}));
    CHECK_RUN (run);
    QList<QJsonObject> kept;
    for (auto const& event : run.events)
      if (event.value ("t").toString () != "reviewed" && !event.contains ("review")) kept.append (event);
    QVERIFY (run.events.size () > kept.size ());
    QCOMPARE (comparable (ofType (kept, "jtty_update")), comparable (ofType (plain.events, "jtty_update")));
    auto const others = [] (QList<QJsonObject> const& events) {
      QList<QJsonObject> rest;
      for (auto const& event : events)
        if (event.value ("t").toString () != "jtty_update") rest.append (event);
      return rest;
    };
    QCOMPARE (others (kept), others (plain.events));
  }
};

QTEST_MAIN (TestStreamingJttyReview);

#include "test_streaming_jtty_review.moc"
