#include "DecoderOutputFramer.hpp"

#include <QIODevice>

void DecoderOutputFramer::drain (QIODevice& device,
                                 EventHandler const& handler)
{
  while (device.canReadLine ())
    {
      auto const rawLine = device.readLine ();
      Event event {EventType::Malformed, generation_, rawLine, {}};

      if (rawLine.startsWith ("<DecodeStarted>"))
        {
          qint32 generation {0};
          if (!generation_ && DecoderIpc::parseStart (rawLine, &generation))
            {
              generation_ = generation;
              event.type = EventType::Started;
              event.generation = generation;
            }
        }
      else if (rawLine.startsWith ("<DecodeFinished>"))
        {
          DecoderIpc::Completion completion {};
          if (DecoderIpc::parseCompletion (rawLine, &completion)
              && generation_ == completion.generation)
            {
              event.type = EventType::Finished;
              event.generation = completion.generation;
              event.completion = completion;
              generation_ = 0;
            }
        }
      else if (generation_)
        {
          event.type = EventType::Record;
        }

      if (handler) handler (event);
    }
}

void DecoderOutputFramer::reset ()
{
  generation_ = 0;
}

qint32 DecoderOutputFramer::currentGeneration () const
{
  return generation_;
}
