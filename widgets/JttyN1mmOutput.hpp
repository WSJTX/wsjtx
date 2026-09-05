// -*- Mode: C++ -*-
#ifndef JTTY_N1MM_OUTPUT_HPP
#define JTTY_N1MM_OUTPUT_HPP

#include <QSet>
#include <QtGlobal>

namespace Jtty
{
  class N1mmOutput
  {
  public:
    void submit (qint64 requestId)
    {
      requests_.insert (requestId);
      completionPending_ = true;
    }

    bool accept (qint64 requestId)
    {
      if (!requests_.contains (requestId)) return false;
      acceptedAudio_ = true;
      return true;
    }
    bool resolve (qint64 requestId) { return requests_.remove (requestId) != 0; }
    bool pending () const { return completionPending_; }
    bool finishRequested () const { return finishRequested_; }
    bool startRequested () const { return startRequested_; }
    void start () { finishRequested_ = false; startRequested_ = true; }
    void started () { startRequested_ = false; }
    void finish ()
    {
      if (completionPending_) finishRequested_ = true;
      if (requests_.isEmpty ()) startRequested_ = false;
    }
    void abort () { *this = N1mmOutput {}; }

    bool takeCompletion (bool audioActive, bool drained = false)
    {
      // Rejections can precede more TXTEXT in the same transaction. OFF
      // closes an empty transaction; accepted audio completes only at drain.
      if (!completionPending_ || !requests_.isEmpty () || audioActive
          || (!finishRequested_ && !(drained && acceptedAudio_))) return false;
      abort ();
      return true;
    }

  private:
    QSet<qint64> requests_;
    bool completionPending_ {false};
    bool finishRequested_ {false};
    bool startRequested_ {false};
    bool acceptedAudio_ {false};
  };
}

#endif
