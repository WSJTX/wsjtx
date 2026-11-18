// SPDX-License-Identifier: GPL-3.0-or-later
// The decoders' side outputs as events, the Q65 and Fox caller lists' edit
// keys, and the submode and period labels on decode events.
//
// The oracle for each event is the side file jt9 writes in WAV mode for the
// same audio: a separate process in a fresh directory, on the worker's path
// (the codec writes the same files). For the Q65 caller list's edits it is
// the GUI's own rm_q3list (side_gui_model) between jt9 runs. Fox, the Q65
// contest and JT4 averaging have no recordings: their audio comes from the
// simulators under WSJT_BUILD_UTILS (CMake's prepare_side_* fixtures).
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <random>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include "DecodeEventLine.hpp"

namespace
{
constexpr int SampleRate = 12000;
// A client that limits a stdout line to 64 KiB.
constexpr int LineLimit = 65536;
constexpr double TwoPi = 6.283185307179586;

struct Pcm
{
  int rate {0};
  QByteArray samples;
};

Pcm readWav (QString const& path)
{
  Pcm pcm;
  QFile file {path};
  if (!file.open (QIODevice::ReadOnly)) return pcm;
  auto const bytes = file.readAll ();
  if (bytes.left (4) != "RIFF" || bytes.mid (8, 4) != "WAVE") return pcm;
  for (int at = 12; at + 8 <= bytes.size ();)
    {
      auto const size = int (qFromLittleEndian<quint32> (bytes.constData () + at + 4));
      if (bytes.mid (at, 4) == "fmt ") pcm.rate = int (qFromLittleEndian<quint32> (bytes.constData () + at + 12));
      if (bytes.mid (at, 4) == "data")
        {
          pcm.samples = bytes.mid (at + 8, size);
          break;
        }
      at += 8 + size + (size & 1);
    }
  return pcm;
}

// The stream takes 12 kHz only; jt9 decodes the same resampled audio.
QByteArray samples12k (Pcm const& pcm)
{
  if (pcm.rate == SampleRate) return pcm.samples;
  auto const count = pcm.samples.size () / 2;
  auto const sample = [&] (int i) {return double (qFromLittleEndian<qint16> (pcm.samples.constData () + 2 * i));};
  auto const n = int (qint64 (count) * SampleRate / pcm.rate);
  QByteArray out (2 * n, '\0');
  for (int i = 0; i < n; ++i)
    {
      double const x = double (i) * pcm.rate / SampleRate;
      int const k = int (x);
      double const f = x - k;
      double const value = sample (k) * (1 - f) + (k + 1 < count ? sample (k + 1) : sample (k)) * f;
      qToLittleEndian<qint16> (qint16 (qBound (-32768, qRound (value), 32767)), out.data () + 2 * i);
    }
  return out;
}

bool writeWav (QString const& path, QByteArray const& samples)
{
  QByteArray header {"RIFF"};
  auto const u32 = [&header] (quint32 v) {char b[4]; qToLittleEndian (v, b); header.append (b, 4);};
  auto const u16 = [&header] (quint16 v) {char b[2]; qToLittleEndian (v, b); header.append (b, 2);};
  u32 (36 + samples.size ());
  header += "WAVEfmt ";
  u32 (16); u16 (1); u16 (1); u32 (SampleRate); u32 (2 * SampleRate); u16 (2); u16 (16);
  header += "data";
  u32 (samples.size ());
  QFile file {path};
  return file.open (QIODevice::WriteOnly) && file.write (header + samples) == header.size () + samples.size ();
}

QByteArray frame (quint8 type, QByteArray const& body)
{
  QByteArray bytes (5, '\0');
  bytes[0] = char (type);
  qToLittleEndian<quint32> (quint32 (body.size ()), bytes.data () + 1);
  return bytes + body;
}

QByteArray sessionHeader ()
{
  return QByteArray {"WSJT\0\1\x0c\0", 8};
}

QByteArray configure (QJsonObject keys)
{
  keys.insert ("t", "configure");
  return frame (0x02u, QJsonDocument {keys}.toJson (QJsonDocument::Compact));
}

QByteArray audio (QByteArray const& samples)
{
  QByteArray frames;
  for (int offset = 0; offset < samples.size (); offset += 24000) frames += frame (0x01u, samples.mid (offset, 24000));
  return frames;
}

QJsonObject with (QJsonObject keys, QJsonObject const& more)
{
  for (auto it = more.begin (); it != more.end (); ++it) keys.insert (it.key (), it.value ());
  return keys;
}

struct Run
{
  bool ok {false};
  QByteArray out;
  QByteArray err;
};

Run run (QString const& program, QStringList const& arguments, QByteArray const& input, QString const& directory)
{
  QProcess process;
  auto environment = QProcessEnvironment::systemEnvironment ();
  environment.insert ("OMP_STACKSIZE", "16M");
  process.setProcessEnvironment (environment);
  process.setWorkingDirectory (directory);
  process.start (program, arguments);
  Run result;
  if (!process.waitForStarted (5000)) return result;
  process.write (input);
  process.closeWriteChannel ();
  bool const finished = process.waitForFinished (-1);
  result.out = process.readAllStandardOutput ().replace ("\r\n", "\n");
  result.err = process.readAllStandardError ();
  result.ok = finished && process.exitStatus () == QProcess::NormalExit && process.exitCode () == 0;
  return result;
}

QList<QJsonObject> eventsOf (QByteArray const& output)
{
  QList<QJsonObject> events;
  for (auto const& line : output.split ('\n'))
    if (!line.isEmpty ()) events << QJsonDocument::fromJson (line).object ();
  return events;
}

QList<QJsonObject> ofType (QList<QJsonObject> const& events, QString const& type)
{
  QList<QJsonObject> matching;
  for (auto const& event : events)
    if (event.value ("t").toString () == type) matching << event;
  return matching;
}

// The events of each period, split after each decode_finished.
QList<QList<QJsonObject>> byPeriod (QList<QJsonObject> const& events)
{
  QList<QList<QJsonObject>> periods {{}};
  for (auto const& event : events)
    {
      periods.last () << event;
      if (event.value ("t").toString () == "decode_finished") periods << QList<QJsonObject> {};
    }
  if (periods.last ().isEmpty ()) periods.removeLast ();
  return periods;
}

// C's printf: Qt's asprintf rounds a tie (a float such as 35007.8203125
// to 6 decimals) away from zero, where gfortran and C round to even.
QString cformat (char const* format, ...)
{
  char buffer[256];
  va_list arguments;
  va_start (arguments, format);
  std::vsnprintf (buffer, sizeof buffer, format, arguments);
  va_end (arguments);
  return QString::fromLatin1 (buffer);
}

QStringList fileLines (QString const& path)
{
  QFile file {path};
  if (!file.open (QIODevice::ReadOnly)) return {};
  // gfortran ends each record in CR LF on Windows.
  auto const bytes = file.readAll ().replace ("\r\n", "\n");
  QStringList lines;
  for (auto const& line : bytes.split ('\n')) lines << QString::fromLatin1 (line);
  if (!lines.isEmpty () && lines.last ().isEmpty ()) lines.removeLast ();
  return lines;
}

// One period of a session: its 12 kHz audio, its label, jt9's file name for
// it (which carries the label), and the keys its configure adds.
struct Period
{
  QByteArray pcm;
  QString utc;
  QString name;
  QJsonObject keys;
};

// Nothing for no audio, so that a missing file shows as missing.
QByteArray cut (QByteArray pcm, double seconds)
{
  if (pcm.isEmpty ()) return {};
  auto const bytes = 2 * int (seconds * SampleRate);
  return pcm.left (bytes).leftJustified (bytes, '\0');
}

// The sum of the parts at 0.3 each, rounded and clipped.
QByteArray mix (QList<QByteArray> const& parts)
{
  int n = 0;
  for (auto const& part : parts) n = std::max (n, int (part.size () / 2));
  QByteArray out (2 * n, '\0');
  for (int k = 0; k < n; ++k)
    {
      double sum = 0;
      for (auto const& part : parts)
        if (2 * k + 1 < part.size ()) sum += qFromLittleEndian<qint16> (part.constData () + 2 * k);
      qToLittleEndian<qint16> (qint16 (std::max (-32768.0, std::min (32767.0, std::round (0.3 * sum)))),
                               out.data () + 2 * k);
    }
  return out;
}

// pcm times gain in Gaussian noise of sigma, from a fixed seed through
// Box-Muller on mt19937's specified output, so every platform gets the same.
QByteArray noisy (QByteArray const& pcm, double gain, double sigma, unsigned seed)
{
  std::mt19937 random {seed};
  auto const uniform = [&random] {return (double (random ()) + 0.5) / 4294967296.0;};
  QByteArray out (pcm.size (), '\0');
  for (int k = 0; 2 * k + 1 < pcm.size (); ++k)
    {
      // Separate statements: the order of the two draws is fixed.
      double const u1 = uniform ();
      double const u2 = uniform ();
      double const noise = sigma * std::sqrt (-2.0 * std::log (u1)) * std::cos (TwoPi * u2);
      double const value = gain * qFromLittleEndian<qint16> (pcm.constData () + 2 * k) + noise;
      qToLittleEndian<qint16> (qint16 (std::max (-32768.0, std::min (32767.0, std::round (value)))),
                               out.data () + 2 * k);
    }
  return out;
}

QString fixtures ()
{
  return qEnvironmentVariable ("WSJT_SIDE_FIXTURES");
}

QString samples ()
{
  return qEnvironmentVariable ("WSJT_SAMPLES_DIR");
}

QByteArray fixture (QString const& path)
{
  return readWav (fixtures () + '/' + path).samples;
}

// A sample, at 12 kHz; nothing when it cannot be read.
QByteArray sample (QString const& path)
{
  auto const pcm = readWav (samples () + '/' + path);
  if (pcm.rate <= 0 || pcm.samples.isEmpty ()) return {};
  return samples12k (pcm);
}

// The audio of a row, period by period; empty when its fixtures or samples
// are not in this build (why, in reason).
QList<Period> periodsOf (QString const& set, QString& reason)
{
  QList<Period> periods;
  if (set.startsWith ("fox"))
    {
      if (fixtures ().isEmpty ()) {reason = "the simulators are built only with WSJT_BUILD_UTILS"; return {};}
      auto const a = fixture ("fox-A/000000_000001.wav"), b = fixture ("fox-B/000000_000001.wav");
      auto const c = fixture ("fox-C/000000_000001.wav"), d = fixture ("fox-D/000000_000001.wav");
      if (a.isEmpty () || b.isEmpty () || c.isEmpty () || d.isEmpty ()) {reason = "no ft8sim fixtures"; return {};}
      // Two callers; a third while both are 2 units old; then a CQ when every
      // caller is 6 or more units old (all aged out); then the two again.
      periods << Period {cut (mix ({a, b}), 15), "00:00:00", "000000_000000.wav", {}}
              << Period {cut (mix ({d}), 15), "00:01:00", "000000_000100.wav", {}}
              << Period {cut (mix ({c}), 15), "00:04:00", "000000_000400.wav", {}}
              << Period {cut (mix ({a, b}), 15), "00:04:15", "000000_000415.wav", {}};
    }
  else if (set == "q65contest")
    {
      if (fixtures ().isEmpty ()) {reason = "the simulators are built only with WSJT_BUILD_UTILS"; return {};}
      auto const w = fixture ("q65-W/000000_0000.wav"), k = fixture ("q65-K/000000_0000.wav");
      auto const g = fixture ("q65-G/000000_0000.wav");
      if (w.isEmpty () || k.isEmpty () || g.isEmpty ()) {reason = "no q65sim fixtures"; return {};}
      periods << Period {cut (mix ({w, k}), 30), "00:00:00", "000000_000000.wav", {}}
              << Period {cut (mix ({g}), 30), "00:00:30", "000000_000030.wav", {}};
    }
  else if (set == "jt4")
    {
      if (fixtures ().isEmpty ()) {reason = "the simulators are built only with WSJT_BUILD_UTILS"; return {};}
      auto const signal = fixture ("jt4/000000_0001.wav");
      if (signal.isEmpty ()) {reason = "no jt4sim fixture"; return {};}
      // About -24 dB: sync without a single-period decode, so the periods average.
      for (int n = 1; n <= 3; ++n)
        periods << Period {cut (noisy (signal, 0.0025, 1500.0, 1000u + unsigned (n)), 60),
                           QString {"00:0%1:00"}.arg (n), QString {"000000_000%1.wav"}.arg (n), {}};
    }
  else if (set == "jt65")
    {
      if (samples ().isEmpty ()) {reason = "no samples/ in this tree"; return {};}
      for (int n = 1; n <= 4; ++n)
        periods << Period {cut (sample (QString {"JT65/JT65B/000000_000%1.wav"}.arg (n)), 60),
                           QString {"00:0%1:00"}.arg (n), QString {"000000_000%1.wav"}.arg (n), {}};
    }
  else if (set == "q65avg")
    {
      if (samples ().isEmpty ()) {reason = "no samples/ in this tree"; return {};}
      for (auto const& time : {"0007", "0147", "0151"})
        periods << Period {cut (sample (QString {"Q65/60B_1296_Troposcatter/210109_%1.wav"}.arg (time)), 60),
                           QString {"%1:%2:00"}.arg (QString {time}.left (2), QString {time}.mid (2)),
                           QString {"210109_%1.wav"}.arg (time), {}};
    }
  for (auto const& period : periods)
    if (period.pcm.isEmpty ()) {reason = "missing audio for " + set; return {};}
  return periods;
}

QByteArray sessionInput (QList<Period> const& periods, QJsonObject const& first, double seconds)
{
  auto input = sessionHeader ();
  for (int n = 0; n < periods.size (); ++n)
    {
      auto keys = with (n ? QJsonObject {} : with (first, {{"live_audio", false}}), periods[n].keys);
      keys.insert ("utc", periods[n].utc);
      input += configure (keys) + audio (cut (periods[n].pcm, seconds));
    }
  return input;
}

QStringList paths (QString const& directory)
{
  return {"-a", directory, "-t", directory, "-r", qEnvironmentVariable ("WSJT_SHARE_DIR")};
}

// jt9 on the first count periods, in a fresh directory of its own; dir keeps it.
Run jt9On (QList<Period> const& periods, int count, QStringList const& options, QTemporaryDir& dir)
{
  QStringList wavs;
  for (int n = 0; n < count; ++n)
    {
      wavs << dir.filePath (periods[n].name);
      if (!writeWav (wavs.last (), periods[n].pcm)) return {};
    }
  return run (QString::fromUtf8 (JT9_EXECUTABLE), options + paths (dir.path ()) + wavs, {}, dir.path ());
}

// Each record of a fox_callers event as houndcallers.txt's line, format 1004
// of lib/ft8_engine_kernel.f90: (a12,1x,a4,i5,i6,i7,i3).
QStringList foxLines (QJsonObject const& event)
{
  QStringList lines;
  for (auto const& value : event.value ("callers").toArray ())
    {
      auto const caller = value.toObject ();
      lines << cformat ("%-12s %-4s%5d%6d%7d%3d", caller.value ("call").toString ().toLatin1 ().constData (),
                                  caller.value ("grid").toString ().toLatin1 ().constData (),
                                  caller.value ("snr").toInt (), caller.value ("freq").toInt (),
                                  caller.value ("distance").toInt (), caller.value ("age").toInt ());
    }
  return lines;
}

// Each entry of an averages event as avemsg.txt's line, as
// write_jt4_averages and write_jt65_averages write it:
// (a1,i5.4,f6.1,f6.2,i6,1x,a1), the time as HHMM.
QStringList averageLines (QJsonObject const& event)
{
  QStringList lines;
  bool const jt4 = event.value ("mode").toString () == "JT4";
  for (auto const& value : event.value ("entries").toArray ())
    {
      auto const entry = value.toObject ();
      auto const polarity = entry.value ("polarity").toInt ();
      char const mark = jt4 ? (polarity < 0 ? '$' : '*') : (polarity < 0 ? '#' : polarity > 0 ? '*' : ' ');
      lines << cformat ("%c%5.4d%6.1f%6.2f%6d %c", entry.value ("used").toBool () ? '$' : '.',
                                  entry.value ("time").toString ().toInt () / 100,
                                  double (float (entry.value ("sync").toDouble ())),
                                  double (float (entry.value ("dt").toDouble ())), entry.value ("freq").toInt (), mark);
    }
  return lines;
}

QVector<float> floats (QJsonValue const& value)
{
  auto const bytes = QByteArray::fromBase64 (value.toString ().toLatin1 ());
  QVector<float> out (bytes.size () / 4);
  for (int i = 0; i < out.size (); ++i) out[i] = qFromLittleEndian<float> (bytes.constData () + 4 * i);
  return out;
}

// A q65_sync event as red.dat's lines, as write_q65_curves writes them
// (lib/q65_host_support.f90): (f10.3,2f15.6), each bin's frequency i*df in
// single precision.
QStringList syncLines (QJsonObject const& event)
{
  auto const averaged = floats (event.value ("averaged")), current = floats (event.value ("current"));
  auto const df = float (event.value ("df").toDouble ());
  auto const first = event.value ("first_bin").toInt ();
  QStringList lines {cformat ("%10.3f%15.6f%15.6f", double (float (event.value ("dt").toDouble ())),
                                        double (float (event.value ("averaged_min").toDouble ())),
                                        double (float (event.value ("averaged_max").toDouble ())))};
  if (averaged.size () != current.size ()) return {"averaged and current differ in length"};
  for (int j = 0; j < averaged.size (); ++j)
    {
      float const freq = float (first + j) * df;
      lines << cformat ("%10.3f%15.6f%15.6f", double (freq), double (averaged[j]), double (current[j]));
    }
  return lines;
}

// A q65_callers event as side_gui_model's list of tsil.3q: "call|grid|nsec|
// nfreq|moonel", the decoder's Moon elevation always 0; withTime false
// leaves nsec out (another process's wall clock).
QStringList callerLines (QJsonObject const& event, bool withTime = true)
{
  QStringList lines;
  for (auto const& value : event.value ("callers").toArray ())
    {
      auto const caller = value.toObject ();
      lines << QStringList {caller.value ("call").toString (), caller.value ("grid").toString (),
                            withTime ? QString::number (qint64 (caller.value ("last_seen").toDouble ())) : QString {},
                            QString::number (caller.value ("freq").toInt ()), "0"}.join ('|');
    }
  return lines;
}

// tsil.3q in directory through the GUI's reader; empty with ok false when it
// cannot be read, and with ok true when the file is absent (the GUI's empty list).
QStringList tsil (QString const& directory, bool& ok, bool withTime = true)
{
  ok = false;
  auto const file = directory + "/tsil.3q";
  if (!QFile::exists (file))
    {
      ok = true;
      return {};
    }
  auto const listed = run (QString::fromUtf8 (SIDE_GUI_MODEL), {"list", file}, {}, directory);
  if (!listed.ok) return {QString::fromLatin1 (listed.out + listed.err)};
  auto lines = QString::fromLatin1 (listed.out).split ('\n', Qt::SkipEmptyParts);
  if (lines.isEmpty () || !lines.first ().startsWith ("count ")) return lines;
  lines.removeFirst ();
  ok = true;
  if (!withTime)
    for (auto& line : lines)
      {
        auto fields = line.split ('|');
        fields[2].clear ();
        line = fields.join ('|');
      }
  return lines;
}

// The last-seen times of two lists of the same callers agree within seconds.
bool timesAgree (QStringList const& a, QStringList const& b, qint64 seconds)
{
  if (a.size () != b.size ()) return false;
  for (int i = 0; i < a.size (); ++i)
    if (std::abs (a[i].section ('|', 2, 2).toLongLong () - b[i].section ('|', 2, 2).toLongLong ()) > seconds) return false;
  return true;
}

QString show (QStringList const& lines)
{
  return "|" + lines.join ("|\n|") + "|";
}

// houndcallers.txt after each pass of fox_gui_model: jt9's WAV path run on
// the periods in one process, as the GUI's decoder process runs, with the
// file deleted before each period in resets, as the GUI's Fox reset does.
struct ModelPass
{
  QString utc;
  int pass {0};
  QStringList lines;
};

bool foxModel (QList<Period> const& periods, bool multithreaded, QSet<int> const& resets, QList<ModelPass>& passes,
               QString& failure)
{
  QTemporaryDir dir;
  if (!dir.isValid ()) {failure = "no directory"; return false;}
  QStringList arguments {dir.path (), qEnvironmentVariable ("WSJT_SHARE_DIR"), "K1ABC", "FN42",
                         multithreaded ? "1" : "0"};
  for (int n = 0; n < periods.size (); ++n)
    {
      if (resets.contains (n)) arguments << "reset";
      auto const wav = dir.filePath (QString {"period%1.wav"}.arg (n + 1));
      if (!writeWav (wav, periods[n].pcm)) {failure = "cannot write " + wav; return false;}
      arguments << wav + '@' + QString {periods[n].utc}.remove (':');
    }
  auto const model = run (QString::fromUtf8 (FOX_GUI_MODEL), arguments, {}, dir.path ());
  if (!model.ok) {failure = "fox_gui_model failed: " + QString::fromLatin1 (model.err); return false;}
  // Its own lines start pass| or hound|; the rest are the decoder's lines.
  for (auto const& line : QString::fromLatin1 (model.out).split ('\n'))
    {
      if (line.startsWith ("pass|"))
        {
          auto const fields = line.split ('|');
          bool number = false;
          int const pass = fields.value (2).toInt (&number);
          if (fields.size () != 3 || fields[1].size () != 6 || !number || pass <= 0)
            {
              failure = "malformed model line: " + line;
              return false;
            }
          passes << ModelPass {fields[1], pass, {}};
        }
      else if (line.startsWith ("hound|"))
        {
          if (passes.isEmpty ()) {failure = "a hound line before any pass: " + line; return false;}
          passes.last ().lines << line.mid (6);
        }
    }
  return true;
}

// Each fox_callers line, in order, against the file after the same pass
// (in any order with the multithreaded decoder, whose threads report in any
// order). A pass that ages out every caller writes no record and leaves the
// last pass's lines in the file, while its line has none
// (doc/codec/interface.md, "Side outputs and labels"): counted in stale.
bool compareFoxPasses (QList<QJsonObject> const& events, QList<ModelPass> const& passes, bool anyOrder, int& stale,
                       QString& failure)
{
  stale = 0;
  if (events.size () != passes.size ())
    {
      failure = QString {"%1 fox_callers lines, %2 passes in the model"}.arg (events.size ()).arg (passes.size ());
      return false;
    }
  QStringList previous;
  for (int i = 0; i < events.size (); ++i)
    {
      auto sent = foxLines (events[i]);
      auto file = passes[i].lines;
      if (events[i].value ("time").toString () != passes[i].utc || events[i].value ("pass").toInt () != passes[i].pass)
        {
          failure = QString {"line %1 is pass %2 of %3, the model's pass %4 of %5"}.arg (i + 1)
            .arg (events[i].value ("pass").toInt ()).arg (events[i].value ("time").toString ())
            .arg (passes[i].pass).arg (passes[i].utc);
          return false;
        }
      if (anyOrder)
        {
          sent.sort ();
          file.sort ();
        }
      if (sent.isEmpty () && !file.isEmpty () && file == previous) ++stale;
      else if (sent != file)
        {
          failure = QString {"pass %1 of %2\nline:\n"}.arg (passes[i].pass).arg (passes[i].utc) + show (sent)
            + "\nhoundcallers.txt:\n" + show (file);
          return false;
        }
      previous = file;
    }
  return true;
}

DecodeEventLine::Decode decodeOf (QJsonObject const& event)
{
  DecodeEventLine::Decode decode;
  decode.mode = event.value ("mode").toString ().toStdString ();
  decode.time = event.value ("time").toString ().toStdString ();
  decode.snr = event.value ("snr").toInt ();
  decode.dt = static_cast<float> (event.value ("dt").toDouble ());
  decode.freq = event.value ("freq").toInt ();
  decode.message = event.value ("message").toString ().toStdString ();
  decode.ap = event.value ("ap").toInt ();
  decode.sync = event.value ("sync").toString ().toStdString ();
  decode.flags = event.value ("flags").toString ().toStdString ();
  decode.q = event.value ("q").toInt (-1);
  decode.avg = event.value ("avg").toInt ();
  decode.copied = event.value ("copied").toBool ();
  decode.period = event.value ("period").toInt ();
  decode.hasSpread = event.contains ("spread");
  decode.spread = static_cast<float> (event.value ("spread").toDouble ());
  decode.textWidth = event.value ("text_width").toInt (-1);
  decode.pass = event.value ("pass").toInt ();
  return decode;
}

int passLines (QByteArray const& err)
{
  return err.count ("wsjt-codec: pass nzhsym=");
}

// The four rows' settings: jt9's options and the codec's first configure.
struct Settings
{
  QString event;
  QString set;
  QStringList jt9;
  QJsonObject keys;
  double seconds;
};

Settings settingsOf (QString const& row)
{
  QJsonObject const fox {{"mode", "FT8"}, {"contest_type", "fox"}, {"mycall", "K1ABC"}, {"mygrid", "FN42"},
                         {"ntol", 20}};
  QStringList const foxOptions {"-8", "-X", "6", "-c", "K1ABC", "-G", "FN42"};
  if (row == "fox") return {"fox_callers", "fox", foxOptions, fox, 15};
  if (row == "fox mtd")
    return {"fox_callers", "fox", foxOptions + QStringList {"-M", "-N", "2", "-D", "3"},
            with (fox, {{"multithreaded_ft8", true}, {"ft8_threads", 2}, {"ft8_decoder_start", 3}}), 15};
  if (row == "jt65 averages")
    return {"averages", "jt65", {"--jt65", "-b", "B", "-f", "1700", "-d", "51"},
            {{"mode", "JT65"}, {"submode", 1}, {"rxfreq", 1700}, {"ntol", 20}, {"depth", 51}}, 60};
  if (row == "jt4 averages")
    return {"averages", "jt4", {"--jt4", "-b", "A", "-f", "1000", "-d", "51"},
            {{"mode", "JT4"}, {"submode", 0}, {"rxfreq", 1000}, {"ntol", 20}, {"depth", 51}}, 60};
  if (row == "q65 sync averaged")
    return {"q65_sync", "q65avg", {"--q65", "-b", "B", "-p", "60", "-f", "1000", "-d", "19"},
            {{"mode", "Q65"}, {"trperiod", 60}, {"submode", 1}, {"rxfreq", 1000}, {"depth", 19},
             {"decode_after_eme_delay", true}}, 60};
  // A narrow window, so that the averaged curve's least value lies outside
  // the bins written: red.dat's first line takes it over all the bins.
  if (row == "q65 sync averaged narrow")
    return {"q65_sync", "q65avg", {"--q65", "-b", "B", "-p", "60", "-f", "1000", "-d", "19", "-L", "900", "-H", "1100"},
            {{"mode", "Q65"}, {"trperiod", 60}, {"submode", 1}, {"rxfreq", 1000}, {"depth", 19},
             {"decode_after_eme_delay", true}, {"nfa", 900}, {"nfb", 1100}}, 60};
  // NA VHF: Q65 contest 1, as jt9's -X 1.
  return {"q65_callers", "q65contest", {"-3", "-p", "30", "-X", "1", "-c", "K1ABC", "-f", "1500", "-d", "3"},
          {{"mode", "Q65"}, {"trperiod", 30}, {"rxfreq", 1500}, {"depth", 3}, {"mycall", "K1ABC"},
           {"contest_type", "na_vhf"}}, 30};
}
}

class TestStreamingSide final : public QObject
{
  Q_OBJECT

private slots:
  // (q) Each event equals, field for field, the side file jt9 writes for the
  // same audio, run on the periods so far in a fresh directory; for FT8, the
  // period's last pass's event. Every attempt sends one, after its decodes
  // and before its period's decode_finished.
  void eventsEqualJt9sSideFiles_data ()
  {
    QTest::addColumn<QString> ("row");
    for (auto const row : {"fox", "fox mtd", "jt65 averages", "jt4 averages", "q65 sync averaged",
                           "q65 sync averaged narrow", "q65 sync and callers"})
      QTest::newRow (row) << QString {row};
  }

  void eventsEqualJt9sSideFiles ()
  {
    QFETCH (QString, row);
    auto const s = settingsOf (row);
    QString reason;
    auto const periods = periodsOf (s.set, reason);
    if (periods.isEmpty ()) QSKIP (qPrintable (reason));
    QTemporaryDir codecDir;
    QVERIFY (codecDir.isValid ());
    auto const codec = run (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), paths (codecDir.path ()),
                            sessionInput (periods, s.keys, s.seconds), codecDir.path ());
    QVERIFY2 (codec.ok, codec.err.constData ());
    auto const events = eventsOf (codec.out);
    auto const split = byPeriod (events);
    QCOMPARE (split.size (), periods.size ());
    QStringList const types = s.event == "q65_callers" ? QStringList {"q65_callers", "q65_sync"} : QStringList {s.event};
    // One event a decode attempt: FT8's passes, one a period otherwise.
    for (auto const& type : types)
      QVERIFY2 (ofType (events, type).size () == (s.event == "fox_callers" ? passLines (codec.err) : periods.size ()),
                qPrintable (QString {"%1 %2 events, %3 passes\n"}.arg (ofType (events, type).size ()).arg (type)
                            .arg (passLines (codec.err)) + codec.out));
    int stale = 0;
    QStringList previousFile;
    for (int n = 0; n < periods.size (); ++n)
      {
        // Each side event follows its pass's decodes and none of its pass come after it.
        auto const& lines = split[n];
        for (int i = 0; i < lines.size (); ++i)
          if (types.contains (lines[i].value ("t").toString ()))
            for (int j = i + 1; j < lines.size (); ++j)
              QVERIFY2 (!(lines[j].value ("t").toString () == "decode"
                          && lines[j].value ("pass").toInt () == lines[i].value ("pass").toInt ()),
                        qPrintable (QJsonDocument {lines[j]}.toJson ()));
        QTemporaryDir jt9Dir;
        QVERIFY (jt9Dir.isValid ());
        auto const jt9 = jt9On (periods, n + 1, s.jt9, jt9Dir);
        QVERIFY2 (jt9.ok, jt9.err.constData ());
        for (auto const& type : types)
          {
            auto const ofPeriod = ofType (lines, type);
            QVERIFY2 (!ofPeriod.isEmpty (), qPrintable (QString {"period %1: no %2 event\n"}.arg (n + 1).arg (type)
                                                        + codec.out));
            auto const event = ofPeriod.last ();
            QCOMPARE (event.value ("time").toString (), QString {periods[n].utc}.remove (':'));
            // Each carries its pass, as the pass's decodes do: one pass a
            // period but in FT8, whose passes each send one.
            for (auto const& each : ofPeriod) QVERIFY2 (each.value ("pass").toInt () > 0, qPrintable (codec.out));
            for (auto const& decode : ofType (lines, "decode"))
              if (type != "fox_callers")
                QCOMPARE (decode.value ("pass").toInt (), event.value ("pass").toInt ());
            if (type == "fox_callers")
              {
                auto file = fileLines (jt9Dir.filePath ("houndcallers.txt"));
                auto sent = foxLines (event);
                // The multithreaded decoder's threads report in any order.
                if (row == "fox mtd")
                  {
                    file.sort ();
                    sent.sort ();
                  }
                // An attempt that ages out every caller writes no record and
                // leaves the last list in the file, while its line has none
                // (doc/codec/interface.md, "Side outputs and labels").
                if (sent.isEmpty () && !file.isEmpty () && file == previousFile) ++stale;
                else QVERIFY2 (sent == file, qPrintable (QString {"period %1\nevent:\n"}.arg (n + 1) + show (sent)
                                                       + "\nhoundcallers.txt:\n" + show (file)));
                previousFile = file;
              }
            else if (type == "averages")
              {
                QCOMPARE (event.value ("mode").toString (), s.keys.value ("mode").toString ());
                auto const file = fileLines (jt9Dir.filePath ("avemsg.txt"));
                QVERIFY2 (averageLines (event) == file, qPrintable (QString {"period %1\nevent:\n"}.arg (n + 1)
                                                                    + show (averageLines (event))
                                                                    + "\navemsg.txt:\n" + show (file)));
              }
            else if (type == "q65_sync")
              {
                auto const file = fileLines (jt9Dir.filePath ("red.dat"));
                auto const sent = syncLines (event);
                int differs = 0;
                while (differs < std::min (sent.size (), file.size ()) && sent[differs] == file[differs]) ++differs;
                QVERIFY2 (sent == file, qPrintable (QString {"period %1: %2 lines sent, %3 in red.dat; line %4:\n%5\n%6"}
                                                    .arg (n + 1).arg (sent.size ()).arg (file.size ()).arg (differs + 1)
                                                    .arg (sent.value (differs), file.value (differs))));
              }
            else
              {
                bool ok;
                auto const file = tsil (jt9Dir.path (), ok, false);
                QVERIFY2 (ok, qPrintable (file.join ('\n')));
                QVERIFY2 (callerLines (event, false) == file,
                          qPrintable (show (callerLines (event, false)) + "\ntsil.3q:\n" + show (file)));
                QVERIFY (timesAgree (callerLines (event), tsil (jt9Dir.path (), ok), 600));
              }
          }
      }
    // Every pass's line against the file after that pass (fox_gui_model).
    if (s.event == "fox_callers")
      {
        QList<ModelPass> passes;
        QString failure;
        QVERIFY2 (foxModel (periods, row == "fox mtd", {}, passes, failure), qPrintable (failure));
        int stalePasses = 0;
        QVERIFY2 (compareFoxPasses (ofType (events, "fox_callers"), passes, row == "fox mtd", stalePasses, failure),
                  qPrintable (failure));
        // The third period ages out every caller: at its first pass, and
        // with the multithreaded decoder's one pass, at the period's end too.
        QCOMPARE (stalePasses, 1);
      }
    // Each row shows what it is for.
    auto const last = ofType (events, s.event).last ();
    if (row == "fox mtd") QCOMPARE (stale, 1);
    else if (s.event == "fox_callers") QCOMPARE (stale, 0);
    if (s.event == "fox_callers")
      {
        QStringList calls;
        for (auto const& event : ofType (events, "fox_callers")) calls += foxLines (event);
        QVERIFY2 (calls.join ('\n').contains ("G4XYZ") && calls.join ('\n').contains ("W9XYZ"), qPrintable (codec.out));
      }
    if (s.event == "averages") QVERIFY2 (last.value ("entries").toArray ().size () > 1, qPrintable (codec.out));
    if (s.event == "q65_sync")
      QVERIFY2 (last.value ("averaged_max").toDouble () != 0.0, "no averaged curve to compare");
    if (s.event == "q65_callers")
      {
        QCOMPARE (last.value ("callers").toArray ().size (), 3);
        // The event equals the codec's own file to the second.
        bool ok;
        QCOMPARE (callerLines (last), tsil (codecDir.path (), ok));
      }
  }

  // (s) The GUI removes a caller (rm_q3list) or erases the list between
  // decodes; the codec's keys do the same at the configure's place in the
  // stream, so the next attempt's q65_callers and tsil.3q are the GUI's.
  void q65CallerEditsRoundTrip_data ()
  {
    QTest::addColumn<QJsonObject> ("edit");
    QTest::addColumn<QStringList> ("removed");   // the GUI's rm_q3list calls; "*" erases
    QTest::addColumn<QStringList> ("expected");  // the second period's calls
    QTest::newRow ("remove") << QJsonObject {{"q65_callers_remove", QJsonArray {"W9XYZ"}}} << QStringList {"W9XYZ"}
                             << QStringList {"K9AN", "G4XYZ"};
    QTest::newRow ("remove two") << QJsonObject {{"q65_callers_remove", QJsonArray {"K9AN", "W9XYZ"}}}
                                 << QStringList {"K9AN", "W9XYZ"} << QStringList {"G4XYZ"};
    QTest::newRow ("remove one not listed") << QJsonObject {{"q65_callers_remove", QJsonArray {"K1JT", "K9AN"}}}
                                            << QStringList {"K1JT", "K9AN"} << QStringList {"W9XYZ", "G4XYZ"};
    QTest::newRow ("clear") << QJsonObject {{"q65_callers_clear", true}} << QStringList {"*"}
                            << QStringList {"G4XYZ"};
    QTest::newRow ("clear false") << QJsonObject {{"q65_callers_clear", false}} << QStringList {}
                                  << QStringList {"W9XYZ", "K9AN", "G4XYZ"};
  }

  void q65CallerEditsRoundTrip ()
  {
    QFETCH (QJsonObject, edit);
    QFETCH (QStringList, removed);
    QFETCH (QStringList, expected);
    auto const s = settingsOf ("q65 sync and callers");
    QString reason;
    auto periods = periodsOf (s.set, reason);
    if (periods.isEmpty ()) QSKIP (qPrintable (reason));
    // The edit comes in a frame of its own before the second period's.
    QTemporaryDir codecDir;
    QVERIFY (codecDir.isValid ());
    auto input = sessionHeader ();
    input += configure (with (with (s.keys, {{"live_audio", false}}), {{"utc", periods[0].utc}}))
      + audio (periods[0].pcm) + configure (edit) + configure ({{"utc", periods[1].utc}}) + audio (periods[1].pcm);
    auto const codec = run (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), paths (codecDir.path ()), input,
                            codecDir.path ());
    QVERIFY2 (codec.ok, codec.err.constData ());
    QVERIFY2 (ofType (eventsOf (codec.out), "error").isEmpty (), codec.out.constData ());
    auto const lists = ofType (eventsOf (codec.out), "q65_callers");
    QCOMPARE (lists.size (), 2);
    QStringList first, second;
    for (auto const& caller : lists[0].value ("callers").toArray ()) first << caller.toObject ().value ("call").toString ();
    for (auto const& caller : lists[1].value ("callers").toArray ()) second << caller.toObject ().value ("call").toString ();
    QCOMPARE (first, (QStringList {"W9XYZ", "K9AN"}));
    QCOMPARE (second, expected);
    bool ok;
    QCOMPARE (callerLines (lists[1]), tsil (codecDir.path (), ok));

    // The GUI's path: the worker decodes each period, and the GUI edits
    // tsil.3q in between with its own routine.
    QTemporaryDir guiDir;
    QVERIFY (guiDir.isValid ());
    QList<Period> one {periods[0]};
    auto const jt9First = jt9On (one, 1, s.jt9, guiDir);
    QVERIFY2 (jt9First.ok, jt9First.err.constData ());
    QVERIFY (QFile::remove (guiDir.filePath (periods[0].name)));
    for (auto const& call : removed)
      if (call == "*") QVERIFY (QFile::remove (guiDir.filePath ("tsil.3q")));
      else QVERIFY (run (QString::fromUtf8 (SIDE_GUI_MODEL), {"remove", guiDir.filePath ("tsil.3q"), call}, {},
                         guiDir.path ()).ok);
    QList<Period> two {periods[1]};
    auto const jt9Second = jt9On (two, 1, s.jt9, guiDir);
    QVERIFY2 (jt9Second.ok, jt9Second.err.constData ());
    auto const gui = tsil (guiDir.path (), ok, false);
    QVERIFY (ok);
    QVERIFY2 (callerLines (lists[1], false) == gui, qPrintable (show (callerLines (lists[1], false)) + "\nGUI:\n"
                                                                 + show (gui)));
  }

  // The GUI's Fox reset (its Reset button and a band change) deletes
  // houndcallers.txt between decodes, and the decoder then starts a new
  // caller list. fox_callers_clear does the same at the configure's place in
  // the stream: every pass's fox_callers is the GUI's, from fox_gui_model,
  // jt9's WAV path decoding the periods in one process with the file deleted
  // between them.
  void foxCallersClearRoundTrip_data ()
  {
    QTest::addColumn<bool> ("clear");
    QTest::newRow ("clear") << true;
    QTest::newRow ("clear false") << false;
  }

  void foxCallersClearRoundTrip ()
  {
    QFETCH (bool, clear);
    auto const s = settingsOf ("fox");
    QString reason;
    auto periods = periodsOf (s.set, reason);
    if (periods.isEmpty ()) QSKIP (qPrintable (reason));
    // Two callers, then a third 15 s later, while the two are still listed.
    periods = {periods[0], periods[1]};
    periods[1].utc = "00:00:15";
    QTemporaryDir codecDir;
    QVERIFY (codecDir.isValid ());
    auto input = sessionHeader ();
    input += configure (with (with (s.keys, {{"live_audio", false}}), {{"utc", periods[0].utc}}))
      + audio (periods[0].pcm) + configure ({{"fox_callers_clear", clear}})
      + configure ({{"utc", periods[1].utc}}) + audio (periods[1].pcm);
    auto const codec = run (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), paths (codecDir.path ()), input,
                            codecDir.path ());
    QVERIFY2 (codec.ok, codec.err.constData ());
    QVERIFY2 (ofType (eventsOf (codec.out), "error").isEmpty (), codec.out.constData ());
    auto const split = byPeriod (eventsOf (codec.out));
    QCOMPARE (split.size (), 2);
    QStringList sent[2];
    for (int n = 0; n < 2; ++n)
      {
        auto const lists = ofType (split[n], "fox_callers");
        QVERIFY2 (!lists.isEmpty (), qPrintable (QString {"period %1: no fox_callers\n"}.arg (n + 1) + codec.out));
        sent[n] = foxLines (lists.last ());
      }
    // The GUI's path, every pass: the file deleted between the periods.
    QList<ModelPass> passes;
    QString failure;
    QVERIFY2 (foxModel (periods, false, clear ? QSet<int> {1} : QSet<int> {}, passes, failure), qPrintable (failure));
    int stale = 0;
    QVERIFY2 (compareFoxPasses (ofType (eventsOf (codec.out), "fox_callers"), passes, false, stale, failure),
              qPrintable (failure));
    QCOMPARE (stale, 0);
    // The clear empties the list the second period adds to; without it the
    // first period's callers stay.
    QCOMPARE (sent[0].size (), 2);
    QCOMPARE (sent[1].size (), clear ? 1 : 3);
  }

  // The edit keys decline values of the wrong type, and more calls than the
  // list holds, as other keys do: one error, and the frame changes nothing.
  void editKeysDeclineBadValues_data ()
  {
    QTest::addColumn<QJsonObject> ("keys");
    QTest::addColumn<QString> ("code");
    QTest::addColumn<QString> ("key");
    QTest::newRow ("remove string") << QJsonObject {{"q65_callers_remove", "W9XYZ"}} << "configure_type_error"
                                    << "q65_callers_remove";
    QTest::newRow ("remove numbers") << QJsonObject {{"q65_callers_remove", QJsonArray {1, 2}}}
                                     << "configure_type_error" << "q65_callers_remove";
    QJsonArray many;
    for (int n = 0; n < 51; ++n) many << QString {"K%1AB"}.arg (n);
    QTest::newRow ("remove 51") << QJsonObject {{"q65_callers_remove", many}} << "configure_range_error"
                                << "q65_callers_remove";
    QTest::newRow ("clear string") << QJsonObject {{"q65_callers_clear", "true"}} << "configure_type_error"
                                   << "q65_callers_clear";
    QTest::newRow ("fox clear string") << QJsonObject {{"fox_callers_clear", "true"}} << "configure_type_error"
                                       << "fox_callers_clear";
  }

  void editKeysDeclineBadValues ()
  {
    QFETCH (QJsonObject, keys);
    QFETCH (QString, code);
    QFETCH (QString, key);
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    // A declined frame changes nothing: its mode stays unapplied too, as the
    // next frame's refusal shows (fast is JT9's: its error names the mode the
    // stream is in, FT8 at the start).
    auto const codec = run (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), paths (directory.path ()),
                            sessionHeader () + configure (with (keys, {{"mode", "JT4"}})) + configure ({{"fast", true}}),
                            directory.path ());
    QVERIFY2 (codec.ok, codec.err.constData ());
    auto const errors = ofType (eventsOf (codec.out), "error");
    QVERIFY2 (errors.size () == 2, codec.out.constData ());
    QCOMPARE (errors[0].value ("code").toString (), code);
    QCOMPARE (errors[0].value ("key").toString (), key);
    QCOMPARE (errors[1].value ("key").toString (), QString {"fast"});
    QCOMPARE (errors[1].value ("mode").toString (), QString {"FT8"});
  }

  // (t) Each decode carries the submode its pass decoded with, in JT4, JT9
  // (fast JT9 too), JT65 and Q65, whatever the last period's was; MSK144's
  // carry the configured period; no other mode carries submode.
  void decodesCarryTheirLabels_data ()
  {
    QTest::addColumn<QString> ("row");
    for (auto const row : {"jt4 A then F", "q65 A then D", "jt65 B", "jt9 A then fast E then fast H",
                           "msk144 15 then 30", "jt65jt9 JT65 lines only", "ft8 none"})
      QTest::newRow (row) << QString {row};
  }

  void decodesCarryTheirLabels ()
  {
    QFETCH (QString, row);
    if (samples ().isEmpty ()) QSKIP ("no samples/ in this tree");
    struct Step
    {
      QByteArray pcm;
      QJsonObject keys;
      QString submode;   // expected on each of its decodes ("": none)
      int period;        // expected "period" (0: none)
      QString needs {};  // a mode that must decode in it ("": any)
    };
    QList<Step> steps;
    auto const settings = QJsonObject {{"live_audio", false}};
    if (row == "jt4 A then F")
      steps << Step {cut (sample ("JT4/JT4A/DF2ZC_070926_040700.WAV"), 60),
                     with (settings, {{"mode", "JT4"}, {"submode", 0}, {"depth", 3}, {"ntol", 20}, {"utc", "04:07:00"}}),
                     "A", 0}
            << Step {cut (sample ("JT4/JT4F/OK1KIR_141105_175700.WAV"), 60),
                     {{"submode", "F"}, {"rxfreq", 1200}, {"utc", "17:57:00"}}, "F", 0};
    else if (row == "q65 A then D")
      steps << Step {cut (sample ("Q65/60A_EME_6m/210106_1621.wav"), 60),
                     with (settings, {{"mode", "Q65"}, {"trperiod", 60}, {"submode", 0}, {"rxfreq", 1000}, {"depth", 3},
                                      {"decode_after_eme_delay", true}, {"utc", "16:21:00"}}), "A", 60}
            << Step {cut (sample ("Q65/60D_EME_10GHz/201212_1838.wav"), 60), {{"submode", 3}, {"utc", "18:38:00"}},
                     "D", 60};
    else if (row == "jt65 B")
      steps << Step {cut (sample ("JT65/JT65B/000000_0004.wav"), 60),
                     with (settings, {{"mode", "JT65"}, {"submode", 1}, {"rxfreq", 1700}, {"ntol", 20}, {"depth", 3},
                                      {"utc", "00:04:00"}}), "B", 0};
    else if (row == "jt9 A then fast E then fast H")
      {
        steps << Step {cut (sample ("JT9/130418_1742.wav"), 60),
                       with (settings, {{"mode", "JT9"}, {"ntol", 20}, {"utc", "17:42:00"}}), "A", 0};
        for (auto const submode : {"E", "H"})
          {
            // The codec's own fast JT9 render in noise, as test_streaming_fast_jt9's rows.
            QTemporaryDir directory;
            QJsonObject request {{"t", "render"}, {"id", 1}, {"mode", "JT9"}, {"text", "CQ K1ABC FN42"},
                                 {"submode", submode}, {"freq", 1200}, {"rate", SampleRate}, {"trperiod", 15},
                                 {"fast", true}};
            auto const rendered = run (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), paths (directory.path ()),
                                       sessionHeader () + frame (0x02u, QJsonDocument {request}.toJson (QJsonDocument::Compact)),
                                       directory.path ());
            QVERIFY2 (rendered.ok, rendered.err.constData ());
            QByteArray pcm;
            for (auto const& event : ofType (eventsOf (rendered.out), "pcm"))
              pcm += QByteArray::fromBase64 (event.value ("data").toString ().toLatin1 ());
            QVERIFY (!pcm.isEmpty ());
            steps << Step {noisy (cut (pcm, 15), 0.2, 1500.0, 15u + unsigned (submode[0])),
                           {{"trperiod", 15}, {"fast", true}, {"submode", submode}, {"rxfreq", 1200}, {"ntol", 100},
                            {"utc", QString {"17:43:%1"}.arg (submode[0] == 'E' ? "00" : "15")}}, submode, 15};
          }
      }
    else if (row == "msk144 15 then 30")
      {
        auto const pcm = sample ("MSK144/181211_120500.wav");
        steps << Step {cut (pcm, 15), with (settings, {{"mode", "MSK144"}, {"trperiod", 15}, {"rxfreq", 1500},
                                                       {"ntol", 50}, {"depth", 3}, {"utc", "12:05:00"}}), "", 15}
              << Step {cut (pcm, 30), {{"trperiod", 30}, {"utc", "12:05:30"}}, "", 30};
      }
    // JT65+JT9's JT65 decoder decodes with the submode (lib/decoder.f90, mode65
    // in lib/jt65_decode.f90), its JT9 decoder without one: JT65 lines carry
    // it, JT9 lines do not (checked by mode below).
    else if (row == "jt65jt9 JT65 lines only")
      steps << Step {cut (sample ("JT65/JT65B/000000_0004.wav"), 60),
                     with (settings, {{"mode", "JT65+JT9"}, {"tx_mode", "JT9"}, {"submode", 1}, {"rxfreq", 1700},
                                      {"depth", 3}, {"utc", "00:04:00"}}), "B", 0, "JT65"}
            << Step {cut (sample ("JT9/130418_1742.wav"), 60),
                     {{"jt65_jt9_split_hz", 1000}, {"utc", "17:42:00"}}, "B", 0, "JT9"};
    else
      steps << Step {cut (sample ("FT8/210703_133430.wav"), 15),
                     with (settings, {{"mode", "FT8"}, {"depth", 3}, {"utc", "13:34:30"}}), "", 0};
    auto input = sessionHeader ();
    for (auto const& step : steps)
      {
        QVERIFY2 (!step.pcm.isEmpty (), "missing audio");
        input += configure (step.keys) + audio (step.pcm);
      }
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    auto const codec = run (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), paths (directory.path ()), input,
                            directory.path ());
    QVERIFY2 (codec.ok, codec.err.constData ());
    auto const events = eventsOf (codec.out);
    // MSK144's periods have no decode_finished between them; their labels tell them apart.
    int at = 0;
    for (int n = 0; n < steps.size (); ++n)
      {
        int decodes = 0, needed = 0;
        auto const label = steps[n].keys.value ("utc").toString ().remove (':');
        for (; at < events.size (); ++at)
          {
            auto const& event = events[at];
            if (event.value ("t").toString () == "decode_finished" && row != "msk144 15 then 30") {++at; break;}
            if (event.value ("t").toString () != "decode") continue;
            if (row == "msk144 15 then 30" && event.value ("time").toString () != label) break;
            ++decodes;
            if (event.value ("mode").toString () == steps[n].needs) ++needed;
            auto const line = QJsonDocument {event}.toJson (QJsonDocument::Compact);
            auto const submode = row.startsWith ("jt65jt9") && event.value ("mode").toString () == "JT9"
              ? QString {} : steps[n].submode;
            if (submode.isEmpty ()) QVERIFY2 (!event.contains ("submode"), line.constData ());
            else QVERIFY2 (event.value ("submode").toString () == submode, line.constData ());
            if (steps[n].period) QVERIFY2 (event.value ("period").toInt () == steps[n].period, line.constData ());
            else QVERIFY2 (!event.contains ("period"), line.constData ());
          }
        QVERIFY2 (decodes > 0, qPrintable (QString {"step %1 has no decode\n"}.arg (n + 1) + codec.out));
        if (!steps[n].needs.isEmpty ())
          QVERIFY2 (needed > 0, qPrintable (QString {"step %1 has no %2 decode\n"}.arg (n + 1).arg (steps[n].needs)
                                            + codec.out));
      }
  }

  // Plan §9: a client that ignores line types it does not know still works:
  // with each new event in the output, every line is one JSON object with
  // v 1 and a t, under a 64 KiB line limit, and the lines such a client
  // reads are jt9's for the same audio.
  void clientsIgnoringTheSideEventsStillWork_data ()
  {
    QTest::addColumn<QString> ("row");
    QTest::newRow ("fox_callers") << QString {"fox"};
    QTest::newRow ("averages") << QString {"jt65 averages"};
    QTest::newRow ("q65_sync") << QString {"q65 sync averaged"};
    QTest::newRow ("q65_callers") << QString {"q65 sync and callers"};
  }

  void clientsIgnoringTheSideEventsStillWork ()
  {
    QFETCH (QString, row);
    auto const s = settingsOf (row);
    QString reason;
    auto const periods = periodsOf (s.set, reason);
    if (periods.isEmpty ()) QSKIP (qPrintable (reason));
    QTemporaryDir codecDir, jt9Dir;
    QVERIFY (codecDir.isValid () && jt9Dir.isValid ());
    auto const codec = run (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), paths (codecDir.path ()),
                            sessionInput (periods, s.keys, s.seconds), codecDir.path ());
    QVERIFY2 (codec.ok, codec.err.constData ());
    auto const jt9 = jt9On (periods, periods.size (), s.jt9, jt9Dir);
    QVERIFY2 (jt9.ok, jt9.err.constData ());
    QStringList known, expected;
    int side = 0;
    for (auto const& line : codec.out.split ('\n'))
      {
        if (line.isEmpty ()) continue;
        QVERIFY2 (line.size () < LineLimit, qPrintable (QString::number (line.size ())));
        QJsonParseError error;
        auto const event = QJsonDocument::fromJson (line, &error).object ();
        QVERIFY2 (error.error == QJsonParseError::NoError && event.value ("v").toInt () == 1
                  && event.value ("t").isString (), line.constData ());
        auto const type = event.value ("t").toString ();
        if (type == s.event) ++side;
        if (type == "decode") known << QString::fromStdString (DecodeEventLine::line (decodeOf (event)));
        else if (type == "decode_finished")
          known << QString::fromStdString (DecodeEventLine::line (DecodeEventLine::Finished {
                event.value ("synced").toInt (), event.value ("decoded").toInt (), event.value ("average").toInt ()}));
      }
    for (auto const& line : jt9.out.split ('\n'))
      if (!line.isEmpty ()) expected << QString::fromLatin1 (line);
    QVERIFY2 (side > 0, codec.out.constData ());
    QVERIFY2 (known == expected, qPrintable ("client:\n" + show (known) + "\njt9:\n" + show (expected)));
  }
};

QTEST_GUILESS_MAIN (TestStreamingSide)
#include "test_streaming_side.moc"
