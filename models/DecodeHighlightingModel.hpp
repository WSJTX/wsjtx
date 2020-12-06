#ifndef DECODE_HIGHLIGHTING_MODEL_HPP_
#define DECODE_HIGHLIGHTING_MODEL_HPP_

#include <QAbstractListModel>
#include <QBrush>
#include <QList>

#include "qt_helpers.hpp"
#include "pimpl_h.hpp"

class QObject;
class QFont;
class QDataStream;
class QDebug;

class DecodeHighlightingModel final
  : public QAbstractListModel
{
  Q_OBJECT
public:
  enum class Highlight : char {CQ, MyCall, Tx, DXCC, DXCCBand, Grid, GridBand, Call, CallBand
                               , Continent, ContinentBand, CQZone, CQZoneBand, ITUZone, ITUZoneBand
                               , LotW};
  Q_ENUM (Highlight)
  static QString highlight_name (Highlight h);

  struct HighlightInfo final
  {
    Highlight type_;
    bool enabled_;
    QBrush foreground_;
    QBrush background_;
    QString toString () const;
  };
  using HighlightItems = QList<HighlightInfo>;

  explicit DecodeHighlightingModel (QObject * parent = 0);
  ~DecodeHighlightingModel();

  // access to raw items nd default items
  static HighlightItems const& default_items ();
  HighlightItems const& items () const;
  void items (HighlightItems const&);

  void set_font (QFont const&);

  enum DefaultRoles {TypeRole = Qt::UserRole, EnabledDefaultRole, ForegroundDefaultRole, BackgroundDefaultRole};

private:
  // implement the QAbstractListModel interface
  int rowCount (QModelIndex const& parent = QModelIndex()) const override;
  QVariant data (QModelIndex const&, int role) const override;
  QVariant headerData (int section, Qt::Orientation, int role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags (QModelIndex const&) const override;
  bool setData (QModelIndex const& index, QVariant const& value, int role) override;
  Qt::DropActions supportedDropActions () const override;
  bool insertRows (int row, int count, QModelIndex const& parent = QModelIndex {}) override;
  bool removeRows (int row, int count, QModelIndex const& parent = QModelIndex {}) override;
  QMap<int, QVariant> itemData (QModelIndex const&) const override;

  class impl;
  pimpl<impl> m_;
};

bool operator == (DecodeHighlightingModel::HighlightInfo const&, DecodeHighlightingModel::HighlightInfo const&);

QDataStream& operator << (QDataStream&, DecodeHighlightingModel::HighlightInfo const&);
QDataStream& operator >> (QDataStream&, DecodeHighlightingModel::HighlightInfo&);

#if !defined (QT_NO_DEBUG_STREAM)
QDebug operator << (QDebug, DecodeHighlightingModel::HighlightInfo const&);
#endif

ENUM_QDATASTREAM_OPS_DECL (DecodeHighlightingModel, Highlight);
ENUM_CONVERSION_OPS_DECL (DecodeHighlightingModel, Highlight);

Q_DECLARE_METATYPE (DecodeHighlightingModel::HighlightInfo);
Q_DECLARE_METATYPE (DecodeHighlightingModel::HighlightItems);

#endif
