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

    // Another propagation than EME, as logged: "SAT", "TR", "MS" ...; empty
    // for EME and for records without PROP_MODE (WSJT-X writes none).
    QString other_propagation (Record const& record)
    {
      if (is_satellite (record)) return QStringLiteral ("SAT");
      auto const mode = record.value ("PROP_MODE").trimmed ().toUpper ();
      return mode == "EME" ? QString {} : mode;
    }

    // hhmmss of TIME_ON, else of TIME_OFF (seconds 00 if not logged); empty
    // if neither is a time
    bool all_digits (QString const& text)
    {
      for (auto const& c : text) if (c < QChar {'0'} || c > QChar {'9'}) return false;
      return !text.isEmpty ();
    }

    QString time6 (Record const& record)
    {
      for (auto const * name : {"TIME_ON", "TIME_OFF"})
        {
          auto const time = record.value (name).trimmed ();
          if ((time.size () == 4 || time.size () == 6) && all_digits (time)) return (time + "00").left (6);
        }
      return {};
    }

    // yyyymmdd of QSO_DATE, else of QSO_DATE_OFF; empty if neither is a date
    QString date8 (Record const& record)
    {
      for (auto const * name : {"QSO_DATE", "QSO_DATE_OFF"})
        {
          auto const date = record.value (name).trimmed ();
          if (date.size () == 8 && all_digits (date)
              && QDate::isValid (date.left (4).toInt (), date.mid (4, 2).toInt (), date.mid (6, 2).toInt ()))
            return date;
        }
      return {};
    }

  }

  QDate record_date (Record const& record)
  {
    return QDate::fromString (date8 (record), "yyyyMMdd");
  }

  QString record_time (Record const& record)
  {
    return time6 (record).left (4);
  }

  Log prepare (QVector<Record> const& records)
  {
    // sorted by a key made once per record (date, time), not by reading
    // the fields again at every comparison
    QVector<QString> keys;
    keys.reserve (records.size ());
    for (auto const& record : records) keys << date8 (record) + time6 (record);
    QVector<int> order (records.size ());
    for (int i = 0; i < order.size (); ++i) order[i] = i;
    std::stable_sort (order.begin (), order.end (), [&keys] (int a, int b) {return keys[a] < keys[b];});
    Log log;
    log.records.reserve (records.size ());
    log.dates.reserve (records.size ());
    for (auto const index : order)
      {
        auto const& record = records[index];
        log.records << record;
        log.dates << QDate::fromString (keys[index].left (8), "yyyyMMdd");  // invalid if it has none
        // the newest grid logged for each call, for QSOs logged without one
        auto const grid = record.value ("GRIDSQUARE");
        if (!grid.isEmpty ())
          {
            auto const g = grid4 (grid);
            if (!g.isEmpty ()) log.known[record.value ("CALL").trimmed ().toUpper ()] = g;
          }
      }
    return log;
  }

  QVector<Record> read_adif (QByteArray const& adif)
  {
    static QRegularExpression const tag {QStringLiteral ("<([A-Za-z0-9_]+)(?::(\\d+)(?::[A-Za-z])?)?>")};
    QVector<Record> records;
    // The tags are read in order and every field's text is skipped by its
    // length, so an "<EOH>" or "<EOR>" inside a field (a comment, say) is
    // never taken for the real one.
    int position = 0;
    // work on Latin-1 text so that offsets are byte offsets, as ADIF counts
    auto const text = QString::fromLatin1 (adif);
    Record current;
    while (true)
      {
        auto const match = tag.match (text, position);
        if (!match.hasMatch ()) break;
        auto const name = match.captured (1).toUpper ();
        position = match.capturedEnd ();
        if (name == "EOH")
          {
            // what came before the first record was the header
            if (records.isEmpty ()) current.clear ();
          }
        else if (name == "EOR")
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
    // a last record without its <EOR> (a file cut short) still counts
    if (!current.value ("CALL").trimmed ().isEmpty ()) records << current;
    return records;
  }

  QString band_code (Record const& record)
  {
    if (!other_propagation (record).isEmpty ()) return {};
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

  QString mode_name (Record const& record)
  {
    auto name = record.value ("SUBMODE").trimmed ().toUpper ();
    if (name.isEmpty ()) name = record.value ("MODE").trimmed ().toUpper ();
    return name.isEmpty () ? QStringLiteral ("?") : name;
  }

  QString ascii (QString const& text)
  {
    // letters that do not decompose into a base letter and an accent
    static QHash<QChar, QString> const spelled {
      {QChar {0x00df}, "ss"}, {QChar {0x00f8}, "o"}, {QChar {0x00d8}, "O"}, {QChar {0x00e6}, "ae"},
      {QChar {0x00c6}, "AE"}, {QChar {0x0142}, "l"}, {QChar {0x0141}, "L"}, {QChar {0x0111}, "d"},
      {QChar {0x0110}, "D"}, {QChar {0x0153}, "oe"}, {QChar {0x0152}, "OE"},
    };
    QString result;
    for (auto const& ch : text)
      {
        if (spelled.contains (ch))
          {
            result += spelled.value (ch);
            continue;
          }
        for (auto const& c : QString {ch}.normalized (QString::NormalizationForm_KD))
          {
            if (c.category () == QChar::Mark_NonSpacing || c.category () == QChar::Mark_SpacingCombining
                || c.category () == QChar::Mark_Enclosing) continue;
            result += c.unicode () < 128 ? c : QChar {'?'};
          }
      }
    return result;
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
    return record.value ("CALL").trimmed ().toUpper () + '|' + date8 (record) + '|' + record_time (record);
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
    struct First
    {
      int log;                          // the log the QSO was first seen in
      int index;                        // its place in result
    };
    QHash<QString, First> first;
    QVector<Record> result;
    for (int i = 0; i < logs.size (); ++i)
      for (auto record : logs[i])
        {
          auto band = band_code (record);
          if (band.isEmpty ()) band = record.value ("BAND").trimmed ().toLower ();
          auto const key = record_key (record) + '|' + band;
          record["_FILE"] = QString::number (i);
          auto const seen = first.constFind (key);
          if (seen != first.cend () && seen->log != i)
            {
              // a copy - but it may know more than the record kept
              auto& kept = result[seen->index];
              auto const grid = grid4 (record.value ("GRIDSQUARE"));
              auto const kept_grid = grid4 (kept.value ("GRIDSQUARE"));
              if (!grid.isEmpty () && kept_grid.isEmpty ())
                kept["GRIDSQUARE"] = record.value ("GRIDSQUARE");
              else if (!grid.isEmpty () && grid != kept_grid)
                kept["_GRID2"] = grid;  // the logs disagree: for the user to settle
              if (copies) *copies << record;
              continue;
            }
          if (seen == first.cend ()) first.insert (key, First {i, int (result.size ())});
          result << record;
        }
    return result;
  }

  Selection select (QVector<Record> const& records, QSet<QDate> const& days,
                    QString const& my_call, QString const& my_grid,
                    QSet<QString> const& bands, QHash<QString, QString> const& fixes,
                    bool leave_out_off_weekend, QSet<QString> const& modes)
  {
    return select (prepare (records), days, my_call, my_grid, bands, fixes, leave_out_off_weekend, modes);
  }

  Selection select (Log const& log, QSet<QDate> const& days,
                    QString const& my_call, QString const& my_grid,
                    QSet<QString> const& bands, QHash<QString, QString> const& fixes,
                    bool leave_out_off_weekend, QSet<QString> const& modes)
  {
    Selection result;
    // The records on the chosen days, in time order: the log's dates are in
    // order, so each day's records are found without going through the log.
    QList<QDate> chosen_days;
    for (auto const& day : days) if (day.isValid ()) chosen_days << day;
    std::sort (chosen_days.begin (), chosen_days.end ());
    QVector<int> chosen;
    for (auto const& day : chosen_days)
      {
        auto const range = std::equal_range (log.dates.cbegin (), log.dates.cend (), day);
        for (auto i = range.first; i != range.second; ++i)
          if (!log.records[int (i - log.dates.cbegin ())].value ("CALL").trimmed ().isEmpty ())
            chosen << int (i - log.dates.cbegin ());
      }
    // Each weekend's band group = the group with more QSOs that weekend,
    // counting only what can be in the entry: QSOs on a band or in a mode
    // that is not chosen do not decide (ten 2 m FT8 contacts must not turn
    // a microwave weekend into a 50-1296 MHz one).
    for (auto const& days_of_weekend : weekends (days))
      {
        Weekend weekend;
        weekend.days = days_of_weekend;
        result.weekends << weekend;
      }
    QHash<QDate, int> weekend_of_day;
    for (int i = 0; i < result.weekends.size (); ++i)
      for (auto const& day : result.weekends[i].days) weekend_of_day[day] = i;
    // a typed grid also serves the call's other QSOs, if none has a grid logged
    auto known = log.known;
    QHash<QString, QString> typed;      // call -> a grid typed for one of its QSOs
    for (auto const index : chosen)
      {
        auto const& record = log.records[index];
        auto const band = band_code (record);
        if (!band.isEmpty () && (bands.isEmpty () || bands.contains (band))
            && (modes.isEmpty () || modes.contains (mode_name (record))))
          ++result.weekends[weekend_of_day.value (log.dates[index])].qsos[band_group (band)];
        if (fixes.isEmpty ()) continue;
        auto const g = grid4 (fixes.value (record_key (record)));
        auto const call = record.value ("CALL").trimmed ().toUpper ();
        if (!g.isEmpty () && !typed.contains (call)) typed[call] = g;
      }
    for (auto i = typed.cbegin (); i != typed.cend (); ++i)
      if (!known.contains (i.key ())) known[i.key ()] = i.value ();
    QHash<QDate, QString> group_of_day;
    for (auto& weekend : result.weekends)
      {
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
        for (auto const& day : weekend.days) group_of_day[day] = weekend.group;
      }
    QSet<QString> seen;
    for (auto const index : chosen)
      {
        auto const& record = log.records[index];
        auto const call = record.value ("CALL").trimmed ().toUpper ();
        auto const date = log.dates[index];
        auto const band = band_code (record);
        if (band.isEmpty ())
          {
            auto const propagation = other_propagation (record);
            if (propagation == "SAT") ++result.satellite;
            else if (!propagation.isEmpty ()) ++result.not_eme;   // logged as TR, MS ...
            else ++result.other;
            continue;
          }
        result.bands << band;
        if (!bands.isEmpty () && !bands.contains (band))
          {
            ++result.other;
            continue;
          }
        auto const name = mode_name (record);
        ++result.modes[name];
        if (!modes.isEmpty () && !modes.contains (name))
          {
            ++result.mode_out;
            continue;
          }
        Qso qso;
        qso.mode_name = name;
        qso.band = band;
        qso.mode = mode_code (record);
        qso.date = date;
        qso.time = record_time (record);
        if (qso.time.isEmpty ())        // no TIME_ON or TIME_OFF: reported, never 0000
          {
            result.no_time << call;
            continue;
          }
        qso.my_call = record.value ("STATION_CALLSIGN").trimmed ().toUpper ();
        if (qso.my_call.isEmpty ()) qso.my_call = my_call.trimmed ().toUpper ();
        qso.my_grid = grid4 (record.value ("MY_GRIDSQUARE"));
        if (qso.my_grid.isEmpty ()) qso.my_grid = grid4 (my_grid);
        qso.call = call;
        qso.key = record_key (record);
        qso.grid = grid4 (record.value ("GRIDSQUARE"));
        qso.grid2 = grid4 (record.value ("_GRID2"));    // another log has another grid
        if ((qso.grid.isEmpty () || !qso.grid2.isEmpty ()) && !grid4 (fixes.value (qso.key)).isEmpty ())
          {
            qso.grid = grid4 (fixes.value (qso.key));
            qso.entered = true;
          }
        else if (qso.grid.isEmpty () && known.contains (call))
          {
            qso.grid = known.value (call);
            qso.filled = true;
          }
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
            qso.dupe = true;
            result.dupes << qso;
            // still listed for its grid: the filled or typed one may be wrong
            if (qso.filled || qso.entered || !qso.grid2.isEmpty ()) result.need_grid << qso;
            continue;
          }
        seen << once;
        // also when the logs disagree about the grid: the user settles it
        if (qso.filled || qso.entered || !qso.grid2.isEmpty ()) result.need_grid << qso;
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
      // the category names 6 m, 4 m and 2 m differently from the QSO lines
      // (Cabrillo 3.0 header: 6M 4M 2M 222 432 902 1.2G ... 10G)
      "CATEGORY-BAND: " + (bands.size () != 1 ? QString {"ALL"}
                           : *bands.cbegin () == "50" ? QString {"6M"}
                           : *bands.cbegin () == "70" ? QString {"4M"}
                           : *bands.cbegin () == "144" ? QString {"2M"} : *bands.cbegin ()),
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
    return ascii (lines.join ('\n') + '\n');    // Cabrillo is plain ASCII
  }
}
