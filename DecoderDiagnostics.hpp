#ifndef DECODER_DIAGNOSTICS_HPP
#define DECODER_DIAGNOSTICS_HPP

#include <functional>
#include <QByteArray>

class DecoderDiagnostics
{
public:
  using LineSink = std::function<void (QByteArray const&)>;
  static constexpr int retentionLimit = 64 * 1024;

  explicit DecoderDiagnostics (LineSink sink);
  void append (QByteArray const& bytes);
  void finish ();
  void reset ();
  QByteArray const& tail () const;

private:
  void emitLine ();

  LineSink sink_;
  QByteArray pending_;
  QByteArray tail_;
};

void logDecoderDiagnostic (QByteArray const& line);

#endif
