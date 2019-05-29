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
  impl (Configuration const *);

  QVariant data (QModelIndex const& index, int role) const
  {
    auto value = QSqlTableModel::data (index, role);
    if (index.column () == fieldIndex ("when")
        && (Qt::DisplayRole == role || Qt::EditRole == role))
      {
        auto t = QDateTime::fromMSecsSinceEpoch (value.toULongLong () * 1000ull, Qt::UTC);
        if (Qt::DisplayRole == role)
          {
            QLocale locale;
            return locale.toString (t, locale.dateFormat (QLocale::ShortFormat) + " hh:mm:ss");
          }
        value = t;
      }
    return value;
  }

  QString cabrillo_frequency_string (Radio::Frequency frequency) const;

  Configuration const * configuration_;
  QSqlQuery mutable dupe_query_;
  QSqlQuery mutable export_query_;
};

CabrilloLog::impl::impl (Configuration const * configuration)
  : QSqlTableModel {}
  , configuration_ {configuration}
{
  if (!database ().tables ().contains ("cabrillo_log"))
    {
      QSqlQuery query;
      SQL_error_check (query, static_cast<bool (QSqlQuery::*) (QString const&)> (&QSqlQuery::exec),
                       "CREATE TABLE cabrillo_log ("
                       "	id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,"
                       "  frequency INTEGER NOT NULL,"
                       "	\"when\" DATETIME NOT NULL,"
                       "	call VARCHAR(20) NOT NULL,"
                       "	exchange_sent VARCHAR(32) NOT NULL,"
                       "	exchange_rcvd VARCHAR(32) NOT NULL,"
                       "	band VARCHAR(6) NOT NULL"
                       ")");
    }

  SQL_error_check (dupe_query_, &QSqlQuery::prepare,
                   "SELECT "
                   "    COUNT(*) "
                   "  FROM "
                   "    cabrillo_log "
                   "  WHERE "
                   "    call = :call "
                   "    AND band = :band");
  
  SQL_error_check (export_query_, &QSqlQuery::prepare,
                   "SELECT "
                   "    frequency"
                   "    , \"when\""
                   "    , exchange_sent"
                   "    , call"
                   "    , exchange_rcvd"
                   "  FROM "
                   "    cabrillo_log "
                   "  ORDER BY "
                   "    \"when\"");
  
  setEditStrategy (QSqlTableModel::OnFieldChange);
  setTable ("cabrillo_log");
  setHeaderData (fieldIndex ("frequency"), Qt::Horizontal, tr ("Freq(kHz)"));
  setHeaderData (fieldIndex ("when"), Qt::Horizontal, tr ("Date & Time(UTC)"));
  setHeaderData (fieldIndex ("call"), Qt::Horizontal, tr ("Call"));
  setHeaderData (fieldIndex ("exchange_sent"), Qt::Horizontal, tr ("Sent"));
  setHeaderData (fieldIndex ("exchange_rcvd"), Qt::Horizontal, tr ("Rcvd"));
  setHeaderData (fieldIndex ("band"), Qt::Horizontal, tr ("Band"));

  // This descending order by time is important, it makes the view
  // place the latest row at the top, without this the model/view
  // interactions are both sluggish and unhelpful.
  setSort (fieldIndex ("when"), Qt::DescendingOrder);

  SQL_error_check (*this, &QSqlTableModel::select);
}

// frequency here is in kHz
QString CabrilloLog::impl::cabrillo_frequency_string (Radio::Frequency frequency) const
{
  QString result;
  auto band = configuration_->bands ()->find (frequency * 1000ull);
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
  else result = QString::number (frequency);
  return result;
}

CabrilloLog::CabrilloLog (Configuration const * configuration)
  : m_ {configuration}
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

bool CabrilloLog::add_QSO (Frequency frequency, QDateTime const& when, QString const& call
                           , QString const& exchange_sent, QString const& exchange_received)
{
  auto record = m_->record ();
  record.setValue ("frequency", frequency / 1000ull); // kHz
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
  set_value_maybe_null (record, "band", m_->configuration_->bands ()->find (frequency));
  if (m_->isDirty ())
    {
      m_->revert ();            // discard any uncommitted changes
    }
  m_->setEditStrategy (QSqlTableModel::OnManualSubmit);
  ConditionalTransaction transaction {*m_};
  auto ok = m_->insertRecord (-1, record);
  transaction.submit ();
  m_->setEditStrategy (QSqlTableModel::OnFieldChange);
  return ok;
}

bool CabrilloLog::dupe (Frequency frequency, QString const& call) const
{
  m_->dupe_query_.bindValue (":call", call);
  m_->dupe_query_.bindValue (":band", m_->configuration_->bands ()->find (frequency));
  SQL_error_check (m_->dupe_query_, static_cast<bool (QSqlQuery::*) ()> (&QSqlQuery::exec));
  m_->dupe_query_.next ();
  return m_->dupe_query_.value (0).toInt ();
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
    }
}

void CabrilloLog::export_qsos (QTextStream& stream) const
{
  SQL_error_check (m_->export_query_, static_cast<bool (QSqlQuery::*) ()> (&QSqlQuery::exec));
  auto record = m_->export_query_.record ();
  auto frequency_index = record.indexOf ("frequency");
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

QSet<QString> CabrilloLog::unique_DXCC_entities (AD1CCty const& countries) const
{
  QSqlQuery q {"SELECT UNIQUE CALL FROM cabrillo_log"};
  auto call_index = q.record ().indexOf ("call");
  QSet<QString> entities;
  while (q.next ())
    {
      entities << countries.lookup (q.value(call_index).toString ()).primary_prefix;
    }
  return entities;
}
