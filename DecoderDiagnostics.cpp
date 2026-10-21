#include "DecoderDiagnostics.hpp"

#include <utility>
#include <boost/log/keywords/channel.hpp>
#include "Logger.hpp"

namespace
{
  void appendBounded (QByteArray& destination, QByteArray const& source, int start, int count)
  {
    if (count >= DecoderDiagnostics::retentionLimit)
      {
        destination = source.mid (start + count - DecoderDiagnostics::retentionLimit,
                                  DecoderDiagnostics::retentionLimit);
        return;
      }
    auto const excess = destination.size () + count - DecoderDiagnostics::retentionLimit;
    if (excess > 0) destination.remove (0, excess);
    destination.append (source.constData () + start, count);
  }
}

DecoderDiagnostics::DecoderDiagnostics (LineSink sink)
  : sink_ {std::move (sink)}
{
}

void DecoderDiagnostics::append (QByteArray const& bytes)
{
  appendBounded (tail_, bytes, 0, bytes.size ());
  int start {0};
  while (start < bytes.size ())
    {
      auto const newline = bytes.indexOf ('\n', start);
      auto const end = newline < 0 ? bytes.size () : newline;
      appendBounded (pending_, bytes, start, end - start);
      if (newline < 0) break;
      emitLine ();
      start = newline + 1;
    }
}

void DecoderDiagnostics::emitLine ()
{
  if (pending_.endsWith ('\r')) pending_.chop (1);
  if (!pending_.isEmpty ()) sink_ (pending_);
  pending_.clear ();
}

void DecoderDiagnostics::finish ()
{
  emitLine ();
}

void DecoderDiagnostics::reset ()
{
  pending_.clear ();
  tail_.clear ();
}

QByteArray const& DecoderDiagnostics::tail () const
{
  return tail_;
}

void logDecoderDiagnostic (QByteArray const& line)
{
  static boost::log::sources::severity_channel_logger_mt<boost::log::trivial::severity_level>
    logger {boost::log::keywords::channel = "DECODER"};
  BOOST_LOG_SEV (logger, boost::log::trivial::warning) << line.toStdString ();
}
