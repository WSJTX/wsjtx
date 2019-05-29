#include "logbook.h"

#include <QDateTime>
#include "Configuration.hpp"
#include "AD1CCty.hpp"
#include "models/CabrilloLog.hpp"
#include "models/FoxLog.hpp"

#include "moc_logbook.cpp"

LogBook::LogBook (Configuration const * configuration)
  : config_ {configuration}
  , worked_before_ {configuration}
{
  Q_ASSERT (configuration);
  connect (&worked_before_, &WorkedBefore::finished_loading, this, &LogBook::finished_loading);
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
  if (call.length() > 0)
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

QByteArray LogBook::QSOToADIF (QString const& hisCall, QString const& hisGrid, QString const& mode,
                               QString const& rptSent, QString const& rptRcvd, QDateTime const& dateTimeOn,
                               QDateTime const& dateTimeOff, QString const& band, QString const& comments,
                               QString const& name, QString const& strDialFreq, QString const& myCall,
                               QString const& myGrid, QString const& txPower, QString const& operator_call,
                               QString const& xSent, QString const& xRcvd)
{
  QString t;
  t = "<call:" + QString::number(hisCall.length()) + ">" + hisCall;
  t += " <gridsquare:" + QString::number(hisGrid.length()) + ">" + hisGrid;
  if (mode != "FT4")
    {
      t += " <mode:" + QString::number(mode.length()) + ">" + mode;
    }
  else
    {
      t += " <mode:4>MFSK <submode:" + QString::number(mode.length()) + ">" + mode;
    }
  t += " <rst_sent:" + QString::number(rptSent.length()) + ">" + rptSent;
  t += " <rst_rcvd:" + QString::number(rptRcvd.length()) + ">" + rptRcvd;
  t += " <qso_date:8>" + dateTimeOn.date().toString("yyyyMMdd");
  t += " <time_on:6>" + dateTimeOn.time().toString("hhmmss");
  t += " <qso_date_off:8>" + dateTimeOff.date().toString("yyyyMMdd");
  t += " <time_off:6>" + dateTimeOff.time().toString("hhmmss");
  t += " <band:" + QString::number(band.length()) + ">" + band;
  t += " <freq:" + QString::number(strDialFreq.length()) + ">" + strDialFreq;
  t += " <station_callsign:" + QString::number(myCall.length()) + ">" + myCall;
  t += " <my_gridsquare:" + QString::number(myGrid.length()) + ">" + myGrid;
  if(txPower!="") t += " <tx_pwr:" + QString::number(txPower.length()) + ">" + txPower;
  if(comments!="") t += " <comment:" + QString::number(comments.length()) + ">" + comments;
  if(name!="") t += " <name:" + QString::number(name.length()) + ">" + name;
  if(operator_call!="") t+=" <operator:" + QString::number(operator_call.length()) + ">" + operator_call;
  if (xSent.size ())
    {
      auto words = xSent.split (' ', QString::SkipEmptyParts);
      if (words.size () > 1)
        {
          if (words.back ().toUInt ())
            {
              // assume last word is a serial if there are at least
              // two words and if it is positive numeric
              t += " <stx:" + QString::number (words.back ().size ()) + '>' + words.back ();
            }
          else
            {
              if (words.front ().toUInt () && words.front ().size () > 3) // EU VHF contest mode
                {
                  auto sn_text = words.front ().mid (2);
                  // assume first word is report+serial if there are
                  // at least two words and if the first word less the
                  // first two characters is a positive numeric
                  t += " <stx:" + QString::number (sn_text.size ()) + '>' + sn_text;
                }
            }
        }
    }
  if (xRcvd.size ()) {
    auto words = xRcvd.split (' ', QString::SkipEmptyParts);
    if (words.size () == 2)
      {
        if (words.at (1).toUInt ())
          {
            t += " <srx:" + QString::number (words.at (1).length ()) + ">" + words.at (1);
          }
        else if (words.at (0).toUInt () && words.at (0).size () > 3) // EU VHF contest exchange
          {
            // strip report and set SRX to serial
            t += " <srx:" + QString::number (words.at (0).mid (2).length ()) + ">" + words.at (0).mid (2);
          }
        else
          {
            t += " <state:" + QString::number (words.at (1).length ()) + ">" + words.at (1);
          }
      }
  }
  return t.toLatin1();
}

CabrilloLog * LogBook::contest_log ()
{
  // lazy create of Cabrillo log object instance
  if (!m_contest_log)
    {
      m_contest_log.reset (new CabrilloLog {config_});
    }
  return m_contest_log.data ();
}

FoxLog * LogBook::fox_log ()
{
  // lazy create of Fox log object instance
  if (!m_fox_log)
    {
      m_fox_log.reset (new FoxLog {config_});
    }
  return m_fox_log.data ();
}
