#ifndef JTTY_TRANSMIT_QUEUE_HPP_
#define JTTY_TRANSMIT_QUEUE_HPP_

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <utility>

namespace Jtty
{
  struct TransmitSegment
  {
    QString text;
    QVector<int> tones;
    qint64 endSample {0};
    int frequency {1500};
    // 0-indexed start offset into text of each encoded frame; empty if the caller didn't ask the encoder for boundaries.
    QVector<int> frameCharStarts {};

    qint64 sampleCount () const
    {
      return qint64 {tones.size ()} * 1536;
    }
  };

  class TransmitQueue
  {
  public:
    struct Request
    {
      qint64 id;
      QVector<TransmitSegment> segments;
      int committed {0};
    };

    void append (qint64 id, QVector<TransmitSegment> segments)
    {
      Q_ASSERT (id > 0);
      Q_ASSERT (!segments.isEmpty ());
      requests_.append ({id, std::move (segments), 0});
    }

    bool empty () const
    {
      return requests_.isEmpty ();
    }

    bool hasUncommitted () const
    {
      return nextSegment () != nullptr;
    }

    qint64 nextRequestId () const
    {
      for (auto const& request : requests_) {
        if (request.committed < request.segments.size ()) {
          return request.id;
        }
      }
      return 0;
    }

    TransmitSegment const * nextSegment () const
    {
      for (auto const& request : requests_) {
        if (request.committed < request.segments.size ()) {
          return &request.segments.at (request.committed);
        }
      }
      return nullptr;
    }

    bool commitNext (qint64 endSample)
    {
      Q_ASSERT (endSample > 0);
      for (auto& request : requests_) {
        if (request.committed < request.segments.size ()) {
          request.segments[request.committed].endSample = endSample;
          return request.committed++ == 0;
        }
      }
      return false;
    }

    // Source consumption only updates progress; completion requires backend drain.
    QVector<qint64> complete (qint64 totalAtDrain)
    {
      QVector<qint64> completed;
      for (auto const& request : requests_) {
        if (request.committed != request.segments.size ()
            || request.segments.constLast ().endSample > totalAtDrain) {
          break;
        }
        completed.append (request.id);
      }
      requests_.remove (0, completed.size ());
      return completed;
    }

    QVector<qint64> cancel ()
    {
      QVector<qint64> cancelled;
      for (auto const& request : requests_) {
        cancelled.append (request.id);
      }
      requests_.clear ();
      return cancelled;
    }

    int segmentCount () const
    {
      int count = 0;
      for (auto const& request : requests_) {
        count += request.segments.size ();
      }
      return count;
    }

    int remainingSegments (qint64 servedSamples) const
    {
      int count = 0;
      for (auto const& request : requests_) {
        for (auto const& segment : request.segments) {
          if (!segment.endSample || segment.endSample > servedSamples) {
            ++count;
          }
        }
      }
      return count;
    }

    // Requests with at least one segment already playing or played. Their text
    // was heard, so a cancellation must not reopen it for editing.
    QSet<qint64> requestsOnAir (qint64 servedSamples) const
    {
      QSet<qint64> ids;
      for (auto const& request : requests_) {
        for (auto const& segment : request.segments) {
          if (!segment.endSample
              || segment.endSample - segment.sampleCount () >= servedSamples) continue;
          ids.insert (request.id);
          break;
        }
      }
      return ids;
    }

    QString pendingText (qint64 servedSamples) const
    {
      QStringList pending;
      for (auto const& request : requests_) {
        for (auto const& segment : request.segments) {
          if (!segment.endSample || segment.endSample > servedSamples) {
            pending.append (segment.text);
          }
        }
      }
      return pending.join ('\n');
    }

    QVector<Request> const& requests () const
    {
      return requests_;
    }

  private:
    QVector<Request> requests_;
  };
}

#endif
