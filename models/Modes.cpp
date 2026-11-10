#include "Modes.hpp"

#include <QString>
#include <QVariant>
#include <QModelIndex>

#include "moc_Modes.cpp"

namespace
{
  // human readable strings for each Mode enumeration value
  char const * const mode_names[] =
  {
    "All",
    "JT65",
    "JT9",
    "JT4",
    "WSPR",
    "Echo",
    "MSK144",
    "FreqCal",
    "FT8",
    "FT4",
    "FST4",
    "FST4W",
    "Q65",
    "JTTY"
  };
  std::size_t constexpr mode_names_size = sizeof (mode_names) / sizeof (mode_names[0]);
}

Modes::Modes (QObject * parent)
  : QAbstractListModel {parent}
{
  static_assert (mode_names_size == MODES_END_SENTINAL_AND_COUNT,
                 "mode_names array must match Mode enumeration");
}

char const * Modes::name (Mode m)
{
  return mode_names[static_cast<int> (m)];
}

QVariant Modes::data (QModelIndex const& index, int role) const
{
  QVariant item;

  if (index.isValid () && index.row () >= 0 && index.row () < MODES_END_SENTINAL_AND_COUNT)
    {
      auto const& row = index.row ();
      switch (role)
        {
        case Qt::ToolTipRole:
        case Qt::AccessibleDescriptionRole:
          item = tr ("Mode");
          break;

        case Qt::EditRole:
          item = QVariant::fromValue (static_cast<Mode> (row));
          break;

        case Qt::DisplayRole:
        case Qt::AccessibleTextRole:
          item = mode_names[row];
          break;

        case Qt::TextAlignmentRole:
          item = Qt::AlignHCenter + Qt::AlignVCenter;
          break;
        }
    }

  return item;
}

QVariant Modes::headerData (int section, Qt::Orientation orientation, int role) const
{
  QVariant result;

  if (Qt::DisplayRole == role && Qt::Horizontal == orientation)
    {
      result = tr ("Mode");
    }
  else
    {
      result = QAbstractListModel::headerData (section, orientation, role);
    }

  return result;
}

ENUM_QDATASTREAM_OPS_IMPL (Modes, Mode);
ENUM_CONVERSION_OPS_IMPL (Modes, Mode);
