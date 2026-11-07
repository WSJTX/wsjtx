#ifndef HISTORY_FOLDER_HPP_
#define HISTORY_FOLDER_HPP_

#include <QList>
#include <QString>

struct HistoryFolder
{
  QString path;
  bool include_subfolders {false};
};

inline bool operator == (HistoryFolder const& lhs, HistoryFolder const& rhs)
{
  return lhs.path == rhs.path && lhs.include_subfolders == rhs.include_subfolders;
}

using HistoryFolders = QList<HistoryFolder>;

#endif
