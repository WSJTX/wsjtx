#ifndef WSJTX_LOGBOOK_EME_CABRILLO_HPP
#define WSJTX_LOGBOOK_EME_CABRILLO_HPP

#include <QByteArray>
#include <QDate>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

//! A Cabrillo log for the ARRL International EME Contest, made from an ADIF log.
//!
//! The rules (v2.2, 2026) ask for both stations' 4-character grid squares on
//! every QSO line ("QSO: band mode date time mycall mygrid call grid"), count
//! a station once per band from each grid it works from (any mode), give
//! 100 points per QSO and take as multipliers the different grids worked on
//! each band, summed over the bands. One log covers all contest weekends.
namespace EmeCabrillo
{
  //! One ADIF record: field name (upper case) -> value.
  using Record = QHash<QString, QString>;

  //! The records of an ADIF file (header skipped; lengths counted in bytes).
  QVector<Record> read_adif (QByteArray const& adif);

  //! Cabrillo band of a record ("50", "144", "1.2G", "10G" ...), empty if it
  //! is not on an EME contest band or is a satellite QSO.
  QString band_code (Record const&);

  //! Cabrillo mode of a record: CW, PH or DG (any digital mode).
  QString mode_code (Record const&);

  //! A 4-character grid square ("JN68") from a grid, empty if it is none.
  QString grid4 (QString const& grid);

  //! Identity of a logged QSO: call, QSO_DATE and the minute of TIME_ON.
  QString record_key (Record const&);

  struct Qso
  {
    QString band;
    QString mode;
    QDate date;
    QString time;               // hhmm
    QString my_call;
    QString my_grid;
    QString call;
    QString grid;
    QString key;                // record_key ()
    bool filled {false};        // grid taken from another QSO with the call
    bool entered {false};       // grid typed in by the user
  };

  struct Selection
  {
    QVector<Qso> qsos;          // the entry, in time order
    QVector<Qso> dupes;         // worked before on the band from that grid
    QVector<Qso> no_grid;       // no grid known: left out
    QVector<Qso> need_grid;     // logged without a grid (not dupes), time order
    QSet<QString> bands;        // bands with QSOs on the days (before the band choice)
    int satellite {0};
    int other {0};              // not an EME band, or a band not chosen
  };

  //! The QSOs of an entry: made on one of days, on a band in bands (all
  //! bands when bands is empty). fixes = grids typed in for records logged
  //! without one (record_key -> grid); they win over a grid found in
  //! another QSO with the same call.
  Selection select (QVector<Record> const& records, QSet<QDate> const& days,
                    QString const& my_call, QString const& my_grid,
                    QSet<QString> const& bands = {},
                    QHash<QString, QString> const& fixes = {});

  struct BandScore
  {
    QString band;
    int qsos;
    int grids;
  };

  struct Score
  {
    int points {0};             // 100 per QSO
    int multipliers {0};        // different grids per band, summed
    qint64 claimed {0};         // points x multipliers
    QVector<BandScore> bands;   // lowest band first
  };

  Score score (QVector<Qso> const&);

  struct Header
  {
    QString callsign;
    QString grid;
    QString category_operator;  // SINGLE-OP or MULTI-OP
    QString operators;
    QString location;           // ARRL section, or DX
    QString name;
    QString address;
    QString city;
    QString state;
    QString postcode;
    QString country;
    QString email;
    QString created_by;
  };

  //! The Cabrillo 3.0 file (lines end in '\n'; blank header items left out).
  QString cabrillo_text (Header const&, QVector<Qso> const&);
}

#endif // WSJTX_LOGBOOK_EME_CABRILLO_HPP
