// -*- Mode: C++ -*-
/*
 * From an ADIF file and cty.dat, get a call's DXCC entity and its worked before status
 */

#ifndef LOG_BOOK_H_
#define LOG_BOOK_H_

#include <QObject>
#include <QString>
#include <QScopedPointer>

#include "WorkedBefore.hpp"

class Configuration;
class QByteArray;
class QDateTime;
class CabrilloLog;
class Multiplier;
class FoxLog;

class LogBook final
  : public QObject
{
  Q_OBJECT

public:
  LogBook (Configuration const *);
  ~LogBook ();
  QString const& path () const {return worked_before_.path ();}
  bool add (QString const& call
            , QString const& grid
            , QString const& band
            , QString const& mode
            , QByteArray const& ADIF_record);
  AD1CCty const * countries () const {return worked_before_.countries ();}
  void rescan ();
  void match (QString const& call, QString const& mode, QString const& grid,
              AD1CCty::Record const&, bool& callB4, bool& countryB4,
              bool &gridB4, bool &continentB4, bool& CQZoneB4, bool& ITUZoneB4,
              QString const& currentBand = QString {}) const;
  QByteArray QSOToADIF (QString const& hisCall, QString const& hisGrid, QString const& mode,
                        QString const& rptSent, QString const& rptRcvd, QDateTime const& dateTimeOn,
                        QDateTime const& dateTimeOff, QString const& band, QString const& comments,
                        QString const& name, QString const& strDialFreq, QString const& myCall,
                        QString const& m_myGrid, QString const& m_txPower, QString const& operator_call,
                        QString const& xSent, QString const& xRcvd, QString const& propmode);

  Q_SIGNAL void finished_loading (int worked_before_record_count, QString const& error) const;

  CabrilloLog * contest_log ();
  Multiplier const * multiplier () const;
  FoxLog * fox_log ();

private:
  Configuration const * config_;
  WorkedBefore worked_before_;
  QScopedPointer<CabrilloLog> contest_log_;
  QScopedPointer<Multiplier> mutable multiplier_;
  QScopedPointer<FoxLog> fox_log_;
};

#endif
