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
  HighlightItems static const defaults2_;
  HighlightItems static const deuteranopia_;
  HighlightItems static const tritanopia_;
  HighlightItems static const high_contrast_;
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

QList<DecodeHighlightingModel::HighlightInfo> const DecodeHighlightingModel::impl::defaults2_ = {
  {Highlight::LotW, true, {Qt::black}, {}}
  , {Highlight::MyCall, true, {{0xaa, 0x00, 0x00}}, {{0x00, 0xff, 0xff}}}
  , {Highlight::Continent, true, {{0xaa, 0x00, 0x00}}, {{0xff, 0x50, 0xff}}}
  , {Highlight::ContinentBand, true, {{0xaa, 0x00, 0x00}}, {{0xff, 0x82, 0xff}}}
  , {Highlight::DXCC, true, {{0xaa, 0x00, 0x00}}, {Qt::yellow}}
  , {Highlight::DXCCBand, true, {{0xaa, 0x00, 0x00}}, {{0xff, 0xff, 0x9b}}}
  , {Highlight::CQZone, true, {{0xaa, 0x00, 0x00}}, {{0xaa, 0xaa, 0x00}}}
  , {Highlight::CQZoneBand, true, {{0xaa, 0x00, 0x00}}, {{0xaa, 0xaa, 0x7f}}}
  , {Highlight::ITUZone, true, {{0xaa, 0x00, 0x00}}, {{0x37, 0xbd, 0xff}}}
  , {Highlight::ITUZoneBand, true, {{0xaa, 0x00, 0x00}}, {{0x91, 0xde, 0xff}}}
  , {Highlight::Grid, true, {{0xaa, 0x00, 0x00}}, {{0xff, 0xa8, 0x80}}}
  , {Highlight::GridBand, true, {{0xaa, 0x00, 0x00}}, {{0xff, 0xcd, 0x9b}}}
  , {Highlight::Call, true, {{0xaa, 0x00, 0x00}}, {{0x64, 0xff, 0x64}}}
  , {Highlight::CallBand, true, {{0xaa, 0x00, 0x00}}, {{0xb4, 0xff, 0xb4}}}
  , {Highlight::CQ, true, {{0xaa, 0x00, 0x00}}, {{0xc3, 0xc3, 0xc3}}}
  , {Highlight::Tx, true, {Qt::black}, {{0xff, 0xa5, 0xc6}}}
};

// Backgrounds drawn from the Okabe-Ito categorical palette, the
// standard reference palette for deuteranopia/protanopia
// accessibility. Foreground is black or white, whichever contrasts
// more with that item's background.
QList<DecodeHighlightingModel::HighlightInfo> const DecodeHighlightingModel::impl::deuteranopia_ = {
  {Highlight::MyCall, true, {Qt::white}, {{0xd5, 0x5e, 0x00}}}
  , {Highlight::Continent, true, {Qt::white}, {{0x00, 0x72, 0xb2}}}
  , {Highlight::ContinentBand, true, {Qt::black}, {{0xa8, 0xcc, 0xe3}}}
  , {Highlight::CQZone, true, {Qt::black}, {{0xe6, 0x9f, 0x00}}}
  , {Highlight::CQZoneBand, true, {Qt::black}, {{0xf5, 0xd9, 0x99}}}
  , {Highlight::ITUZone, false, {Qt::black}, {{0x56, 0xb4, 0xe9}}}
  , {Highlight::ITUZoneBand, false, {Qt::black}, {{0xbe, 0xe3, 0xf5}}}
  , {Highlight::DXCC, true, {Qt::black}, {{0xcc, 0x79, 0xa7}}}
  , {Highlight::DXCCBand, true, {Qt::black}, {{0xe8, 0xbf, 0xd6}}}
  , {Highlight::Grid, false, {Qt::white}, {Qt::black}}
  , {Highlight::GridBand, false, {Qt::black}, {{0xcc, 0xcc, 0xcc}}}
  , {Highlight::Call, false, {Qt::black}, {{0x99, 0x99, 0x99}}}
  , {Highlight::CallBand, false, {Qt::black}, {{0xdd, 0xdd, 0xdd}}}
  , {Highlight::LotW, false, {{0x00, 0x55, 0x44}}, {}}
  , {Highlight::CQ, true, {Qt::white}, {{0x00, 0x9e, 0x73}}}
  , {Highlight::Tx, true, {Qt::black}, {{0xf0, 0xe4, 0x42}}}
};

// Backgrounds drawn from the red/green/orange/purple axis, which
// tritanopia does not impair, avoiding blue vs. yellow as a
// differentiator between categories.
QList<DecodeHighlightingModel::HighlightInfo> const DecodeHighlightingModel::impl::tritanopia_ = {
  {Highlight::MyCall, true, {Qt::white}, {{0xe4, 0x1a, 0x1c}}}
  , {Highlight::Continent, true, {Qt::black}, {{0xf7, 0x81, 0xbf}}}
  , {Highlight::ContinentBand, true, {Qt::black}, {{0xfc, 0xe0, 0xef}}}
  , {Highlight::CQZone, true, {Qt::white}, {{0xa6, 0x56, 0x28}}}
  , {Highlight::CQZoneBand, true, {Qt::black}, {{0xe3, 0xc6, 0xb3}}}
  , {Highlight::ITUZone, false, {Qt::white}, {{0xb0, 0x30, 0x60}}}
  , {Highlight::ITUZoneBand, false, {Qt::black}, {{0xe8, 0xb9, 0xc7}}}
  , {Highlight::DXCC, true, {Qt::white}, {{0x98, 0x4e, 0xa3}}}
  , {Highlight::DXCCBand, true, {Qt::black}, {{0xe0, 0xc6, 0xe6}}}
  , {Highlight::Grid, false, {Qt::black}, {{0x99, 0x99, 0x99}}}
  , {Highlight::GridBand, false, {Qt::black}, {{0xdd, 0xdd, 0xdd}}}
  , {Highlight::Call, false, {Qt::black}, {{0xfd, 0xbf, 0x6f}}}
  , {Highlight::CallBand, false, {Qt::black}, {{0xfd, 0xe9, 0xd0}}}
  , {Highlight::LotW, false, {{0x6e, 0x14, 0x23}}, {}}
  , {Highlight::CQ, true, {Qt::black}, {{0x4d, 0xaf, 0x4a}}}
  , {Highlight::Tx, true, {Qt::black}, {{0xff, 0x7f, 0x00}}}
};

// Strongly saturated, high-luminance-difference colors. "Band"
// variants use a darker/desaturated shade of the base category's
// hue rather than a pale tint, since pale tints wash out for low
// vision.
QList<DecodeHighlightingModel::HighlightInfo> const DecodeHighlightingModel::impl::high_contrast_ = {
  {Highlight::MyCall, true, {Qt::white}, {{0xff, 0x00, 0x00}}}
  , {Highlight::Continent, true, {Qt::black}, {{0xff, 0x80, 0x00}}}
  , {Highlight::ContinentBand, true, {Qt::white}, {{0x99, 0x50, 0x00}}}
  , {Highlight::CQZone, true, {Qt::white}, {{0x00, 0x40, 0xff}}}
  , {Highlight::CQZoneBand, true, {Qt::white}, {{0x00, 0x29, 0x66}}}
  , {Highlight::ITUZone, false, {Qt::white}, {{0x00, 0x80, 0x80}}}
  , {Highlight::ITUZoneBand, false, {Qt::white}, {{0x00, 0x4d, 0x4d}}}
  , {Highlight::DXCC, true, {Qt::white}, {{0xcc, 0x00, 0xcc}}}
  , {Highlight::DXCCBand, true, {Qt::white}, {{0x7a, 0x00, 0x7a}}}
  , {Highlight::Grid, false, {Qt::white}, {{0x66, 0x66, 0x66}}}
  , {Highlight::GridBand, false, {Qt::white}, {{0x33, 0x33, 0x33}}}
  , {Highlight::Call, false, {Qt::white}, {{0xd6, 0x00, 0x6d}}}
  , {Highlight::CallBand, false, {Qt::white}, {{0x80, 0x00, 0x41}}}
  , {Highlight::LotW, false, {{0x00, 0x1a, 0x66}}, {}}
  , {Highlight::CQ, true, {Qt::white}, {{0x00, 0xa0, 0x00}}}
  , {Highlight::Tx, true, {Qt::black}, {{0xff, 0xff, 0x00}}}
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

auto DecodeHighlightingModel::default_items2 () -> HighlightItems const&
{
  return impl::defaults2_;
}

auto DecodeHighlightingModel::default_items_deuteranopia () -> HighlightItems const&
{
  return impl::deuteranopia_;
}

auto DecodeHighlightingModel::default_items_tritanopia () -> HighlightItems const&
{
  return impl::tritanopia_;
}

auto DecodeHighlightingModel::default_items_high_contrast () -> HighlightItems const&
{
  return impl::high_contrast_;
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
