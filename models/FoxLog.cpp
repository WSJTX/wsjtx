#include "FoxLog.hpp"

#include <stdexcept>
#include <utility>
#include <QString>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlTableModel>
#include <QSqlRecord>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QDebug>
#include "Configuration.hpp"
#include "qt_db_helpers.hpp"
#include "pimpl_impl.hpp"

class FoxLog::impl final
  : public QSqlTableModel
{
  Q_OBJECT

public:
  impl (Configuration const * configuration);

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

  Configuration const * configuration_;
  QSqlQuery mutable dupe_query_;
  QSqlQuery mutable export_query_;
};

#include "FoxLog.moc"

namespace 
{
  QString const fox_log_ddl {
                             "CREATE %1 TABLE fox_log%2 ("
                             "	id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,"
                             "	\"when\" DATETIME NOT NULL,"
                             "	call VARCHAR(20) NOT NULL,"
                             "	grid VARCHAR(4),"
                             "	report_sent VARCHAR(3),"
                             "	report_rcvd VARCHAR(3),"
                             "	band VARCHAR(6) NOT NULL"
                             ")"
  };
}

FoxLog::impl::impl (Configuration const * configuration)
  : configuration_ {configuration}
{
  if (!database ().tables ().contains ("fox_log"))
    {
      QSqlQuery query;
      SQL_error_check (query, static_cast<bool (QSqlQuery::*) (QString const&)> (&QSqlQuery::exec),
                       fox_log_ddl.arg ("").arg (""));
    }
  else
    {
      QSqlQuery query;
      // query to check if table has a unique constraint
      SQL_error_check (query, static_cast<bool (QSqlQuery::*) (QString const&)> (&QSqlQuery::exec),
                       "SELECT COUNT(*)"
                       "       FROM sqlite_master"
                       "    WHERE"
                       "       type = 'index' AND tbl_name = 'fox_log'");
      query.next ();
      if (query.value (0).toInt ())
        {
          // update to new schema with no dupe disallowing unique
          // constraint
          database ().transaction ();
          SQL_error_check (query, static_cast<bool (QSqlQuery::*) (QString const&)> (&QSqlQuery::exec),
                           fox_log_ddl.arg ("TEMPORARY").arg ("_backup"));
          SQL_error_check (query, static_cast<bool (QSqlQuery::*) (QString const&)> (&QSqlQuery::exec),
                           "INSERT INTO fox_log_backup SELECT * from fox_log");
          SQL_error_check (query, static_cast<bool (QSqlQuery::*) (QString const&)> (&QSqlQuery::exec),
                           "DROP TABLE fox_log");
          SQL_error_check (query, static_cast<bool (QSqlQuery::*) (QString const&)> (&QSqlQuery::exec),
                           fox_log_ddl.arg ("").arg (""));
          SQL_error_check (query, static_cast<bool (QSqlQuery::*) (QString const&)> (&QSqlQuery::exec),
                           "INSERT INTO fox_log SELECT * from fox_log_backup");
          SQL_error_check (query, static_cast<bool (QSqlQuery::*) (QString const&)> (&QSqlQuery::exec),
                           "DROP TABLE fox_log_backup");
          database ().commit ();
        }
    }

  SQL_error_check (dupe_query_, &QSqlQuery::prepare,
                   "SELECT "
                   "    COUNT(*) "
                   "  FROM "
                   "    fox_log "
                   "  WHERE "
                   "    call = :call "
                   "    AND band = :band");

  SQL_error_check (export_query_, &QSqlQuery::prepare,
                   "SELECT "
                   "    band"
                   "    , \"when\""
                   "    , call"
                   "    , grid"
                   "    , report_sent"
                   "    , report_rcvd "
                   "  FROM "
                   "    fox_log "
                   "  ORDER BY "
                   "    \"when\"");

  setEditStrategy (QSqlTableModel::OnFieldChange);
  setTable ("fox_log");
  setHeaderData (fieldIndex ("when"), Qt::Horizontal, tr ("Date & Time(UTC)"));
  setHeaderData (fieldIndex ("call"), Qt::Horizontal, tr ("Call"));
  setHeaderData (fieldIndex ("grid"), Qt::Horizontal, tr ("Grid"));
  setHeaderData (fieldIndex ("report_sent"), Qt::Horizontal, tr ("Sent"));
  setHeaderData (fieldIndex ("report_rcvd"), Qt::Horizontal, tr ("Rcvd"));
  setHeaderData (fieldIndex ("band"), Qt::Horizontal, tr ("Band"));

  // This descending order by time is important, it makes the view
  // place the latest row at the top, without this the model/view
  // interactions are both sluggish and unhelpful.
  setSort (fieldIndex ("when"), Qt::DescendingOrder);

  SQL_error_check (*this, &QSqlTableModel::select);
}

FoxLog::FoxLog (Configuration const * configuration)
  : m_ {configuration}
{
}

FoxLog::~FoxLog ()
{
}

QSqlTableModel * FoxLog::model ()
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

bool FoxLog::add_QSO (QDateTime const& when, QString const& call, QString const& grid
                      , QString const& report_sent, QString const& report_received
                      , QString const& band)
{
  auto record = m_->record ();
  if (!when.isNull ())
    {
      record.setValue ("when", when.toMSecsSinceEpoch () / 1000);
    }
  else
    {
      record.setNull ("when");
    }
  set_value_maybe_null (record, "call", call);
  set_value_maybe_null (record, "grid", grid);
  set_value_maybe_null (record, "report_sent", report_sent);
  set_value_maybe_null (record, "report_rcvd", report_received);
  set_value_maybe_null (record, "band", band);
  if (m_->isDirty ())
    {
      m_->revert ();            // discard any uncommitted changes
    }
  m_->setEditStrategy (QSqlTableModel::OnManualSubmit);
  ConditionalTransaction transaction {*m_};
  auto ok = m_->insertRecord (-1, record);
  if (ok)
    {
      ok = transaction.submit (false);
    }
  m_->setEditStrategy (QSqlTableModel::OnFieldChange);
  return ok;
}

bool FoxLog::dupe (QString const& call, QString const& band) const
{
  m_->dupe_query_.bindValue (":call", call);
  m_->dupe_query_.bindValue (":band", band);
  SQL_error_check (m_->dupe_query_, static_cast<bool (QSqlQuery::*) ()> (&QSqlQuery::exec));
  m_->dupe_query_.next ();
  return m_->dupe_query_.value (0).toInt ();
}

void FoxLog::reset ()
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

namespace
{
  struct ADIF_field
  {
    explicit ADIF_field (QString const& name, QString const& value)
      : name_ {name}
      , value_ {value}
    {
    }

    QString name_;
    QString value_;
  };

  QTextStream& operator << (QTextStream& os, ADIF_field const& field)
  {
    if (field.value_.size ())
      {
        os << QString {"<%1:%2>%3 "}.arg (field.name_).arg (field.value_.size ()).arg (field.value_);
      }
    return os;
  }
}

void FoxLog::export_qsos (QTextStream& out) const
{
  out << "WSJT-X FT8 DXpedition Mode Fox Log\n<eoh>";

  SQL_error_check (m_->export_query_, static_cast<bool (QSqlQuery::*) ()> (&QSqlQuery::exec));
  auto record = m_->export_query_.record ();
  auto band_index = record.indexOf ("band");
  auto when_index = record.indexOf ("when");
  auto call_index = record.indexOf ("call");
  auto grid_index = record.indexOf ("grid");
  auto sent_index = record.indexOf ("report_sent");
  auto rcvd_index = record.indexOf ("report_rcvd");
  while (m_->export_query_.next ())
    {
      auto when = QDateTime::fromMSecsSinceEpoch (m_->export_query_.value (when_index).toULongLong () * 1000ull, Qt::UTC);
      out << '\n'
          << ADIF_field {"band", m_->export_query_.value (band_index).toString ()}
          << ADIF_field {"mode", "FT8"}
          << ADIF_field {"qso_date", when.toString ("yyyyMMdd")}
          << ADIF_field {"time_on", when.toString ("hhmmss")}
          << ADIF_field {"call", m_->export_query_.value (call_index).toString ()}
          << ADIF_field {"gridsquare", m_->export_query_.value (grid_index).toString ()}
          << ADIF_field {"rst_sent", m_->export_query_.value (sent_index).toString ()}
          << ADIF_field {"rst_rcvd", m_->export_query_.value (rcvd_index).toString ()}
          << ADIF_field {"station_callsign", m_->configuration_->my_callsign ()}
          << ADIF_field {"my_gridsquare", m_->configuration_->my_grid ()}
          << ADIF_field {"operator", m_->configuration_->opCall ()}
          << "<eor>";
    }
  out << endl;
}
