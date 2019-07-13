#include "CabrilloLog.hpp"

#include <stdexcept>
#include <utility>
#include <QString>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlTableModel>
#include <QSqlRecord>
#include <QSqlError>
#include <QSqlQuery>
#include <QDataStream>
#include "Configuration.hpp"
#include "Bands.hpp"
#include "logbook/AD1CCty.hpp"
#include "qt_db_helpers.hpp"
#include "pimpl_impl.hpp"

class CabrilloLog::impl final
  : public QSqlTableModel
{
public:
  impl (CabrilloLog *, Configuration const *);

  int columnCount (QModelIndex const& /*index */) const override
  {
    return QSqlTableModel::columnCount () + 1;
  }

  Qt::ItemFlags flags (QModelIndex const& index) const override
  {
    auto flags = QSqlTableModel::flags (index);
    if (index.isValid () && index.column () == columnCount (index) - 1)
      {
        flags = Qt::ItemIsEnabled;
      }
    return flags;
  }

  QVariant data (QModelIndex const& model_index, int role) const override
  {
    QVariant value;
    if (model_index.isValid () && model_index.column () == columnCount (model_index) - 1)
      {                         // derive band column
        if (Qt::DisplayRole == role)
          {
            value = configuration_->bands ()->find (QSqlTableModel::data (index (model_index.row (), fieldIndex ("frequency"))).toULongLong ());
          }
      }
    else
      {
        value = QSqlTableModel::data (model_index, role);
        if (model_index.column () == fieldIndex ("frequency") && Qt::DisplayRole == role)
          {
            value = Radio::frequency_MHz_string (value.value<Radio::Frequency> (), 3); // kHz precision
          }
        else if (model_index.column () == fieldIndex ("when")
            && (Qt::DisplayRole == role || Qt::EditRole == role))
          {                     // adjust date/time to Qt format
            auto t = QDateTime::fromMSecsSinceEpoch (value.toULongLong () * 1000ull, Qt::UTC);
            if (Qt::DisplayRole == role)
              {
                QLocale locale;
                return locale.toString (t, locale.dateFormat (QLocale::ShortFormat) + " hh:mm:ss");
              }
            value = t;
          }
      }
    return value;
  }

  QString cabrillo_frequency_string (Radio::Frequency frequency) const;
  void create_table ();

  CabrilloLog * self_;
  Configuration const * configuration_;
  QSqlQuery mutable dupe_query_;
  QSqlQuery mutable export_query_;
  bool adding_row_;
};

CabrilloLog::impl::impl (CabrilloLog * self, Configuration const * configuration)
  : self_ {self}
  , configuration_ {configuration}
  , adding_row_ {false}
{
  if (!database ().tables ().contains ("cabrillo_log_v2"))
    {
      create_table ();
    }

  setEditStrategy (QSqlTableModel::OnFieldChange);
  setTable ("cabrillo_log_v2");
  setHeaderData (fieldIndex ("frequency"), Qt::Horizontal, tr ("Freq(MHz)"));
  setHeaderData (fieldIndex ("mode"), Qt::Horizontal, tr ("Mode"));
  setHeaderData (fieldIndex ("when"), Qt::Horizontal, tr ("Date & Time(UTC)"));
  setHeaderData (fieldIndex ("call"), Qt::Horizontal, tr ("Call"));
  setHeaderData (fieldIndex ("exchange_sent"), Qt::Horizontal, tr ("Sent"));
  setHeaderData (fieldIndex ("exchange_rcvd"), Qt::Horizontal, tr ("Rcvd"));
  setHeaderData (columnCount (QModelIndex {}) - 1, Qt::Horizontal, tr ("Band"));

  // This descending order by time is important, it makes the view
  // place the latest row at the top, without this the model/view
  // interactions are both sluggish and unhelpful.
  setSort (fieldIndex ("when"), Qt::DescendingOrder);

  connect (this, &CabrilloLog::impl::modelReset, self_, &CabrilloLog::data_changed);
  connect (this, &CabrilloLog::impl::dataChanged, [this] (QModelIndex const& tl, QModelIndex const& br) {
      if (!adding_row_ && !(tl == br)) // ignore single cell changes
                                       // as a another change for the
                                       // whole row will follow
        {
          Q_EMIT self_->data_changed ();
        }
    });

  SQL_error_check (*this, &QSqlTableModel::select);

  SQL_error_check (dupe_query_, &QSqlQuery::prepare,
                   "SELECT "
                   "    frequency "
                   "  FROM "
                   "    cabrillo_log_v2 "
                   "  WHERE "
                   "    call = :call ");
  
  SQL_error_check (export_query_, &QSqlQuery::prepare,
                   "SELECT "
                   "    frequency"
                   "    , \"when\""
                   "    , exchange_sent"
                   "    , call"
                   "    , exchange_rcvd"
                   "  FROM "
                   "    cabrillo_log_v2 "
                   "  ORDER BY "
                   "    \"when\"");
}

void CabrilloLog::impl::create_table ()
{
  QSqlQuery query;
  SQL_error_check (query, static_cast<bool (QSqlQuery::*) (QString const&)> (&QSqlQuery::exec),
                   "CREATE TABLE cabrillo_log_v2 ("
                   "	id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,"
                   "  frequency INTEGER NOT NULL,"
                   "  mode VARCHAR(6) NOT NULL,"
                   "	\"when\" DATETIME NOT NULL,"
                   "	call VARCHAR(20) NOT NULL,"
                   "	exchange_sent VARCHAR(32) NOT NULL,"
                   "	exchange_rcvd VARCHAR(32) NOT NULL"
                   ")");
}

// frequency here is in kHz
QString CabrilloLog::impl::cabrillo_frequency_string (Radio::Frequency frequency) const
{
  QString result;
  auto band = configuration_->bands ()->find (frequency);
  if ("1mm" == band) result = "LIGHT";
  else if ("2mm" == band) result = "241G";
  else if ("2.5mm" == band) result = "134G";
  else if ("4mm" == band) result = "75G";
  else if ("6mm" == band) result = "47G";
  else if ("1.25cm" == band) result = "24G";
  else if ("3cm" == band) result = "10G";
  else if ("6cm" == band) result = "5.7G";
  else if ("9cm" == band) result = "3.4G";
  else if ("13cm" == band) result = "2.3G";
  else if ("23cm" == band) result = "1.2G";
  else if ("33cm" == band) result = "902";
  else if ("70cm" == band) result = "432";
  else if ("1.25m" == band) result = "222";
  else if ("2m" == band) result = "144";
  else if ("4m" == band) result = "70";
  else if ("6m" == band) result = "50";
  else result = QString::number (frequency / 1000ull);
  return result;
}

#include "moc_CabrilloLog.cpp"

CabrilloLog::CabrilloLog (Configuration const * configuration, QObject * parent)
  : QObject {parent}
  , m_ {this, configuration}
{
  Q_ASSERT (configuration);
}

CabrilloLog::~CabrilloLog ()
{
}

QSqlTableModel * CabrilloLog::model ()
{
  return &*m_;
}

namespace
{
  void set_value_maybe_null (QSqlRecord& record, QString const& name, QString const& value)
  {
    if (value.size ())
      {
        record.setValue (name, value);
      }
    else
      {
        record.setNull (name);
      }
  }
}

bool CabrilloLog::add_QSO (Frequency frequency, QString const& mode, QDateTime const& when, QString const& call
                           , QString const& exchange_sent, QString const& exchange_received)
{
  auto record = m_->record ();
  record.setValue ("frequency", frequency);
  record.setValue ("mode", mode);
  if (!when.isNull ())
    {
      record.setValue ("when", when.toMSecsSinceEpoch () / 1000ull);
    }
  else
    {
      record.setNull ("when");
    }
  set_value_maybe_null (record, "call", call);
  set_value_maybe_null (record, "exchange_sent", exchange_sent);
  set_value_maybe_null (record, "exchange_rcvd", exchange_received);
  if (m_->isDirty ())
    {
      m_->revert ();            // discard any uncommitted changes
    }
  m_->setEditStrategy (QSqlTableModel::OnManualSubmit);
  ConditionalTransaction transaction {*m_};
  m_->adding_row_ = true;
  auto ok = m_->insertRecord (-1, record);
  transaction.submit ();
  m_->adding_row_ = false;
  m_->setEditStrategy (QSqlTableModel::OnFieldChange);
  return ok;
}

bool CabrilloLog::dupe (Frequency frequency, QString const& call) const
{
  m_->dupe_query_.bindValue (":call", call);
  SQL_error_check (m_->dupe_query_, static_cast<bool (QSqlQuery::*) ()> (&QSqlQuery::exec));
  auto record = m_->dupe_query_.record ();
  auto frequency_index = record.indexOf ("frequency");
  while (m_->dupe_query_.next ())
    {
      if (m_->configuration_->bands ()->find (m_->dupe_query_.value (frequency_index).toULongLong ())
          == m_->configuration_->bands ()->find (frequency))
        {
          return true;
        }
    }
  return false;
}

void CabrilloLog::reset ()
{
  // synchronize model
  while (m_->canFetchMore ()) m_->fetchMore ();
  if (m_->rowCount ())
    {
      m_->setEditStrategy (QSqlTableModel::OnManualSubmit);
      ConditionalTransaction transaction {*m_};
      SQL_error_check (*m_, &QSqlTableModel::removeRows, 0, m_->rowCount (), QModelIndex {});
      transaction.submit ();
      m_->select ();            // to refresh views
      m_->setEditStrategy (QSqlTableModel::OnFieldChange);
      Q_EMIT data_changed ();
    }
}

void CabrilloLog::export_qsos (QTextStream& stream) const
{
  SQL_error_check (m_->export_query_, static_cast<bool (QSqlQuery::*) ()> (&QSqlQuery::exec));
  auto record = m_->export_query_.record ();
  auto frequency_index = record.indexOf ("frequency");
  //  auto mode_index = record.indexOf ("mode");
  auto when_index = record.indexOf ("when");
  auto call_index = record.indexOf ("call");
  auto sent_index = record.indexOf ("exchange_sent");
  auto rcvd_index = record.indexOf ("exchange_rcvd");
  while (m_->export_query_.next ())
    {
      auto my_call = m_->configuration_->my_callsign ();
      stream << QString {"QSO: %1 DG %2 %3 %4 %5 %6\n"}
                      .arg (m_->cabrillo_frequency_string (m_->export_query_.value (frequency_index).value<Radio::Frequency> ()), 5)
                      .arg (QDateTime::fromMSecsSinceEpoch (m_->export_query_.value (when_index).toULongLong () * 1000ull, Qt::UTC).toString ("yyyy-MM-dd hhmm"))
                      .arg (my_call, -12)
                      .arg (m_->export_query_.value (sent_index).toString (), -13)
                      .arg (m_->export_query_.value (call_index).toString (), -12)
                      .arg (m_->export_query_.value (rcvd_index).toString (), -13);
    }
}

auto CabrilloLog::unique_DXCC_entities (AD1CCty const * countries) const -> worked_set
{
  QSqlQuery q {"SELECT DISTINCT BAND, CALL FROM cabrillo_log_v2"};
  auto band_index = q.record ().indexOf ("band");
  auto call_index = q.record ().indexOf ("call");
  worked_set entities;
  while (q.next ())
    {
      entities << worked_item {q.value (band_index).toString ()
          , countries->lookup (q.value (call_index).toString ()).primary_prefix};
    }
  return entities;
}
