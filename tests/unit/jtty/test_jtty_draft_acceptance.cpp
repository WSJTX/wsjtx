#include <QtTest>

#include "widgets/JttyDraftAcceptanceTracker.hpp"

class TestJttyDraftAcceptance final
  : public QObject
{
  Q_OBJECT

private slots:
  void acceptedCurrentSubmissionClearsDraft ()
  {
    JttyDraftAcceptanceTracker tracker;
    tracker.trackSubmission (1);

    QVERIFY (tracker.accept (1));
  }

  void rejectedSubmissionPreservesDraft ()
  {
    JttyDraftAcceptanceTracker tracker;
    tracker.trackSubmission (1);
    tracker.reject (1);

    QVERIFY (!tracker.accept (1));
  }

  void delayedAcceptancePreservesNewerDraft ()
  {
    JttyDraftAcceptanceTracker tracker;
    tracker.trackSubmission (1);
    tracker.noteDraftChanged ();

    QVERIFY (!tracker.accept (1));
  }

  void identicalRetypedDraftHasNewIdentity ()
  {
    JttyDraftAcceptanceTracker tracker;
    tracker.trackSubmission (1);
    tracker.noteDraftChanged ();
    tracker.noteDraftChanged ();

    QVERIFY (!tracker.accept (1));
  }

  void unrelatedAcceptancePreservesDraft ()
  {
    JttyDraftAcceptanceTracker tracker;

    QVERIFY (!tracker.accept (1));
  }

  void successiveAcceptedDraftsCanClear ()
  {
    JttyDraftAcceptanceTracker tracker;
    tracker.trackSubmission (1);
    QVERIFY (tracker.accept (1));

    tracker.noteDraftChanged ();
    tracker.noteDraftChanged ();
    tracker.trackSubmission (2);
    QVERIFY (tracker.accept (2));
  }

  void olderAcceptanceDoesNotClearSuccessiveDraft ()
  {
    JttyDraftAcceptanceTracker tracker;
    tracker.trackSubmission (1);
    tracker.noteDraftChanged ();
    tracker.trackSubmission (2);

    QVERIFY (!tracker.accept (1));
    QVERIFY (tracker.accept (2));
  }
};

QTEST_GUILESS_MAIN (TestJttyDraftAcceptance)

#include "test_jtty_draft_acceptance.moc"
