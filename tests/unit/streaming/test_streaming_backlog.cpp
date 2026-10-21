// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTime>
#include <QtTest>

#include "Ft8MtdDecodeScheduler.hpp"

namespace
{
using Stage = Ft8MtdDecodeScheduler::Stage;
using Action = Ft8MtdDecodeScheduler::Action;

constexpr int HsymSamples = 3456;
constexpr int Ft8PeriodSamples = 15 * 12000;
constexpr int Ft4PeriodSamples = 90000;
constexpr int Msk144PeriodSamples = 5 * 12000;
// A paced client's frame, 0.1 s of audio.
constexpr int FrameSamples = 1200;
// A burst's late audio is due this long before its anchor is sent, so that
// each trigger in it fires when it arrives, during the anchor's decode,
// rather than when it falls due.
constexpr int DueMarginMs = 500;
// A trigger in a burst due this much after its late audio fires when due,
// after the anchor's decode and any drained final have ended.
constexpr int FreeAfterSamples = 10 * 12000;
// One wait covers a decode or two; sanitizer builds raise QtTest's limit and this with it.
int waitMs ()
{
  return std::max (60000, qEnvironmentVariableIntValue ("QTEST_FUNCTION_TIMEOUT") / 6);
}
// ft8-engine.wav's one signal; the other recording never decodes it.
constexpr char EngineMessage[] = "CQ K1JT FN20";

QByteArray frame (quint8 type, QByteArray const& body)
{
  QByteArray bytes;
  bytes.append (char (type));
  auto const length = quint32 (body.size ());
  for (int shift = 0; shift < 32; shift += 8) bytes.append (char ((length >> shift) & 0xffu));
  return bytes + body;
}

QByteArray sessionHeader ()
{
  QByteArray bytes {"WSJT", 4};
  bytes.append ('\0');
  bytes.append ('\1');
  bytes.append ('\x0c');
  bytes.append ('\0');
  return bytes;
}

QByteArray controlFrame (QJsonObject const& keys)
{
  return frame (0x02u, QJsonDocument {keys}.toJson (QJsonDocument::Compact));
}

QByteArray configureFrame (QJsonObject keys)
{
  keys.insert ("t", "configure");
  return controlFrame (keys);
}

// A control frame without "t": its configure_parse_error shows the stream has
// read everything before it.
QByteArray marker ()
{
  return frame (0x02u, "{}");
}

QJsonObject with (QJsonObject keys, QJsonObject const& more)
{
  for (auto it = more.constBegin (); it != more.constEnd (); ++it) keys.insert (it.key (), it.value ());
  return keys;
}

struct Trigger
{
  int offset;          // the period's samples when it fires
  int nzhsym;
  Stage stage;         // MainWindow::dataSink's stage for usesFt8MtdFinal ()
  bool final;
  bool multithreaded;  // the pass line's flag
};

enum class Step
{
  Gate,     // audio, then wait until the stream is idle: its trigger finds the decoder free
  Anchor,   // audio up to one trigger, or a decode again, then wait for its pass line
  Drained,  // wait for the pass line of the final the burst before deferred
  Burst,    // the rest at once while the anchor decodes: each late trigger in it finds the decoder busy
  Again     // a decode-again request, then wait until the stream is idle
};

struct Send
{
  Step step;
  qint64 until;                   // stream samples sent after this step
  bool again;                     // a decode-again request after the audio
  qint64 dueBefore;               // wait until these samples are due before this step
  qint64 late;                    // a burst's late audio ends here (0: at until)
  QList<QJsonObject> configures;  // a burst's configures after its audio
  bool cancels;                   // the first of them cancels the deferred final
  bool dropsAtDrain;              // they leave the deferred final's search limits changed
  bool halt;                      // a burst that ends the session with halt
};

struct Row
{
  QJsonObject settings;
  QString mode;
  QList<QByteArray> recordings;  // each one period long
  QList<int> sources;            // each period's recording (absent: the first)
  QList<Trigger> schedule;
  bool live;
  bool checked;                  // the decoder's busy state decides its passes
  bool backpressure;             // MainWindow::usesFt8MtdFinal () on live audio
  int earlyStages;               // MainWindow::configuredFt8MtdEarlyStageCount ()
  int againNzhsym;
  QStringList utc;
  QList<Send> sends;
  qint64 firstDue {0};           // the wait after the first frame
};

int periodSamples (Row const& row)
{
  return row.recordings.first ().size () / 2;
}

QByteArray const& recording (Row const& row, int period)
{
  return row.recordings.at (row.sources.value (period, 0));
}

QString label (Row const& row, int period)
{
  return QString {row.utc.at (period)}.remove (':');
}

qint64 at (Row const& row, int period, int hsym)
{
  return qint64 {period} * periodSamples (row) + hsym * HsymSamples;
}

qint64 start (Row const& row, int period)
{
  return qint64 {period} * periodSamples (row);
}

std::vector<std::pair<int, Trigger>> crossed (Row const& row, qint64 from, qint64 to)
{
  std::vector<std::pair<int, Trigger>> found;
  auto const n = periodSamples (row);
  for (auto p = from / n; p <= to / n; ++p)
    {
      for (auto const& trigger : row.schedule)
        {
          auto const sample = p * n + trigger.offset;
          if (sample > from && sample <= to) found.emplace_back (int (p), trigger);
        }
    }
  return found;
}

qint64 sampleOf (Row const& row, std::pair<int, Trigger> const& found)
{
  return start (row, found.first) + found.second.offset;
}

Send step (Step kind, qint64 until)
{
  Send send {};
  send.step = kind;
  send.until = until;
  return send;
}

Send gate (qint64 until) {return step (Step::Gate, until);}
Send anchor (qint64 until) {return step (Step::Anchor, until);}
Send drained (qint64 until) {return step (Step::Drained, until);}
Send burst (qint64 until) {return step (Step::Burst, until);}

Send again (qint64 until)
{
  auto send = step (Step::Again, until);
  send.again = true;
  return send;
}

Send againAnchor (qint64 until)
{
  auto send = step (Step::Anchor, until);
  send.again = true;
  return send;
}

Send burstAgain (qint64 until)
{
  auto send = burst (until);
  send.again = true;
  return send;
}

Send burstLate (qint64 until, qint64 late)
{
  auto send = burst (until);
  send.late = late;
  return send;
}

Send burstHalt (qint64 until)
{
  auto send = burst (until);
  send.halt = true;
  return send;
}

Send burstCancel (qint64 until, QList<QJsonObject> const& configures)
{
  auto send = burst (until);
  send.configures = configures;
  send.cancels = true;
  return send;
}

Send burstConfigure (qint64 until, QList<QJsonObject> const& configures, bool dropsAtDrain)
{
  auto send = burst (until);
  send.configures = configures;
  send.dropsAtDrain = dropsAtDrain;
  return send;
}

qint64 lateEnd (Send const& send)
{
  return send.late > 0 ? send.late : send.until;
}

// Each burst's late audio is due before its anchor is sent, the first
// anchor's already before the first gate.
Row due (Row row)
{
  int anchorAt = -1;
  for (int i = 0; i < row.sends.size (); ++i)
    {
      auto const& send = row.sends.at (i);
      if (Step::Anchor == send.step) anchorAt = i;
      if (Step::Burst != send.step || anchorAt < 0) continue;
      auto& before = row.sends[anchorAt].dueBefore;
      before = std::max (before, lateEnd (send));
    }
  for (auto const& send : row.sends)
    {
      if (send.dueBefore > 0)
        {
          row.firstDue = send.dueBefore;
          break;
        }
    }
  return row;
}

// The same session with the decoder free at every trigger: the anchor waits
// until the stream is idle, and the burst goes one trigger at a time.
Row withTheDecoderFree (Row row)
{
  QList<Send> sends;
  qint64 sent = 1;
  for (auto const& send : row.sends)
    {
      if (Step::Anchor == send.step)
        {
          auto free = send.again ? again (send.until) : gate (send.until);
          free.dueBefore = send.dueBefore;
          sends << free;
        }
      else if (Step::Burst == send.step)
        {
          auto last = sent;
          for (auto const& found : crossed (row, sent, send.until))
            {
              last = sampleOf (row, found);
              sends << gate (last);
            }
          if (last < send.until) sends << gate (send.until);
          if (send.again) sends << again (send.until);
        }
      else if (Step::Drained != send.step)
        {
          sends << send;
        }
      sent = send.until;
    }
  row.sends = sends;
  return row;
}

QStringList utcLabels (QTime first, int periodMs, int periods)
{
  QStringList labels;
  for (int p = 0; p < periods; ++p) labels << first.addMSecs (p * periodMs).toString ("HH:mm:ss");
  return labels;
}

QByteArray samples (char const* variable, int count)
{
  QFile wav {qEnvironmentVariable (variable)};
  if (!wav.open (QIODevice::ReadOnly)) return {};
  auto pcm = wav.readAll ().mid (44, 2 * count);
  pcm.append (QByteArray (2 * count - pcm.size (), '\0'));
  return pcm;
}

Row rowOf (QString const& mode, QJsonObject const& settings, QByteArray const& recording, bool live)
{
  Row row;
  row.settings = with (settings, {{"mode", mode}, {"live_audio", live}});
  row.mode = mode;
  row.recordings << recording;
  row.live = live;
  row.checked = true;
  row.backpressure = false;
  row.earlyStages = 0;
  row.againNzhsym = 0;
  return row;
}

Row ft8 (QJsonObject const& settings, bool live, int periods)
{
  auto row = rowOf ("FT8", with (settings, {{"depth", 3}}), samples ("WSJT_FT8_SAMPLE_WAV", Ft8PeriodSamples),
                    live);
  row.againNzhsym = 50;
  row.utc = utcLabels (QTime {13, 34, 30}, 15000, periods);
  return row;
}

// The sample's audio in even periods, ft8-engine.wav's in odd ones.
Row alternating (Row row)
{
  row.recordings << samples ("WSJT_FT8_ENGINE_WAV", Ft8PeriodSamples);
  for (int p = 0; p < row.utc.size (); ++p) row.sources << p % 2;
  return row;
}

Trigger trigger (int nzhsym, Stage stage, bool final, bool multithreaded)
{
  return {nzhsym * HsymSamples, nzhsym, stage, final, multithreaded};
}

// FT8 with the multithreaded decoder (MainWindow::fixStop, dataSink): live,
// its early stages on the standard decoder at 41 and, from start 1, at 46,
// then the final; from a file jt9a's 41, 46 and the final.
Row mtd (int start, bool live, int periods)
{
  auto row = ft8 ({{"multithreaded_ft8", true}, {"ft8_threads", 2}, {"ft8_decoder_start", start}},
                  live, periods);
  auto const stop = 0 == start ? 49 : 50;
  row.schedule << trigger (41, Stage::EarlyOne, false, false);
  if (1 == start || !live) row.schedule << trigger (46, Stage::EarlyTwo, false, false);
  row.schedule << trigger (stop, Stage::Final, true, true);
  row.backpressure = live;
  row.earlyStages = 0 == start ? 1 : 2;
  return row;
}

Row standardFt8 (int periods)
{
  auto row = ft8 ({}, true, periods);
  row.schedule << trigger (41, Stage::None, false, false) << trigger (47, Stage::None, false, false)
               << trigger (50, Stage::None, true, false);
  return row;
}

Row ft4 (int periods)
{
  auto row = rowOf ("FT4", {{"depth", 3}}, samples ("WSJT_FT4_SAMPLE_WAV", Ft4PeriodSamples), true);
  row.schedule << trigger (21, Stage::None, true, false);
  row.againNzhsym = 21;
  row.utc = utcLabels (QTime {12, 0, 0}, 7500, periods);
  return row;
}

// Start 0, one period behind: period 1 arrives while period 0's final
// decodes, and with it period 2's audio up to its 41, which falls due well
// after the burst, so the deferred final drains before it. One decoder
// cycle keeps both finals short enough for that on a slow runner.
Row mtdBehindOnePeriod (bool live)
{
  auto row = alternating (mtd (0, live, 5));
  row.settings["ft8_cycles"] = 1;
  row.sends = {gate (at (row, 0, 41)), anchor (at (row, 0, 49)),
               burstLate (at (row, 2, 41), start (row, 2) + 1), gate (at (row, 2, 49)),
               gate (at (row, 3, 41)), gate (at (row, 3, 49)), gate (at (row, 4, 41)),
               gate (at (row, 4, 49)), gate (start (row, 5))};
  return due (row);
}

// Start 0, two periods behind: periods 1 and 2 arrive while period 0's final
// decodes, then halt, with period 3 cut short.
Row mtdBehindTwoPeriods ()
{
  auto row = alternating (mtd (0, true, 4));
  row.sends = {gate (at (row, 0, 41)), anchor (at (row, 0, 49)), burstHalt (start (row, 3) + 1)};
  return due (row);
}

// Start 1: one period behind, then the probe's final arrives while the
// probe's second early pass decodes.
Row mtdFinalDuringItsProbe ()
{
  auto row = mtd (1, true, 7);
  row.recordings << samples ("WSJT_FT8_ENGINE_WAV", Ft8PeriodSamples);
  // The anchors, periods 0 and 3, decode the busier recording.
  row.sources = {0, 1, 0, 0, 1, 0, 1};
  row.sends = {gate (at (row, 0, 41)), gate (at (row, 0, 46)), anchor (at (row, 0, 50)),
               burst (start (row, 2) + 1)};
  row.sends << gate (at (row, 2, 41)) << gate (at (row, 2, 46)) << gate (at (row, 2, 50))
            << gate (at (row, 3, 41)) << anchor (at (row, 3, 46)) << burst (start (row, 4) + 1);
  for (int p = 4; p < 7; ++p)
    {
      row.sends << gate (at (row, p, 41)) << gate (at (row, p, 46)) << gate (at (row, p, 50));
    }
  row.sends << gate (start (row, 7));
  return due (row);
}

// Start 1: period 0's final arrives while its second early pass decodes, and
// period 1's first early pass while that final drains; period 2 probes and
// recovers; period 3's first early pass arrives while period 2's final
// decodes, and the backoff starts again from one period.
Row mtdBehindAfterRecovery ()
{
  auto row = alternating (mtd (1, true, 6));
  row.sends = {gate (at (row, 0, 41)), anchor (at (row, 0, 46)), burst (start (row, 1) + 1),
               drained (start (row, 1) + 1), burst (at (row, 1, 41)), gate (at (row, 1, 46)),
               gate (at (row, 1, 50)), gate (at (row, 2, 41)), gate (at (row, 2, 46)),
               anchor (at (row, 2, 50)), burst (at (row, 3, 41))};
  for (int p = 3; p < 6; ++p)
    {
      if (p > 3) row.sends << gate (at (row, p, 41));
      row.sends << gate (at (row, p, 46)) << gate (at (row, p, 50));
    }
  row.sends << gate (start (row, 6));
  return due (row);
}

// Start 1: each of periods 1 to 4 has its final deferred, then a configure
// that changes what the GUI cancels on (an FT8 decoder setting, the mode,
// file input, the station's call, K1ABC until then), and the next period's
// early passes run. The GUI compares the search limits only when it would
// run the final: period 5's final is dropped then, and period 6's, whose
// limit is changed and put back, runs and keeps the backoff.
Row mtdCancels ()
{
  auto row = mtd (1, true, 8);
  row.settings.insert ("nfa", 200);
  // Period 7's other recording shows period 6's drained final decoded its own.
  row.recordings << samples ("WSJT_FT8_ENGINE_WAV", Ft8PeriodSamples);
  row.sources = {0, 0, 0, 0, 0, 0, 0, 1};
  QJsonObject const settings {{"mode", "FT8"}, {"depth", 2}, {"multithreaded_ft8", true}, {"ft8_threads", 2},
                              {"ft8_decoder_start", 1}, {"live_audio", true}, {"nfa", 200}};
  row.sends = {gate (at (row, 0, 41)), gate (at (row, 0, 46)), anchor (at (row, 0, 50)),
               burstCancel (start (row, 2), {{{"depth", 2}}}),
               gate (at (row, 2, 41)), anchor (at (row, 2, 46)),
               burstCancel (start (row, 3), {{{"mode", "FT4"}}, settings}),
               gate (at (row, 3, 41)), anchor (at (row, 3, 46)),
               burstCancel (start (row, 4), {{{"live_audio", false}}, {{"live_audio", true}}}),
               gate (at (row, 4, 41)), anchor (at (row, 4, 46)),
               burstCancel (start (row, 5), {{{"mycall", "W1AW"}}}),
               gate (at (row, 5, 41)), anchor (at (row, 5, 46)),
               burstConfigure (start (row, 6), {{{"nfa", 300}}}, true),
               gate (at (row, 6, 41)), anchor (at (row, 6, 46)),
               burstConfigure (start (row, 7), {{{"nfa", 500}}, {{"nfa", 300}}}, false),
               gate (at (row, 7, 41)), gate (at (row, 7, 46)), gate (at (row, 7, 50)), gate (start (row, 8))};
  return due (row);
}

// SuperFox Hound decodes on the standard decoder with multithreading on
// (MainWindow::usesFt8MtdFinal), so its final drops when busy.
Row houndBehindOnePeriod ()
{
  auto row = ft8 ({{"multithreaded_ft8", true}, {"ft8_threads", 2}, {"ft8_decoder_start", 0},
                   {"contest_type", "hound"}, {"superfox_mode", true}, {"even_sequence", false}}, true, 3);
  row.schedule << trigger (41, Stage::None, false, false) << trigger (50, Stage::None, true, false);
  row.sends = {gate (at (row, 0, 41)), anchor (at (row, 0, 50)), burst (start (row, 2) + 1),
               gate (at (row, 2, 41)), gate (at (row, 2, 50)), gate (start (row, 3))};
  return due (row);
}

Row standardBehindOnePeriod ()
{
  auto row = standardFt8 (3);
  row.sends = {gate (at (row, 0, 41)), gate (at (row, 0, 47)), anchor (at (row, 0, 50)),
               burst (start (row, 2) + 1), gate (at (row, 2, 41)), gate (at (row, 2, 47)),
               gate (at (row, 2, 50)), gate (start (row, 3))};
  return due (row);
}

Row ft4BehindOnePeriod ()
{
  auto row = ft4 (3);
  row.sends = {anchor (at (row, 0, 21)), burst (start (row, 2) + 1), gate (at (row, 2, 21)),
               gate (start (row, 3))};
  return due (row);
}

// A decode again sent while the period's decode runs, then one after it;
// period 1 arrives while that one decodes.
Row decodeAgainDuringADecode ()
{
  auto row = ft4 (3);
  row.sends = {anchor (at (row, 0, 21)), burstAgain (at (row, 0, 21)), againAnchor (at (row, 0, 21)),
               burst (start (row, 2) + 1), gate (at (row, 2, 21)), gate (start (row, 3))};
  return due (row);
}

// MSK144 decodes each period at its end and is never refused (MainWindow::dataSink
// decodes it on its own path, without a busy check).
Row msk144BehindOnePeriod ()
{
  auto row = rowOf ("MSK144", {{"trperiod", 5}}, samples ("WSJT_MSK144_SAMPLE_WAV", Msk144PeriodSamples), true);
  row.schedule << Trigger {Msk144PeriodSamples, 16, Stage::None, true, false};
  row.checked = false;
  row.utc = utcLabels (QTime {12, 5, 0}, 5000, 3);
  row.sends = {anchor (start (row, 1)), burst (start (row, 2) + 1), gate (start (row, 3))};
  return due (row);
}

struct Expected
{
  QStringList decisions;
  QStringList skipped;
  QStringList finished;
  QList<int> finishedPeriods;  // each decode_finished's period
  QList<int> ran;              // indexes into finished whose final ran
  QList<bool> drains;          // per send: a deferred final drains once the stream is idle
  int refused {0};
};

QString passLine (int nzhsym, bool multithreaded, bool again = false)
{
  return QString {"pass %1 %2%3"}.arg (nzhsym).arg (multithreaded ? "T" : "F").arg (again ? " again" : "");
}

// The GUI's decisions for the row's triggers, with the decoder busy at every
// late trigger in a burst: MainWindow::decode () and, for the multithreaded
// final, Ft8MtdDecodeCoordinator, whose Ft8MtdDecodeScheduler this test runs.
Expected expected (Row const& row)
{
  Expected e;
  Ft8MtdDecodeScheduler scheduler;
  // currentFt8DecodePeriod () counts periods since the epoch; only differences matter.
  qint64 const first = 1000;
  struct Pending
  {
    int period;
    int nzhsym;
  };
  std::optional<Pending> pending;
  // The GUI consumes the anchor's completion after the triggers that fired
  // while it decoded (MainWindow::readFromStdout).
  std::optional<std::pair<Stage, int>> running;
  int decoded = -1;
  auto const finish = [&] (int p, bool ran) {
    if (ran) e.ran << e.finished.size ();
    e.finished << label (row, p);
    e.finishedPeriods << p;
  };
  auto const skip = [&] (int p, int nzhsym, QString const& stage, QString const& reason) {
    e.decisions << QString {"skip %1 %2 %3"}.arg (nzhsym).arg (stage, reason);
    e.skipped << QString {"%1 %2 %3 %4"}.arg (label (row, p)).arg (nzhsym).arg (stage, reason);
  };
  auto const frees = [&] (bool drop) {
    if (running) scheduler.completed (running->first, first + running->second);
    running.reset ();
    if (!pending) return false;
    if (drop)
      {
        // Ft8MtdDecodeCoordinator::drainPending drops a final whose identity
        // changed (DecodeOperatingContext::hasSameFt8PendingIdentity) and cancels.
        e.decisions << QString {"skip %1 final cancelled"}.arg (pending->nzhsym);
        e.skipped << QString {"%1 %2 final cancelled"}.arg (label (row, pending->period))
          .arg (pending->nzhsym);
        finish (pending->period, false);
        scheduler.cancel ();
        pending.reset ();
        return true;
      }
    // Ft8MtdDecodeCoordinator::drainPending runs the deferred final.
    e.decisions << passLine (pending->nzhsym, true);
    scheduler.published (Stage::Final, first + pending->period);
    running = std::make_pair (Stage::Final, pending->period);
    finish (pending->period, true);
    decoded = pending->period;
    pending.reset ();
    return true;
  };
  qint64 sent = 1;
  for (auto const& send : row.sends)
    {
      for (auto const& found : crossed (row, sent, send.until))
        {
          auto const p = found.first;
          auto const& t = found.second;
          auto busy = row.live && row.checked && Step::Burst == send.step;
          if (busy && sampleOf (row, found) > lateEnd (send))
            {
              busy = false;
              frees (send.dropsAtDrain);
            }
          auto ran = false;
          if (row.backpressure)
            {
              auto const blocked = busy || pending.has_value ();
              auto const decision = scheduler.request (t.stage, first + p, row.earlyStages, blocked,
                                                       pending.has_value ());
              switch (decision.action)
                {
                case Action::SkipEarly:
                  skip (p, t.nzhsym, "early", blocked ? "busy" : "backoff");
                  break;
                case Action::Publish:
                  ran = true;
                  scheduler.published (t.stage, first + p);
                  break;
                case Action::DeferFinal:
                  e.decisions << QString {"defer %1"}.arg (t.nzhsym);
                  pending = Pending {p, t.nzhsym};
                  break;
                case Action::ReplaceFinal:
                  e.decisions << QString {"replace %1"}.arg (t.nzhsym);
                  e.skipped << QString {"%1 %2 final replaced"}.arg (label (row, pending->period))
                    .arg (pending->nzhsym);
                  finish (pending->period, false);
                  pending = Pending {p, t.nzhsym};
                  break;
                }
            }
          else if (busy)
            {
              skip (p, t.nzhsym, t.final ? "final" : "early", "busy");
              if (t.final) finish (p, false);
            }
          else
            {
              ran = true;
            }
          if (!ran) continue;
          e.decisions << passLine (t.nzhsym, t.multithreaded);
          if (t.final)
            {
              finish (p, true);
              decoded = p;
            }
          if (Step::Anchor == send.step) running = std::make_pair (t.stage, p);
          else scheduler.completed (t.stage, first + p);
        }
      if (send.cancels)
        {
          // The GUI cancels on its decoder settings, mode and settings
          // dialog (MainWindow::cancelPendingFt8Decode), before they apply.
          if (pending)
            {
              e.decisions << QString {"skip %1 final cancelled"}.arg (pending->nzhsym);
              e.skipped << QString {"%1 %2 final cancelled"}.arg (label (row, pending->period))
                .arg (pending->nzhsym);
              finish (pending->period, false);
              pending.reset ();
            }
          scheduler.cancel ();
        }
      if (send.again)
        {
          if (Step::Burst == send.step)
            {
              ++e.refused;
            }
          else
            {
              e.decisions << passLine (row.againNzhsym, false, true);
              e.finished << label (row, decoded);
              e.finishedPeriods << decoded;
              if (Step::Anchor == send.step) running = std::make_pair (Stage::None, decoded);
            }
        }
      e.drains << (Step::Anchor != send.step && frees (send.dropsAtDrain));
      if (send.halt)
        {
          // The period cut short decodes what it has, every pass of it.
          auto const p = int (send.until / periodSamples (row));
          if (send.until % periodSamples (row) > 0)
            {
              for (auto const& t : row.schedule) e.decisions << passLine (t.nzhsym, t.multithreaded);
              finish (p, false);
            }
          break;
        }
      sent = send.until;
    }
  return e;
}

struct Run
{
  QByteArray out;
  QByteArray err;
  bool exited {false};
  int exitCode {-1};
  QProcess::ExitStatus exitStatus {QProcess::CrashExit};
  int markers {0};
  QString failure;       // a wait that ran out
  QStringList bursts;    // per burst: when the anchor's pass showed, when the burst was in, when the anchor ended
  QStringList late;      // bursts the stream had already run past when they were in
  qint64 elapsedMs {0};
};

QRegularExpression const decisionLine {
  R"(wsjt-codec --stream: (?:pass|skip|defer) nzhsym=)"};

Run run (Row const& row, QList<bool> const& drains)
{
  Run r;
  QTemporaryDir directory;
  if (!directory.isValid ())
    {
      r.failure = "no temporary directory";
      return r;
    }
  QProcess process;
  process.setProcessChannelMode (QProcess::SeparateChannels);
  // waitForReadyRead wakes on the pass lines.
  process.setReadChannel (QProcess::StandardError);
  process.setWorkingDirectory (directory.path ());
  process.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE),
                 {"-a", directory.path (), "-t", directory.path (), "--stream"});
  if (!process.waitForStarted (5000))
    {
      r.failure = process.errorString ();
      return r;
    }
  auto const read = [&] {
    // Qt 5 on Windows returns on stdout's data with stderr's readyRead still queued.
    QCoreApplication::processEvents ();
    r.out += process.readAllStandardOutput ();
    r.err += process.readAllStandardError ();
  };
  auto const pump = [&] (int ms) {
    if (process.bytesToWrite () > 0) process.waitForBytesWritten (ms);
    else process.waitForReadyRead (ms);
    read ();
  };
  auto const waitFor = [&] (std::function<bool ()> const& done, QString const& what, int ms = waitMs ()) {
    QElapsedTimer timer;
    timer.start ();
    while (!done ())
      {
        if (timer.elapsed () > ms || QProcess::Running != process.state ())
          {
            read ();
            if (done ()) return true;
            if (r.failure.isEmpty ()) r.failure = "waiting for " + what;
            return false;
          }
        pump (5);
      }
    return true;
  };
  auto const count = [&] (QByteArray const& text) {return r.out.count (text);};
  auto const finishedCount = [&] {return count (R"("t":"decode_finished")");};
  auto const markerCount = [&] {return count (R"("code":"configure_parse_error")");};

  process.write (sessionHeader () + configureFrame (with (row.settings, {{"utc", row.utc.first ()}})));
  if (!waitFor ([&] {return r.out.contains (R"("t":"ready")");}, "ready")) return r;
  process.write (frame (0x01u, recording (row, 0).left (2)));
  while (process.bytesToWrite () > 0 && QProcess::Running == process.state ()) process.waitForBytesWritten (5);
  QElapsedTimer clock;
  clock.start ();
  auto const waitUntilDue = [&] (qint64 samples) {
    auto const ms = samples / 12 + DueMarginMs;
    waitFor ([&] {return clock.elapsed () >= ms;}, "the audio to fall due", int (ms + 1000));
  };
  waitUntilDue (row.firstDue);

  auto const audio = [&] (qint64 from, qint64 to) {
    QByteArray bytes;
    auto const n = periodSamples (row);
    for (auto k = from; k < to;)
      {
        auto const p = int (k / n);
        auto const offset = int (k % n);
        if (0 == offset && p > 0) bytes += configureFrame ({{"utc", row.utc.at (p)}});
        auto const take = int (std::min<qint64> ({qint64 {FrameSamples}, qint64 {n - offset}, to - k}));
        bytes += frame (0x01u, recording (row, p).mid (2 * offset, 2 * take));
        k += take;
      }
    return bytes;
  };

  qint64 sent = 1;
  int markersSent = 0;
  int finishes = 0;      // decode_finished owed once the stream is idle
  int anchorAt = 0;      // r.err up to the anchor's pass line
  int anchorFinished = 0;
  qint64 anchorSeen = 0;
  bool anchorFinal = false;
  auto const finalNzhsym = row.schedule.last ().nzhsym;
  for (int i = 0; i < row.sends.size (); ++i)
    {
      auto const& send = row.sends.at (i);
      if (send.dueBefore > 0) waitUntilDue (send.dueBefore);
      auto const found = crossed (row, sent, send.until);
      for (auto const& each : found)
        {
          if (each.second.final) ++finishes;
        }
      if (send.again && Step::Burst != send.step) ++finishes;
      QByteArray bytes = audio (sent, send.until);
      for (auto const& keys : send.configures) bytes += configureFrame (keys);
      if (send.again) bytes += configureFrame ({{"nagain_flag", true}});
      if (Step::Anchor == send.step || Step::Drained == send.step)
        {
          auto const from = Step::Drained == send.step ? anchorAt : r.err.size ();
          QRegularExpression const pass {
            Step::Drained == send.step ? QString {"pass nzhsym=%1 [^\\n]*multithreaded=T[^\\n]*\\n"}.arg (finalNzhsym)
            : send.again ? QString {"pass nzhsym=%1 [^\\n]*again=T[^\\n]*\\n"}.arg (row.againNzhsym)
            : QString {"pass nzhsym=%1 [^\\n]*\\n"}.arg (found.back ().second.nzhsym)};
          anchorFinal = Step::Anchor != send.step || send.again || found.back ().second.final;
          QRegularExpression const declined {
            Step::Anchor == send.step && !send.again
            ? QString {"(?:skip|defer) nzhsym=%1 "}.arg (found.back ().second.nzhsym) : QString {"$^"}};
          process.write (bytes);
          QRegularExpressionMatch match;
          if (!waitFor ([&] {
                auto const text = QString::fromUtf8 (r.err);
                match = pass.match (text, from);
                if (!match.hasMatch () && text.indexOf (declined, from) >= 0) r.failure = "the anchor did not run";
                return match.hasMatch () || !r.failure.isEmpty ();
              }, "the anchor's pass line") || !r.failure.isEmpty ())
            {
              return r;
            }
          anchorSeen = clock.elapsed ();
          anchorAt = match.capturedEnd ();
          anchorFinished = anchorFinal ? finishes - 1 : finishes;
        }
      else
        {
          if (!send.halt)
            {
              bytes += marker ();
              ++markersSent;
            }
          else
            {
              bytes += controlFrame ({{"t", "halt"}});
            }
          process.write (bytes);
          if (Step::Burst == send.step)
            {
              while (process.bytesToWrite () > 0 && QProcess::Running == process.state ())
                {
                  process.waitForBytesWritten (5);
                }
              auto const in = clock.elapsed ();
              read ();
              auto const ended = [&] {
                return QString::fromUtf8 (r.err).indexOf (decisionLine, anchorAt) >= 0
                  || (anchorFinal && finishedCount () > anchorFinished);
              };
              auto const ranPast = ended ();
              waitFor (ended, "the anchor to end");
              auto const line = QString {"anchor seen %1 ms, burst in %2 ms later, anchor ended %3 ms later"}
                .arg (anchorSeen).arg (in - anchorSeen).arg (clock.elapsed () - anchorSeen);
              r.bursts << line;
              if (ranPast) r.late << line;
            }
          if (send.halt) break;
          // Every decode_finished but a drained final's comes before the marker's
          // error; the next step may anchor on the drained final.
          auto const next = i + 1 < row.sends.size () ? row.sends.at (i + 1).step : Step::Gate;
          auto const draining = drains.value (i) && Step::Drained != next;
          if (!waitFor ([&] {return markerCount () >= markersSent && (!draining || finishedCount () >= finishes);},
                        QString {"the stream to read up to sample %1"}.arg (send.until)))
            {
              return r;
            }
        }
      sent = send.until;
    }
  process.closeWriteChannel ();
  r.exited = process.waitForFinished (waitMs ());
  if (!r.exited)
    {
      r.failure = "waiting for the stream to exit";
      process.kill ();
      process.waitForFinished (5000);
    }
  read ();
  r.exitCode = process.exitCode ();
  r.exitStatus = process.exitStatus ();
  r.markers = markersSent;
  r.elapsedMs = clock.elapsed ();
  return r;
}

struct Observed
{
  QStringList decisions;
  QStringList skipped;
  QStringList finished;
  QList<QList<QJsonObject>> decodes;   // per decode_finished
  QStringList errors;                  // codes other than the markers'
  int markers {0};
  int refused {0};
  QString invalid;
  QStringList unfinished;              // a skipped final's period with no decode_finished after it
};

Observed observe (Run const& r, QString const& mode)
{
  Observed o;
  QRegularExpression const line {
    R"(wsjt-codec --stream: (?:pass nzhsym=(\d+) samples=\d+ depth=\d+ cycles=\d+ multithreaded=([TF]) live=[TF] again=([TF]) emedelay=-?[0-9.]+|skip nzhsym=(\d+) stage=(\w+) reason=(\w+)|defer nzhsym=(\d+))\r?$)",
    QRegularExpression::MultilineOption};
  auto matches = line.globalMatch (QString::fromUtf8 (r.err));
  while (matches.hasNext ())
    {
      auto const m = matches.next ();
      if (m.capturedLength (1) > 0)
        {
          o.decisions << passLine (m.captured (1).toInt (), "T" == m.captured (2), "T" == m.captured (3));
        }
      else if (m.capturedLength (4) > 0)
        {
          o.decisions << QString {"skip %1 %2 %3"}.arg (m.captured (4), m.captured (5), m.captured (6));
        }
      else
        {
          o.decisions << QString {"defer %1"}.arg (m.captured (7));
        }
    }
  // A replaced final's skip and its successor's deferral, in either order.
  for (int i = 0; i < o.decisions.size (); ++i)
    {
      auto const parts = o.decisions.at (i).split (' ');
      if (4 != parts.size () || "skip" != parts.at (0) || "replaced" != parts.at (3)) continue;
      auto const deferral = "defer " + parts.at (1);
      auto const replace = "replace " + parts.at (1);
      if (i + 1 < o.decisions.size () && deferral == o.decisions.at (i + 1))
        {
          o.decisions.removeAt (i + 1);
          o.decisions[i] = replace;
        }
      else if (i > 0 && deferral == o.decisions.at (i - 1))
        {
          o.decisions.removeAt (i);
          o.decisions[--i] = replace;
        }
    }

  QList<QJsonObject> current;
  for (auto const& text : QByteArray {r.out}.replace ("\r\n", "\n").split ('\n'))
    {
      if (text.trimmed ().isEmpty ()) continue;
      auto const event = QJsonDocument::fromJson (text).object ();
      auto const type = event.value ("t").toString ();
      if (event.value ("v").toInt () != 1 || type.isEmpty ())
        {
          o.invalid = QString::fromUtf8 (text);
          continue;
        }
      if ("pass_skipped" == type)
        {
          o.skipped << QString {"%1 %2 %3 %4"}.arg (event.value ("period_end").toString ())
            .arg (event.value ("nzhsym").toInt ())
            .arg (event.value ("stage").toString (), event.value ("reason").toString ());
          if (event.value ("mode").toString () != mode) o.invalid = QString::fromUtf8 (text);
          if ("final" == event.value ("stage").toString ())
            {
              o.unfinished << event.value ("period_end").toString ();
            }
        }
      else if ("decode_finished" == type)
        {
          o.unfinished.removeOne (event.value ("period_end").toString ());
          o.finished << event.value ("period_end").toString ();
          o.decodes << current;
          current.clear ();
        }
      else if ("decode" == type)
        {
          current << event;
        }
      else if ("error" == type)
        {
          auto const code = event.value ("code").toString ();
          if ("configure_parse_error" == code) ++o.markers;
          else if ("decode_again_unavailable" == code) ++o.refused;
          else o.errors << QString::fromUtf8 (text);
        }
    }
  return o;
}

// An anchor ends at a trigger, or is a decode again. Live, it and each gate
// carry no other trigger, or one after the first would arrive while the
// first decodes; a burst's free trigger is its last, and falls due well
// after its late audio.
QString construction (Row const& row)
{
  qint64 sent = 1;
  for (auto const& send : row.sends)
    {
      auto const found = crossed (row, sent, send.until);
      auto const anchorEnd = !found.empty () && sampleOf (row, found.back ()) == send.until;
      if (Step::Anchor == send.step && !send.again
          && (!anchorEnd || (row.live && 1 != found.size ())))
        {
          return QString {"the anchor at %1 is not one trigger"}.arg (send.until);
        }
      if ((Step::Drained == send.step || (Step::Anchor == send.step && send.again)) && !found.empty ())
        {
          return QString {"the step at %1 carries audio"}.arg (send.until);
        }
      if (row.live && Step::Gate == send.step && found.size () > 1)
        {
          return QString {"the gate at %1 carries %2 triggers"}.arg (send.until).arg (found.size ());
        }
      if (Step::Burst == send.step)
        {
          for (std::size_t i = 0; i < found.size (); ++i)
            {
              auto const sample = sampleOf (row, found.at (i));
              if (sample <= lateEnd (send)) continue;
              if (i + 1 != found.size () || sample < lateEnd (send) + FreeAfterSamples)
                {
                  return QString {"the burst at %1 has a trigger neither late nor free"}.arg (send.until);
                }
            }
        }
      sent = send.until;
    }
  return {};
}

void check (Row const& row)
{
  for (auto const& pcm : row.recordings) QVERIFY2 (!pcm.isEmpty (), "no recording");
  QVERIFY2 (construction (row).isEmpty (), qPrintable (construction (row)));
  auto const want = expected (row);
  auto const got = run (row, want.drains);
  auto const seen = observe (got, row.mode);
  auto const report = QString {
    "\nexpected passes: %1\nseen passes:     %2\nexpected pass_skipped: %3\nseen pass_skipped:     %4"
    "\nexpected decode_finished: %5\nseen decode_finished:     %6\nbursts: %7\n%8"}
    .arg (want.decisions.join (" | "), seen.decisions.join (" | "), want.skipped.join (" | "),
          seen.skipped.join (" | "), want.finished.join (" "), seen.finished.join (" "),
          got.bursts.join ("; "), got.failure);
  QVERIFY2 (got.failure.isEmpty (), qPrintable (report + "\n" + QString::fromUtf8 (got.err)));
  QCOMPARE (got.exitStatus, QProcess::NormalExit);
  QCOMPARE (got.exitCode, 0);
  QVERIFY2 (seen.invalid.isEmpty (), qPrintable (seen.invalid));
  // A skipped final's pass_skipped comes before its period's decode_finished.
  QVERIFY2 (seen.unfinished.isEmpty (), qPrintable (seen.unfinished.join (' ')));
  QVERIFY2 (seen.decisions == want.decisions, qPrintable (report));
  QVERIFY2 (seen.skipped == want.skipped, qPrintable (report));
  QVERIFY2 (seen.finished == want.finished, qPrintable (report));
  QCOMPARE (seen.refused, want.refused);
  QCOMPARE (seen.markers, got.markers);
  QVERIFY2 (seen.errors.isEmpty (), qPrintable (seen.errors.join ('\n')));
  // Each decode carries its period's time and, when the periods alternate
  // recordings, its period's messages; a deferred final keeps both.
  auto const alternate = row.recordings.size () > 1;
  for (int i = 0; i < seen.decodes.size (); ++i)
    {
      auto const engine = alternate && 1 == row.sources.value (want.finishedPeriods.at (i), 0);
      for (auto const& decode : seen.decodes.at (i))
        {
          QCOMPARE (decode.value ("time").toString (), seen.finished.at (i));
          auto const message = decode.value ("message").toString ().trimmed ();
          if (alternate)
            {
              QVERIFY2 (engine == (EngineMessage == message),
                        qPrintable (QString {"%1 decoded in %2"}.arg (message, seen.finished.at (i))));
            }
        }
      if (alternate && want.ran.contains (i))
        {
          QVERIFY2 (!seen.decodes.at (i).isEmpty (),
                    qPrintable (QString {"the final of %1 decoded nothing"}.arg (seen.finished.at (i))));
        }
    }
  // A burst the stream had run past before it was in tests nothing live.
  if (row.live) QVERIFY2 (got.late.isEmpty (), qPrintable (got.late.join ("; ")));
  qInfo ().noquote () << QString {"%1 s; bursts: %2"}.arg (got.elapsedMs / 1000.0, 0, 'f', 1)
    .arg (got.bursts.join ("; "));
}

bool recordings ()
{
  for (auto const name : {"WSJT_FT8_SAMPLE_WAV", "WSJT_FT8_ENGINE_WAV", "WSJT_FT4_SAMPLE_WAV",
                          "WSJT_MSK144_SAMPLE_WAV"})
    {
      if (qEnvironmentVariableIsEmpty (name)) return false;
    }
  return true;
}
}

// Live audio that arrives while the stream decodes is decoded as the GUI
// decodes when it falls behind (widgets/mainwindow.cpp decode ()): each row
// stalls, then sends audio during a decode, so the decoder is busy by
// construction at each late trigger in the burst and free at every other.
class TestStreamingBacklog final : public QObject
{
  Q_OBJECT

private slots:
  // Period 1's early pass is skipped and its final deferred, then decoded
  // before period 2's audio falls due; period 2 waits out the backoff and
  // period 3 probes.
  void mtdBehindOnePeriodSkipsAndDefers ()
  {
    if (!recordings ()) QSKIP ("run by test_streaming_backlog_*");
    check (mtdBehindOnePeriod (true));
  }

  void mtdRunsEveryPassWhenFree ()
  {
    if (!recordings ()) QSKIP ("run by test_streaming_backlog_*");
    check (withTheDecoderFree (mtdBehindOnePeriod (true)));
  }

  // From a file the same burst skips nothing.
  void mtdFromAFileNeverSkips ()
  {
    if (!recordings ()) QSKIP ("run by test_streaming_backlog_*");
    check (mtdBehindOnePeriod (false));
  }

  // Period 2's final replaces period 1's pending one; at halt it drains and
  // the period cut short decodes what it has.
  void mtdBehindTwoPeriodsReplacesTheFinal ()
  {
    if (!recordings ()) QSKIP ("run by test_streaming_backlog_*");
    check (mtdBehindTwoPeriods ());
  }

  // A probe whose final finds its second early pass still decoding fails,
  // and the backoff doubles.
  void mtdFinalDuringItsProbeBacksOff ()
  {
    if (!recordings ()) QSKIP ("run by test_streaming_backlog_*");
    check (mtdFinalDuringItsProbe ());
  }

  // A final that published at once took the stream out of the backoff, which
  // the next fall behind shows.
  void mtdRecoveryRestartsTheBackoff ()
  {
    if (!recordings ()) QSKIP ("run by test_streaming_backlog_*");
    check (mtdBehindAfterRecovery ());
  }

  // Each cancel ends the deferred final's period and resets the backoff.
  void mtdCancelDropsTheDeferredFinal ()
  {
    if (!recordings ()) QSKIP ("run by test_streaming_backlog_*");
    check (mtdCancels ());
  }

  void houndDropsTheFinalWhileBusy ()
  {
    if (!recordings ()) QSKIP ("run by test_streaming_backlog_*");
    check (houndBehindOnePeriod ());
  }

  void standardFt8DropsPassesWhileBusy ()
  {
    if (!recordings ()) QSKIP ("run by test_streaming_backlog_*");
    check (standardBehindOnePeriod ());
  }

  void standardFt8RunsPassesWhenFree ()
  {
    if (!recordings ()) QSKIP ("run by test_streaming_backlog_*");
    check (withTheDecoderFree (standardBehindOnePeriod ()));
  }

  void ft4DropsTheDecodeWhileBusy ()
  {
    if (!recordings ()) QSKIP ("run by test_streaming_backlog_*");
    check (ft4BehindOnePeriod ());
  }

  void ft4DecodesWhenFree ()
  {
    if (!recordings ()) QSKIP ("run by test_streaming_backlog_*");
    check (withTheDecoderFree (ft4BehindOnePeriod ()));
  }

  // The GUI disables its Decode button while the decoder is busy, and a
  // decode again keeps it busy.
  void decodeAgainRefusedWhileBusy ()
  {
    if (!recordings ()) QSKIP ("run by test_streaming_backlog_*");
    check (decodeAgainDuringADecode ());
  }

  void decodeAgainRunsWhenFree ()
  {
    if (!recordings ()) QSKIP ("run by test_streaming_backlog_*");
    check (withTheDecoderFree (decodeAgainDuringADecode ()));
  }

  void msk144NeverRefused ()
  {
    if (!recordings ()) QSKIP ("run by test_streaming_backlog_*");
    check (msk144BehindOnePeriod ());
  }
};

QTEST_GUILESS_MAIN (TestStreamingBacklog)

#include "test_streaming_backlog.moc"
