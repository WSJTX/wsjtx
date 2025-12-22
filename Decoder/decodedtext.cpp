#include "decodedtext.h"

#include <QStringList>
#include <QRegularExpression>
#include <QDebug>
#include "qt_helpers.hpp"

extern "C" {
  bool stdmsg_(char const * msg, fortran_charlen_t);
}

namespace
{
  QRegularExpression tokens_re {R"(
^
  (?:(?<dual>[A-Z0-9/]+)\sRR73;\s)? # dual reply DXpedition message
  (?:
    (?<word1>
      (?:CQ|DE|QRZ)
      (?:\s?DX|\s
        (?:[A-Z]{1,4}|\d{3})  # directional CQ
      )
      | [A-Z0-9/]+            # DX call
      |\.{3}                  # unknown hash code
    )\s
  )
  (?:
    (?<word2>[A-Z0-9/]+)      # DE call
    (?:\s
      (?<word3>[-+A-Z0-9]+)   # report
      (?:\s
        (?<word4>
          (?:
            OOO               # EME
            | (?!RR73)[A-R]{2}[0-9]{2} # grid square (not RR73)
            | 5[0-9]{5}       # EU VHF Contest report & serial
          )
        )
        (?:\s
          (?<word5>[A-R]{2}[0-9]{2}[A-X]{2}) # EU VHF Contest grid locator
        )?
      )?
    )?
  )?
)"
                               , QRegularExpression::ExtendedPatternSyntaxOption};
}

DecodedText::DecodedText (QString const& the_string)
  : string_ {the_string.left (the_string.indexOf (QChar::Nbsp))} // discard appended info
  , clean_string_ {string_}
  , padding_ {string_.indexOf (" ") > 4 ? 2 : 0} // allow for
                                                    // seconds
  , message_ {string_.mid (column_qsoText + padding_).trimmed ()}
  , is_standard_ {false}
{
  // discard appended AP info
  clean_string_.replace (QRegularExpression {R"(^(.*?)(?:\?\s)?[aq][0-9].*$)"}, "\\1");

//  qDebug () << "DecodedText: the_string:" << the_string << "Nbsp pos:" << the_string.indexOf (QChar::Nbsp);
  if (message_.length() >= 1)
    {
// remove appended confidence (?) and ap designators before truncating the message
       message_ = clean_string_.mid (column_qsoText + padding_).trimmed ();
       message0_ = message_.left(37);
       message_ = message_.left(37).remove (QRegularExpression {"[<>]"});
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
    return tokens_re.match(t).capturedTexts();
  }
  // simple word split for free text messages
  auto words = message_.split (' ', SkipEmptyParts);
  // add whole message and two empty strings as item 0 & 1 to mimic RE
  // capture list
  words.prepend (QString {});
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
  return QChar {'?'} == string_.mid (padding_ + column_qsoText + 36, 1);
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

  QStringList const& w = message_.split(" ", SkipEmptyParts);
  int offset {0};
  if (w.size () > 2)
    {
      if ("RR73;" == w[1] && w.size () > 3)
        {
          offset = 2;
        }
      if (is_standard_ && (w[offset] == myBaseCall
                           || w[offset].endsWith ("/" + myBaseCall)
                           || w[offset].startsWith (myBaseCall + "/")
                           || (w.size () > offset + 1 && !dxBaseCall.isEmpty ()
                               && (w[offset + 1] == dxBaseCall
                                   || w[offset + 1].endsWith ("/" + dxBaseCall)
                                   || w[offset + 1].startsWith (dxBaseCall + "/")))))
        {
          bool ok;
          auto tt = w[offset + 2];
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
    }
  return is_standard_;
}

// get the first text word, usually the call
QString DecodedText::call() const
{
  return tokens_re.match (message_).captured ("word1");
}

// get the second word, most likely the de call and the third word, most likely grid
void DecodedText::deCallAndGrid(/*out*/QString& call, QString& grid) const
{
  auto msg = message_;
  auto p = msg.indexOf ("; ");
  if (p >= 0)
    {
      msg = msg.mid (p + 2);
    }
  auto const& match = tokens_re.match (msg);
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

QString DecodedText::report() const // returns a string of the SNR field with a leading + or - followed by two digits
{
    int sr = snr();
    if (sr<-50)
        sr = -50;
    else
        if (sr > 49)
            sr = 49;

    QString rpt;
    rpt = rpt.asprintf("%d",abs(sr));
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
