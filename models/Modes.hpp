#ifndef MODES_HPP__
#define MODES_HPP__

#include <QAbstractListModel>

#include "qt_helpers.hpp"

class QString;
class QVariant;
class QModelIndex;

//
// Class Modes - Qt model that implements a list of data modes
//
//
// Responsibilities
//
// 	Provides  a  single column  list  model  that contains  the  human
// 	readable string version of the data mode in the display role. Also
// 	provided  is a  translatable  column header  string  and tool  tip
// 	string.
//
//
// Collaborations
//
// 	Implements a concrete sub-class of the QAbstractListModel class.
//
class Modes final
  : public QAbstractListModel
{
  Q_OBJECT

public:
  //
  // This enumeration contains the supported modes, to complement this
  // an array of human readable strings in the implementation
  // (Modes.cpp) must be maintained in parallel.
  //
  enum Mode
  {
    ALL,                        // matches with all modes
    JT65,
    JT9,
    JT4,
    WSPR,
    Echo,
    MSK144,
    FreqCal,
    FT8,
    FT4,
    FST4,
    FST4W,
    Q65,
    MODES_END_SENTINAL_AND_COUNT // this must be last
  };
  Q_ENUM (Mode)

  explicit Modes (QObject * parent = nullptr);

  // translate between enumeration and human readable strings
  static char const * name (Mode);
  static Mode value (QString const&);

  // Implement the QAbstractListModel interface
  int rowCount (QModelIndex const& parent = QModelIndex {}) const override
  {
    return parent.isValid () ? 0 : MODES_END_SENTINAL_AND_COUNT; // Number of modes in Mode enumeration class
  }
  QVariant data (QModelIndex const&, int role = Qt::DisplayRole) const override;
  QVariant headerData (int section, Qt::Orientation, int = Qt::DisplayRole) const override;
};

ENUM_QDATASTREAM_OPS_DECL (Modes, Mode);
ENUM_CONVERSION_OPS_DECL (Modes, Mode);

#endif
