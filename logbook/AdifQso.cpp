#include "logbook/AdifQso.hpp"

#include <QDateTime>
#include <QStringList>

QByteArray AdifQso::to_adif (QString const& hisCall, QString const& hisGrid, QString const& mode,
                             QString const& rptSent, QString const& rptRcvd, QDateTime const& dateTimeOn,
                             QDateTime const& dateTimeOff, QString const& band, QString const& comments,
                             QString const& name, QString const& strDialFreq, QString const& myCall,
                             QString const& myGrid, QString const& txPower, QString const& operator_call,
                             QString const& xSent, QString const& xRcvd, QString const& propmode,
                             QString const& satellite, QString const& satmode, QString const& freqRx,
                             Contest contest)
{
  QString t;

  auto to_adi_text = [] (QString value)
  {
    for (auto& ch : value)
      {
        if (ch == QChar {'<'} || ch == QChar {'>'})
          {
            ch = QChar {};
          }
        else if (ch < QChar {' '} || ch > QChar {'~'})
          {
            ch = QChar {'?'};
          }
      }
    return value.remove (QChar {});
  };

  auto append_field = [&t, &to_adi_text] (QString const& fieldName, QString const& value)
  {
    auto const adiValue = to_adi_text (value);
    if (!t.isEmpty ()) t += ' ';
    t += '<' + fieldName + ':' + QString::number (adiValue.size ()) + '>' + adiValue;
  };
  // Optional fields are omitted entirely when empty.
  auto append_optional = [&] (QString const& fieldName, QString const& value)
  {
    if (value.size ()) append_field (fieldName, value);
  };
  append_field ("call", hisCall);
  append_field ("gridsquare", hisGrid); // emitted even when empty for consistency
  if (mode != "FT4" && mode != "FST4" && mode != "Q65")
    {
      append_field ("mode", mode);
    }
  else
    {
      append_field ("mode", "MFSK");
      append_field ("submode", mode);
    }
  append_field ("rst_sent", rptSent);
  append_field ("rst_rcvd", rptRcvd);
  append_field ("qso_date", dateTimeOn.date ().toString ("yyyyMMdd"));
  append_field ("time_on", dateTimeOn.time ().toString ("hhmmss"));
  append_field ("qso_date_off", dateTimeOff.date ().toString ("yyyyMMdd"));
  append_field ("time_off", dateTimeOff.time ().toString ("hhmmss"));
  append_field ("band", band);
  append_field ("freq", strDialFreq);
  append_field ("station_callsign", myCall);
  append_optional ("my_gridsquare", myGrid);
  append_optional ("tx_pwr", txPower);
  append_optional ("comment", comments);
  append_optional ("name", name);
  append_optional ("operator", operator_call);
  append_optional ("prop_mode", propmode);
  append_optional ("sat_name", satellite);
  append_optional ("sat_mode", satmode);
  append_optional ("freq_rx", freqRx);
  if (xSent.size ())
    {
      auto words = xSent.split (' '
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                                , QString::SkipEmptyParts
#else
                                , Qt::SkipEmptyParts
#endif
                                );
      if (words.size () > 1)
        {
          // Supported contest exchanges are either two tokens, or EU VHF's
          // report+serial token followed by a locator.
          if (words.size () == 2 && words.back ().toUInt ())
            {
              append_field ("stx", words.back ()); // last word is a positive serial
            }
          else if (words.size () == 2 && words.front ().toUInt () && words.front ().size () > 3) // EU VHF contest mode
            {
              append_field ("stx", words.front ().mid (2)); // first word is report+serial
            }
          else
            {
              // non-serial exchange (e.g. Field Day class+section) kept verbatim
              append_field ("STX_STRING", xSent);
            }
        }
    }
  if (xRcvd.size ())
    {
      auto words = xRcvd.split (' '
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                                , QString::SkipEmptyParts
#else
                                , Qt::SkipEmptyParts
#endif
                                );
      if (words.size () > 1)
        {
          // Supported contest exchanges are either two tokens, or EU VHF's
          // report+serial token followed by a locator.
          if (words.size () == 2 && words.back ().toUInt ())
            {
              append_field ("srx", words.back ()); // last word is a positive serial
            }
          else if (words.size () == 2 && words.front ().toUInt () && words.front ().size () > 3) // EU VHF contest exchange
            {
              append_field ("srx", words.front ().mid (2)); // strip report, keep serial
            }
          else if (words.size () == 2 && Contest::FieldDay == contest)
            {
              // include DX as an ARRL_SECT value even though it is not in the
              // ADIF spec ARRL_SECT enumeration, done because N1MM does the same
              append_field ("contest_id", "ARRL-FIELD-DAY");
              append_field ("SRX_STRING", xRcvd);
              append_field ("class", words.front ());
              append_field ("arrl_sect", words.back ());
            }
          else if (words.size () == 2 && Contest::Rtty == contest)
            {
              append_field ("state", words.back ());
            }
          else
            {
              // non-serial exchange with no special handling kept verbatim
              append_field ("SRX_STRING", xRcvd);
            }
        }
    }
  return t.toLatin1 ();
}
