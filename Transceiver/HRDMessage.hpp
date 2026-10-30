#ifndef HRD_MESSAGE_HPP__
#define HRD_MESSAGE_HPP__

#include <array>

#include <QByteArray>
#include <QString>

namespace HRDMessage
{
  int header_size ();
  int max_message_size ();

  struct ParseResult
  {
    bool valid;
    bool complete;
    int required_size;
    QString payload;
    QString error;
  };

  QByteArray make (QString const& payload);
  ParseResult parse (QByteArray const& message);
  bool parse_radio (QString const& reply, unsigned * id, QString * name);
  bool parse_frequencies (QString const& reply, std::array<quint64, 2> * frequencies);
}

#endif
