#ifndef MAP65_DECODE_DISPLAY_FILTER_H
#define MAP65_DECODE_DISPLAY_FILTER_H

#include <QString>
#include <QVector>

class Map65DecodeDisplayFilter
{
public:
  bool handleControlLine(QString const& line);
  bool shouldDisplay(QString const& decodeLine);
  void completeEarlyPass();
  void completeCycle();

private:
  enum class Pass
  {
    Unknown,
    Early,
    Final,
    Manual,
    Disk
  };

  struct Identity
  {
    bool q65;
    int utc;
    int frequencyHz;
    QString message;
  };

  static bool parseIdentity(QString const& line, Identity * identity);
  static bool matches(Identity const& left, Identity const& right);

  Pass pass_ {Pass::Unknown};
  QVector<Identity> earlyDecodes_;
};

#endif
