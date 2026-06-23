#include "logbook.h"

#include <QDateTime>
#include "Configuration.hpp"
#include "AD1CCty.hpp"
#include "Multiplier.hpp"
#include "logbook/AD1CCty.hpp"
#include "models/CabrilloLog.hpp"
#include "models/FoxLog.hpp"
#include "logbook/AdifQso.hpp"

#include "moc_logbook.cpp"

LogBook::LogBook (Configuration const * configuration)
  : config_ {configuration}
  , worked_before_ {configuration}
{
  Q_ASSERT (configuration);
  connect (&worked_before_, &WorkedBefore::finished_loading, this, &LogBook::finished_loading);
}

LogBook::~LogBook ()
{
}

void LogBook::match (QString const& call, QString const& mode, QString const& grid,
                     AD1CCty::Record const& looked_up,
                     bool& callB4,
                     bool& countryB4,
                     bool& gridB4,
                     bool& continentB4,
                     bool& CQZoneB4,
                     bool& ITUZoneB4,
                     QString const& band) const
{
  // Default every flag so callers reading them after an empty call see defined
  // values: "not worked" for call/grid, and "do not flag" for entity-derived
  // categories (mirroring the unknown-entity policy below).
  callB4 = false;
  gridB4 = false;
  countryB4 = true;
  continentB4 = true;
  CQZoneB4 = true;
  ITUZoneB4 = true;
  if (call.size() > 0)
    {
      auto const& mode_to_check = (config_ && !config_->highlight_by_mode ()) ? QString {} : mode;
      callB4 = worked_before_.call_worked (call, mode_to_check, band);
      gridB4 = worked_before_.grid_worked(grid, mode_to_check, band);
      auto const& countryName = looked_up.entity_name;
      if (countryName.size ())
        {
          countryB4 = worked_before_.country_worked (countryName, mode_to_check, band);
          continentB4 = worked_before_.continent_worked (looked_up.continent, mode_to_check, band);
          CQZoneB4 = worked_before_.CQ_zone_worked (looked_up.CQ_zone, mode_to_check, band);
          ITUZoneB4 = worked_before_.ITU_zone_worked (looked_up.ITU_zone, mode_to_check, band);
        }
      else
        {
          countryB4 = true;  // we don't want to flag unknown entities
          continentB4 = true;
          CQZoneB4 = true;
          ITUZoneB4 = true;
        }
    }
}

bool LogBook::add (QString const& call
                   , QString const& grid
                   , QString const& band
                   , QString const& mode
                   , QByteArray const& ADIF_record)
{
  return worked_before_.add (call, grid, band, mode, ADIF_record);
}

void LogBook::rescan ()
{
  worked_before_.reload ();
}

QString const LogBook::cty_version() const
{
  return worked_before_.cty_version();
}

QByteArray LogBook::QSOToADIF (QString const& hisCall, QString const& hisGrid, QString const& mode,
                               QString const& rptSent, QString const& rptRcvd, QDateTime const& dateTimeOn,
                               QDateTime const& dateTimeOff, QString const& band, QString const& comments,
                               QString const& name, QString const& strDialFreq, QString const& myCall,
                               QString const& myGrid, QString const& txPower, QString const& operator_call,
                               QString const& xSent, QString const& xRcvd, QString const& propmode,
                               QString const& satellite, QString const& satmode, QString const& freqRx)
{
  Q_ASSERT (config_);
  auto contest = AdifQso::Contest::None;
  switch (config_->special_op_id ())
    {
    case Configuration::SpecialOperatingActivity::FIELD_DAY:
      contest = AdifQso::Contest::FieldDay;
      break;
    case Configuration::SpecialOperatingActivity::RTTY:
      contest = AdifQso::Contest::Rtty;
      break;
    default:
      break;
    }
  return AdifQso::to_adif (hisCall, hisGrid, mode, rptSent, rptRcvd, dateTimeOn, dateTimeOff,
                           band, comments, name, strDialFreq, myCall, myGrid, txPower, operator_call,
                           xSent, xRcvd, propmode, satellite, satmode, freqRx, contest);
}

CabrilloLog * LogBook::contest_log ()
{
  // lazy create of Cabrillo log object instance
  if (!contest_log_)
    {
      contest_log_.reset (new CabrilloLog {config_});
      if (!multiplier_)
        {
          multiplier_.reset (new Multiplier {countries ()});
        }
      connect (contest_log_.data (), &CabrilloLog::data_changed, [this] () {
          multiplier_->reload (contest_log_.data ());
        });
    }
  return contest_log_.data ();
}

Multiplier const * LogBook::multiplier () const
{
  // lazy create of Multiplier object instance
  if (!multiplier_)
    {
      multiplier_.reset (new Multiplier {countries ()});
    }
  return multiplier_.data ();
}

FoxLog * LogBook::fox_log ()
{
  // lazy create of Fox log object instance
  if (!fox_log_)
    {
      fox_log_.reset (new FoxLog {config_});
    }
  return fox_log_.data ();
}
