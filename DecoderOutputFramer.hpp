#ifndef DECODER_OUTPUT_FRAMER_HPP
#define DECODER_OUTPUT_FRAMER_HPP

#include <functional>

#include <QByteArray>

#include "DecoderIpc.hpp"

class QIODevice;

class DecoderOutputFramer
{
public:
  enum class EventType
  {
    Started,
    Record,
    Finished,
    Malformed
  };

  struct Event
  {
    EventType type;
    qint32 generation;
    QByteArray rawLine;
    DecoderIpc::Completion completion;
  };

  using EventHandler = std::function<void (Event const&)>;

  void drain (QIODevice& device, EventHandler const& handler);
  void reset ();
  qint32 currentGeneration () const;

private:
  qint32 generation_ {0};
};

#endif
