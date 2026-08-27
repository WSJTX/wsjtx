#ifndef QMAP_LIVE_CQ_PARSER_H
#define QMAP_LIVE_CQ_PARSER_H

#include <QString>
#include <QStringList>

namespace QMapLiveCQ
{
  struct Record
  {
    QString callsign;
    QString grid;
    QString message;
    int receiveFrequency {0};
    int scheduledFrequency {0};
  };

  bool parse(QStringList const& tokens, int frequencyOffset, Record& record);
}

#endif
