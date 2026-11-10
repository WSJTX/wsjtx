#ifndef DECODED_GRID_HISTORY_HPP
#define DECODED_GRID_HISTORY_HPP

#include <QHash>
#include <QString>

class DecodedText;

class DecodedGridHistory
{
public:
  void observe(DecodedText const& message);

  QString lookup(QString const& call, QString const& currentGrid = {}) const
  {
    return currentGrid.isEmpty() ? grids_.value(call) : currentGrid;
  }

private:
  QHash<QString, QString> grids_;
};

#endif
