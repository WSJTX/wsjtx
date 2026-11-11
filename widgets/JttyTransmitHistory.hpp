// -*- Mode: C++ -*-
#ifndef JTTY_TRANSMIT_HISTORY_HPP_
#define JTTY_TRANSMIT_HISTORY_HPP_

#include <QString>
#include <QStringList>

namespace Jtty
{
  // Recent overs, recalled into the Tx box with Ctrl+Up / Ctrl+Down like a
  // shell history. Stepping down past the newest gives back the draft that
  // was there before recall began.
  class TransmitHistory
  {
  public:
    static constexpr int capacity = 20;

    void add (QString const& over)
    {
      QString const text = over.simplified ();
      if (text.isEmpty ()) return;
      if (m_overs.isEmpty () || m_overs.constLast () != text) m_overs.append (text);
      while (m_overs.size () > capacity) m_overs.removeFirst ();
      endRecall ();
    }

    // step -1 for older, +1 for newer. Returns false, leaving *text alone,
    // when there is nothing further that way.
    bool recall (int step, QString const& draft, QString * text)
    {
      if (m_overs.isEmpty ()) return false;
      int const newest = m_overs.size ();
      int const from = m_index < 0 ? newest : m_index;
      int const to = qBound (0, from + (step < 0 ? -1 : 1), newest);
      if (to == from) return false;
      if (m_index < 0) m_draft = draft;
      m_index = to == newest ? -1 : to;
      *text = m_index < 0 ? m_draft : m_overs.at (m_index);
      return true;
    }

    // Any edit other than a recall ends it; the next Ctrl+Up starts afresh.
    void endRecall () { m_index = -1; }

    QStringList const& overs () const { return m_overs; }

  private:
    QStringList m_overs;
    int m_index {-1};
    QString m_draft;
  };
}

#endif
