#include <QtTest>

#include "widgets/WaterfallCalls.hpp"

using namespace WaterfallCalls;

class TestJttyWaterfallCalls final : public QObject
{
  Q_OBJECT
private slots:
  void callsignRule ()
  {
    QVERIFY (isCallsign ("DL3WDG"));
    QVERIFY (isCallsign ("K1ABC"));
    QVERIFY (isCallsign ("W3SZ/P"));
    QVERIFY (isCallsign ("9A1A"));
    QVERIFY (!isCallsign ("CQ"));
    QVERIFY (!isCallsign ("DX"));
    QVERIFY (!isCallsign ("HELLO"));       // no digit
    QVERIFY (!isCallsign ("599"));         // no letter
    QVERIFY (!isCallsign ("JN68"));        // a grid square
    QVERIFY (!isCallsign ("TU"));
    QVERIFY (!isCallsign ("20M"));         // a band
    QVERIFY (!isCallsign ("40M"));
    QVERIFY (!isCallsign ("160M"));
    QVERIFY (!isCallsign ("70CM"));
    QVERIFY (!isCallsign ("100W"));        // a power
    QVERIFY (!isCallsign ("1KW"));
    QVERIFY (!isCallsign ("5NN"));         // a report
    QVERIFY (isCallsign ("2E0ABC"));       // real calls that start like them
    QVERIFY (isCallsign ("4X4AA"));
    QVERIFY (isCallsign ("3W5A"));
  }

  void onlyCqAndColonLinesAreLabelled ()
  {
    // a CQ: the first call after CQ (or CQ DX)
    QCOMPARE (jttyCall ("CQ K1ABC FN20", false, "DL3WDG").call, QString ("K1ABC"));
    QCOMPARE (jttyCall ("CQ DX K1ABC FN20", false, "DL3WDG").call, QString ("K1ABC"));
    QCOMPARE (jttyCall ("cq k1abc cq", false, "DL3WDG").call, QString ("K1ABC"));
    QCOMPARE (jttyCall ("CQ <K1ABC> FN20", false, "DL3WDG").call, QString ("K1ABC"));
    QCOMPARE (int (jttyCall ("CQ K1ABC", false, "DL3WDG").kind), int (Kind::Cq));
    // "CALL: text"
    QCOMPARE (jttyCall ("W1AW: HELLO THERE", false, "DL3WDG").call, QString ("W1AW"));
    QCOMPARE (int (jttyCall ("W1AW: HELLO THERE", false, "DL3WDG").kind), int (Kind::Other));
    // a message that merely names a call is not labelled
    QVERIFY (!jttyCall ("G4ABC DL3WDG JN68", false, "DL3WDG").valid ());
    QVERIFY (!jttyCall ("THE QUICK BROWN FOX K1ABC", false, "DL3WDG").valid ());
    QVERIFY (!jttyCall ("", false, "DL3WDG").valid ());
    QVERIFY (!jttyCall ("CQ", false, "DL3WDG").valid ());
    QVERIFY (!jttyCall ("CQ CQ CQ", false, "DL3WDG").valid ());
  }

  void aCutOffCallIsNeverShown ()
  {
    // growing: the call is the last word, it may still be cut off
    QVERIFY (!jttyCall ("CQ K1AB", true, "DL3WDG").valid ());
    QVERIFY (!jttyCall ("CQ K1ABC", true, "DL3WDG").valid ());
    QCOMPARE (jttyCall ("CQ K1ABC F", true, "DL3WDG").call, QString ("K1ABC"));
    QCOMPARE (jttyCall ("CQ K1ABC", false, "DL3WDG").call, QString ("K1ABC"));
    QVERIFY (!jttyCall ("W1AW:", true, "DL3WDG").valid ());
    QVERIFY (!jttyCall ("W1AW: ", true, "DL3WDG").valid ());
    QCOMPARE (jttyCall ("W1AW: H", true, "DL3WDG").call, QString ("W1AW"));
    QCOMPARE (jttyCall ("W1AW:", false, "DL3WDG").call, QString ("W1AW"));
  }

  void kindsOfMyOwnAndMyCall ()
  {
    QCOMPARE (int (jttyCall ("CQ DL3WDG CQ", false, "DL3WDG").kind), int (Kind::Own));
    QCOMPARE (int (jttyCall ("CQ DL3WDG CQ", false, "dl3wdg").kind), int (Kind::Own));
    QCOMPARE (int (jttyCall ("K1ABC: DL3WDG HELLO", false, "DL3WDG").kind), int (Kind::Me));
    QCOMPARE (int (jttyCall ("K1ABC: HELLO", false, "DL3WDG").kind), int (Kind::Other));
    QCOMPARE (int (jttyCall ("DL3WDG: TEST", false, "DL3WDG").kind), int (Kind::Own));
    // a portable or visiting form of my call is still me
    QCOMPARE (int (jttyCall ("CQ DL3WDG/P CQ", false, "DL3WDG").kind), int (Kind::Own));
    QCOMPARE (int (jttyCall ("CQ EA8/DL3WDG CQ", false, "DL3WDG").kind), int (Kind::Own));
    QCOMPARE (int (jttyCall ("CQ DL3WDG CQ", false, "DL3WDG/P").kind), int (Kind::Own));
    QCOMPARE (int (jttyCall ("K1ABC: DL3WDG/P HELLO", false, "DL3WDG").kind), int (Kind::Me));
    QCOMPARE (int (jttyCall ("CQ DL3WDGX CQ", false, "DL3WDG").kind), int (Kind::Cq));
    // a band or report word after CQ is not the call
    QCOMPARE (jttyCall ("CQ 20M DL3WDG", false, "K1ABC").call, QString ("DL3WDG"));
    QCOMPARE (jttyCall ("CQ 5NN K1ABC FN20", false, "DL3WDG").call, QString ("K1ABC"));
    QVERIFY (!jttyCall ("CQ 40M", false, "DL3WDG").valid ());
    // no call set: nothing is "mine"
    QCOMPARE (int (jttyCall ("CQ DL3WDG CQ", false, "").kind), int (Kind::Cq));
  }

  void aGrowingMessageReplacesItsOwnLabel ()
  {
    Board board;
    qint64 t = 1000000;
    QVERIFY (!board.update (7, "CQ K1AB", 1000, true, "DL3WDG", t));       // nothing yet
    QVERIFY (board.labels (t).isEmpty ());
    QVERIFY (board.update (7, "CQ K1ABC F", 1000, true, "DL3WDG", t + 1000));
    QCOMPARE (board.labels (t + 1000).size (), 1);
    QVERIFY (board.update (7, "CQ K1ABC FN20", 1002, false, "DL3WDG", t + 2000));
    auto const labels = board.labels (t + 2000);
    QCOMPARE (labels.size (), 1);                      // replaced, not added
    QCOMPARE (labels[0].call, QString ("K1ABC"));
    QCOMPARE (labels[0].freq, 1002.f);
    QCOMPARE (labels[0].width, jttySignalWidth);
    // later text that no longer gives a call leaves the label alone
    QVERIFY (!board.update (7, "CQ K1ABC FN20 MORE", 1002, true, "DL3WDG", t + 3000)
             || board.labels (t + 3000).size () == 1);
    QCOMPARE (board.labels (t + 3000).size (), 1);
  }

  void labelsExpireSixtySecondsAfterTheLastUpdate ()
  {
    Board board;
    qint64 const t = 5000000;
    board.update (1, "CQ K1ABC FN20", 1000, false, "DL3WDG", t);
    board.update (2, "CQ K2DEF FN20", 1300, false, "DL3WDG", t + 30000);
    QCOMPARE (board.labels (t + 59000).size (), 2);
    QCOMPARE (board.labels (t + 61000).size (), 1);    // first one gone
    QCOMPARE (board.labels (t + 61000)[0].call, QString ("K2DEF"));
    // an update restarts the 60 s
    board.update (2, "CQ K2DEF FN20", 1300, false, "DL3WDG", t + 80000);
    QCOMPARE (board.labels (t + 139000).size (), 1);
    QCOMPARE (board.labels (t + 141000).size (), 0);
    QVERIFY (board.isEmpty ());
  }

  void priorityAndOneLabelPerCall ()
  {
    Board board;
    qint64 const t = 9000000;
    board.update (1, "W1AW: HELLO", 1000, false, "DL3WDG", t);               // other
    board.update (2, "CQ K1ABC FN20", 1200, false, "DL3WDG", t + 10);        // CQ
    board.update (3, "CQ DL3WDG CQ", 1400, false, "DL3WDG", t + 20);         // own
    board.update (4, "G4ABC: DL3WDG HI", 1600, false, "DL3WDG", t + 30);     // to me
    board.update (5, "CQ K2DEF FN20", 1800, false, "DL3WDG", t + 40);        // newer CQ
    board.update (6, "CQ K1ABC FN20", 2000, false, "DL3WDG", t + 50);        // K1ABC again
    auto const labels = board.labels (t + 100);
    QStringList calls;
    for (auto const& l : labels) calls << l.call;
    QCOMPARE (calls, (QStringList {"G4ABC", "DL3WDG", "K1ABC", "K2DEF", "W1AW"}));
    QCOMPARE (labels[2].freq, 2000.f);                // the newest K1ABC
  }

  void competingLabelsDoNotSwapWhenTheirMessagesAreUpdated ()
  {
    // two CQs 50 Hz apart: their labels overlap, one has to give way
    Board board;
    qint64 const t = 9000000;
    board.update (1, "CQ DL3ABC CQ", 1540, false, "DL3WDG", t);
    board.update (2, "CQ K1JT CQ", 1590, false, "DL3WDG", t + 10);
    auto order = [&board] (qint64 now) {
      QStringList calls;
      for (auto const& l : board.labels (now)) calls << l.call;
      return calls;
    };
    auto const first = order (t + 20);
    QCOMPARE (first, (QStringList {"DL3ABC", "K1JT"}));
    for (int i = 1; i <= 6; ++i) {                  // each message updated again and again
      board.update (1 + (i & 1), i & 1 ? "CQ K1JT CQ" : "CQ DL3ABC CQ",
                    i & 1 ? 1590 : 1540, false, "DL3WDG", t + 1000 * i);
      QCOMPARE (order (t + 1000 * i + 1), first);
    }
  }

  void aLabelIsCentredBetweenToneOneAndToneTwo ()
  {
    // freq is the lowest tone (tone 0); tones are 31.25 Hz apart, so the middle
    // of the signal is 46.875 Hz above it, not 63.5 Hz (half the 127 Hz
    // occupied bandwidth)
    Label l;
    l.call = "K1ABC"; l.freq = 1000; l.width = jttySignalWidth;
    auto const placed = place ({l}, [] (double f) {return f;},
                               [] (QString const&) {return 20.0;}, 5000);
    QCOMPARE (placed.size (), 1);
    QCOMPARE (placed[0].xCentre, 1000.0 + 1.5 * 31.25);
  }

  void placementTwoRowsOverlapAndEdges ()
  {
    auto const x = [] (double f) {return f / 5.0;};    // 5 Hz per pixel
    auto const text = [] (QString const&) {return 60.0;};
    auto mk = [] (QString const& call, float freq) {
      Label l; l.call = call; l.freq = freq; l.width = jttySignalWidth; return l;
    };
    // 1000, 1150, 1300, 1540 Hz: 150 Hz = 30 px apart, labels 60 px wide:
    // alternate rows, the 3rd would overlap the 1st in row 0 only if
    // closer than 64 px; 300 Hz = 60 px < 64, so it goes to row 1 ...
    QVector<Label> labels {mk ("A1AAA", 1000), mk ("B2BBB", 1150), mk ("C3CCC", 1300),
                           mk ("D4DDD", 1540)};
    auto placed = place (labels, x, text, 1000);
    QCOMPARE (placed.size (), 3);                      // C3CCC fits neither row
    QCOMPARE (placed[0].index, 0);
    QCOMPARE (placed[0].row, 0);
    QCOMPARE (placed[1].index, 1);
    QCOMPARE (placed[1].row, 1);
    QCOMPARE (placed[2].index, 3);                     // 1540 Hz is clear of both
    QCOMPARE (placed[2].row, 0);
    // priority: the first in the list wins a crowded spot
    placed = place ({mk ("X1XXX", 1000), mk ("Y2YYY", 1005), mk ("Z3ZZZ", 1010)},
                    x, text, 1000);
    QCOMPARE (placed.size (), 2);
    QCOMPARE (placed[0].index, 0);
    QCOMPARE (placed[1].index, 1);
    // off the strip: left out; at an edge: pushed back in
    placed = place ({mk ("L1LLL", -2000), mk ("E2EEE", 0), mk ("R3RRR", 4900)},
                    x, text, 1000);
    QCOMPARE (placed.size (), 2);
    QCOMPARE (placed[0].index, 1);
    QVERIFY (placed[0].xCentre - 30.0 >= -1e-6);
    QCOMPARE (placed[1].index, 2);
    QVERIFY (placed[1].xCentre + 30.0 <= 1000.0 + 1e-6);
  }
};

QTEST_MAIN (TestJttyWaterfallCalls)
#include "test_jtty_waterfall_calls.moc"
