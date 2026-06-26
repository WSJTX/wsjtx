#ifndef WSJTX_LOGBOOK_ADIFQSO_HPP
#define WSJTX_LOGBOOK_ADIFQSO_HPP

#include <QByteArray>
#include <QString>

class QDateTime;

namespace AdifQso
{
  //! Contest-exchange handling required by the ADIF formatter.
  enum class Contest { None, FieldDay, Rtty };

  //! Format a single QSO as an ADIF (.adi) record.
  //!
  //! The returned record has no <eor> terminator; callers append their own.
  //! Values are converted to printable ADI text and stripped of '<'/'>'
  //! before field lengths are calculated.
  QByteArray to_adif (QString const& hisCall, QString const& hisGrid, QString const& mode,
                      QString const& rptSent, QString const& rptRcvd, QDateTime const& dateTimeOn,
                      QDateTime const& dateTimeOff, QString const& band, QString const& comments,
                      QString const& name, QString const& strDialFreq, QString const& myCall,
                      QString const& myGrid, QString const& txPower, QString const& operator_call,
                      QString const& xSent, QString const& xRcvd, QString const& propmode,
                      QString const& satellite, QString const& satmode, QString const& freqRx,
                      Contest contest);
}

#endif // WSJTX_LOGBOOK_ADIFQSO_HPP
