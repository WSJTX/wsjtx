#include <QtTest>
#include <QTextDocument>

#include "JttyReceiveResultController.hpp"

namespace {
using Controller = Jtty::ReceiveResultController;
using Source = Controller::Source;
using Pane = Controller::Pane;
using Terminal = Jtty::ReceiveTerminal;

Jtty::ReceiveUpdate update (qint64 id, QString const& text,
                            Terminal terminal = Terminal::Growing,
                            float frequency = 1500, int snr = -7)
{
  return {id, frequency, text, 2.25, 4.5, terminal, snr};
}

struct Harness
{
  Controller controller;
  QTextDocument all, qso;
  QFont font {"Monospace", 10};
  Controller::Reception reception;
  Controller::Selection selection {true, 1, 1500, 50};
  JttyReceiveLine::Options display;
  Controller::EchoOptions echo {true, false};
  bool reporting {true};
  QStringList trace, echoes, logs;
  QVector<DecodeOperatingContext> logContexts;
  QVector<Controller::Spot> spots;
  QVector<QPair<qint64, qint64>> intervals;
  QVector<JttyReceiveLine::Presentation> presentations;
  std::function<void(QString const&)> after;
  Controller::Inputs inputs;
  Controller::Effects effects;

  void event (QString const& name)
  {
    trace.append (name);
    if (after) after (name);
  }

  Harness ()
  {
    reception.contextId = 1;
    reception.displayGroup = 1;
    reception.anchor = QDateTime {QDate {2026, 9, 11}, QTime {12, 34, 56}, Qt::UTC};
    reception.displayAnchor = reception.anchor;
    reception.context.mode = "JTTY";
    reception.context.myCall = "N0ME";
    reception.context.periodFrequency = 14078000;
    reception.context.band = "20m";
    inputs.display = [this] { event ("display"); return display; };
    inputs.selection = [this] { event ("selection"); return selection; };
    inputs.echo = [this] { event ("echo-options"); return echo; };
    inputs.reporting = [this] { event ("reporting"); return reporting; };
    effects.reviewHeading = [this] (Pane pane) {
      auto& document = pane == Pane::All ? all : qso;
      QTextCursor cursor {&document};
      cursor.movePosition (QTextCursor::End);
      if (cursor.position ()) cursor.insertBlock ();
      cursor.insertText ("REVIEW");
      event (pane == Pane::All ? "heading-all" : "heading-qso");
    };
    effects.render = [this] (Pane pane, JttyReceiveLine& line,
                             JttyReceiveLine::Presentation const& presentation,
                             JttyReceiveLine::Options const& options) {
      line.render (pane == Pane::All ? all : qso, presentation, options,
                   font, pane == Pane::All);
      presentations.append (presentation);
      event (pane == Pane::All ? "all" : "qso");
    };
    effects.echo = [this] (QString const& text) { echoes.append (text); event ("echo"); };
    effects.noteDecoded = [this] (qint64 first, qint64 last) {
      intervals.append (qMakePair (first, last)); event ("recording");
    };
    effects.log = [this] (QString const& text, DecodeOperatingContext const& context) {
      logs.append (text); logContexts.append (context); event ("log");
    };
    effects.spot = [this] (Controller::Spot const& spot) { spots.append (spot); event ("spot"); };
  }

  void apply (QVector<Jtty::ReceiveUpdate> const& updates)
  {
    controller.apply (updates, reception, inputs, effects);
  }
};
}

class TestJttyReceiveResults final : public QObject
{
  Q_OBJECT

private slots:
  void interleavedGrowthAndTerminalOnlyDelivery ()
  {
    Harness h;
    h.apply ({update (1, "CQ K1ABC"), update (2, "OTHER")});
    auto const firstBlock = h.all.begin ();
    auto const firstQsoBlock = h.qso.begin ();
    h.apply ({update (1, "CQ K1ABC FN42"), update (1, "CQ K1ABC FN42", Terminal::Complete),
              update (3, "CQ K1ABC FN42", Terminal::Complete)});
    QCOMPARE (h.all.blockCount (), 3);
    QCOMPARE (h.qso.blockCount (), 3);
    QCOMPARE (h.all.begin (), firstBlock);
    QCOMPARE (h.qso.begin (), firstQsoBlock);
    QCOMPARE (h.all.toPlainText (), QString {"1500  -7  CQ K1ABC FN42\n1500  -7  OTHER\n1500  -7  CQ K1ABC FN42"});
    QCOMPARE (h.logs.size (), 2);
    QCOMPARE (h.logs.front (), QString {"1500  -7  CQ K1ABC FN42"});
    QCOMPARE (h.spots.size (), 2);
    QCOMPARE (h.intervals.size (), 5);
    QCOMPARE (h.intervals.front (), qMakePair (qint64 {54000}, qint64 {76656}));
    h.apply ({update (1, "CQ K1ABC FN42", Terminal::Complete)});
    QCOMPARE (h.logs.size (), 3);
    QCOMPARE (h.spots.size (), 3);
    QCOMPARE (h.all.blockCount (), 4);
  }

  void sourceAndTerminalPolicy_data ()
  {
    QTest::addColumn<int> ("source");
    QTest::addColumn<int> ("terminal");
    QTest::addColumn<bool> ("empty");
    for (int source = 0; source < 3; ++source)
      for (int terminal = 0; terminal < 4; ++terminal)
        for (bool empty : {false, true}) {
          auto const name = QString {"source-%1-terminal-%2-empty-%3"}.arg (source).arg (terminal).arg (empty);
          QTest::newRow (qPrintable (name)) << source << terminal << empty;
        }
  }

  void sourceAndTerminalPolicy ()
  {
    QFETCH (int, source);
    QFETCH (int, terminal);
    QFETCH (bool, empty);
    Harness h;
    h.reception.source = static_cast<Source> (source);
    auto const review = h.reception.source == Source::Review;
    auto const live = h.reception.source == Source::Live;
    h.apply ({update (1, empty ? QString {} : QString {"CQ K1ABC FN42"}, static_cast<Terminal> (terminal))});
    QCOMPARE (h.logs.size (), int (!empty && !review && terminal != 0));
    QCOMPARE (h.spots.size (), int (!empty && live && terminal == 1));
    QCOMPARE (h.intervals.size (), int (!empty && live));
    QCOMPARE (h.echoes.size (), int (!empty && !review));
    QCOMPARE (h.trace.count ("heading-all"), int (!empty && review));
    QCOMPARE (h.trace.count ("heading-qso"), int (!empty && review));
    QCOMPARE (h.presentations.size (), empty ? 0 : 2);
    if (empty) QVERIFY (h.trace.isEmpty ());
  }

  void tuningAdmissionIsStickyAndUsesCurrentLiveContext ()
  {
    Harness h;
    h.selection.center = 1600;
    h.apply ({update (1, "EXCLUDED")});
    QVERIFY (h.qso.isEmpty ());
    h.selection.center = 1500;
    QVERIFY (h.qso.isEmpty ());
    h.apply ({update (1, "EXCLUDED")});
    QVERIFY (h.qso.toPlainText ().contains ("EXCLUDED"));
    QCOMPARE (h.echoes, QStringList {"\r\nEXCLUDED"});
    h.selection.center = 1700;
    h.apply ({update (1, "EXCLUDED NOW INCLUDED")});
    QVERIFY (h.qso.toPlainText ().contains ("EXCLUDED NOW INCLUDED"));
    QCOMPARE (h.echoes.last (), QString {" NOW INCLUDED"});

    for (Source source : {Source::Live, Source::Review, Source::Wav}) {
      Harness captured;
      captured.reception.source = source;
      captured.selection.contextId = 2;
      captured.selection.center = 1700;
      captured.apply ({update (1, "CAPTURED")});
      QVERIFY (captured.qso.toPlainText ().contains ("CAPTURED"));
    }
    Harness inactive;
    inactive.selection.jttyActive = false;
    inactive.selection.center = 1700;
    inactive.apply ({update (1, "INACTIVE")});
    QVERIFY (inactive.qso.toPlainText ().contains ("INACTIVE"));
  }

  void retainedContextAndPresentationHaveSeparateAnchors ()
  {
    Harness h;
    auto const firstAnchor = h.reception.anchor;
    h.reception.displayAnchor = firstAnchor.addDays (1);
    h.display = {true, true};
    h.apply ({update (1, "CQ K1ABC")});
    auto const trace = h.trace;
    h.apply ({update (1, {}, Terminal::Complete)});
    QCOMPARE (h.trace, trace);
    h.reception.context.myCall = "K1ABC";
    h.reception.context.periodFrequency = 7000000;
    h.reception.context.band = "40m";
    h.reception.anchor = firstAnchor.addDays (2);
    h.reception.displayAnchor = firstAnchor.addDays (3);
    auto complete = update (1, "CQ K1ABC FN42", Terminal::Complete, 1500.6f, -3);
    complete.startSeconds = 9;
    h.apply ({complete});
    QCOMPARE (h.logContexts.size (), 1);
    QCOMPARE (h.logContexts.front ().sequenceStart, firstAnchor.addMSecs (2250));
    QCOMPARE (h.logContexts.front ().band, QString {"20m"});
    QCOMPARE (h.logContexts.front ().myCall, QString {"N0ME"});
    QCOMPARE (h.logs.front (), QString {"1501  -3  CQ K1ABC FN42"});
    QCOMPARE (h.presentations.last ().startUtc, h.reception.displayAnchor.addSecs (9));
    QCOMPARE (h.spots.size (), 1);
    auto const& spot = h.spots.front ();
    QCOMPARE (spot.sender, QString {"K1ABC"});
    QCOMPARE (spot.grid, QString {"FN42"});
    QCOMPARE (spot.frequency, Radio::Frequency {14079501});
    QCOMPARE (spot.mode, QString {"JTTY"});
    QCOMPARE (spot.snr, -3);
    QCOMPARE (spot.time, firstAnchor.addMSecs (2250));
    QCOMPARE (spot.time.timeSpec (), Qt::UTC);
    QVERIFY (h.all.toPlainText ().contains ("cq k1abc fn42"));
  }

  void reportSyntax_data ()
  {
    QTest::addColumn<QString> ("text");
    QTest::addColumn<bool> ("eligible");
    QTest::addColumn<QString> ("grid");
    QTest::newRow ("cq") << QString {"cq  k1abc fn42ab"} << true << QString {"fn42ab"};
    QTest::newRow ("de") << QString {"De K1ABC"} << true << QString {};
    QTest::newRow ("call pair") << QString {"W9XYZ K1ABC FN42"} << true << QString {"FN42"};
    QTest::newRow ("portable") << QString {"CQ K1ABC/P FN42"} << true << QString {"FN42"};
    QTest::newRow ("invalid grid") << QString {"CQ K1ABC ZZ99"} << true << QString {};
    QTest::newRow ("rr73") << QString {"CQ K1ABC RR73"} << true << QString {};
    QTest::newRow ("free text") << QString {"HELLO K1ABC FN42"} << false << QString {};
    QTest::newRow ("invalid sender") << QString {"CQ EA8/K1ABC FN42"} << false << QString {};
    QTest::newRow ("self") << QString {"CQ n0me FN42"} << false << QString {};
    QTest::newRow ("missing sender") << QString {"CQ"} << false << QString {};
  }

  void reportSyntax ()
  {
    QFETCH (QString, text);
    QFETCH (bool, eligible);
    QFETCH (QString, grid);
    Harness h;
    h.apply ({update (1, text, Terminal::Complete)});
    QCOMPARE (h.spots.size (), int (eligible));
    if (eligible) QCOMPARE (h.spots.front ().grid, grid);
    h.reporting = false;
    h.apply ({update (2, text, Terminal::Complete)});
    QCOMPARE (h.spots.size (), int (eligible));
    h.reporting = true;
    h.reception.anchor = {};
    h.apply ({update (3, text, Terminal::Complete)});
    QCOMPARE (h.spots.size (), int (eligible));
  }

  void snapshotsAndEffectsFollowSynchronousPhases ()
  {
    Harness h;
    h.selection.center = 1700;
    h.reporting = false;
    h.after = [&h] (QString const& event) {
      if (event == "all") { h.selection.center = 1500; h.display.lowerCase = true; }
      if (event == "qso") h.echo.lowerCase = true;
      if (event == "log") h.reporting = true;
      if (event == "spot")
        QCOMPARE (h.controller.snrForSelectedWord ("K1ABC", Pane::All), -10);
    };
    h.apply ({update (1, "CQ K1ABC FN42", Terminal::Complete)});
    QCOMPARE (h.trace, QStringList ({"display", "all", "selection", "qso", "echo-options",
                                    "echo", "recording", "log", "reporting", "spot"}));
    QVERIFY (h.qso.toPlainText ().contains ("CQ K1ABC FN42"));
    QCOMPARE (h.echoes, QStringList {"\r\ncq k1abc fn42"});
    QCOMPARE (h.spots.size (), 1);
    QCOMPARE (h.controller.snrForSelectedWord ("K1ABC", Pane::All), -7);
  }

  void echoDeltaPolicy ()
  {
    Harness h;
    h.apply ({update (1, "FIRST"), update (1, "FIRST LONGER"), update (1, "FIRST LONGER"),
              update (1, "REPLACED")});
    QCOMPARE (h.echoes, QStringList ({"\r\nFIRST", " LONGER", "\r\nREPLACED"}));
    h.echo.enabled = false;
    h.apply ({update (1, "REPLACED SILENT")});
    QCOMPARE (h.echoes.size (), 3);
    h.echo = {true, true};
    h.apply ({update (1, "REPLACED SILENT END", Terminal::Complete)});
    QCOMPARE (h.echoes.last (), QString {" end"});
  }

  void replayResetAndRefreshPreserveLiveHistory ()
  {
    Harness h;
    h.apply ({update (1, "LIVE")});
    h.reception.source = Source::Review;
    h.reception.displayGroup = 2;
    h.apply ({update (2, {})});
    QCOMPARE (h.trace.count ("heading-all"), 0);
    h.apply ({update (2, "REVIEW MESSAGE", Terminal::Complete)});
    QCOMPARE (h.trace.count ("heading-all"), 1);
    QVERIFY (h.trace.indexOf ("heading-all") < h.trace.indexOf ("heading-qso"));
    h.reception.displayGroup = 3;
    h.apply ({update (3, "LATER REVIEW", Terminal::Complete)});
    QCOMPARE (h.trace.count ("heading-all"), 1);
    h.controller.resetReplay ();
    h.reception.displayGroup = 4;
    h.apply ({update (4, "RESET REVIEW", Terminal::Complete)});
    QCOMPARE (h.trace.count ("heading-all"), 2);
    h.reception.source = Source::Wav;
    h.reception.displayGroup = 5;
    h.apply ({update (5, "WAV", Terminal::Complete)});
    QCOMPARE (h.trace.count ("heading-all"), 2);
    h.reception.source = Source::Live;
    h.reception.displayGroup = 1;
    h.apply ({update (1, "LIVE COMPLETE", Terminal::Complete)});
    QCOMPARE (h.all.toPlainText ().count ("LIVE"), 1);
    QCOMPARE (h.logs.size (), 2);
    QCOMPARE (h.intervals.size (), 2);
    auto const trace = h.trace;
    auto const original = h.all.toPlainText ();
    JttyReceiveLine::refresh (h.all, {true, true}, h.font);
    JttyReceiveLine::refresh (h.qso, {true, true}, h.font);
    QVERIFY (h.all.toPlainText ().contains ("live complete"));
    JttyReceiveLine::refresh (h.all, {}, h.font);
    QCOMPARE (h.all.toPlainText (), original);
    QCOMPARE (h.trace, trace);
    QCOMPARE (h.controller.snrForSelectedWord ("MESSAGE", Pane::All), -7);
  }

  void clearedActiveHandlesDoNotResurrectMessages ()
  {
    Harness h;
    h.apply ({update (1, "REMOVED")});
    h.all.clear ();
    h.qso.clear ();
    h.apply ({update (2, "RETAINED"), update (1, "REMOVED COMPLETE", Terminal::Complete)});
    QCOMPARE (h.all.toPlainText (), QString {"1500  -7  RETAINED"});
    QCOMPARE (h.qso.toPlainText (), h.all.toPlainText ());
    QCOMPARE (h.logs.size (), 1);
    QVERIFY (h.logs.front ().contains ("REMOVED COMPLETE"));
  }

  void replayResetRetiresOnlyReplayActiveRecords ()
  {
    Harness h;
    h.apply ({update (1, "LIVE")});
    h.reception.source = Source::Review;
    h.reception.displayGroup = 2;
    h.apply ({update (1, "REPLAY")});
    QCOMPARE (h.all.toPlainText ().count ("LIVE"), 1);
    QCOMPARE (h.all.toPlainText ().count ("REPLAY"), 1);
    h.controller.resetReplay ();
    h.reception.source = Source::Wav;
    h.reception.displayGroup = 3;
    h.reception.context.band = "40m";
    h.apply ({update (1, "WAV", Terminal::Complete)});
    QCOMPARE (h.logContexts.front ().band, QString {"40m"});
    QVERIFY (h.all.toPlainText ().contains ("REPLAY"));
    QVERIFY (h.all.toPlainText ().contains ("WAV"));
    h.reception.source = Source::Live;
    h.reception.displayGroup = 1;
    h.apply ({update (1, "LIVE COMPLETE", Terminal::Complete)});
    QCOMPARE (h.logContexts.last ().band, QString {"20m"});
    QCOMPARE (h.all.toPlainText ().count ("LIVE"), 1);
    QVERIFY (h.all.toPlainText ().contains ("LIVE COMPLETE"));
  }

  void snrLookupTracksEveryUpdateAndEvictsOldEntries ()
  {
    Harness h;
    h.apply ({update (1, "CQ TOKEN", Terminal::Growing, 1500, -2)});
    h.apply ({update (1, "CQ TOKEN", Terminal::Complete, 1500, -3)});
    h.selection.center = 1700;
    h.apply ({update (2, "CQ TOKEN", Terminal::Complete, 1500, -4)});
    QCOMPARE (h.controller.snrForSelectedWord ("TOKEN", Pane::All), -4);
    QCOMPARE (h.controller.snrForSelectedWord ("TOKEN", Pane::Qso), -3);
    QCOMPARE (h.controller.snrForSelectedWord ("token", Pane::All), -10);
    QCOMPARE (h.controller.snrForSelectedWord ("TOK", Pane::All), -10);
    h.reception.source = Source::Review;
    h.apply ({update (3, "REVIEWTOKEN", Terminal::Complete, 1500, -5)});
    h.controller.resetReplay ();
    h.reception.source = Source::Wav;
    h.apply ({update (4, "WAVTOKEN", Terminal::Complete, 1500, -6)});
    QCOMPARE (h.controller.snrForSelectedWord ("REVIEWTOKEN", Pane::Qso), -5);
    QCOMPARE (h.controller.snrForSelectedWord ("WAVTOKEN", Pane::All), -6);
    h.apply ({update (5, "EVICT", Terminal::Complete, 1500, -8)});
    for (int i = 0; i < 499; ++i)
      h.apply ({update (6, "FILLER", Terminal::Growing, 1500, i)});
    QCOMPARE (h.controller.snrForSelectedWord ("EVICT", Pane::All), -8);
    h.apply ({update (6, "FILLER", Terminal::Complete, 1500, 499)});
    QCOMPARE (h.controller.snrForSelectedWord ("EVICT", Pane::All), -10);
    QCOMPARE (h.controller.snrForSelectedWord ("FILLER", Pane::All), 499);
  }

  void snrLookupMatchesSlashedCallWithAttachedPunctuation ()
  {
    Harness h;
    h.apply ({update (1, "CQ (N0LL/P),: 599", Terminal::Complete, 1500, -11)});

    QCOMPARE (h.controller.snrForSelectedWord ("N0LL/P", Pane::All), -11);
    QCOMPARE (h.controller.snrForSelectedWord ("(N0LL/P),:", Pane::All), -11);
  }
};

QTEST_MAIN (TestJttyReceiveResults)
#include "test_jtty_receive_results.moc"
