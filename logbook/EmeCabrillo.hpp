#ifndef WSJTX_LOGBOOK_EME_CABRILLO_HPP
#define WSJTX_LOGBOOK_EME_CABRILLO_HPP

#include <QByteArray>
#include <QDate>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

//! A Cabrillo log for the ARRL International EME Contest, made from an ADIF log.
//!
//! The rules ask for both stations' 4-character grid squares on
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
  //! is not on an EME contest band, is a satellite QSO, or is logged with
  //! another PROP_MODE than EME (records without PROP_MODE - WSJT-X writes
  //! none - may be EME).
  QString band_code (Record const&);

  //! Cabrillo mode of a record: CW, PH or DG (any digital mode).
  QString mode_code (Record const&);

  //! The mode as the log names it: SUBMODE if there is one (Q65, FT8,
  //! MSK144 ...), else MODE (CW, SSB ...). The ADIF log holds every mode, so
  //! QSOs that were not EME can be told apart, and left out, by this.
  QString mode_name (Record const&);

  //! Text as plain ASCII, as Cabrillo wants: accents taken off (Jürgen ->
  //! Jurgen), a few letters spelled out; what is still not ASCII becomes '?'.
  QString ascii (QString const&);

  //! A 4-character grid square ("JN68") from a grid, empty if it is none.
  QString grid4 (QString const& grid);

  //! The QSO's date: QSO_DATE, else QSO_DATE_OFF (ARRL's ADIF field list
  //! allows either); invalid if neither is a date.
  QDate record_date (Record const&);

  //! The QSO's time, hhmm: TIME_ON, else TIME_OFF; empty if neither is a
  //! time. Such a QSO is reported, never written as 0000.
  QString record_time (Record const&);

  //! Identity of a logged QSO: call, record_date () and record_time ().
  QString record_key (Record const&);

  //! A log made ready for select (): everything that does not depend on
  //! the days, bands, modes or typed grids is worked out once here, so that
  //! a selection only goes through the QSOs on the chosen days.
  struct Log
  {
    QVector<Record> records;            // in time order
    QVector<QDate> dates;               // record_date () of each, so in order too
    QHash<QString, QString> known;      // call -> the newest grid logged for it
  };

  Log prepare (QVector<Record> const& records);

  //! The contest's two band groups have their own weekends (for example
  //! 2.3 GHz and up on two weekends, 50-1296 MHz on two later ones). Which
  //! weekend is which is taken from the log; no dates are built in.
  //! Returns "50-1296 MHz" or "2.3 GHz and up".
  QString band_group (QString const& band_code);

  //! The chosen days as weekends: runs of consecutive dates, in date order.
  QVector<QVector<QDate>> weekends (QSet<QDate> const& days);

  //! Records of several logs (e.g. one per PC of a multi-operator station)
  //! as one list, each tagged "_FILE" = the index of its log. The same QSO
  //! (record_key and band) in a later log is a copy (one log imported into
  //! the other) and is left out, into copies if given; repeats inside one
  //! log are kept - they are the contest's dupes. A copy may know more
  //! than the record kept: its grid is taken if that one has none, and a
  //! different grid is noted in "_GRID2" for the user to settle, so that
  //! the order of the logs does not decide the exchange.
  QVector<Record> merge (QVector<QVector<Record>> const& logs, QVector<Record> * copies = nullptr);

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
    bool dupe {false};          // worked before on the band from that grid
    QString grid2;              // another log has this other grid for the QSO
    QString mode_name;          // mode_name ()
    QString op;                 // OPERATOR
  };

  struct Weekend
  {
    QVector<QDate> days;
    QString group;              // band_group () with more QSOs; empty if equal or none
    QMap<QString, int> qsos;    // band group -> QSOs that weekend
  };

  struct Selection
  {
    QVector<Qso> qsos;          // the entry, in time order
    QVector<Qso> dupes;         // worked before on the band from that grid
    QVector<Qso> no_grid;       // no grid known: left out
    QVector<Qso> need_grid;     // logged without a grid, time order; also those
                                // a filled or typed grid makes dupes, so that
                                // the grid can still be corrected
    QMap<QString, int> modes;   // mode_name -> QSOs on the days and bands
    int mode_out {0};           // left out: their mode was not chosen
    QSet<QString> bands;        // bands with QSOs on the days (before the band choice)
    QStringList no_time;        // calls of QSOs without a usable time: left out
    int not_eme {0};            // logged with another PROP_MODE than EME or SAT
    QVector<Weekend> weekends;  // the days as weekends, with their band group
                                // (from the QSOs on the bands and modes chosen)
    QVector<Qso> off_weekend;   // on a band of the other group than their weekend's
    QStringList operators;      // OPERATOR fields of the QSOs kept, sorted
    int satellite {0};
    int other {0};              // not an EME band, or a band not chosen
  };

  //! The QSOs of an entry: made on one of days, on a band in bands (all
  //! bands when bands is empty). fixes = grids typed in for records logged
  //! without one (record_key -> grid); they win over a grid found in
  //! another QSO with the same call, and fill other QSOs with that call
  //! that have no grid logged anywhere. A QSO on a band whose group is not
  //! its weekend's group is listed in off_weekend and, if
  //! leave_out_off_weekend, left out. modes (mode_name values; empty = all)
  //! leaves the other modes out.
  Selection select (Log const& log, QSet<QDate> const& days,
                    QString const& my_call, QString const& my_grid,
                    QSet<QString> const& bands = {},
                    QHash<QString, QString> const& fixes = {},
                    bool leave_out_off_weekend = false,
                    QSet<QString> const& modes = {});

  //! The same from records as read: select (prepare (records), ...).
  Selection select (QVector<Record> const& records, QSet<QDate> const& days,
                    QString const& my_call, QString const& my_grid,
                    QSet<QString> const& bands = {},
                    QHash<QString, QString> const& fixes = {},
                    bool leave_out_off_weekend = false,
                    QSet<QString> const& modes = {});

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
