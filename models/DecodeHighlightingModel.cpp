#include "DecodeHighlightingModel.hpp"

#include <QString>
#include <QVariant>
#include <QList>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QMap>
#include <QVector>
#include <QTextStream>
#include <QDataStream>
#include <QMetaType>
#include <QDebug>

#include "pimpl_impl.hpp"

#include "moc_DecodeHighlightingModel.cpp"

class DecodeHighlightingModel::impl final
{
public:
  explicit impl ()
    : data_ {defaults_}
  {
  }

  HighlightItems static const defaults_;
  HighlightItems data_;
  QFont font_;
};

QList<DecodeHighlightingModel::HighlightInfo> const DecodeHighlightingModel::impl::defaults_ = {
  {Highlight::MyCall, true, {}, {{0xff, 0x66, 0x66}}}
  , {Highlight::Continent, true, {}, {{0xff, 0x00, 0x63}}}
  , {Highlight::ContinentBand, true, {}, {{0xff, 0x99, 0xc2}}}
  , {Highlight::CQZone, true, {}, {{0xff, 0xbf, 0x00}}}
  , {Highlight::CQZoneBand, true, {}, {{0xff, 0xe4, 0x99}}}
  , {Highlight::ITUZone, false, {}, {{0xa6, 0xff, 0x00}}}
  , {Highlight::ITUZoneBand, false, {}, {{0xdd, 0xff, 0x99}}}
  , {Highlight::DXCC, true, {}, {{0xff, 0x00, 0xff}}}
  , {Highlight::DXCCBand, true, {}, {{0xff, 0xaa, 0xff}}}
  , {Highlight::Grid, false, {}, {{0xff, 0x80, 0x00}}}
  , {Highlight::GridBand, false, {}, {{0xff, 0xcc, 0x99}}}
  , {Highlight::Call, false, {}, {{0x00, 0xff, 0xff}}}
  , {Highlight::CallBand, false, {}, {{0x99, 0xff, 0xff}}}
  , {Highlight::LotW, false, {{0x99, 0x00, 0x00}}, {}}
  , {Highlight::CQ, true, {}, {{0x66, 0xff, 0x66}}}
  , {Highlight::Tx, true, {}, {Qt::yellow}}
};

bool operator == (DecodeHighlightingModel::HighlightInfo const& lhs, DecodeHighlightingModel::HighlightInfo const& rhs)
{
  return lhs.type_ == rhs.type_
    && lhs.enabled_ == rhs.enabled_
    && lhs.foreground_ == rhs.foreground_
    && lhs.background_ == rhs.background_;
}

QDataStream& operator << (QDataStream& os, DecodeHighlightingModel::HighlightInfo const& item)
{
  return os << item.type_
            << item.enabled_
            << item.foreground_
            << item.background_;
}

QDataStream& operator >> (QDataStream& is, DecodeHighlightingModel::HighlightInfo& item)
{
  return is >> item.type_
           >> item.enabled_
           >> item.foreground_
           >> item.background_;
}

QString DecodeHighlightingModel::HighlightInfo::toString () const
{
  QString string;
  QTextStream ots {&string};
  ots << "HighlightInfo("
      << highlight_name (type_) << ", "
      << enabled_ << ", "
      << foreground_.color ().name () << ", "
      << background_.color ().name () << ')';
  return string;
}

#if !defined (QT_NO_DEBUG_STREAM)
QDebug operator << (QDebug debug, DecodeHighlightingModel::HighlightInfo const& item)
{
  QDebugStateSaver save {debug};
  return debug.nospace () << item.toString ();
}
#endif

ENUM_QDATASTREAM_OPS_IMPL (DecodeHighlightingModel, Highlight);
ENUM_CONVERSION_OPS_IMPL (DecodeHighlightingModel, Highlight);

DecodeHighlightingModel::DecodeHighlightingModel (QObject * parent)
  : QAbstractListModel {parent}
{
}

DecodeHighlightingModel::~DecodeHighlightingModel ()
{
}

QString DecodeHighlightingModel::highlight_name (Highlight h)
{
  switch (h)
    {
    case Highlight::CQ: return tr ("CQ in message");
    case Highlight::MyCall: return tr ("My Call in message");
    case Highlight::Tx: return tr ("Transmitted message");
    case Highlight::DXCC: return tr ("New DXCC");
    case Highlight::DXCCBand: return tr ("New DXCC on Band");
    case Highlight::Grid: return tr ("New Grid");
    case Highlight::GridBand: return tr ("New Grid on Band");
    case Highlight::Call: return tr ("New Call");
    case Highlight::CallBand: return tr ("New Call on Band");
    case Highlight::Continent: return tr ("New Continent");
    case Highlight::ContinentBand: return tr ("New Continent on Band");
    case Highlight::CQZone: return tr ("New CQ Zone");
    case Highlight::CQZoneBand: return tr ("New CQ Zone on Band");
    case Highlight::ITUZone: return tr ("New ITU Zone");
    case Highlight::ITUZoneBand: return tr ("New ITU Zone on Band");
    case Highlight::LotW: return tr ("LoTW User");
    }
  return "Unknown";
}

auto DecodeHighlightingModel::default_items () -> HighlightItems const&
{
  return impl::defaults_;
}

auto DecodeHighlightingModel::items () const -> HighlightItems const&
{
  return m_->data_;
}

void DecodeHighlightingModel::items (HighlightItems const& items)
{
  m_->data_ = items;
  QVector<int> roles;
  roles << Qt::CheckStateRole << Qt::ForegroundRole << Qt::BackgroundRole;
  Q_EMIT dataChanged (index (0, 0), index (rowCount () - 1, 0), roles);
}

void DecodeHighlightingModel::set_font (QFont const& font)
{
  m_->font_ = font;
}

int DecodeHighlightingModel::rowCount (const QModelIndex& parent) const
{
  return parent.isValid () ? 0 : m_->data_.size ();
}

QVariant DecodeHighlightingModel::data (const QModelIndex& index, int role) const
{
  QVariant result;
  if (index.isValid () && index.row () < rowCount ())
    {
      auto const& item = m_->data_[index.row ()];
      auto fg_unset = Qt::NoBrush == item.foreground_.style ();
      auto bg_unset = Qt::NoBrush == item.background_.style ();
      switch (role)
        {
        case Qt::CheckStateRole:
          result = item.enabled_ ? Qt::Checked : Qt::Unchecked;
          break;
        case Qt::DisplayRole:
          return QString {"%1%2%3%4%4%5%6"}
             .arg (highlight_name (item.type_))
             .arg (fg_unset || bg_unset ? QString {" ["} : QString {})
             .arg (fg_unset ? tr ("f/g unset") : QString {})
             .arg (fg_unset && bg_unset ? QString {" "} : QString {})
             .arg (bg_unset ? tr ("b/g unset") : QString {})
             .arg (fg_unset || bg_unset ? QString {"]"} : QString {});
          break;
        case Qt::ForegroundRole:
          if (!fg_unset)
            {
              result = item.foreground_;
            }
          break;
        case Qt::BackgroundRole:
          if (!bg_unset)
            {
              result = item.background_;
            }
          break;
        case Qt::FontRole:
          result = m_->font_;
          break;
        case TypeRole:
          result = static_cast<int> (item.type_);
          break;
        case EnabledDefaultRole:
          for (auto const& default_item : impl::defaults_)
            {
              if (default_item.type_ == item.type_)
                {
                  result = default_item.enabled_ ? Qt::Checked : Qt::Unchecked;
                }
            }
          break;
        case ForegroundDefaultRole:
          for (auto const& default_item : impl::defaults_)
            {
              if (default_item.type_ == item.type_)
                {
                  result = default_item.foreground_;
                }
            }
          break;
        case BackgroundDefaultRole:
          for (auto const& default_item : impl::defaults_)
            {
              if (default_item.type_ == item.type_)
                {
                  result = default_item.background_;
                }
            }
          break;
        }
    }
  return result;
}

// Override  QAbstractItemModel::itemData()  as  it  is  used  by  the
// default mime encode  routine used in drag'n'drop  operations and we
// want to transport  the type role, this is because  the display role
// is derived from the type role.
QMap<int, QVariant> DecodeHighlightingModel::itemData (QModelIndex const& index) const
{
  auto roles = QAbstractListModel::itemData (index);
  QVariant variantData = data (index, TypeRole);
  if (variantData.isValid ())
    {
      roles.insert (TypeRole, variantData);
    }
  return roles;
}

QVariant DecodeHighlightingModel::headerData (int /*section*/, Qt::Orientation orientation, int role) const
{
  QVariant header;
  if (Qt::DisplayRole == role && Qt::Horizontal == orientation)
    {
      header = tr ("Highlight Type");
    }
  return header;
}

Qt::ItemFlags DecodeHighlightingModel::flags (QModelIndex const& index) const
{
  auto flags = QAbstractListModel::flags (index) | Qt::ItemIsDragEnabled;
  if (index.isValid ())
    {
      flags |= Qt::ItemIsUserCheckable;
    }
  else
    {
      flags |= Qt::ItemIsDropEnabled;
    }
  return flags;
}

bool DecodeHighlightingModel::setData (QModelIndex const& index, QVariant const& value, int role)
{
  bool ok {false};
  if (index.isValid () && index.row () < rowCount ())
    {
      auto& item = m_->data_[index.row ()];
      QVector<int> roles;
      roles << role;
      switch (role)
        {
        case Qt::DisplayRole:
        case Qt::FontRole:
          ok = true;
          break;
        case Qt::CheckStateRole:
          if (item.enabled_ != (Qt::Checked == value))
            {
              item.enabled_ = Qt::Checked == value;
              Q_EMIT dataChanged (index, index, roles);
            }
          ok = true;
          break;
        case Qt::ForegroundRole:
          if (item.foreground_ != value.value<QBrush> ())
            {
              item.foreground_ = value.value<QBrush> ();
              Q_EMIT dataChanged (index, index, roles);
            }
          ok = true;
          break;
        case Qt::BackgroundRole:
          if (item.background_ != value.value<QBrush> ())
            {
              item.background_ = value.value<QBrush> ();
              Q_EMIT dataChanged (index, index, roles);
            }
          ok = true;
          break;
        case TypeRole:
          if (item.type_ != static_cast<Highlight> (value.toInt ()))
            {
              item.type_ = static_cast<Highlight> (value.toInt ());
              roles << Qt::DisplayRole;
              Q_EMIT dataChanged (index, index, roles);
            }
          ok = true;
          break;
        }
    }
  return ok;
}

Qt::DropActions DecodeHighlightingModel::supportedDropActions () const
{
  return Qt::MoveAction;
}

bool DecodeHighlightingModel::insertRows (int row, int count, QModelIndex const& parent)
{
  beginInsertRows (parent, row, row + count - 1);
  for (int index = 0; index < count; ++index)
    {
      m_->data_.insert (row, HighlightInfo {Highlight::CQ, false, {}, {}});
    }
  endInsertRows ();
  return true;
}

bool DecodeHighlightingModel::removeRows (int row, int count, QModelIndex const& parent)
{
  beginRemoveRows (parent, row, row + count - 1);
  for (int index = 0; index < count; ++index)
    {
      m_->data_.removeAt (row);
    }
  endRemoveRows ();
  return true;
}
