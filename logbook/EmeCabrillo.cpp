#include "EmeCabrillo.hpp"

#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

namespace EmeCabrillo
{
  namespace
  {
    struct Band
    {
      char const * adif;        // ADIF BAND
      char const * code;        // Cabrillo
      double low;               // MHz
      double high;
    };

    // EME contest bands, lowest first: ADIF name, Cabrillo code, edges.
    Band const bands_table[] {
      {"6m", "50", 50., 54.},
      {"4m", "70", 70., 71.},
      {"2m", "144", 144., 148.},
      {"1.25m", "222", 222., 225.},
      {"70cm", "432", 420., 450.},
      {"33cm", "902", 902., 928.},
      {"23cm", "1.2G", 1240., 1300.},
      {"13cm", "2.3G", 2300., 2450.},
      {"9cm", "3.4G", 3300., 3500.},
      {"6cm", "5.7G", 5650., 5925.},
      {"3cm", "10G", 10000., 10500.},
      {"1.25cm", "24G", 24000., 24250.},
      {"6mm", "47G", 47000., 47200.},
      {"4mm", "75G", 75500., 81000.},
      {"2.5mm", "122G", 119980., 123000.},
      {"2mm", "134G", 134000., 149000.},
      {"1mm", "241G", 241000., 250000.},
    };

    int band_order (QString const& code)
    {
      for (int i = 0; i < int (sizeof bands_table / sizeof bands_table[0]); ++i)
        if (code == QLatin1String {bands_table[i].code}) return i;
      return 99;
    }

    bool is_satellite (Record const& record)
    {
      return !record.value ("SAT_NAME").trimmed ().isEmpty ()
        || record.value ("PROP_MODE").trimmed ().toUpper () == "SAT";
    }

    QString padded_time (QString const& time_on)
    {
      return (time_on.trimmed () + "0000").left (4);
    }

    bool earlier (Record const& a, Record const& b)
    {
      auto const da = a.value ("QSO_DATE"), db = b.value ("QSO_DATE");
      if (da != db) return da < db;
      return (a.value ("TIME_ON") + "000000").left (6) < (b.value ("TIME_ON") + "000000").left (6);
    }
  }

  QVector<Record> read_adif (QByteArray const& adif)
  {
    static QRegularExpression const tag {QStringLiteral ("<([A-Za-z0-9_]+)(?::(\\d+)(?::[A-Za-z])?)?>")};
    QVector<Record> records;
    // the header, if there is one, ends at <EOH>
    int position = 0;
    auto const eoh = adif.toUpper ().indexOf ("<EOH>");
    if (eoh >= 0) position = eoh + 5;
    // work on Latin-1 text so that offsets are byte offsets, as ADIF counts
    auto const text = QString::fromLatin1 (adif);
    Record current;
    while (true)
      {
        auto const match = tag.match (text, position);
        if (!match.hasMatch ()) break;
        auto const name = match.captured (1).toUpper ();
        position = match.capturedEnd ();
        if (name == "EOR")
          {
            if (!current.isEmpty ()) records << current;
            current.clear ();
          }
        else if (match.capturedLength (2))
          {
            auto const length = match.captured (2).toInt ();
            current[name] = QString::fromUtf8 (adif.mid (position, length));
            position += length;
          }
      }
    return records;
  }

  QString band_code (Record const& record)
  {
    if (is_satellite (record)) return {};
    auto const band = record.value ("BAND").trimmed ().toLower ();
    if (!band.isEmpty ())
      {
        for (auto const& b : bands_table)
          if (band == QLatin1String {b.adif}) return QString::fromLatin1 (b.code);
        return {};
      }
    bool ok;
    auto const mhz = record.value ("FREQ").trimmed ().toDouble (&ok);
    if (ok)
      for (auto const& b : bands_table)
        if (b.low <= mhz && mhz <= b.high) return QString::fromLatin1 (b.code);
    return {};
  }

  QString mode_code (Record const& record)
  {
    auto const mode = record.value ("MODE").trimmed ().toUpper ();
    if (mode == "CW") return "CW";
    static QStringList const phone {"SSB", "USB", "LSB", "AM", "FM", "DIGITALVOICE"};
    return phone.contains (mode) ? "PH" : "DG";
  }

  QString grid4 (QString const& grid)
  {
    static QRegularExpression const pattern {QStringLiteral ("\\A[A-R]{2}[0-9]{2}\\z")};
    auto const g = grid.trimmed ().toUpper ().left (4);
    // RR73 is the sign-off, never a grid (as everywhere in WSJT-X)
    return g != "RR73" && pattern.match (g).hasMatch () ? g : QString {};
  }

  QString record_key (Record const& record)
  {
    return record.value ("CALL").trimmed ().toUpper () + '|' + record.value ("QSO_DATE").trimmed ()
      + '|' + padded_time (record.value ("TIME_ON"));
  }

  QString band_group (QString const& band_code)
  {
    // 50 MHz to 1296 MHz are the first seven bands of the table
    return band_order (band_code) <= band_order ("1.2G") ? QStringLiteral ("50-1296 MHz")
      : QStringLiteral ("2.3 GHz and up");
  }

  QVector<QVector<QDate>> weekends (QSet<QDate> const& days)
  {
    QList<QDate> sorted;
    for (auto const& day : days) if (day.isValid ()) sorted << day;
    std::sort (sorted.begin (), sorted.end ());
    QVector<QVector<QDate>> result;
    for (auto const& day : sorted)
      {
        if (result.size () && result.last ().last ().daysTo (day) == 1)
          result.last () << day;
        else
          result << QVector<QDate> {day};
      }
    return result;
  }

  QVector<Record> merge (QVector<QVector<Record>> const& logs, QVector<Record> * copies)
  {
    QHash<QString, int> first;          // QSO -> the log it was first seen in
    QVector<Record> result;
    for (int i = 0; i < logs.size (); ++i)
      for (auto record : logs[i])
        {
          auto band = band_code (record);
          if (band.isEmpty ()) band = record.value ("BAND").trimmed ().toLower ();
          auto const key = record_key (record) + '|' + band;
          record["_FILE"] = QString::number (i);
          if (first.value (key, i) != i)
            {
              if (copies) *copies << record;
              continue;
            }
          first.insert (key, i);
          result << record;
        }
    return result;
  }

  Selection select (QVector<Record> const& records, QSet<QDate> const& days,
                    QString const& my_call, QString const& my_grid,
                    QSet<QString> const& bands, QHash<QString, QString> const& fixes,
                    bool leave_out_off_weekend)
  {
    Selection result;
    auto sorted = records;
    std::stable_sort (sorted.begin (), sorted.end (), earlier);
    // each weekend's band group = the group with more QSOs that weekend
    QHash<QDate, QString> group_of_day;
    for (auto const& days_of_weekend : weekends (days))
      {
        Weekend weekend;
        weekend.days = days_of_weekend;
        for (auto const& record : sorted)
          {
            auto const date = QDate::fromString (record.value ("QSO_DATE").trimmed (), "yyyyMMdd");
            if (record.value ("CALL").trimmed ().isEmpty () || !days_of_weekend.contains (date)) continue;
            auto const band = band_code (record);
            if (!band.isEmpty ()) ++weekend.qsos[band_group (band)];
          }
        int most = 0;
        for (auto i = weekend.qsos.cbegin (); i != weekend.qsos.cend (); ++i)
          {
            if (i.value () > most)
              {
                most = i.value ();
                weekend.group = i.key ();
              }
            else if (i.value () == most)
              {
                weekend.group.clear ();     // as many of each: can't tell
              }
          }
        for (auto const& day : days_of_weekend) group_of_day[day] = weekend.group;
        result.weekends << weekend;
      }
    // the newest grid logged for each call, for QSOs logged without one
    QHash<QString, QString> known;
    for (auto const& record : sorted)
      {
        auto const g = grid4 (record.value ("GRIDSQUARE"));
        if (!g.isEmpty ()) known[record.value ("CALL").trimmed ().toUpper ()] = g;
      }
    QSet<QString> seen;
    for (auto const& record : sorted)
      {
        auto const call = record.value ("CALL").trimmed ().toUpper ();
        auto const date = QDate::fromString (record.value ("QSO_DATE").trimmed (), "yyyyMMdd");
        if (call.isEmpty () || !days.contains (date)) continue;
        auto const band = band_code (record);
        if (band.isEmpty ())
          {
            if (is_satellite (record)) ++result.satellite; else ++result.other;
            continue;
          }
        result.bands << band;
        if (!bands.isEmpty () && !bands.contains (band))
          {
            ++result.other;
            continue;
          }
        Qso qso;
        qso.band = band;
        qso.mode = mode_code (record);
        qso.date = date;
        qso.time = padded_time (record.value ("TIME_ON"));
        qso.my_call = record.value ("STATION_CALLSIGN").trimmed ().toUpper ();
        if (qso.my_call.isEmpty ()) qso.my_call = my_call.trimmed ().toUpper ();
        qso.my_grid = grid4 (record.value ("MY_GRIDSQUARE"));
        if (qso.my_grid.isEmpty ()) qso.my_grid = grid4 (my_grid);
        qso.call = call;
        qso.key = record_key (record);
        qso.grid = grid4 (record.value ("GRIDSQUARE"));
        if (qso.grid.isEmpty () && !grid4 (fixes.value (qso.key)).isEmpty ())
          {
            qso.grid = grid4 (fixes.value (qso.key));
            qso.entered = true;
          }
        else if (qso.grid.isEmpty () && known.contains (call))
          {
            qso.grid = known.value (call);
            qso.filled = true;
          }
        qso.log = record.value ("_FILE").toInt ();
        qso.op = record.value ("OPERATOR").trimmed ().toUpper ();
        auto const weekend_group = group_of_day.value (date);
        if (!weekend_group.isEmpty () && band_group (band) != weekend_group)
          {
            result.off_weekend << qso;
            if (leave_out_off_weekend) continue;
          }
        if (qso.grid.isEmpty ())
          {
            result.no_grid << qso;
            result.need_grid << qso;
            continue;
          }
        auto const once = band + '|' + call + '|' + qso.grid;
        if (seen.contains (once))
          {
            result.dupes << qso;
            continue;
          }
        seen << once;
        if (qso.filled || qso.entered) result.need_grid << qso;
        result.qsos << qso;
      }
    QSet<QString> operators;
    for (auto const& qso : result.qsos) if (!qso.op.isEmpty ()) operators << qso.op;
    for (auto const& op : operators) result.operators << op;
    result.operators.sort ();
    return result;
  }

  Score score (QVector<Qso> const& qsos)
  {
    Score result;
    QHash<QString, QSet<QString>> grids;
    QHash<QString, int> count;
    for (auto const& qso : qsos)
      {
        grids[qso.band] << qso.grid;
        ++count[qso.band];
      }
    for (auto i = grids.cbegin (); i != grids.cend (); ++i)
      {
        result.bands << BandScore {i.key (), count.value (i.key ()), int (i.value ().size ())};
        result.multipliers += i.value ().size ();
      }
    std::sort (result.bands.begin (), result.bands.end (), [] (BandScore const& a, BandScore const& b) {
        return band_order (a.band) < band_order (b.band);
      });
    result.points = 100 * qsos.size ();
    result.claimed = qint64 (result.points) * result.multipliers;
    return result;
  }

  QString cabrillo_text (Header const& header, QVector<Qso> const& qsos)
  {
    QSet<QString> bands, modes;
    for (auto const& qso : qsos)
      {
        bands << qso.band;
        modes << qso.mode;
      }
    QString category_mode {"MIXED"};
    if (modes.size () == 1)
      {
        auto const mode = *modes.cbegin ();
        category_mode = mode == "DG" ? "DIGI" : mode == "CW" ? "CW" : "SSB";
      }
    auto const total = score (qsos);
    QStringList lines {
      "START-OF-LOG: 3.0",
      "CALLSIGN: " + header.callsign.trimmed ().toUpper (),
      "CONTEST: ARRL-EME",
      "CATEGORY-OPERATOR: " + (header.category_operator.isEmpty () ? QString {"SINGLE-OP"}
                               : header.category_operator),
      "CATEGORY-BAND: " + (bands.size () == 1 ? *bands.cbegin () : QString {"ALL"}),
      "CATEGORY-MODE: " + category_mode,
      "GRID-LOCATOR: " + grid4 (header.grid),
      "CLAIMED-SCORE: " + QString::number (total.claimed),
    };
    auto add = [&lines] (char const * tag, QString const& value) {
      auto const v = value.simplified ();
      if (!v.isEmpty ()) lines << QString {"%1: %2"}.arg (QLatin1String {tag}, v);
    };
    add ("LOCATION", header.location);
    add ("OPERATORS", header.operators);
    add ("NAME", header.name);
    add ("ADDRESS", header.address);
    add ("ADDRESS-CITY", header.city);
    add ("ADDRESS-STATE-PROVINCE", header.state);
    add ("ADDRESS-POSTALCODE", header.postcode);
    add ("ADDRESS-COUNTRY", header.country);
    add ("EMAIL", header.email);
    add ("CREATED-BY", header.created_by);
    for (auto const& qso : qsos)
      {
        lines << QString {"QSO: %1 %2 %3 %4 %5 %6 %7 %8"}
          .arg (qso.band, 5).arg (qso.mode).arg (qso.date.toString (Qt::ISODate)).arg (qso.time)
          .arg (qso.my_call, -13).arg (qso.my_grid).arg (qso.call, -13).arg (qso.grid);
      }
    lines << "END-OF-LOG:";
    return lines.join ('\n') + '\n';
  }
}
