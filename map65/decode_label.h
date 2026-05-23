#ifndef MAP65_DECODE_LABEL_H
#define MAP65_DECODE_LABEL_H

#include <QString>
#include <QtGlobal>

// One decoded-callsign label drawn on top of the WideGraph waterfall
// at its audio-offset x-position. WideGraph maintains a list of these
// (refreshed each time mainwindow taps the decode-append path); CPlotter
// reads the list via setDecodeLabels() and overlays them in paintEvent.
//
// Lives in its own header so widegraph.h and plotter.h can both include
// it without creating a circular dependency between the two larger
// headers.
//
// Mirrors qmap/decode_label.h — MAP65 port (N6NU 2026-05-12) matches
// the QMAP feature exactly.
struct DecodeLabel {
    double  freq_khz;       // audio offset (kHz), straight from the decode line
    QString callsign;       // sender's call extracted from the message field
    qint64  last_seen_ms;   // wall-clock of most recent fresh decode
    int     hits;           // for tie-breaking when stacking
    bool    is_jt65;        // true = JT65 (orange label); false = Q65 (yellow)

    DecodeLabel(double f, const QString& c, qint64 t, int h, bool jt65)
        : freq_khz(f), callsign(c), last_seen_ms(t), hits(h), is_jt65(jt65) {}
    DecodeLabel() : freq_khz(0), last_seen_ms(0), hits(0), is_jt65(false) {}
};

#endif // MAP65_DECODE_LABEL_H
