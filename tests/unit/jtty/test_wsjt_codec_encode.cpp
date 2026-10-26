// SPDX-License-Identifier: GPL-3.0-or-later
// JTTY encode in wsjt-codec's stream session, end to end: requests on stdin,
// every stdout line parsed strictly, as lib/streaming_jtty.f90 states them.
// Expected encodings are the GUI's own (widgets/JttyTransmitText.hpp,
// widgets/JttyMessages.hpp).

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

#include <cmath>

#include "widgets/JttyMessages.hpp"
#include "widgets/JttyTransmitText.hpp"

namespace
{
constexpr int Nsps = 384;
constexpr int FrameSymbols = 59;
constexpr int FrameSamples = FrameSymbols * Nsps;    // one frame at 12 kHz
constexpr int PcmLineSamples = 16384;
constexpr int LeadSamples = 6000;                   // silence before a transmission sent back
constexpr int ProcessTimeoutMs = 120000;

using Profile = Jtty::NativeExchangeProfile;

// 200 characters: mixed case, a doubled space and an em dash, which becomes
// '#'; cut 78 + 77 + 45.
QString const longLine = QString {"Thanks for the QSO, Bob.  Rig here is a home-brew transceiver running 5 watts "
                                  "into a dipole at 30 feet; the weather is clear and warm, about 25 C. "
                                  "Hope to work you again on JTTY soon "}
  + QChar (0x2014) + " 73 and good DX!";

QStringList const longLineCanonicals {
  "THANKS FOR THE QSO, BOB. RIG HERE IS A HOME-BREW TRANSCEIVER RUNNING 5 WATTS",
  "INTO A DIPOLE AT 30 FEET; THE WEATHER IS CLEAR AND WARM, ABOUT 25 C. HOPE TO",
  "WORK YOU AGAIN ON JTTY SOON # 73 AND GOOD DX!"};

// The native forms and control phrases the stream contract publishes.
QStringList const nativeForms {
  "CQ %M CQ", "%H %E", "%H 599 %N", "%H %G", "%H TU CQ %M CQ", "%M", "%H", "TU NOW %Q %E",
  "TU NOW %Q 599 %N", "TU NOW %Q %G", "%H AGN?", "%E", "599 %N", "%G", "599 %G"};
QStringList const controlPhrases {
  "AGN?", "CALL?", "AGN CALL", "NR?", "AGN NR", "EXCH?", "STATE?", "SECTION?", "ZONE?", "GRID?", "RPRT?",
  "QSL TU", "TU", "QRZ?", "QSO B4", "WAIT", "NIL?", "OK?"};

QByteArray const Station {R"({"t":"configure","mycall":"K1ABC","mygrid":"FN42"})"};

// --- input ------------------------------------------------------------------

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

QByteArray request (QJsonObject const& object)
{
  return control (QJsonDocument {object}.toJson (QJsonDocument::Compact));
}

QJsonObject with (QJsonObject object, QString const& key, QJsonValue const& value)
{
  object.insert (key, value);
  return object;
}

QJsonObject pack (qint64 id, QString const& key, QString const& value, QJsonObject members = {})
{
  members.insert ("t", "pack");
  members.insert ("id", id);
  members.insert (key, value);
  return members;
}

QJsonObject text (qint64 id, QString const& value, QJsonObject const& members = {})
{
  return pack (id, "text", value, members);
}

QJsonObject key (qint64 id, QString const& value, QJsonObject const& members = {})
{
  return pack (id, "template", value, members);
}

QJsonObject render (QJsonObject request, int rate, double freq)
{
  request.insert ("t", "render");
  request.insert ("rate", rate);
  request.insert ("freq", freq);
  return request;
}

// A JSON string escape of one UTF-16 unit, as a request may spell a character.
QByteArray escape (ushort unit)
{
  return QByteArray {"\\u"} + QByteArray::number (unit, 16).rightJustified (4, '0');
}

// pcm as audio frames of 4096 samples.
QByteArray audio (QByteArray const& pcm)
{
  QByteArray bytes;
  for (int offset = 0; offset < pcm.size (); offset += 2 * 4096) bytes += frame (0x01u, pcm.mid (offset, 2 * 4096));
  return bytes;
}

QByteArray silence (int samples)
{
  return audio (QByteArray (2 * samples, '\0'));
}

QByteArray wavData (QString const& path)
{
  QFile file {path};
  if (!file.open (QIODevice::ReadOnly)) return {};
  auto const bytes = file.readAll ();
  for (int offset = 12; offset + 8 <= bytes.size ();)
    {
      auto const size = qFromLittleEndian<quint32> (reinterpret_cast<uchar const *> (bytes.constData () + offset + 4));
      if (bytes.mid (offset, 4) == "data") return bytes.mid (offset + 8, int (size));
      offset += 8 + int (size);
    }
  return {};
}

// --- the GUI's encoding -----------------------------------------------------

Jtty::NativeMacroContext gui (QString const& hisCall = "W9XYZ", int serial = 107)
{
  return Jtty::nativeMacroContext ("K1ABC", hisCall, serial, "FN42", -7, Profile::None, {});
}

// The tones a function key sends: a native form's atoms, else its text.
QVector<int> keyTones (QString const& form, Jtty::NativeMacroContext const& context)
{
  auto const compiled = Jtty::compileNativeMacro (form, context);
  if (compiled.isNative ()) return Jtty::encodeNativeAtoms (compiled.atoms).tones;
  return Jtty::encodeTransmitText (compiled.text, Profile::None, true).segments.value (0).transmit.tones;
}

QVector<int> textTones (QString const& message)
{
  return Jtty::encodeTransmitText (message, Profile::None, true).segments.value (0).transmit.tones;
}

QByteArray guiAudio (QVector<int> const& tones, int rate, double freq)
{
  auto const samples = Jtty::renderTransmitTones (tones.constData (), tones.size (), rate, float (freq));
  QByteArray bytes (2 * samples.size (), '\0');
  for (int i = 0; i < samples.size (); ++i)
    qToLittleEndian<qint16> (samples[i], reinterpret_cast<uchar *> (bytes.data ()) + 2 * i);
  return bytes;
}

// --- output -----------------------------------------------------------------

struct Reply
{
  QList<QJsonObject> segments;
  QList<QByteArray> pcm;          // each segment's audio, decoded
  QJsonObject terminal;           // packed, rendered or rejected
  QList<QByteArray> lines;        // as written
};

struct Run
{
  bool started {};
  bool timedOut {};
  int exitCode {-1};
  QProcess::ExitStatus exitStatus {QProcess::CrashExit};
  QList<QByteArray> lines;        // stdout, without line ends
  QByteArray standardError;
  QList<QJsonObject> events;      // one per line
  QMap<qint64, Reply> replies;
  QString error;                  // the first protocol violation found
};

QList<QByteArray> outputLines (QByteArray const& output)
{
  auto lines = output.split ('\n');
  if (!lines.isEmpty () && lines.last ().isEmpty ()) lines.removeLast ();
  for (auto& line : lines)
    if (line.endsWith ('\r')) line.chop (1);
  return lines;
}

bool isReplyLine (QString const& type)
{
  return type == "segment" || type == "pcm" || type == "packed" || type == "rendered" || type == "rejected";
}

QString segmentProblem (QJsonObject const& segment, int index)
{
  auto keys = segment.keys ();
  keys.removeOne ("samples");
  if (keys != QStringList {"canonical", "final", "frames", "id", "seconds", "seg", "substituted", "t", "text", "v"})
    return "a segment's members are not the documented ones";
  if (segment.value ("seg").toInt (-1) != index) return "segments are not numbered 0, 1, 2 ...";
  if (!segment.value ("text").isString () || !segment.value ("canonical").isString ()
      || !segment.value ("final").isBool () || !segment.value ("substituted").isBool ()
      || !segment.value ("seconds").isDouble () || !segment.value ("frames").isArray ())
    return "a segment member has the wrong JSON type";
  auto const frames = segment.value ("frames").toArray ();
  if (frames.isEmpty () || frames.size () > 16) return "a segment has no frames or more than 16";
  QString joined;
  for (auto const& value : frames)
    {
      auto const object = value.toObject ();
      if (object.keys () != QStringList {"text"} || !object.value ("text").isString ())
        return "a frame is not {\"text\":S}";
      joined += object.value ("text").toString ();
    }
  if (joined.remove (' ') != segment.value ("canonical").toString ().remove (' '))
    return "the frame texts do not spell the canonical text";
  if (std::abs (segment.value ("seconds").toDouble () - frames.size () * FrameSamples / 12000.) > 1e-9)
    return "seconds is not the frames' time on air";
  if (segment.contains ("samples"))
    {
      auto const samples = qint64 (segment.value ("samples").toDouble ());
      if (samples != qint64 (frames.size ()) * FrameSamples && samples != qint64 (frames.size ()) * FrameSamples * 4)
        return "samples is not the frames' audio at 12 or 48 kHz";
    }
  return {};
}

QString errorProblem (QJsonObject const& event)
{
  auto const keys = event.keys ();
  if (event.contains ("msg")) return keys == QStringList {"msg", "t", "v"} ? QString {} : "bad uncoded error";
  if (event.value ("code").toString () == "configure_range_error")
    return keys == QStringList {"code", "detail", "key", "t", "v"}
           || keys == QStringList {"code", "detail", "key", "mode", "t", "v"}
      ? QString {} : "bad configure_range_error";
  return keys == QStringList {"code", "detail", "t", "v"} && event.value ("detail").isString ()
    ? QString {} : "an error is not one of the documented shapes";
}

void checkProtocol (Run& run)
{
  QSet<qint64> answered;
  bool open {false};
  qint64 openId {0};
  qint64 pending {0};             // the open segment's samples not yet sent
  for (int i = 0; i < run.lines.size () && run.error.isEmpty (); ++i)
    {
      auto const& line = run.lines[i];
      QJsonParseError parseError;
      auto const document = QJsonDocument::fromJson (line, &parseError);
      if (parseError.error != QJsonParseError::NoError || !document.isObject ())
        {
          run.error = "invalid JSON line: " + QString::fromUtf8 (line.left (200));
          return;
        }
      auto const event = document.object ();
      run.events.append (event);
      auto const type = event.value ("t").toString ();
      QString problem;
      if (event.value ("v").toInt (-1) != 1 || type.isEmpty ())
        problem = "bad envelope";
      else if (i == 0 && type != "ready")
        problem = "the first line is not the ready line";
      else if (!isReplyLine (type))
        {
          if (open) problem = "a reply is interrupted";
          else if (type == "error") problem = errorProblem (event);
          else if (type != "ready" && type != "decode" && type != "decode_finished" && type != "session"
                   && type != "jtty_update")
            problem = "unknown line type";
        }
      else if (!event.value ("id").isDouble ())
        problem = "a reply line without an id";
      else
        {
          auto const id = qint64 (event.value ("id").toDouble ());
          if (open && id != openId) problem = "replies interleave";
          else if (!open && answered.contains (id)) problem = "a second reply to one request";
          open = true;
          openId = id;
          auto& reply = run.replies[id];
          reply.lines.append (line);
          if (!problem.isEmpty ())
            {
            }
          else if (type == "segment")
            {
              if (pending) problem = "a segment's audio is incomplete";
              else problem = segmentProblem (event, reply.segments.size ());
              reply.segments.append (event);
              reply.pcm.append (QByteArray {});
              pending = qint64 (event.value ("samples").toDouble ());
            }
          else if (type == "pcm")
            {
              auto const data = event.value ("data").toString ().toLatin1 ();
              auto const decoded = QByteArray::fromBase64Encoding (data, QByteArray::AbortOnBase64DecodingErrors);
              int const lines = reply.pcm.isEmpty () ? 0 : int (reply.pcm.last ().size () / (2 * PcmLineSamples));
              if (event.keys () != QStringList {"data", "id", "seg", "seq", "t", "v"})
                problem = "a pcm line's members are not the documented ones";
              else if (reply.segments.isEmpty () || event.value ("seg").toInt (-1) != reply.segments.size () - 1)
                problem = "pcm that does not follow its segment";
              else if (event.value ("seq").toInt (-1) != lines)
                problem = "pcm lines are not numbered 0, 1, 2 ...";
              else if (!decoded || decoded.decoded.toBase64 () != data)
                problem = "pcm data is not canonical base64";
              else if (decoded.decoded.isEmpty () || decoded.decoded.size () % 2
                       || decoded.decoded.size () > 2 * PcmLineSamples)
                problem = "a pcm line holds no whole samples or more than 16384";
              else
                {
                  pending -= decoded.decoded.size () / 2;
                  if (pending < 0 || (pending > 0 && decoded.decoded.size () != 2 * PcmLineSamples))
                    problem = "a segment's pcm lines do not hold its samples";
                  reply.pcm.last () += decoded.decoded;
                }
            }
          else
            {
              if (pending) problem = "a segment's audio is incomplete";
              else if (type == "rejected")
                {
                  auto keys = event.keys ();
                  keys.removeOne ("key");
                  if (!reply.segments.isEmpty ()) problem = "a rejected request has segments";
                  else if (keys != QStringList {"detail", "id", "reason", "t", "v"})
                    problem = "a rejected line's members are not the documented ones";
                  else if (!QStringList {"bad_request", "too_long", "not_configured", "missing", "empty",
                                         "invalid_runtime", "encoding_failed"}.contains (event.value ("reason").toString ())
                           || event.value ("detail").toString ().isEmpty ()
                           || (event.contains ("key") && event.value ("key").toString ().isEmpty ()))
                    problem = "rejected without a documented reason and a detail";
                }
              else
                {
                  bool const rendered = type == "rendered";
                  if (event.keys () != QStringList {"id", "segments", "substituted", "t", "v"}
                      || !event.value ("substituted").isBool ())
                    problem = type + "'s members are not the documented ones";
                  else if (event.value ("segments").toInt (-1) != reply.segments.size () || reply.segments.isEmpty ())
                    problem = type + " counts other than the segments sent";
                  for (auto const& segment : reply.segments)
                    if (segment.contains ("samples") != rendered) problem = "samples in a pack, or none in a render";
                }
              reply.terminal = event;
              answered.insert (id);
              open = false;
            }
        }
      if (!problem.isEmpty ()) run.error = problem + ": " + QString::fromUtf8 (line.left (300));
    }
  if (run.error.isEmpty () && open) run.error = "the output ends inside a reply";
}

QByteArray allPcm (Reply const& reply)
{
  QByteArray pcm;
  for (auto const& segment : reply.pcm) pcm += segment;
  return pcm;
}

// Every row shares one data directory, so the FFTW wisdom the warm-up saves
// spares the JTTY rows JTTY's measured planning.
QTemporaryDir& dataDirectory ()
{
  static QTemporaryDir directory;
  return directory;
}

void startCodec (QProcess& process, QStringList options = {})
{
  auto const& directory = dataDirectory ();
  options << "-a" << directory.path () << "-t" << directory.path ();
  process.setProcessChannelMode (QProcess::SeparateChannels);
  process.setWorkingDirectory (directory.path ());
  process.start (QString::fromUtf8 (WSJT_CODEC_EXECUTABLE), options);
}

void finish (QProcess& process, Run& run, QByteArray output)
{
  if (!process.waitForFinished (ProcessTimeoutMs))
    {
      run.timedOut = true;
      process.kill ();
      process.waitForFinished (5000);
    }
  run.exitCode = process.exitCode ();
  run.exitStatus = process.exitStatus ();
  run.lines = outputLines (output + process.readAllStandardOutput ());
  run.standardError = process.readAllStandardError ();
  checkProtocol (run);
}

Run runCodec (QByteArray const& input, QStringList const& options = {})
{
  Run run;
  if (!dataDirectory ().isValid ()) return run;
  QProcess process;
  startCodec (process, options);
  run.started = process.waitForStarted (5000);
  if (!run.started) return run;
  process.write (header () + input);
  process.closeWriteChannel ();
  finish (process, run, {});
  return run;
}

// requests, then the audio of the render whose id is renderId sent back as
// audio, with LeadSamples of silence before it and a frame after, then halt.
Run roundTrip (QByteArray const& requests, qint64 renderId)
{
  Run run;
  if (!dataDirectory ().isValid ()) return run;
  QProcess process;
  startCodec (process);
  run.started = process.waitForStarted (5000);
  if (!run.started) return run;
  process.write (header () + requests);
  QByteArray output;
  QByteArray pcm;
  for (bool done = false; !done;)
    {
      if (!process.waitForReadyRead (ProcessTimeoutMs))
        {
          run.timedOut = true;
          process.kill ();
          process.waitForFinished (5000);
          return run;
        }
      output += process.readAllStandardOutput ();
      Run sent;
      sent.lines = outputLines (output.left (output.lastIndexOf ('\n') + 1));
      checkProtocol (sent);
      if (!sent.error.isEmpty () && !sent.error.startsWith ("the output ends inside a reply"))
        {
          process.kill ();
          process.waitForFinished (5000);
          run.error = sent.error;
          return run;
        }
      auto const reply = sent.replies.value (renderId);
      done = !reply.terminal.isEmpty ();
      if (done) pcm = allPcm (reply);
    }
  process.write (silence (LeadSamples) + audio (pcm) + silence (FrameSamples) + control (R"({"t":"halt"})"));
  process.closeWriteChannel ();
  finish (process, run, output);
  return run;
}

QList<QJsonObject> ofType (QList<QJsonObject> const& events, QString const& type)
{
  QList<QJsonObject> matching;
  for (auto const& event : events)
    if (event.value ("t").toString () == type) matching.append (event);
  return matching;
}

QStringList member (Reply const& reply, char const * name)
{
  QStringList values;
  for (auto const& segment : reply.segments) values << segment.value (name).toVariant ().toString ();
  return values;
}

QStringList frameTexts (QJsonObject const& segment)
{
  QStringList texts;
  for (auto const& frame : segment.value ("frames").toArray ()) texts << frame.toObject ().value ("text").toString ();
  return texts;
}

QJsonObject rejected (qint64 id, QString const& reason, QString const& key, QString const& detail)
{
  QJsonObject object {{"v", 1}, {"t", "rejected"}, {"id", id}, {"reason", reason}, {"detail", detail}};
  if (!key.isEmpty ()) object.insert ("key", key);
  return object;
}

QJsonObject codedError (QString const& code, QString const& detail)
{
  return {{"v", 1}, {"t", "error"}, {"code", code}, {"detail", detail}};
}

QJsonObject const InvalidRequestId {codedError ("invalid_request_id",
                                                "pack and render need an integer id from -2147483648 to 2147483647")};

#define CHECK_RUN(run)                                                  \
  do {                                                                  \
    QVERIFY2 ((run).started, "wsjt-codec did not start");               \
    QVERIFY2 (!(run).timedOut, "wsjt-codec timed out");                 \
    QVERIFY2 ((run).error.isEmpty (), qPrintable ((run).error));        \
    QCOMPARE ((run).exitStatus, QProcess::NormalExit);                  \
    QCOMPARE ((run).exitCode, 0);                                       \
  } while (false)

// The one segment of id's reply has canonical text, and it ends its message.
#define CHECK_ONE(run, id, canonical)                                                       \
  do {                                                                                      \
    auto const reply_ = (run).replies.value (id);                                          \
    QCOMPARE (reply_.terminal.value ("t").toString (), QString {"packed"});                 \
    QCOMPARE (reply_.segments.size (), 1);                                                  \
    QCOMPARE (reply_.segments[0].value ("canonical").toString (), QString {canonical});    \
    QCOMPARE (reply_.segments[0].value ("final").toBool (), true);                          \
  } while (false)
}

class TestWsjtCodecEncode
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase ()
  {
    QVERIFY (dataDirectory ().isValid ());
    // A decode of a transmission (silence plans nothing), whose clean exit
    // saves JTTY's FFTW wisdom for every later row.
    auto const transmission = guiAudio (textTones ("CQ"), 12000, 1500);
    QVERIFY (!transmission.isEmpty ());
    auto const warm = runCodec (control (R"({"t":"configure","mode":"JTTY"})") + silence (LeadSamples)
                                + audio (transmission) + silence (FrameSamples) + control (R"({"t":"halt"})"));
    CHECK_RUN (warm);
    QVERIFY (!ofType (warm.events, "jtty_update").isEmpty ());
  }

  // doc/user_guide/en/jtty.adoc's function-key table: K1ABC, W9XYZ, W7UVW
  // queued, serial 107 (108 for F6).
  void functionKeysAsTheUserGuideShows ()
  {
    struct Key {QString form; QString canonical; int frames; bool readsSerial;};
    QList<Key> const keys {
      {"CQ %M CQ", "CQ K1ABC CQ", 1, false}, {"%H %E", "W9XYZ 599 107", 2, true},
      {"%H TU CQ %M CQ", "W9XYZ TU CQ K1ABC CQ", 2, false}, {"%M", "K1ABC", 1, false},
      {"%H", "W9XYZ", 1, false}, {"TU NOW %Q %E", "TU NOW W7UVW 599 108", 2, true},
      {"%H AGN?", "W9XYZ AGN?", 1, false}, {"%E", "599 107", 1, true}};
    QByteArray input = control (Station);
    for (int i = 0; i < keys.size (); ++i)
      {
        bool const queued = i == 5;
        QJsonObject const context {{"his_call", queued ? "W7UVW" : "W9XYZ"}, {"serial", queued ? 108 : 107}};
        input += request (key (i + 1, keys[i].form, context));
        if (!keys[i].readsSerial) input += request (key (i + 11, keys[i].form, {{"his_call", "w9xyz"}}));
      }
    auto const run = runCodec (input);
    CHECK_RUN (run);
    for (int i = 0; i < keys.size (); ++i)
      {
        auto const reply = run.replies.value (i + 1);
        CHECK_ONE (run, i + 1, keys[i].canonical);
        QCOMPARE (reply.segments[0].value ("text").toString (), keys[i].canonical);
        QCOMPARE (reply.segments[0].value ("frames").toArray ().size (), keys[i].frames);
        QCOMPARE (reply.segments[0].value ("substituted").toBool (), false);
        QCOMPARE (reply.terminal.value ("substituted").toBool (), false);
        if (!keys[i].readsSerial) CHECK_ONE (run, i + 11, keys[i].canonical);
      }
  }

  // Each published form is sent natively, as the GUI sends it, and the
  // encoder has no native form the list lacks.
  void everyPublishedFormIsNative ()
  {
    QStringList listed;
    for (int form = int (Jtty::Encoder::NativeForm::Cq); form <= int (Jtty::Encoder::NativeForm::ReportGrid); ++form)
      listed << Jtty::fromEncoder (Jtty::Encoder::nativeFormTemplate (Jtty::Encoder::NativeForm (form)));
    QCOMPARE (QSet<QString> (listed.cbegin (), listed.cend ()), QSet<QString> (nativeForms.cbegin (), nativeForms.cend ()));
    QStringList phrases;
    for (int phrase = 0; !Jtty::controlPhrase (phrase).isEmpty (); ++phrase) phrases << Jtty::controlPhrase (phrase);
    QCOMPARE (phrases, controlPhrases);

    auto const forms = nativeForms + controlPhrases;
    QByteArray input = control (Station);
    for (int i = 0; i < forms.size (); ++i)
      input += request (render (key (i + 1, forms[i], {{"his_call", "W9XYZ"}, {"serial", 107}}), 12000, 1500));
    auto const run = runCodec (input);
    CHECK_RUN (run);
    for (int i = 0; i < forms.size (); ++i)
      {
        auto const compiled = Jtty::compileNativeMacro (forms[i], gui ());
        QVERIFY2 (compiled.isNative (), qPrintable (forms[i]));
        auto const reply = run.replies.value (i + 1);
        QCOMPARE (reply.terminal.value ("t").toString (), QString {"rendered"});
        QCOMPARE (reply.segments.size (), 1);
        QCOMPARE (reply.segments[0].value ("canonical").toString (), compiled.text);
        QVERIFY2 (allPcm (reply) == guiAudio (Jtty::encodeNativeAtoms (compiled.atoms).tones, 12000, 1500),
                  qPrintable (forms[i]));
      }
  }

  // A native form puts other bits on the air than the same text typed.
  void nativeFormsAreNotTheirText ()
  {
    struct Pair {QString form; QString typed; QString hisCall; int serial; bool identical;};
    QList<Pair> const pairs {{"CQ %M CQ", "CQ K1ABC CQ", "W9XYZ", 107, true},
                             {"%H %E", "W9XYZ 599 107", "W9XYZ", 107, false},
                             {"TU NOW %Q %E", "TU NOW W7UVW 599 108", "W7UVW", 108, false}};
    QByteArray input = control (Station);
    for (int i = 0; i < pairs.size (); ++i)
      {
        QJsonObject const context {{"his_call", pairs[i].hisCall}, {"serial", pairs[i].serial}};
        input += request (render (key (2 * i + 1, pairs[i].form, context), 12000, 1500));
        input += request (render (text (2 * i + 2, pairs[i].typed, context), 12000, 1500));
      }
    auto const run = runCodec (input);
    CHECK_RUN (run);
    for (int i = 0; i < pairs.size (); ++i)
      {
        auto const native = allPcm (run.replies.value (2 * i + 1));
        auto const typed = allPcm (run.replies.value (2 * i + 2));
        QCOMPARE (native == typed, pairs[i].identical);
        QVERIFY (native == guiAudio (keyTones (pairs[i].form, gui (pairs[i].hisCall, pairs[i].serial)), 12000, 1500));
        QVERIFY (typed == guiAudio (textTones (pairs[i].typed), 12000, 1500));
      }
  }

  void renderIsTheGuisAudio_data ()
  {
    QTest::addColumn<bool> ("native");
    QTest::addColumn<int> ("rate");
    QTest::addColumn<double> ("freq");
    QTest::newRow ("text 12 kHz") << false << 12000 << 1500.0;
    QTest::newRow ("text 48 kHz") << false << 48000 << 1234.5;
    QTest::newRow ("F2 12 kHz") << true << 12000 << 1234.5;
    QTest::newRow ("F2 48 kHz") << true << 48000 << 1500.0;
  }

  void renderIsTheGuisAudio ()
  {
    QFETCH (bool, native);
    QFETCH (int, rate);
    QFETCH (double, freq);
    QString const typed {"CQ K1ABC CQ 599 FN42 HE SAID \"HI\""};
    auto const tones = native ? keyTones ("%H %E", gui ()) : textTones (typed);
    QCOMPARE (tones.size (), (native ? 2 : 5) * FrameSymbols);
    auto const object = native ? key (5, "%H %E", {{"his_call", "W9XYZ"}, {"serial", 107}}) : text (5, typed);
    auto const run = runCodec (control (Station) + request (render (object, rate, freq)));
    CHECK_RUN (run);
    auto const reply = run.replies.value (5);
    QCOMPARE (qint64 (reply.lines.size ()),
              2 + (qint64 (tones.size ()) * Nsps * rate / 12000 + PcmLineSamples - 1) / PcmLineSamples);
    QCOMPARE (reply.segments[0].value ("canonical").toString (), native ? QString {"W9XYZ 599 107"} : typed);
    QCOMPARE (qint64 (reply.segments[0].value ("samples").toDouble ()), qint64 (tones.size ()) * Nsps * rate / 12000);
    QVERIFY2 (allPcm (reply) == guiAudio (tones, rate, freq), "the PCM differs from the GUI's rendering");
    QCOMPARE (reply.terminal, (QJsonObject {{"v", 1}, {"t", "rendered"}, {"id", 5}, {"segments", 1},
                                            {"substituted", false}}));
  }

  // 16384 samples a line but the last; base64 padding as each line's byte
  // count gives it.
  void pcmLinesAreChunked ()
  {
    auto const run = runCodec (control (Station)
                               + request (render (text (1, "CQ K1ABC CQ 599 FN42 HE SAID \"HI\""), 12000, 1500))
                               + request (render (key (2, "%H %E", {{"his_call", "W9XYZ"}, {"serial", 107}}), 12000, 1500))
                               + request (render (key (3, "CQ %M CQ"), 12000, 1500)));
    CHECK_RUN (run);
    auto const lines = run.replies.value (1).lines;
    QCOMPARE (lines.size (), 1 + 7 + 1);           // 113,280 samples
    for (int seq = 0; seq < 7; ++seq)
      {
        auto const pcm = QJsonDocument::fromJson (lines[1 + seq]).object ();
        QCOMPARE (pcm.value ("seq").toInt (), seq);
        auto const data = pcm.value ("data").toString ();
        // 32,768 bytes leave two in the last group; the last line's 29,952 none.
        QCOMPARE (data.size (), seq < 6 ? 43692 : 39936);
        QCOMPARE (data.endsWith ("=") && !data.endsWith ("=="), seq < 6);
      }
    auto padding = [&run] (qint64 id) {
      auto const lines = run.replies.value (id).lines;
      auto const data = QJsonDocument::fromJson (lines[lines.size () - 2]).object ().value ("data").toString ();
      return data.size () - QString {data}.remove ('=').size ();
    };
    QCOMPARE (padding (2), 1);                     // 25,088 bytes
    QCOMPARE (padding (3), 2);                     // 12,544 bytes
  }

  void renderSendsEverySegment ()
  {
    auto const run = runCodec (request (render (text (1, longLine), 48000, 1500)));
    CHECK_RUN (run);
    auto const reply = run.replies.value (1);
    QCOMPARE (member (reply, "canonical"), longLineCanonicals);
    QList<int> const frames {16, 16, 9};
    for (int i = 0; i < 3; ++i)
      {
        QCOMPARE (reply.segments[i].value ("frames").toArray ().size (), frames[i]);
        QCOMPARE (reply.pcm[i].size (), 2 * frames[i] * FrameSamples * 4);
      }
    QCOMPARE (reply.terminal.value ("substituted").toBool (), true);
  }

  // A render holds at most 600 s of audio; a longer one sends none.
  void renderIsCapped ()
  {
    auto const fits = QString (80 * 19, 'A') + " " + QString (64, 'B');    // 304 + 13 frames, 598.496 s
    auto const run = runCodec (request (render (text (1, fits), 12000, 1500))
                               + request (render (text (2, fits + "BBBBB"), 12000, 1500))
                               + request (render (text (3, QString (400, 'A')), 48000, 1500)));    // 80 frames
    CHECK_RUN (run);
    QCOMPARE (run.replies.value (1).terminal.value ("t").toString (), QString {"rendered"});
    QCOMPARE (allPcm (run.replies.value (1)).size (), 2 * 317 * FrameSamples);
    // The cap is in seconds at either rate: 151.04 s at 48 kHz renders.
    QCOMPARE (run.replies.value (3).terminal.value ("t").toString (), QString {"rendered"});
    QCOMPARE (allPcm (run.replies.value (3)).size (), 2 * 80 * FrameSamples * 4);
    auto const over = run.replies.value (2);
    QCOMPARE (over.lines.size (), 1);
    QCOMPARE (over.terminal, rejected (2, "too_long", {}, "the audio is longer than 600 s"));
  }

  void idsAreEchoedExactly ()
  {
    QList<qint64> const ids {-2147483647LL - 1, 0, 2147483647};
    QByteArray input;
    for (auto const id : ids) input += request (render (text (id, "HI"), 48000, 1500));
    auto const run = runCodec (input);
    CHECK_RUN (run);
    for (auto const id : ids)
      {
        auto const reply = run.replies.value (id);
        QCOMPARE (reply.terminal.value ("t").toString (), QString {"rendered"});
        QVERIFY (reply.lines.size () >= 3);
        for (auto const& line : reply.lines) QVERIFY (line.contains (",\"id\":" + QByteArray::number (id) + ","));
      }
  }

  // The waveform cache holds one pulse per rate, so rates may alternate.
  void ratesAlternate ()
  {
    QList<int> const rates {12000, 48000, 12000};
    QByteArray input;
    for (int i = 0; i < rates.size (); ++i) input += request (render (text (i + 1, "CQ CQ"), rates[i], 1500));
    auto const run = runCodec (input);
    CHECK_RUN (run);
    for (int i = 0; i < rates.size (); ++i)
      {
        auto const alone = runCodec (request (render (text (1, "CQ CQ"), rates[i], 1500)));
        CHECK_RUN (alone);
        QVERIFY (allPcm (run.replies.value (i + 1)) == allPcm (alone.replies.value (1)));
      }
  }

  // Text is sent as typed, its placeholders expanded and its case kept.
  void textIsSentAsTyped ()
  {
    struct Row {QString text; QString profile; QString canonical; bool substituted;};
    QList<Row> const rows {
      {"cq %M %G", "none", "CQ K1ABC FN42", false},
      {"%M %H %Q %N %E %G %R", "none", "K1ABC W9XYZ W9XYZ 107 107 FN42 -07", false},
      {"%H 599 %E", "rtty", "W9XYZ 599 CA", false},
      {QString {"stra"} + QChar (0xDF) + "e " + QChar (0x131) + "x", "none", "STRA#E #X", true},
      {QString (QChar (0xE9)) + "t" + QChar (0xE9), "none", "#T#", true},
      {"ABC~DEF", "none", "ABC DEF", true}};
    QJsonObject const context {{"his_call", "W9XYZ"}, {"serial", 107}, {"report", -7}, {"exchange", "CA"}};
    QByteArray input = control (R"({"t":"configure","mycall":"K1ABC","mygrid":"fn42mn"})");
    for (int i = 0; i < rows.size (); ++i) input += request (text (i + 1, rows[i].text, with (context, "profile", rows[i].profile)));
    auto const run = runCodec (input);
    CHECK_RUN (run);
    for (int i = 0; i < rows.size (); ++i)
      {
        CHECK_ONE (run, i + 1, rows[i].canonical);
        QCOMPARE (run.replies.value (i + 1).segments[0].value ("substituted").toBool (), rows[i].substituted);
        QCOMPARE (run.replies.value (i + 1).terminal.value ("substituted").toBool (), rows[i].substituted);
      }
  }

  // lib/jtty/jtty_design.md's exchange-profile rows.
  void profilesShapeTheText ()
  {
    struct Row {QString text; QString profile; QString canonical; int frames;};
    QList<Row> const rows {{"599 05", "rtty", "599 005", 1}, {"599 05", "none", "599 05", 2},
                           {"1D EMA", "field_day", "1D EMA", 1}};
    QByteArray input;
    for (int i = 0; i < rows.size (); ++i) input += request (text (i + 1, rows[i].text, {{"profile", rows[i].profile}}));
    auto const run = runCodec (input);
    CHECK_RUN (run);
    for (int i = 0; i < rows.size (); ++i)
      {
        CHECK_ONE (run, i + 1, rows[i].canonical);
        auto const segment = run.replies.value (i + 1).segments[0];
        QCOMPARE (segment.value ("text").toString (), rows[i].text);
        QCOMPARE (segment.value ("frames").toArray ().size (), rows[i].frames);
      }
  }

  // A line longer than a segment is one message: only its last segment ends it.
  // A segment that does not encode refuses the request rather than being cut shorter.
  void longTextIsSegmented ()
  {
    QStringList pairs;
    for (int i = 1; i <= 20; ++i) pairs << QString {"599 %1"}.arg (i);
    auto const run = runCodec (request (text (1, longLine))
                               + request (text (2, QString (85, 'X') + " Y"))
                               + request (text (3, pairs.join (' '), {{"profile", "rtty"}}))
                               + request (text (4, longLine, {{"final", false}}))
                               + request (text (5, "HELLO", {{"final", false}}))
                               + request (text (6, "HELLO", {{"final", true}}))
                               + request (text (7, "HELLO" + QString (75, ' ') + QString (80, '~') + " WORLD")));
    CHECK_RUN (run);
    auto const line = run.replies.value (1);
    QCOMPARE (member (line, "canonical"), longLineCanonicals);
    QCOMPARE (member (line, "final"), (QStringList {"false", "false", "true"}));
    QCOMPARE (member (line, "substituted"), (QStringList {"false", "false", "true"}));
    QCOMPARE (line.terminal.value ("substituted").toBool (), true);
    QCOMPARE (line.segments[0].value ("text").toString (),
              QString {"Thanks for the QSO, Bob.  Rig here is a home-brew transceiver running 5 watts"});
    QCOMPARE (line.segments[2].value ("text").toString (), QString {"work you again on JTTY soon # 73 and good DX!"});
    QCOMPARE (member (run.replies.value (2), "canonical"), (QStringList {QString (80, 'X'), "XXXXX Y"}));
    QVERIFY (run.replies.value (3).segments.isEmpty ());
    QCOMPARE (run.replies.value (3).terminal, rejected (3, "encoding_failed", {}, "the text cannot be encoded"));
    QCOMPARE (member (run.replies.value (4), "final"), (QStringList {"false", "false", "false"}));
    QCOMPARE (member (run.replies.value (5), "final"), QStringList {"false"});
    QCOMPARE (member (run.replies.value (6), "final"), QStringList {"true"});
    // A replacement counts for the request even in a blank run sent as no segment.
    QCOMPARE (member (run.replies.value (7), "canonical"), (QStringList {"HELLO", "WORLD"}));
    QCOMPARE (member (run.replies.value (7), "substituted"), (QStringList {"false", "false"}));
    QCOMPARE (run.replies.value (7).terminal.value ("substituted").toBool (), true);
  }

  // A newline, written as a JSON escape, ends a message and replaces nothing.
  void newlineEndsAMessage ()
  {
    auto const run = runCodec (request (text (1, "HELLO\nWORLD", {{"final", false}}))
                               + control ("{\"t\":\"pack\",\"id\":2,\"final\":false,\"text\":\"HELLO\\r\\nWORLD\"}"));
    CHECK_RUN (run);
    for (qint64 id : {1, 2})
      {
        auto const reply = run.replies.value (id);
        QVERIFY (reply.lines[0].contains ("HELLO"));
        QCOMPARE (member (reply, "canonical"), (QStringList {"HELLO", "WORLD"}));
        QCOMPARE (member (reply, "final"), (QStringList {"true", "false"}));
        QCOMPARE (member (reply, "substituted"), (QStringList {"false", "false"}));
        QCOMPARE (reply.terminal.value ("substituted").toBool (), false);
      }
  }

  // Escaped characters are sent one UTF-16 unit at a time, as the GUI sends them.
  void escapesAreCharacters ()
  {
    auto body = [] (qint64 id, QByteArray const& value) {
      return control ("{\"t\":\"pack\",\"id\":" + QByteArray::number (id) + ",\"text\":\"" + value + "\"}");
    };
    auto const run = runCodec (body (1, "A" + escape (0xD83D) + escape (0xDE00) + "B")
                               + body (2, "A" + escape (0) + "B")
                               + body (3, escape (0xE9) + "t" + escape (0xE9))
                               + body (4, escape (0xFEFF) + "HI"));
    CHECK_RUN (run);
    QStringList const canonicals {"A##B", "A B", "#T#", "#HI"};
    for (int id = 1; id <= 4; ++id)
      {
        CHECK_ONE (run, id, canonicals[id - 1]);
        QCOMPARE (run.replies.value (id).segments[0].value ("substituted").toBool (), true);
      }
  }

  void framesAreWhatTheReceiverShows ()
  {
    auto const run = runCodec (request (text (1, "TEST 1 2 3 HELLO WORLD"))
                               + request (key (2, "%H %E", {{"his_call", "W9XYZ"}, {"serial", 107}}))
                               + request (text (3, "HI", {{"final", false}}))
                               + request (text (4, "HI"))
                               + request (text (6, "HI\nHO", {{"final", false}}))
                               + request (render (text (5, "HI", {{"final", false}}), 12000, 1500)));
    CHECK_RUN (run);
    QCOMPARE (frameTexts (run.replies.value (1).segments.value (0)),
              (QStringList {"TEST ", "1", "2 3 H", "ELLO ", "WORLD"}));
    QCOMPARE (frameTexts (run.replies.value (2).segments.value (0)), (QStringList {"W9XYZ", "599 107"}));
    // An open message's last frame keeps its padding: the receiver counts its
    // five columns before whatever continues the message.
    QCOMPARE (frameTexts (run.replies.value (3).segments.value (0)), QStringList {"HI   "});
    QCOMPARE (frameTexts (run.replies.value (4).segments.value (0)), QStringList {"HI"});
    QCOMPARE (frameTexts (run.replies.value (5).segments.value (0)), QStringList {"HI   "});
    QCOMPARE (member (run.replies.value (6), "final"), (QStringList {"true", "false"}));
    QCOMPARE (frameTexts (run.replies.value (6).segments.value (0)), QStringList {"HI"});
    QCOMPARE (frameTexts (run.replies.value (6).segments.value (1)), QStringList {"HO   "});
  }

  void outputIsEscaped ()
  {
    auto const run = runCodec (request (text (1, "HE SAID \"73\" OK")));
    CHECK_RUN (run);
    CHECK_ONE (run, 1, "HE SAID \"73\" OK");
    QVERIFY (run.replies.value (1).lines[0].contains ("\"canonical\":\"HE SAID \\\"73\\\" OK\""));
  }

  void functionKeysAreRefused ()
  {
    auto context = [] (QJsonObject members) {
      if (!members.contains ("his_call")) members.insert ("his_call", "W9XYZ");
      if (!members.contains ("serial")) members.insert ("serial", 107);
      return members;
    };
    auto const run = runCodec (
      control (Station)
      + request (key (1, "TU NOW %Q %E", context ({{"his_call", "W9XYZ/P"}})))
      + request (key (2, "%H %E", context ({{"serial", 131072}})))
      + request (key (3, "%E", context ({{"profile", "field_day"}})))
      + request (key (4, "%E", context ({{"profile", "field_day"}, {"exchange", "1d xyz"}})))
      + request (key (5, "  ", context ({})))
      + request (key (6, "%H 599 5", context ({{"profile", "rtty"}})))
      + request (key (7, "%H 599 5", context ({})))
      + request (key (8, QString {"stra"} + QChar (0xDF) + "e", context ({})))
      + request (key (9, "%H 599 5", context ({{"final", false}})))
      + request (key (10, "%H %E", context ({{"final", false}})))
      + request (key (11, QString {"\t"} + QChar (0x3000), context ({}))));
    CHECK_RUN (run);
    QCOMPARE (run.replies.value (1).terminal,
              rejected (1, "invalid_runtime", {}, "Queued callsign is not a native Call8 callsign"));
    QCOMPARE (run.replies.value (2).terminal,
              rejected (2, "invalid_runtime", {}, "Serial number must be between 0 and 131071"));
    QCOMPARE (run.replies.value (3).terminal,
              rejected (3, "invalid_runtime", {}, "Field Day exchange must be COUNTCLASS SECTION"));
    QCOMPARE (run.replies.value (4).terminal,
              rejected (4, "encoding_failed", {}, "Field Day section is not registered in the ARRL/RAC table"));
    QCOMPARE (run.replies.value (5).terminal.value ("reason").toString (), QString {"empty"});
    // A template of Unicode spaces is blank, not text to send.
    QCOMPARE (run.replies.value (11).terminal, rejected (11, "empty", {}, "the template has nothing to send"));
    CHECK_ONE (run, 6, "W9XYZ 599 005");
    CHECK_ONE (run, 7, "W9XYZ 599 5");
    CHECK_ONE (run, 8, "STRA#E");
    CHECK_ONE (run, 9, "W9XYZ 599 5");
    CHECK_ONE (run, 10, "W9XYZ 599 107");
  }

  // The station's call and grid come from accepted configures only; the
  // stream's start values (K1ABC) and -c/-G never reach the air.
  void stationIdentityComesFromConfigure ()
  {
    QByteArray const notUsable {"{\"t\":\"configure\",\"mycall\":\"KJ5HSTABCDE" + escape (0xE9) + "\"}"};
    auto const run = runCodec (
      request (text (1, "%M")) + request (key (2, "CQ %M CQ")) + request (key (3, "%G"))
      + control (R"({"t":"configure","mode":"JTTY","mycall":"W1AW","rxfreq":9000})")
      + request (text (4, "%M"))
      + control (R"({"t":"configure","mode":"FT8","mycall":"W9XYZ","nfa":3000,"nfb":2000})")
      + request (text (19, "%M"))
      + control (R"({"t":"configure","mycall":"KJ5HST"})")
      + request (text (5, "%M")) + request (text (6, "%N")) + request (text (7, "%R")) + request (text (8, "%G"))
      + control (R"({"t":"configure","mygrid":"EM18"})") + control (R"({"t":"configure","mode":"FT8"})")
      + request (text (10, "%M %G"))
      + control (R"({"t":"configure","mycall":"VE3/KJ5HST/MM"})") + request (text (11, "DE %M"))
      + control ("{\"t\":\"configure\",\"mycall\":\"" + QByteArray (65, 'W') + "\"}")
      + request (text (12, "%M")) + request (text (13, "CQ CQ"))
      + control (R"({"t":"configure","mycall":"W1AW"})") + request (text (14, "%M"))
      + control ("{\"t\":\"configure\",\"mycall\":\"KJ5" + QByteArray (1, '\\') + "x\"}") + request (text (15, "%M"))
      + control (R"({"t":"configure","mycall":"W1AW"})") + control (R"({"t":"configure","mycall":""})")
      + request (text (16, "%M"))
      + control (R"({"t":"configure","mycall":"  W2XYZ "})") + request (text (17, "[%M]"))
      + control ("{\"t\":\"configure\",\"mygrid\":\"" + QByteArray (65, 'G') + "\"}") + request (text (18, "%G"))
      + control (notUsable) + request (text (9, "%M")));
    CHECK_RUN (run);
    QString const unset {"the request uses mycall, which no configure has set"};
    QCOMPARE (run.replies.value (1).terminal, rejected (1, "not_configured", "mycall", unset));
    QCOMPARE (run.replies.value (2).terminal, rejected (2, "not_configured", "mycall", unset));
    QCOMPARE (run.replies.value (3).terminal,
              rejected (3, "not_configured", "mygrid", "the request uses mygrid, which no configure has set"));
    QCOMPARE (ofType (run.events, "error").value (0).value ("code").toString (), QString {"configure_range_error"});
    QCOMPARE (run.replies.value (4).terminal, rejected (4, "not_configured", "mycall", unset));
    // A configure the decoder would refuse is declined whole, its call with it.
    QCOMPARE (ofType (run.events, "error").value (1).value ("key").toString (), QString {"nfa"});
    QCOMPARE (run.replies.value (19).terminal, rejected (19, "not_configured", "mycall", unset));
    CHECK_ONE (run, 5, "KJ5HST");
    QCOMPARE (run.replies.value (6).terminal,
              rejected (6, "missing", "serial", "the request uses serial and does not give it"));
    QCOMPARE (run.replies.value (7).terminal,
              rejected (7, "missing", "report", "the request uses report and does not give it"));
    QCOMPARE (run.replies.value (8).terminal.value ("key").toString (), QString {"mygrid"});
    // A configure without mycall or mygrid keeps both.
    CHECK_ONE (run, 10, "KJ5HST EM18");
    // The whole call goes on the air, as the GUI sends it.
    CHECK_ONE (run, 11, "DE VE3/KJ5HST/MM");
    QCOMPARE (run.replies.value (12).terminal,
              rejected (12, "not_configured", "mycall",
                        "the request uses mycall, and the configured mycall is longer than 64 characters"));
    CHECK_ONE (run, 13, "CQ CQ");
    // A new call that cannot be read, or a blank one, stops the old one going out.
    CHECK_ONE (run, 14, "W1AW");
    QString const unusable {"the request uses mycall, and the configured mycall is not a usable callsign"};
    QCOMPARE (run.replies.value (15).terminal, rejected (15, "not_configured", "mycall", unusable));
    QCOMPARE (run.replies.value (16).terminal, rejected (16, "not_configured", "mycall", unusable));
    // White space at either end of a call is dropped, as the GUI trims it.
    CHECK_ONE (run, 17, "[W2XYZ]");
    QCOMPARE (run.replies.value (18).terminal,
              rejected (18, "not_configured", "mygrid",
                        "the request uses mygrid, and the configured mygrid is longer than 64 characters"));
    // A call with a character outside printable ASCII is not usable.
    QCOMPARE (run.replies.value (9).terminal,
              rejected (9, "not_configured", "mycall",
                        "the request uses mycall, and the configured mycall is not a usable callsign"));
    for (auto const& line : run.lines) QVERIFY2 (!line.contains ("K1ABC"), line.constData ());

    auto const options = runCodec (request (text (1, "%M")) + request (text (2, "%G")), {"-c", "W1XX", "-G", "EM10"});
    CHECK_RUN (options);
    QCOMPARE (options.replies.value (1).terminal.value ("reason").toString (), QString {"not_configured"});
    QCOMPARE (options.replies.value (2).terminal.value ("key").toString (), QString {"mygrid"});
  }

  // Coded errors answer what has no id; the stream reads on after each.
  void requestsThatCannotBeAnswered ()
  {
    QByteArray const open {R"({"t":"pack","id":4,"text":"HI")"};
    auto const largest = open + QByteArray (262144 - open.size () - 1, ' ') + "}";
    auto const run = runCodec (
      control (R"({"t":"pack","text":"HI"})") + control (R"({"t":"pack","id":"5","text":"HI"})")
      + control (R"({"t":"pack","id":1.5,"text":"HI"})")
      + control (R"({"t":"pack","id":/,"text":"HI"})")
      + control (R"({"t":"pack","id":5,"text":"%N","serial":1*})")
      + control (R"({"t":"render","id":1.5,"text":"HI","rate":12000,"freq":1500})")
      + control (R"({"t":"transmit","id":8})")
      + control (largest + ' ')
      + control (R"({"t":"pack","id":3,"text":"HI","context":{"his_call":"W9XYZ"}})")
      + control (R"({"t":"pack","x":{"id":2},"id":6,"text":"HI"})")
      // Qt writes keys sorted, so this nested context comes before the id.
      + request ({{"t", "pack"}, {"id", 7}, {"text", "HI"}, {"context", QJsonObject {{"his_call", "W9XYZ"}}}})
      // Brackets and an escaped quote inside a string before the id are text.
      + request ({{"t", "pack"}, {"id", 10}, {"text", "HI"}, {"his_call", "W1{X\"["}})
      // A stray closer does not end the nesting that follows it.
      + control ("{\"t\":\"pack\",\"text\":\"HI\"}}{\"x\":{\"id\":2}}")
      + control (largest)
      + request (text (9, "HI")));
    CHECK_RUN (run);
    QList<QJsonObject> const errors {
      InvalidRequestId, InvalidRequestId, InvalidRequestId, InvalidRequestId, InvalidRequestId,
      codedError ("control_frame_too_large", "control frame exceeds the configured buffer capacity"),
      InvalidRequestId};
    QCOMPARE (ofType (run.events, "error"), errors);
    for (qint64 id : {3, 6, 7})
      QCOMPARE (run.replies.value (id).terminal, rejected (id, "bad_request", {}, "a request is a flat JSON object"));
    QCOMPARE (run.replies.value (5).terminal,
              rejected (5, "bad_request", "serial", "serial must be an integer from -2147483648 to 2147483647"));
    CHECK_ONE (run, 4, "HI");
    CHECK_ONE (run, 9, "HI");
    CHECK_ONE (run, 10, "HI");
    QCOMPARE (run.replies.keys (), (QList<qint64> {3, 4, 5, 6, 7, 9, 10}));
  }

  void haltAndEndOfInput ()
  {
    auto const halted = runCodec (request (text (1, "HI")) + control (R"({"t":"halt"})") + request (text (2, "HO")));
    CHECK_RUN (halted);
    QCOMPARE (halted.replies.keys (), QList<qint64> {1});
    auto const truncated = runCodec (request (text (1, "HI")) + request (text (2, "HO")).left (12));
    CHECK_RUN (truncated);
    QCOMPARE (truncated.replies.keys (), QList<qint64> {1});
  }

  // From a file, a request answered mid-period changes none of the period's decodes.
  void requestsLeavePeriodsAlone ()
  {
    auto const ft8 = wavData (QString::fromUtf8 (FT8_ENGINE_WAV));
    QCOMPARE (ft8.size (), 15 * 12000 * 2);
    auto const configure = control (R"({"t":"configure","mode":"FT8","utc":"05:11:15"})") + control (Station);
    auto const alone = runCodec (configure + audio (ft8));
    auto const interleaved = runCodec (configure + audio (ft8.left (ft8.size () / 2))
                                       + request (render (text (1, "CQ %M"), 12000, 1500))
                                       + audio (ft8.mid (ft8.size () / 2)));
    CHECK_RUN (alone);
    CHECK_RUN (interleaved);
    auto decodes = [] (Run const& run) {
      QList<QByteArray> lines;
      for (int i = 0; i < run.events.size (); ++i)
        if (run.events[i].value ("t").toString ().startsWith ("decode")) lines << run.lines[i];
      return lines;
    };
    QVERIFY (!ofType (alone.events, "decode").isEmpty ());
    QCOMPARE (decodes (interleaved), decodes (alone));
    auto const reply = interleaved.replies.value (1);
    QCOMPARE (reply.terminal.value ("t").toString (), QString {"rendered"});
    QCOMPARE (interleaved.lines.indexOf (reply.lines.last ()) + 1, interleaved.lines.indexOf (decodes (alone).first ()));
  }

  // Rendered audio, sent back to the same process in JTTY, decodes as sent.
  void renderDecodesAsSent_data ()
  {
    QTest::addColumn<QJsonObject> ("object");
    QTest::addColumn<QStringList> ("complete");
    QTest::addColumn<QString> ("seen");
    QTest::newRow ("text") << text (1, "cq cq de %M k") << QStringList {"CQ CQ DE K1ABC K"} << QString {};
    QTest::newRow ("F2") << key (1, "%H %E", {{"his_call", "W9XYZ"}, {"serial", 107}})
                         << QStringList {"W9XYZ 599 107"} << QString {};
    // The receiver keeps at most 80 characters of a message, and the 200
    // characters are one message.
    QTest::newRow ("200 characters") << text (1, longLine) << QStringList {longLineCanonicals[0]} << QString {};
    QTest::newRow ("open message") << text (1, "HELLO\nWORLD", {{"final", false}}) << QStringList {"HELLO"}
                                   << QString {"WORLD"};
  }

  void renderDecodesAsSent ()
  {
    QFETCH (QJsonObject, object);
    QFETCH (QStringList, complete);
    QFETCH (QString, seen);
    auto const run = roundTrip (control (R"({"t":"configure","mode":"JTTY","mycall":"K1ABC"})")
                                + request (render (object, 12000, 1500)), 1);
    CHECK_RUN (run);
    QCOMPARE (run.replies.value (1).terminal.value ("t").toString (), QString {"rendered"});
    QStringList completed, texts;
    for (auto const& update : ofType (run.events, "jtty_update"))
      {
        texts << update.value ("text").toString ();
        if (update.value ("state").toString () == "complete") completed << update.value ("text").toString ();
      }
    QCOMPARE (completed, complete);
    if (!seen.isEmpty ()) QVERIFY2 (texts.contains (seen), qPrintable (texts.join ('|')));
  }
};

QTEST_GUILESS_MAIN (TestWsjtCodecEncode);

#include "test_wsjt_codec_encode.moc"
