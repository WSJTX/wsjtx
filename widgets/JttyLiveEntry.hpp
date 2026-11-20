// -*- Mode: C++ -*-
#ifndef JTTY_LIVE_ENTRY_HPP
#define JTTY_LIVE_ENTRY_HPP

#include <QString>
#include <QStringList>

namespace Jtty
{
  // Widest lookahead try_compact() uses (lib/jtty/jtty_mod.f90, words(3)); holding back this many trailing words guarantees an atom in progress is never split by an early commit.
  // Typed live entry passes 0; the holdback is for macro text in the Tx box.
  inline constexpr int maxCompactAtomWords = 3;

  // One transmitted frame at 48 kHz (genjtty_frames, lib/jtty/genjtty.f90).
  inline constexpr qint64 samplesPerFrame = 59LL * 1536;

  // True once the backlog is under a frame, where diddle would otherwise start.
  inline bool shouldCommitForQueueDepth (qint64 queuedSamples,
                                         qint64 thresholdSamples = samplesPerFrame)
  {
    return queuedSamples < thresholdSamples;
  }

  struct IncrementalCommitPlan
  {
    QString text;        // Text to pack and send now; empty means nothing yet.
    int length {0};       // uncommittedText.left(length) == text, exactly.
  };

  // Number of complete (space-terminated) words in uncommittedText -- a word still being typed (no trailing space yet) doesn't count. Exposed so callers can gate on how much has piled up, not just whether anything has.
  inline int completeWordCount (QString const& uncommittedText)
  {
    if (uncommittedText.isEmpty ()) return 0;
    bool const endsWithCompleteWord = uncommittedText.back () == QLatin1Char {' '};
    QStringList const words = uncommittedText.split (
      QLatin1Char {' '}, Qt::SkipEmptyParts);
    return endsWithCompleteWord ? words.size () : words.size () - 1;
  }

  // Releases uncommittedText up to its trailing holdbackWords complete words (plus any partial word), since a compact atom there could still be extended by what's typed next; forceFlush releases all of it (newline/segment-limit boundary). Assumes single-space-normalized input, matching prepareTransmitText's convention.
  inline IncrementalCommitPlan planIncrementalCommit (
      QString const& uncommittedText, bool forceFlush = false,
      int holdbackWords = maxCompactAtomWords)
  {
    if (uncommittedText.isEmpty ()) return {};
    if (forceFlush) return {uncommittedText, uncommittedText.size ()};

    int const commitWordCount = completeWordCount (uncommittedText) - holdbackWords;
    if (commitWordCount <= 0) return {};

    // Advance to the end of the commitWordCount-th word's trailing space, so the remainder starts cleanly at the next word.
    int length = 0;
    int wordsSeen = 0;
    while (wordsSeen < commitWordCount) {
      int const spaceAt = uncommittedText.indexOf (QLatin1Char {' '}, length);
      Q_ASSERT (spaceAt >= 0);  // completeWordCount already bounds this walk.
      length = spaceAt + 1;
      ++wordsSeen;
    }
    return {uncommittedText.left (length), length};
  }
}

#endif
