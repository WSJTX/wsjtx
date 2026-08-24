#ifndef QMAP_DECODE_LABEL_H
#define QMAP_DECODE_LABEL_H

#include <QString>
#include <QtGlobal>

// One decoded-callsign label drawn on top of the wideband waterfall at
// its audio-offset x-position. WideGraph maintains a list of these
// (fed from mainwindow's decode-fetch loop); CPlotter reads the list
// via setDecodeLabels() and overlays them in paintEvent.
//
// Lives in its own header so widegraph.h and plotter.h can both include
// it without widegraph.h having to pull in all of plotter.h's QFrame-
// based widget declarations just for this one struct.
struct WideDecodeLabel
{
  double  freq_khz;        // audio offset (kHz), same scale as m_StartFreq/XfromFreq
  QString callsign;
  int     last_seen_secs;  // decode line's own hhmmss-derived seconds-of-day
  bool    second_half;     // true = decoded from the second 30s half of a 60s Rx interval
};

#endif // QMAP_DECODE_LABEL_H
