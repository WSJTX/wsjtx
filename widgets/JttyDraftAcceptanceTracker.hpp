#ifndef JTTY_DRAFT_ACCEPTANCE_TRACKER_HPP_
#define JTTY_DRAFT_ACCEPTANCE_TRACKER_HPP_

#include <QHash>

class JttyDraftAcceptanceTracker
{
public:
  void noteDraftChanged () noexcept
  {
    ++revision_;
  }

  void trackSubmission (qint64 requestId)
  {
    submissions_.insert (requestId, revision_);
  }

  bool accept (qint64 requestId)
  {
    auto const it = submissions_.find (requestId);
    if (it == submissions_.end ()) {
      return false;
    }

    auto const submittedRevision = it.value ();
    submissions_.erase (it);
    return submittedRevision == revision_;
  }

  void reject (qint64 requestId)
  {
    submissions_.remove (requestId);
  }

private:
  quint64 revision_ {0};
  QHash<qint64, quint64> submissions_;
};

#endif
