#include "decodedtext.h"

#include <QStringList>
#include <QRegularExpression>
#include <QDebug>

extern "C" {
  bool stdmsg_(char const * msg, fortran_charlen_t);
}

namespace
{
  QRegularExpression words_re {R"(^(?:(?<word1>(?:CQ|DE|QRZ)(?:\s?DX|\s(?:[A-Z]{1,4}|\d{3}))|[A-Z0-9/]+|\.{3})\s)(?:(?<word2>[A-Z0-9/]+)(?:\s(?<word3>[-+A-Z0-9]+)(?:\s(?<word4>(?:OOO|(?!RR73)[A-R]{2}[0-9]{2})))?)?)?)"};
}

DecodedText::DecodedText (QString const& the_string)
  : string_ {the_string.left (the_string.indexOf (QChar::Nbsp))} // discard appended info
  , padding_ {string_.indexOf (" ") > 4 ? 2 : 0} // allow for
                                                    // seconds
  , message_ {string_.mid (column_qsoText + padding_).trimmed ()}
  , is_standard_ {false}
{
//  qDebug () << "DecodedText: the_string:" << the_string << "Nbsp pos:" << the_string.indexOf (QChar::Nbsp);
  if (message_.length() >= 1)
    {
       message0_ = message_.left(36);
       message_ = message_.left(36).remove (QRegularExpression {"[<>]"});
      int i1 = message_.indexOf ('\r');
      if (i1 > 0)
        {
          message_ = message_.left (i1 - 1);
        }
      if (message_.contains (QRegularExpression {"^(CQ|QRZ)\\s"}))
        {
          // TODO this magic position 16 is guaranteed to be after the
          // last space in a decoded CQ or QRZ message but before any
          // appended DXCC entity name or worked before information
          auto eom_pos = message_.indexOf (' ', 16);
          // we always want at least the characters to position 16
          if (eom_pos < 16) eom_pos = message_.size () - 1;
          // remove DXCC entity and worked B4 status. TODO need a better way to do this
          message_ = message_.left (eom_pos + 1);
        }
      // stdmsg is a Fortran routine that packs the text, unpacks it
      // and compares the result
      auto message_c_string = message0_.toLocal8Bit ();
      message_c_string += QByteArray {37 - message_c_string.size (), ' '};
      is_standard_ = stdmsg_(message_c_string.constData(),37);
    }
};

QStringList DecodedText::messageWords () const
{
  if(is_standard_) {
    // extract up to the first four message words
    QString t=message_;
    if(t.left(4)=="TU; ") t=message_.mid(4,-1);
    return words_re.match(t).capturedTexts();
  }
  // simple word split for free text messages
  auto words = message_.split (' ', QString::SkipEmptyParts);
  // add whole message as item 0 to mimic RE capture list
  words.prepend (message_);
  return words;
}

QString DecodedText::CQersCall() const
{
  QRegularExpression callsign_re {R"(^(CQ|DE|QRZ)(\s?DX|\s([A-Z]{1,4}|\d{3}))?\s(?<callsign>[A-Z0-9/]{2,})(\s[A-R]{2}[0-9]{2})?)"};
  return callsign_re.match (message_).captured ("callsign");
}


bool DecodedText::isJT65() const
{
    return string_.indexOf("#") == column_mode + padding_;
}

bool DecodedText::isJT9() const
{
    return string_.indexOf("@") == column_mode + padding_;
}

bool DecodedText::isTX() const
{
    int i = string_.indexOf("Tx");
    return (i >= 0 && i < 15); // TODO guessing those numbers. Does Tx ever move?
}

bool DecodedText::isLowConfidence () const
{
  return QChar {'?'} == string_.mid (padding_ + column_qsoText + 21, 1);
}

int DecodedText::frequencyOffset() const
{
    return string_.mid(column_freq + padding_,4).toInt();
}

int DecodedText::snr() const
{
  int i1=string_.indexOf(" ")+1;
  return string_.mid(i1,3).toInt();
}

float DecodedText::dt() const
{
  return string_.mid(column_dt + padding_,5).toFloat();
}

/*
2343 -11  0.8 1259 # YV6BFE F6GUU R-08
2343 -19  0.3  718 # VE6WQ SQ2NIJ -14
2343  -7  0.3  815 # KK4DSD W7VP -16
2343 -13  0.1 3627 @ CT1FBK IK5YZT R+02

0605  Tx      1259 # CQ VK3ACF QF22
*/

// find and extract any report. Returns true if this is a standard message
bool DecodedText::report(QString const& myBaseCall, QString const& dxBaseCall, /*mod*/QString& report) const
{
  if (message_.size () < 1) return false;

  QStringList const& w = message_.split(" ",QString::SkipEmptyParts);
  if (w.size ()
      && is_standard_ && (w[0] == myBaseCall
                          || w[0].endsWith ("/" + myBaseCall)
                          || w[0].startsWith (myBaseCall + "/")
                          || (w.size () > 1 && !dxBaseCall.isEmpty ()
                              && (w[1] == dxBaseCall
                                  || w[1].endsWith ("/" + dxBaseCall)
                                  || w[1].startsWith (dxBaseCall + "/")))))
    {
      QString tt="";
      if(w.size() > 2) tt=w[2];
      bool ok;
      auto i1=tt.toInt(&ok);
      if (ok and i1>=-50 and i1<50)
        {
          report = tt;
        }
      else
        {
          if (tt.mid(0,1)=="R")
            {
              i1=tt.mid(1).toInt(&ok);
              if(ok and i1>=-50 and i1<50)
                {
                  report = tt.mid(1);
                }
            }
        }
    }
  return is_standard_;
}

// get the first text word, usually the call
QString DecodedText::call() const
{
  return words_re.match (message_).captured ("word1");
}

// get the second word, most likely the de call and the third word, most likely grid
void DecodedText::deCallAndGrid(/*out*/QString& call, QString& grid) const
{
  auto const& match = words_re.match (message_);
  call = match.captured ("word2");
  grid = match.captured ("word3");
  if ("R" == grid) grid = match.captured ("word4");
}

unsigned DecodedText::timeInSeconds() const
{
  return 3600 * string_.mid (column_time, 2).toUInt ()
    + 60 * string_.mid (column_time + 2, 2).toUInt()
    + (padding_ ? string_.mid (column_time + 2 + padding_, 2).toUInt () : 0U);
}

/*
2343 -11  0.8 1259 # YV6BFE F6GUU R-08
2343 -19  0.3  718 # VE6WQ SQ2NIJ -14
2343  -7  0.3  815 # KK4DSD W7VP -16
2343 -13  0.1 3627 @ CT1FBK IK5YZT R+02

0605  Tx      1259 # CQ VK3ACF QF22
*/

QString DecodedText::report() const // returns a string of the SNR field with a leading + or - followed by two digits
{
    int sr = snr();
    if (sr<-50)
        sr = -50;
    else
        if (sr > 49)
            sr = 49;

    QString rpt;
    rpt.sprintf("%d",abs(sr));
    if (sr > 9)
        rpt = "+" + rpt;
    else
        if (sr >= 0)
            rpt = "+0" + rpt;
        else
            if (sr >= -9)
                rpt = "-0" + rpt;
            else
                rpt = "-" + rpt;
    return rpt;
}
