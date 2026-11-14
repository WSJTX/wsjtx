// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include "BestSPController.hpp"

namespace
{
  using Controller = BestSPController;
  using Reason = Controller::Reason;

  QDateTime now ()
  {
    return QDateTime::fromString ("2026-11-06T12:00:01Z", Qt::ISODate);
  }

  Controller::Request request (quint64 generation = 1, bool disk = false)
  {
    Controller::Request result;
    result.generation = generation;
    result.backendRequest = generation + 100;
    result.context.mode = "FT4";
    result.context.periodFrequency = 14080000;
    result.context.band = "20m";
    result.context.sequenceStart = now ().addSecs (-1);
    result.context.trPeriod = 7.5;
    result.context.diskData = disk;
    result.context.myCall = "K1ABC";
    return result;
  }

  DecodedText cq (QString const& call)
  {
    return DecodedText {"120000 -10  0.3 1900 +  CQ " + call + " FN31"};
  }

  Controller::Eligibility const callOnBand = [] (DecodedText const&, Controller::Policy const&)
    -> std::optional<Reason> {return Reason::CallBand;};

  Controller::Eligibility const noCandidate = [] (DecodedText const&, Controller::Policy const&)
    -> std::optional<Reason> {return {};};

  std::optional<Controller::Selection> complete (
    Controller& controller, Controller::Request const& current,
    Controller::Eligibility const& eligibility = callOnBand,
    Controller::Policy const& policy = {})
  {
    return controller.complete (current, now (), policy, eligibility);
  }
}

class TestBestSPController final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void lateDxccWinsWithoutEarlySelection ()
  {
    Controller controller;
    auto const current = request ();
    controller.arm (now ());
    controller.beginRequest (current);
    controller.observe (current, cq ("W1AW"));
    controller.observe (current, cq ("JA1ABC"));
    QVERIFY (controller.armed ());
    QVERIFY (controller.activeRequest ());
    auto selected = complete (controller, current,
      [] (DecodedText const& message, Controller::Policy const&) -> std::optional<Reason> {
        return parseDecodedMessage (message.message ()).sender == "JA1ABC"
          ? Reason::NewDXCC : Reason::CallBand;
      });
    QVERIFY (selected);
    QCOMPARE (selected->message.string (), cq ("JA1ABC").string ());
    QCOMPARE (selected->reason, Reason::NewDXCC);
    QVERIFY (!controller.armed ());
    QVERIFY (!controller.activeRequest ());
  }

  void tiesRetainFirstObserved ()
  {
    Controller controller;
    auto const current = request ();
    controller.arm (now ());
    controller.beginRequest (current);
    controller.observe (current, cq ("W1AW"));
    controller.observe (current, cq ("W9XYZ"));
    auto const selected = complete (controller, current);
    QVERIFY (selected);
    QCOMPARE (selected->message.string (), cq ("W1AW").string ());
  }

  void completionReevaluatesEveryObservation ()
  {
    Controller controller;
    auto const current = request ();
    controller.beginRequest (current);
    controller.observe (current, cq ("JA1ABC"));
    controller.observe (current, cq ("W1AW"));
    controller.arm (now ());
    QStringList evaluated;
    auto const selected = complete (controller, current,
      [&evaluated] (DecodedText const& message, Controller::Policy const&) -> std::optional<Reason> {
        auto const call = parseDecodedMessage (message.message ()).sender;
        evaluated.append (call);
        if (call == "JA1ABC") return {};
        return Reason::CallBand;
      });
    QCOMPARE (evaluated, QStringList ({"JA1ABC", "W1AW"}));
    QVERIFY (selected);
    QCOMPARE (selected->message.string (), cq ("W1AW").string ());
  }

  void completionUsesCurrentPolicyAndLogbook ()
  {
    Controller controller;
    auto const current = request ();
    controller.arm (now ());
    controller.beginRequest (current);
    controller.observe (current, cq ("JA1ABC"));
    controller.observe (current, cq ("W1AW"));
    Controller::Policy policy;
    policy.newDxcc = false;
    bool firstCallNowWorked = true;
    auto const selected = complete (controller, current,
      [&firstCallNowWorked] (DecodedText const& message, Controller::Policy const& currentPolicy)
        -> std::optional<Reason> {
        auto const call = parseDecodedMessage (message.message ()).sender;
        if (call == "JA1ABC" && firstCallNowWorked) return {};
        if (currentPolicy.newDxcc) return Reason::NewDXCC;
        return Reason::CallBand;
      }, policy);
    QVERIFY (selected);
    QCOMPARE (selected->message.string (), cq ("W1AW").string ());
    QCOMPARE (selected->reason, Reason::CallBand);
  }

  void disabledCategoriesCannotSelect ()
  {
    Controller controller;
    auto const current = request ();
    controller.arm (now ());
    controller.beginRequest (current);
    controller.observe (current, cq ("W1AW"));
    QVERIFY (!complete (controller, current, callOnBand, {false, false}));
    QVERIFY (controller.armed ());
  }

  void staleAndDuplicateCompletions ()
  {
    Controller controller;
    auto const current = request (2);
    controller.arm (now ());
    controller.beginRequest (current);
    controller.observe (request (1), cq ("JA1ABC"));
    controller.observe (current, cq ("W1AW"));
    QVERIFY (!complete (controller, request (1)));
    QVERIFY (controller.activeRequest ());
    auto const selected = complete (controller, current);
    QVERIFY (selected);
    QCOMPARE (selected->message.string (), cq ("W1AW").string ());
    QVERIFY (!complete (controller, current));
  }

  void requestIdentityChangesRejectCompletion_data ()
  {
    QTest::addColumn<QString> ("field");
    for (auto const& field : {"generation", "backend", "mode", "frequency", "band",
                             "sequence", "disk", "callsign", "period", "special operation"})
      QTest::newRow (field) << QString {field};
  }

  void requestIdentityChangesRejectCompletion ()
  {
    QFETCH (QString, field);
    Controller controller;
    auto const current = request ();
    auto changed = current;
    if (field == "generation") ++changed.generation;
    else if (field == "backend") ++changed.backendRequest;
    else if (field == "mode") changed.context.mode = "FT8";
    else if (field == "frequency") ++changed.context.periodFrequency;
    else if (field == "band") changed.context.band = "17m";
    else if (field == "sequence") changed.context.sequenceStart = now ().addSecs (7);
    else if (field == "disk") changed.context.diskData = true;
    else if (field == "callsign") changed.context.myCall = "W9XYZ";
    else if (field == "period") changed.context.trPeriod = 15;
    else changed.context.specOp = SpecialOperatingActivity::ARRL_DIGI;
    controller.arm (now ());
    controller.beginRequest (current);
    controller.observe (current, cq ("W1AW"));
    QVERIFY (!complete (controller, changed));
    QVERIFY (controller.activeRequest ());
  }

  void pendingIsConsumedBeforeCallbacks ()
  {
    Controller controller;
    auto const current = request ();
    controller.arm (now ());
    controller.beginRequest (current);
    controller.observe (current, cq ("W1AW"));
    bool consumed = false;
    bool duplicateRejected = false;
    auto const selected = complete (controller, current,
      [&] (DecodedText const&, Controller::Policy const&) -> std::optional<Reason> {
        consumed = !controller.activeRequest ();
        duplicateRejected = !complete (controller, current);
        return Reason::CallBand;
      });
    QVERIFY (consumed);
    QVERIFY (duplicateRejected);
    QVERIFY (selected);
  }

  void cancelDuringEvaluationInvalidatesWinner ()
  {
    Controller controller;
    auto const current = request ();
    controller.arm (now ());
    controller.beginRequest (current);
    controller.observe (current, cq ("W1AW"));
    QVERIFY (!complete (controller, current,
      [&] (DecodedText const&, Controller::Policy const&) -> std::optional<Reason> {
        controller.cancel ();
        controller.arm (now ());
        return Reason::CallBand;
      }));
    QVERIFY (controller.armed ());
  }

  void explicitCancellationAndRearming ()
  {
    Controller controller;
    auto const current = request ();
    controller.arm (now ());
    controller.beginRequest (current);
    controller.observe (current, cq ("W1AW"));
    controller.cancel ();
    QVERIFY (!controller.armed ());
    QVERIFY (!complete (controller, current));
    controller.arm (now ());
    controller.beginRequest (current);
    controller.observe (current, cq ("W1AW"));
    QVERIFY (complete (controller, current));
  }

  void timeoutPreventsCompletion ()
  {
    Controller controller;
    auto const current = request ();
    controller.arm (now ());
    controller.beginRequest (current);
    controller.observe (current, cq ("W1AW"));
    QVERIFY (!controller.timeout (now ().addMSecs (119999)));
    QVERIFY (!controller.complete (current, now ().addSecs (120), {}, callOnBand));
    QVERIFY (!controller.armed ());
    QVERIFY (!controller.timeout (now ().addSecs (121)));
  }

  void rearmDuringRequestRetainsAllObservations ()
  {
    Controller controller;
    auto const current = request ();
    controller.beginRequest (current);
    controller.observe (current, cq ("W1AW"));
    controller.arm (now ());
    controller.cancel ();
    controller.observe (current, cq ("JA1ABC"));
    controller.arm (now ());
    auto const selected = complete (controller, current,
      [] (DecodedText const& message, Controller::Policy const&) -> std::optional<Reason> {
        return parseDecodedMessage (message.message ()).sender == "JA1ABC"
          ? Reason::NewDXCC : Reason::CallBand;
      });
    QVERIFY (selected);
    QCOMPARE (selected->message.string (), cq ("JA1ABC").string ());
  }

  void failurePreservesArmingIntent ()
  {
    Controller controller;
    auto const failed = request ();
    auto const next = request (2);
    controller.arm (now ());
    controller.beginRequest (failed);
    controller.observe (failed, cq ("JA1ABC"));
    controller.failRequest ();
    QVERIFY (controller.armed ());
    QVERIFY (!complete (controller, failed));
    controller.beginRequest (next);
    controller.observe (next, cq ("W1AW"));
    QVERIFY (complete (controller, next));
  }

  void redecodeCanFindFirstWinnerButCannotRepeatSelection ()
  {
    Controller controller;
    auto const first = request ();
    auto const repeated = request (2);
    controller.arm (now ());
    controller.beginRequest (first);
    controller.observe (first, cq ("W1AW"));
    QVERIFY (!complete (controller, first, noCandidate));
    controller.beginRequest (repeated);
    controller.observe (repeated, cq ("W1AW"));
    QVERIFY (complete (controller, repeated));
    QVERIFY (!controller.armed ());
    controller.beginRequest (request (3));
    controller.observe (request (3), cq ("JA1ABC"));
    QVERIFY (!complete (controller, request (3)));
    controller.arm (now ());
    controller.beginRequest (repeated);
    controller.observe (repeated, cq ("W1AW"));
    QVERIFY (complete (controller, repeated));
  }

  void nextLiveReceptionAndDiskRequestsAreIndependent ()
  {
    for (bool const disk : {false, true})
      {
        Controller controller;
        auto const first = request (1, disk);
        auto second = request (2, disk);
        if (!disk) second.context.sequenceStart = second.context.sequenceStart.addMSecs (7500);
        controller.arm (now ());
        controller.beginRequest (first);
        controller.observe (first, cq ("JA1ABC"));
        QVERIFY (!complete (controller, first, noCandidate));
        controller.beginRequest (second);
        controller.observe (second, cq ("W1AW"));
        auto const selected = complete (controller, second);
        QVERIFY (selected);
        QCOMPARE (selected->message.string (), cq ("W1AW").string ());
      }
  }

  void onlyStandardCqsParticipate ()
  {
    Controller controller;
    auto current = request ();
    controller.arm (now ());
    current.context.mode = "FT8";
    controller.beginRequest (current);
    QVERIFY (!controller.activeRequest ());
    current.context.mode = "FT4";
    controller.beginRequest (current);
    controller.observe (current, DecodedText {"malformed"});
    controller.observe (current, DecodedText {"120000 -10  0.3 1900 +  K1ABC W1AW -10"});
    controller.observe (current, DecodedText {"120000 -10  0.3 1900 +  HELLO WORLD"});
    int candidates = 0;
    QVERIFY (!complete (controller, current,
      [&] (DecodedText const&, Controller::Policy const&) -> std::optional<Reason> {
        ++candidates;
        return Reason::CallBand;
      }));
    QCOMPARE (candidates, 0);
  }
};

QTEST_GUILESS_MAIN (TestBestSPController)

#include "test_best_sp_controller.moc"
