// -*- Mode: C++ -*-
#ifndef JTTY_TRANSMIT_ECHO_HPP_
#define JTTY_TRANSMIT_ECHO_HPP_

#include <QHash>
#include <QPair>
#include <QString>
#include <QStringList>

#include <functional>

#include "JttyTransmitQueue.hpp"

namespace Jtty
{
  inline constexpr qint64 transmitFrameSamples = 59 * 1536;

  // Characters of segment.text carried by frames fully sent by servedSamples.
  // Without frame boundaries only a fully sent segment counts, so a shorthand
  // is never shown half sent.
  inline int sentChars (TransmitSegment const& segment, qint64 servedSamples)
  {
    if (!segment.endSample) return 0;
    if (servedSamples >= segment.endSample) return segment.text.size ();
    int const frames = segment.tones.size () / 59;
    if (frames <= 0 || segment.frameCharStarts.size () != frames) return 0;
    qint64 const elapsed = servedSamples - (segment.endSample - segment.sampleCount ());
    if (elapsed <= 0) return 0;
    int const done = int (qMin (qint64 (frames), elapsed / transmitFrameSamples));
    return done < frames ? segment.frameCharStarts.at (done) : segment.text.size ();
  }

  // The text of one over (PTT on to PTT off), built up frame by frame as it
  // is sent. Diddle is left out.
  class TransmitEcho
  {
  public:
    using FillerTest = std::function<bool (QString const&)>;

    struct Update
    {
      bool grew {false};
      QStringList completedSegments;  // non-filler segments that finished sending
    };

    Update update (QVector<TransmitQueue::Request> const& requests, qint64 servedSamples,
                   FillerTest const& isFiller)
    {
      Update result;
      for (auto const& request : requests) {
        for (int i = 0; i < request.segments.size (); ++i) {
          auto const& segment = request.segments.at (i);
          if (!segment.endSample || isFiller (segment.text)) continue;
          int const sent = sentChars (segment, servedSamples);
          int& echoed = m_echoed[qMakePair (request.id, i)];
          if (sent <= echoed) continue;
          QString chunk = segment.text.mid (echoed, sent - echoed);
          // Separately committed pieces may not carry the space between them.
          if (!echoed && !m_text.isEmpty () && !m_text.endsWith (QLatin1Char (' '))
              && !chunk.startsWith (QLatin1Char (' '))) {
            chunk.prepend (QLatin1Char (' '));
          }
          if (m_text.isEmpty ()) {
            m_firstRequestId = request.id;
            m_frequency = segment.frequency;
          }
          m_text += chunk;
          echoed = sent;
          result.grew = true;
          if (sent == segment.text.size ()) result.completedSegments.append (segment.text);
        }
      }
      return result;
    }

    QString text () const { return m_text.trimmed (); }
    bool isEmpty () const { return text ().isEmpty (); }
    qint64 firstRequestId () const { return m_firstRequestId; }
    int frequency () const { return m_frequency; }

    void reset ()
    {
      m_text.clear ();
      m_echoed.clear ();
      m_firstRequestId = 0;
      m_frequency = 0;
    }

  private:
    QString m_text;
    QHash<QPair<qint64, int>, int> m_echoed;
    qint64 m_firstRequestId {0};
    int m_frequency {0};
  };
}

#endif
