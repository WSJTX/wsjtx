#include "WorkedBefore.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <limits>
#include <stdexcept>
#include <boost/functional/hash.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/key_extractors.hpp>
#include <boost/range/iterator_range.hpp>
#include <QCoreApplication>
#include <QtConcurrent/QtConcurrentRun>
#include <QFuture>
#include <QFutureWatcher>
#include <QChar>
#include <QString>
#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QBuffer>
#include <QFile>
#include <QHash>
#include <QLockFile>
#include <QTimer>
#include <QTextStream>
#include <QRegularExpression>
#include <QDateTime>
#include <QPointer>
#include <QSet>
#include <QVector>
#include "Configuration.hpp"
#include "PerformanceTrace.hpp"
#include "revision_utils.hpp"
#include "Logger.hpp"
#include "qt_helpers.hpp"
#include "WorkedBeforeLoadState.hpp"
#include "pimpl_impl.hpp"

#include "moc_WorkedBefore.cpp"

using namespace boost::multi_index;

// hash function for QString members in hashed indexes
inline
std::size_t hash_value (QString const& s)
{
  return std::hash<QString> {} (s);
}

//
// worked before set element
//
struct worked_entry
{
  explicit worked_entry (QString const& call, QString const& grid, QString const& band
                         , QString const& mode, AD1CCty::Record const& entity
                         , QDate const& date = {})
    : call_ {call}
    , grid_ {grid}
    , band_ {band}
    , mode_ {mode}
    , dxcc_country_ {entity.identity (AD1CCty::EntityView::DXCC).name}
    , wae_country_ {entity.identity (AD1CCty::EntityView::WAE).name}
    , continent_ {entity.continent}
    , CQ_zone_ {entity.CQ_zone}
    , ITU_zone_ {entity.ITU_zone}
  {
    add_date (date);
  }

  // Note a QSO_DATE for this entry (invalid dates are ignored).
  void add_date (QDate const& date) const
  {
    if (!date.isValid ()) return;
    auto const pos = std::lower_bound (dates_.begin (), dates_.end (), date);
    if (pos == dates_.end () || *pos != date) dates_.insert (pos, date);
  }

  // Worked on one of days; an empty set means on any date.
  bool worked_on (QSet<QDate> const& days) const
  {
    if (days.isEmpty ()) return true;
    for (auto const& date : dates_)
      {
        if (days.contains (date)) return true;
      }
    return false;
  }

  QString call_;
  QString grid_;
  QString band_;
  QString mode_;
  QString dxcc_country_;
  QString wae_country_;
  AD1CCty::Continent continent_;
  int CQ_zone_;
  int ITU_zone_;

  // Every QSO_DATE seen for this entry, sorted, no repeats (empty if the
  // records had none). Not part of the entry's identity or of any index, so
  // it may be updated in place; see insert_worked().
  mutable QVector<QDate> dates_;
};

bool operator == (worked_entry const& lhs, worked_entry const& rhs)
{
  return
    lhs.continent_ == rhs.continent_  // check 1st as it is fast
    && lhs.CQ_zone_ == rhs.CQ_zone_   // ditto
    && lhs.ITU_zone_ == rhs.ITU_zone_ // ditto
    && lhs.call_ == rhs.call_         // check the rest in decreasing
    && lhs.grid_ == rhs.grid_         // domain size order to shortcut
    && lhs.dxcc_country_ == rhs.dxcc_country_
    && lhs.wae_country_ == rhs.wae_country_
    && lhs.band_ == rhs.band_
    && lhs.mode_ == rhs.mode_;
}

std::size_t hash_value (worked_entry const& we)
{
  std::size_t seed {0};
  boost::hash_combine (seed, we.call_);
  boost::hash_combine (seed, we.grid_);
  boost::hash_combine (seed, we.band_);
  boost::hash_combine (seed, we.mode_);
  boost::hash_combine (seed, we.dxcc_country_);
  boost::hash_combine (seed, we.wae_country_);
  boost::hash_combine (seed, we.continent_);
  boost::hash_combine (seed, we.CQ_zone_);
  boost::hash_combine (seed, we.ITU_zone_);
  return seed;
}

#if !defined (QT_NO_DEBUG_STREAM)
QDebug operator << (QDebug dbg, worked_entry const& e)
{
  QDebugStateSaver saver {dbg};
  dbg.nospace () << "worked_entry("
                 << e.call_ << ", "
                 << e.grid_ << ", "
                 << e.band_ << ", "
                 << e.mode_ << ", "
                 << e.dxcc_country_ << ", "
                 << e.wae_country_ << ", "
                 << e.continent_ << ", "
                 << e.CQ_zone_ << ", "
                 << e.ITU_zone_ << ", "
                 << e.dates_ << ')';
  return dbg;
}
#endif

// less then predidate for the Continent enum class, needed for
// ordered indexes
struct Continent_less
{
  bool operator () (AD1CCty::Continent lhs, AD1CCty::Continent rhs) const
  {
    return static_cast<int> (lhs) < static_cast<int> (rhs);
  }
};

// index tags
struct call_mode_band {};
struct call_band {};
struct grid_mode_band {};
struct grid_band {};
struct dxcc_entity_mode_band {};
struct dxcc_entity_band {};
struct wae_entity_mode_band {};
struct wae_entity_band {};
struct continent_mode_band {};
struct continent_band {};
struct CQ_zone_mode_band {};
struct CQ_zone_band {};
struct ITU_zone_mode_band {};
struct ITU_zone_band {};

// set with multiple ordered unique indexes that allow for optimally
// efficient determination of various categories of worked before
// status
typedef multi_index_container<
  worked_entry,
  indexed_by<
    // basic unordered set constraint - we don't need duplicate worked entries
    hashed_unique<identity<worked_entry>>,

    //
    // The following indexes are used to discover worked before stuff.
    //
    // They are ordered so as to support partial lookups and
    // non-unique because container inserts must be valid for all
    // indexes.
    //

    // call+mode+band
    ordered_non_unique<tag<call_mode_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, QString, &worked_entry::call_>,
                                     member<worked_entry, QString, &worked_entry::mode_>,
                                     member<worked_entry, QString, &worked_entry::band_> > >,
    // call+band
    ordered_non_unique<tag<call_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, QString, &worked_entry::call_>,
                                     member<worked_entry, QString, &worked_entry::band_> > >,
    // grid+mode+band
    ordered_non_unique<tag<grid_mode_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, QString, &worked_entry::grid_>,
                                     member<worked_entry, QString, &worked_entry::mode_>,
                                     member<worked_entry, QString, &worked_entry::band_> > >,
    // grid+band
    ordered_non_unique<tag<grid_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, QString, &worked_entry::grid_>,
                                     member<worked_entry, QString, &worked_entry::band_> > >,
    // DXCC country+mode+band
    ordered_non_unique<tag<dxcc_entity_mode_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, QString, &worked_entry::dxcc_country_>,
                                     member<worked_entry, QString, &worked_entry::mode_>,
                                     member<worked_entry, QString, &worked_entry::band_> > >,
    // DXCC country+band
    ordered_non_unique<tag<dxcc_entity_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, QString, &worked_entry::dxcc_country_>,
                                     member<worked_entry, QString, &worked_entry::band_> > >,
    // WAE country+mode+band
    ordered_non_unique<tag<wae_entity_mode_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, QString, &worked_entry::wae_country_>,
                                     member<worked_entry, QString, &worked_entry::mode_>,
                                     member<worked_entry, QString, &worked_entry::band_> > >,
    // WAE country+band
    ordered_non_unique<tag<wae_entity_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, QString, &worked_entry::wae_country_>,
                                     member<worked_entry, QString, &worked_entry::band_> > >,
    // continent+mode+band
    ordered_non_unique<tag<continent_mode_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, AD1CCty::Continent, &worked_entry::continent_>,
                                     member<worked_entry, QString, &worked_entry::mode_>,
                                     member<worked_entry, QString, &worked_entry::band_> >,
                       composite_key_compare<Continent_less, std::less<QString>, std::less<QString> > >,
    // continent+band
    ordered_non_unique<tag<continent_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, AD1CCty::Continent, &worked_entry::continent_>,
                                     member<worked_entry, QString, &worked_entry::band_> >,
                       composite_key_compare<Continent_less, std::less<QString> > >,
    // CQ-zone+mode+band
    ordered_non_unique<tag<CQ_zone_mode_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, int, &worked_entry::CQ_zone_>,
                                     member<worked_entry, QString, &worked_entry::mode_>,
                                     member<worked_entry, QString, &worked_entry::band_> > >,
    // CQ-zone+band
    ordered_non_unique<tag<CQ_zone_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, int, &worked_entry::CQ_zone_>,
                                     member<worked_entry, QString, &worked_entry::band_> > >,
    // ITU-zone+mode+band
    ordered_non_unique<tag<ITU_zone_mode_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, int, &worked_entry::ITU_zone_>,
                                     member<worked_entry, QString, &worked_entry::mode_>,
                                     member<worked_entry, QString, &worked_entry::band_> > >,
    // ITU-zone+band
    ordered_non_unique<tag<ITU_zone_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, int, &worked_entry::ITU_zone_>,
                                     member<worked_entry, QString, &worked_entry::band_> > > >
  > worked_before_database_type;

namespace
{
  // Insert an entry, or, if an equal entry is already there, add its QSO
  // dates to that one.
  void insert_worked (worked_before_database_type& worked, worked_entry const& entry)
  {
    auto const result = worked.insert (entry);
    if (!result.second)
      {
        for (auto const& date : entry.dates_) result.first->add_date (date);
      }
  }

  // Exception class suitable for using with QtConcurrent across
  // thread boundaries
  class LoaderException final
    : public QException
  {
  public:
    LoaderException (std::exception const& e) : error_ {e.what ()} {}
    QString error () const {return error_;}
    void raise () const override {throw *this;}
    LoaderException * clone () const override {return new LoaderException {*this};}
  private:
    QString error_;
  };

  struct LoadCancelled {};

  void check_load_cancelled (std::atomic_bool const& stop, char const * checkpoint)
  {
#ifdef WSJT_WORKED_BEFORE_LOAD_CHECKPOINT
    WSJT_WORKED_BEFORE_LOAD_CHECKPOINT (checkpoint, stop);
#else
    Q_UNUSED (checkpoint);
#endif
    if (stop.load (std::memory_order_relaxed)) throw LoadCancelled {};
  }

  bool used_adif_field (QString const& name)
  {
    return name == "CALL" || name == "BAND" || name == "MODE" || name == "SUBMODE"
      || name == "GRIDSQUARE" || name == "QSO_DATE";
  }

  struct adif_field
  {
    QString name;
    int data_start;
    int end;
    bool valid_length {false};
    bool needs_more {false};
    bool recover_boundary {false};
    bool valid_type {true};
    bool non_ascii {false};
  };

  // Consume a payload, request more input, or flag an untrusted boundary;
  // valid_length separately identifies descriptors without a usable length.
  adif_field read_adif_field (QString const& input, int position, int closing, bool at_eof,
                            std::atomic_bool const& stop, bool character_lengths = false,
                            quint64 length_limit = std::numeric_limits<quint64>::max ())
  {
    check_load_cancelled (stop, character_lengths ? "field_characters" : "field");
    auto descriptor = input.mid (position + 1, closing - position - 1);
    auto delimiter = descriptor.indexOf (QChar {':'});
    adif_field field {descriptor.left (delimiter).toUpper (), closing + 1, closing + 1};
    if (delimiter < 0) return field;
    static QRegularExpression const field_name {QStringLiteral ("\\A[A-Z][A-Z0-9_]*\\z")};
    if (!field_name.match (field.name).hasMatch ()) return field;
    auto type_delimiter = descriptor.indexOf (QChar {':'}, delimiter + 1);
    if (type_delimiter >= 0)
      {
        field.valid_type = descriptor.size () == type_delimiter + 2;
        if (field.valid_type)
          {
            auto type = descriptor[type_delimiter + 1].toUpper ();
            field.valid_type = type >= QChar {'A'} && type <= QChar {'Z'};
          }
      }
    auto length_text = descriptor.mid (delimiter + 1,
      (type_delimiter < 0 ? descriptor.size () : type_delimiter) - delimiter - 1);
    bool digits {!length_text.isEmpty ()};
    int characters {0};
    for (auto character : length_text)
      {
        if (!(characters++ % 4096)) check_load_cancelled (stop, "field_length");
        digits = digits && character >= QChar {'0'} && character <= QChar {'9'};
      }
    bool ok {false};
    auto length = digits ? length_text.toULongLong (&ok) : 0;
    if (!ok) return field;
    field.valid_length = true;
    if (length > length_limit)
      {
        field.recover_boundary = true;
        return field;
      }

    bool const used = used_adif_field (field.name);
    if (used && character_lengths)
      {
        auto opening = input.indexOf ('<', field.data_start);
        if (opening >= 0 && static_cast<quint64> (opening - field.data_start) < length)
          {
            field.recover_boundary = true;
            return field;
          }
      }
    quint64 bytes {0};
    int end = field.data_start;
    characters = 0;
    while (!character_lengths && end < input.size () && bytes < length)
      {
        if (!(characters++ % 4096)) check_load_cancelled (stop, "field_data");
        auto character = input[end++].unicode ();
        if (used && character == '<')
          {
            field.recover_boundary = true;
            return field;
          }
        field.non_ascii = field.non_ascii || character > 127;
        if (QChar::isHighSurrogate (character))
          {
            if (end == input.size () && !at_eof)
              {
                field.needs_more = true;
                return field;
              }
            if (end < input.size () && input[end].isLowSurrogate ())
              {
                ++end;
                bytes += 4;
                continue;
              }
          }
        bytes += character < 0x80 ? 1 : character < 0x800 ? 2 : 3;
      }
    if (character_lengths || bytes != length)
      {
        if (length > static_cast<quint64> (input.size () - field.data_start))
          {
            field.needs_more = !at_eof;
            field.recover_boundary = true;
            return field;
          }
        // Some exporters count decoded characters in UTF-8 ADI fields.
        end = field.data_start + static_cast<int> (length);
      }

    auto opening = input.lastIndexOf (QChar {'<'}, end - 1);
    if (opening >= field.data_start)
      {
        auto tag_closing = input.indexOf (QChar {'>'}, opening + 1);
        if (tag_closing < 0 && !at_eof)
          {
            field.needs_more = true;
            return field;
          }
        if (tag_closing >= end)
          {
            static QRegularExpression const pattern {
              QStringLiteral ("\\A(?:EO[HR]|[A-Z][A-Z0-9_]*:[0-9]+(?::[A-Z])?)\\z"),
              QRegularExpression::CaseInsensitiveOption};
            if (pattern.match (input.mid (opening + 1, tag_closing - opening - 1)).hasMatch ())
              {
                if (!character_lengths && static_cast<quint64> (end - field.data_start) < length)
                  {
                    // A byte endpoint inside a tag can be character-counted field data.
                    // Resolve it before exposing embedded delimiters to boundary recovery.
                    auto characters = read_adif_field (input, position, closing, at_eof, stop, true);
                    characters.non_ascii = characters.non_ascii || field.non_ascii;
                    if (characters.needs_more || !characters.recover_boundary) return characters;
                  }
                field.recover_boundary = true;
                return field;
              }
          }
      }
    if (end == input.size () && !at_eof)
      {
        field.needs_more = true;
        return field;
      }
    auto payload = input.mid (field.data_start, end - field.data_start);
    bool const middle_of_token = end < input.size () && input[end] != QChar {'<'} && !input[end].isSpace ();
    bool cuts_used_value {false};
    if (!used && middle_of_token && opening >= field.data_start)
      {
        auto closing = input.indexOf ('>', opening + 1);
        auto name = input.mid (opening + 1, closing - opening - 1).section (':', 0, 0).toUpper ();
        if (closing >= 0 && closing + 1 < end && used_adif_field (name))
          {
            auto embedded = read_adif_field (input, opening, closing, at_eof, stop, character_lengths, length_limit);
            field.non_ascii = field.non_ascii || embedded.non_ascii;
            if (embedded.needs_more)
              {
                field.needs_more = true;
                return field;
              }
            cuts_used_value = embedded.valid_length && embedded.valid_type
              && !embedded.recover_boundary && embedded.end > end;
          }
      }
    if ((used && (payload.contains ('<') || middle_of_token))
        || (!used && middle_of_token && (payload.contains ("<EOR>", Qt::CaseInsensitive) || cuts_used_value)))
      {
        if (!used && !character_lengths && static_cast<quint64> (end - field.data_start) < length)
          {
            auto characters = read_adif_field (input, position, closing, at_eof, stop, true);
            characters.non_ascii = characters.non_ascii || field.non_ascii;
            if (characters.needs_more || !characters.recover_boundary) return characters;
          }
        field.recover_boundary = true;
        return field;
      }
    field.end = end;
    return field;
  }

  bool adif_free_text (QString const& input, int position, int end)
  {
    auto descriptor = input.mid (position + 1, end - position - 1);
    static QRegularExpression const field {QStringLiteral ("\\A[A-Za-z][A-Za-z0-9_]*:")};
    static QRegularExpression const url {QStringLiteral ("\\A[A-Za-z][A-Za-z0-9+.-]*://")};
    return !field.match (descriptor).hasMatch () || url.match (descriptor).hasMatch ();
  }

  struct adif_scan_cursor
  {
    int position {0};
    bool non_ascii {false};
    quint64 length_limit {std::numeric_limits<quint64>::max ()};
    bool include_header {false};
    qint64 buffer_offset {0};
  };

  int find_adif_tag (QString const& input, QString const& tag, int start, int& tag_end, bool at_eof,
                    std::atomic_bool const& stop, bool character_lengths = false,
                    adif_scan_cursor * cursor = nullptr)
  {
    check_load_cancelled (stop, character_lengths ? "tag_characters" : "tag");
    int position {cursor ? cursor->position : start};
    while ((position = input.indexOf (QChar {'<'}, position)) >= 0)
      {
        check_load_cancelled (stop, "tag");
        auto closing = input.indexOf (QChar {'>'}, position + 1);
        auto opening = input.indexOf (QChar {'<'}, position + 1);
        if (opening >= 0 && (closing < 0 || opening < closing))
          {
            position = opening;
            continue;
          }
        if (cursor) cursor->position = position;
        if (closing < 0) return -1;
        auto descriptor = input.mid (position + 1, closing - position - 1);
        if (descriptor.compare (tag, Qt::CaseInsensitive) == 0
            || (cursor && cursor->include_header && descriptor.compare ("EOH", Qt::CaseInsensitive) == 0))
          {
            tag_end = closing + 1;
            return position;
          }
        auto length_limit = std::numeric_limits<quint64>::max ();
        if (cursor)
          {
            // Decoded offsets undercount consumed UTF-8 bytes, giving a safe
            // upper bound for either supported interpretation of field length.
            auto consumed = static_cast<quint64> (cursor->buffer_offset) + closing + 1;
            length_limit = cursor->length_limit - std::min (cursor->length_limit, consumed);
          }
        auto field = read_adif_field (input, position, closing, at_eof, stop, character_lengths, length_limit);
        if (cursor) cursor->non_ascii = cursor->non_ascii || field.non_ascii;
        if (field.needs_more) return -1;
        position = field.recover_boundary ? field.data_start : field.end;
        if (cursor) cursor->position = position;
      }
    if (cursor) cursor->position = input.size ();
    return -1;
  }

  struct adif_issue
  {
    int position;
    QString field;
    QString reason;
    QString descriptor;
  };

  struct adif_record
  {
    QHash<QString, QString> fields;
    QVector<adif_issue> issues;
    quint64 warning_count {0};
    bool partial {false};
    bool contact {false};

    QString mode () const
    {
      auto value = fields.value ("MODE").toUpper ();
      return value.isEmpty () || value == "MFSK" ? fields.value ("SUBMODE").toUpper () : value;
    }
  };

  adif_record inspect_adif_record (QString const& input, std::atomic_bool const& stop,
                                   bool character_lengths = false, bool header = false)
  {
    adif_record record;
    auto warn = [&] (int position, QString const& field, QString const& reason) {
      check_load_cancelled (stop, "recovery");
      ++record.warning_count;
      if (record.issues.size () < 20)
        record.issues.append ({position, field, reason,
          input.mid (position, 160).section ('>', 0, 0).simplified () + '>'});
    };
    bool call_seen {false};
    bool frozen {false};
    QSet<QString> damaged_fields;
    int position {0};
    while ((position = input.indexOf (QChar {'<'}, position)) >= 0)
      {
        check_load_cancelled (stop, "record_field");
        auto closing = input.indexOf (QChar {'>'}, position + 1);
        auto opening = input.indexOf (QChar {'<'}, position + 1);
        bool const unterminated = closing < 0 || (opening >= 0 && opening < closing);
        auto descriptor_end = unterminated ? (opening < 0 ? input.size () : opening) : closing;
        auto descriptor = input.mid (position + 1, descriptor_end - position - 1);
        auto name = descriptor.section (':', 0, 0).toUpper ();
        if (!unterminated && (name == "EOR" || (header && name == "EOH"))
            && !descriptor.contains (':')) break;
        bool const used = !header && used_adif_field (name);
        record.contact = record.contact || used;
        if (used && name == "CALL")
          {
            if (call_seen)
              {
                frozen = true;
                record.partial = true;
                warn (position, name, QCoreApplication::translate ("WorkedBefore", "Repeated CALL (possible missing <EOR>); later fields ignored"));
              }
            call_seen = true;
          }
        if (unterminated)
          {
            if (used || !adif_free_text (input, position, descriptor_end))
              {
                warn (position, name, QCoreApplication::translate ("WorkedBefore", "Unterminated ADIF descriptor"));
                if (used && !frozen) damaged_fields.insert (name);
              }
            if (opening < 0) break;
            position = opening;
            continue;
          }
        auto field = read_adif_field (input, position, closing, true, stop, character_lengths);
        if (!used && !field.valid_length && adif_free_text (input, position, closing))
          {
            position = closing + 1;
            continue;
          }
        if (!field.valid_length || field.recover_boundary)
          {
            warn (position, name, QCoreApplication::translate ("WorkedBefore", "Unreadable ADIF field length; scanning resumes at the next descriptor"));
            if (used && !frozen) damaged_fields.insert (name);
          }
        else
          {
            if (!field.valid_type)
              warn (position, name, QCoreApplication::translate ("WorkedBefore", "Malformed datatype suffix; declared payload length retained"));
            if (used && !frozen)
              {
                auto value = input.mid (field.data_start, field.end - field.data_start);
                if (!record.fields.contains (name)) record.fields.insert (name, value);
                else if (record.fields.value (name) != value)
                  warn (position, name, QCoreApplication::translate ("WorkedBefore", "Conflicting duplicate field; first readable value retained"));
              }
          }
        position = field.recover_boundary ? field.data_start : field.end;
      }
    for (auto const& name : damaged_fields)
      if (!record.fields.contains (name)) record.partial = true;
    return record;
  }

  QDate adif_record_date (QByteArray const& record)
  {
    std::atomic_bool const no_stop {false};
    auto parsed = inspect_adif_record (QString::fromUtf8 (record), no_stop);
    auto date = QDate::fromString (parsed.fields.value ("QSO_DATE"), "yyyyMMdd");
    return date.isValid () ? date : QDateTime::currentDateTimeUtc ().date ();
  }

  enum class source_status {Complete, Warnings, Incomplete, Failed};

  struct adif_diagnostic
  {
    qint64 line;
    QString field;
    QString reason;
    QString descriptor;
    QString outcome;
    QString call;
  };

  struct source_result
  {
    worked_before_database_type worked;
    QString error;
    source_status status {source_status::Complete};
    quint64 imported_records {0};
    quint64 partial_records {0};
    quint64 rejected_records {0};
    quint64 unfinished_tails {0};
    quint64 diagnostic_count {0};
    QVector<adif_diagnostic> diagnostics;
    bool stopped {false};
  };

  source_result loader (QString const& path, AD1CCty const * prefixes,
                        PerformanceTrace::RunId run, std::atomic_bool const& stop,
                        QByteArray const * data = nullptr, bool pending_append = false)
  {
    check_load_cancelled (stop, "file");
    PerformanceTrace::Phase parse {run, "logbook.parse"};
    source_result result;
    auto& worked = result.worked;
    try
      {
        QFile inputFile {path};
        QBuffer memory;
        QIODevice * device = &inputFile;
        bool opened {false};
        if (data)
          {
            memory.setData (*data);
            device = &memory;
            opened = memory.open (QIODevice::ReadOnly);
          }
        else
          {
            opened = inputFile.open (QFile::ReadOnly);
          }
        if (opened)
          {
            check_load_cancelled (stop, "file_opened");
            if (device->size ())
              {
                QTextStream in {device};
                in.setCodec ("UTF-8");
                QString buffer;
                // A growing append can end inside a valid field payload.
                auto length_limit = pending_append ? std::numeric_limits<quint64>::max () : static_cast<quint64> (device->size ());
                qint64 buffer_line {1};
                qint64 buffer_offset {0};
                auto discard = [&] (int count) {
                  buffer_line += buffer.left (count).count ('\n');
                  buffer_offset += count;
                  buffer.remove (0, count);
                };
                auto diagnose = [&] (int position, QString const& field, QString const& reason,
                                     QString const& descriptor, QString const& outcome, QString const& call = QString {}) {
                  ++result.diagnostic_count;
                  if (result.diagnostics.size () < 20)
                    result.diagnostics.append ({buffer_line + buffer.left (position).count ('\n'),
                                                field, reason, descriptor, outcome, call});
                };
                constexpr int read_chunk_size {64 * 1024};
                bool eof {false};
                auto read_more = [&] {
                  check_load_cancelled (stop, "read");
                  auto chunk = in.read (read_chunk_size);
                  check_load_cancelled (stop, "read_complete");
                  length_limit = std::max (length_limit, static_cast<quint64> (device->size ()));
                  buffer += chunk;
                  eof = chunk.isEmpty ();
                };
                int first_character {0};
                while (true)
                  {
                    first_character = 0;
                    while (first_character < buffer.size () && buffer[first_character].isSpace ())
                      {
                        if (!(first_character % 4096)) check_load_cancelled (stop, "whitespace");
                        ++first_character;
                      }
                    if (first_character < buffer.size () || eof) break;
                    discard (buffer.size ());
                    read_more ();
                  }
                bool const header_required = first_character < buffer.size () && buffer[first_character] != QChar {'<'};
                bool initial {true};
                int buffer_start {0};
                adif_scan_cursor cursor;
                while (true)
                  {
                    cursor.length_limit = length_limit;
                    cursor.buffer_offset = buffer_offset;
                    cursor.include_header = initial;
                    auto tag = initial && header_required ? "EOH" : "EOR";
                    int record_end {0};
                    bool character_lengths {false};
                    auto boundary = find_adif_tag (buffer, tag, buffer_start, record_end, eof && !pending_append, stop, false, &cursor);
                    if (boundary < 0 && !eof)
                      {
                        read_more ();
                        continue;
                      }
                    auto is_header = [&] (int end) {
                      return initial && end >= 5 && buffer.mid (end - 5, 5).compare ("<EOH>", Qt::CaseInsensitive) == 0;
                    };
                    auto text = buffer.mid (buffer_start, boundary < 0 ? -1 : record_end - buffer_start);
                    auto record = inspect_adif_record (text, stop, false, is_header (record_end));
                    if (cursor.non_ascii && (boundary < 0 || record.warning_count))
                      {
                        int fallback_end {0};
                        adif_scan_cursor fallback_cursor;
                        fallback_cursor.position = buffer_start;
                        fallback_cursor.include_header = initial;
                        fallback_cursor.length_limit = length_limit;
                        fallback_cursor.buffer_offset = buffer_offset;
                        auto fallback = find_adif_tag (buffer, tag, buffer_start, fallback_end, eof && !pending_append, stop, true, &fallback_cursor);
                        while (fallback < 0 && !eof)
                          {
                            read_more ();
                            fallback_cursor.length_limit = length_limit;
                            fallback = find_adif_tag (buffer, tag, buffer_start, fallback_end, eof && !pending_append, stop, true, &fallback_cursor);
                          }
                        if (fallback >= 0)
                          {
                            auto alternative = inspect_adif_record (buffer.mid (buffer_start, fallback_end - buffer_start), stop, true, is_header (fallback_end));
                            if (boundary < 0 || alternative.warning_count < record.warning_count)
                              {
                                record = alternative;
                                record_end = fallback_end;
                                boundary = fallback;
                                character_lengths = true;
                              }
                          }
                      }
                    if (boundary < 0)
                      {
                        if ((initial && header_required)
                            || text.contains (QRegularExpression {QStringLiteral ("<[A-Za-z][A-Za-z0-9_]*(?::|$)")}))
                          {
                            ++result.unfinished_tails;
                            result.status = source_status::Incomplete;
                            diagnose (std::max (buffer_start, buffer.indexOf ('<', buffer_start)), {}, QCoreApplication::translate ("WorkedBefore", "Incomplete ADIF header or record"), {},
                                      QCoreApplication::translate ("WorkedBefore", "Unfinished tail not imported; end of file reached"));
                          }
                        break;
                      }
                    bool const header = is_header (record_end);
                    auto call = record.fields.value ("CALL");
                    auto outcome = QCoreApplication::translate ("WorkedBefore", "Header read with warnings");
                    if (!header)
                      {
                        check_load_cancelled (stop, character_lengths ? "record_characters" : "record");
                        if (!call.trimmed ().isEmpty ())
                          {
                            insert_worked (worked, worked_entry {call.toUpper (), record.fields.value ("GRIDSQUARE").left (4).toUpper (),
                              record.fields.value ("BAND").toUpper (), record.mode (), prefixes->lookup (call),
                              QDate::fromString (record.fields.value ("QSO_DATE"), "yyyyMMdd")});
                            ++result.imported_records;
                            if (record.partial) ++result.partial_records;
                            outcome = record.partial ? QCoreApplication::translate ("WorkedBefore", "Contact imported with unavailable fields")
                              : QCoreApplication::translate ("WorkedBefore", "Contact imported");
                          }
                        else if (record.contact)
                          {
                            ++result.rejected_records;
                            outcome = QCoreApplication::translate ("WorkedBefore", "Contact skipped: CALL is unavailable");
                            if (!record.warning_count)
                              diagnose (buffer.indexOf ('<', buffer_start), "CALL", QCoreApplication::translate ("WorkedBefore", "Missing or empty CALL"), {}, outcome);
                          }
                      }
                    for (auto const& issue : record.issues)
                      diagnose (buffer_start + issue.position, issue.field, issue.reason, issue.descriptor, outcome, call);
                    result.diagnostic_count += record.warning_count - record.issues.size ();
                    initial = false;
                    buffer_start = record_end;
                    cursor = {};
                    cursor.position = buffer_start;
                    if (buffer_start >= read_chunk_size)
                      {
                        discard (buffer_start);
                        buffer_start = 0;
                        cursor.position = 0;
                      }
                  }
                if ((!data && inputFile.error () != QFile::NoError) || in.status () == QTextStream::ReadCorruptData)
                  {
                    throw LoaderException (std::runtime_error {QCoreApplication::translate ("WorkedBefore", "Error reading ADIF file: %1").arg (device->errorString ()).toLocal8Bit ()});
                  }
              }
          }
        else
          {
            throw LoaderException (std::runtime_error {QCoreApplication::translate ("WorkedBefore", "Error opening ADIF file: %1").arg (device->errorString ()).toLocal8Bit ()});
          }
      }
    catch (LoaderException const& e)
      {
        result.error = e.error ();
        result.stopped = true;
        result.status = worked.empty () ? source_status::Failed : source_status::Incomplete;
      }

    if (result.diagnostic_count)
      {
        QStringList details;
        details.append (QCoreApplication::translate ("WorkedBefore", "%1 records imported; %2 partially imported; %3 records skipped; %4 unfinished tails. %5")
                        .arg (result.imported_records).arg (result.partial_records).arg (result.rejected_records).arg (result.unfinished_tails)
                        .arg (result.stopped ? QCoreApplication::translate ("WorkedBefore", "Scan stopped early.")
                                            : QCoreApplication::translate ("WorkedBefore", "Scan reached end of file.")));
        for (auto const& diagnostic : result.diagnostics)
          details.append (QCoreApplication::translate ("WorkedBefore", "Line %1: %2%3. %4 %5 %6")
                          .arg (QString::number (diagnostic.line), diagnostic.reason,
                                diagnostic.field.isEmpty () ? QString {} : ": " + diagnostic.field,
                                diagnostic.descriptor, diagnostic.outcome, diagnostic.call.isEmpty () ? QString {} : "CALL: " + diagnostic.call));
        if (result.diagnostic_count > static_cast<quint64> (result.diagnostics.size ()))
          details.append (QCoreApplication::translate ("WorkedBefore", "%1 additional diagnostics omitted.")
                          .arg (result.diagnostic_count - result.diagnostics.size ()));
        if (!result.error.isEmpty ()) details.append (result.error);
        result.error = details.join ('\n');
        if (result.status == source_status::Complete) result.status = source_status::Warnings;
      }
    check_load_cancelled (stop, "file_complete");
    parse.finish (QString {"unique_entries=%1"}.arg (worked.size ()));
    return result;
  }

  constexpr int anchor_size {64};

  // What a source file looked like when it was last read or written by us.
  struct file_signature
  {
    bool exists {false};
    qint64 size {0};
    qint64 modified {0};
    // Last bytes of the file, used to recognise that it only grew by appending.
    QByteArray anchor;
    bool operator== (file_signature const& other) const
    {
      return exists == other.exists && size == other.size && modified == other.modified;
    }
  };

  file_signature signature_of (QString const& path, bool with_anchor = false)
  {
    QFileInfo info {path};
    info.refresh ();
    file_signature signature {info.exists (), info.size (), info.lastModified ().toMSecsSinceEpoch (), {}};
    if (with_anchor && signature.exists && signature.size > 0)
      {
        QFile file {path};
        auto const length = qMin<qint64> (anchor_size, signature.size);
        if (file.open (QIODevice::ReadOnly) && file.seek (signature.size - length))
          signature.anchor = file.read (length);
      }
    return signature;
  }

  struct load_result
  {
    worked_before_database_type worked;
    QHash<QString, file_signature> signatures; // baseline for the change check
    QStringList errors;
    QStringList folder_summaries;
    int directory_failures {0};
    int files_read {0};
    int files_incomplete {0};
    quint64 warnings {0};
    quint64 partial_records {0};
    quint64 rejected_records {0};
    int files_failed {0};
    int files_expected {0};
    bool primary_absent {false};
    bool cancelled {false};
  };

  QString history_path_key (QString path)
  {
#ifdef Q_OS_WIN
    path = path.toCaseFolded ();
#endif
    return path;
  }

  QStringList discover_folder (HistoryFolder const& folder, load_result& result, std::atomic_bool const& stop)
  {
    QStringList matches;
    QStringList directories {folder.path};
    QStringList errors;
    while (!directories.isEmpty ())
      {
        check_load_cancelled (stop, "directory");
        auto path = directories.takeLast ();
        QFileInfo info {path};
        if (path != folder.path && info.isSymLink ()) continue;
        QString error;
        if (!info.exists ()) error = QCoreApplication::translate ("WorkedBefore", "Folder does not exist");
        else if (!info.isDir ()) error = QCoreApplication::translate ("WorkedBefore", "Not a folder");
        else if (!info.isReadable () || !QDir {path}.isReadable ()
#ifndef Q_OS_WIN
                 || !info.isExecutable ()
#endif
                 ) error = QCoreApplication::translate ("WorkedBefore", "Folder cannot be read or searched");
        if (!error.isEmpty ())
          {
            errors.append (QDir::toNativeSeparators (path) + ": " + error);
            continue;
          }

        // Qt cannot report every enumeration failure or provide an atomic snapshot;
        // check accessibility before traversal so known failures are not reported as empty folders.
        QDirIterator entries {path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot};
        QStringList children;
        while (entries.hasNext ())
          {
            check_load_cancelled (stop, "directory_entry");
            entries.next ();
            check_load_cancelled (stop, "directory_entry_ready");
            auto const entry = entries.fileInfo ();
            if (entry.isDir ())
              {
                if (folder.include_subfolders && !entry.isSymLink ())
                  children.append (entry.absoluteFilePath ());
              }
            else if (entry.isFile ()
                     && (entry.suffix ().compare ("adi", Qt::CaseInsensitive) == 0
                         || entry.suffix ().compare ("adif", Qt::CaseInsensitive) == 0))
              matches.append (entry.absoluteFilePath ());
          }
        check_load_cancelled (stop, "directory_sort");
        children.sort ();
        check_load_cancelled (stop, "directory_sort_complete");
        for (auto it = children.crbegin (); it != children.crend (); ++it)
          {
            check_load_cancelled (stop, "directory_queue");
            directories.append (*it);
          }
      }
    check_load_cancelled (stop, "folder_sort");
    matches.sort ();
    check_load_cancelled (stop, "folder_sort_complete");
    auto summary = QCoreApplication::translate ("WorkedBefore", "Folder — %1 — %2 — %3 matching files")
      .arg (QDir::toNativeSeparators (folder.path))
      .arg (folder.include_subfolders
            ? QCoreApplication::translate ("WorkedBefore", "Includes subfolders")
            : QCoreApplication::translate ("WorkedBefore", "This folder only"))
      .arg (matches.size ());
    if (!errors.isEmpty ())
      summary += QCoreApplication::translate ("WorkedBefore", "; discovery incomplete (%1 directory failures)").arg (errors.size ());
    result.folder_summaries.append (summary);
    result.directory_failures += errors.size ();
    result.errors.append (errors);
    return matches;
  }

  load_result load_sources (QString const& primary, QStringList const& supplemental, HistoryFolders const& folders,
                            AD1CCty const * prefixes, PerformanceTrace::RunId run, std::atomic_bool const& stop)
  {
    try
      {
        check_load_cancelled (stop, "load_start");
        load_result result;
        auto configured = WorkedBefore::normalize_history_files (supplemental, primary);
        check_load_cancelled (stop, "sources_normalized");
        configured.prepend (primary);
        QStringList discovered;
        for (auto const& folder : folders)
          {
            check_load_cancelled (stop, "folder");
            discovered.append (discover_folder (folder, result, stop));
          }
        check_load_cancelled (stop, "sources_sort");
        discovered.sort ();
        check_load_cancelled (stop, "sources_sort_complete");
        configured.append (discovered);
        QStringList sources;
        QSet<QString> seen;
        for (auto const& path : configured)
          {
            check_load_cancelled (stop, "canonical_path");
            auto canonical = QFileInfo {path}.canonicalFilePath ();
            auto key = history_path_key (canonical.isEmpty () ? path : canonical);
            if (!seen.contains (key))
              {
                seen.insert (key);
                sources.append (path);
              }
          }
        result.files_expected = sources.size ();
        for (auto const& path : sources)
          {
            check_load_cancelled (stop, "source");
            if (path == primary && !QFileInfo::exists (path) && !QFileInfo {path}.isSymLink ())
              {
                result.primary_absent = true;
                --result.files_expected;
                continue;
              }
            auto source = loader (path, prefixes, run, stop);
            bool recovered = source.status == source_status::Incomplete;
            result.warnings += source.diagnostic_count;
            result.partial_records += source.partial_records;
            result.rejected_records += source.rejected_records;
            if (source.status != source_status::Failed)
              {
                if (result.worked.empty ()) result.worked.swap (source.worked);
                else
                  {
                    unsigned merged {0};
                    for (auto const& entry : source.worked)
                      {
                        if (!(merged++ % 256)) check_load_cancelled (stop, "merge");
                        insert_worked (result.worked, entry);
                      }
                  }
                if (recovered) ++result.files_incomplete;
                else ++result.files_read;
              }
            else
              {
                ++result.files_failed;
              }
            if (!source.error.isEmpty ())
              result.errors.append (QDir::toNativeSeparators (path) + ": " + source.error);
          }
        check_load_cancelled (stop, "load_complete");
        return result;
      }
    catch (LoadCancelled const&)
      {
        load_result result;
        result.cancelled = true;
        return result;
      }
  }
}

namespace
{
  struct poll_result
  {
    enum class action {None, Reload, Append} what {action::None};
    QString primary;
    file_signature known;   // primary log signature this result is based on
    file_signature current; // primary log signature after the appended bytes
    QByteArray appended;
  };

  constexpr qint64 max_append_bytes {256 * 1024};

  bool ends_at_record_boundary (QByteArray const& bytes)
  {
    auto const end = bytes.trimmed ().toLower ();
    return end.endsWith ("<eor>") || end.endsWith ("<eoh>");
  }

  // Decide whether anything changed since 'known'. When only the primary log grew
  // and the bytes before the growth are the ones we saw, return the appended bytes.
  // Runs on a worker thread and touches only files, never shared state.
  poll_result check_sources (QStringList const& paths, QHash<QString, file_signature> const& known)
  {
    poll_result result;
    QStringList changed;
    for (auto const& path : paths)
      {
        auto const old = known.constFind (path);
        if (old == known.constEnd () || !(*old == signature_of (path))) changed.append (path);
      }
    if (changed.isEmpty ()) return result;
    result.what = poll_result::action::Reload;
    auto const primary = paths.first ();
    result.primary = primary;
    auto const old = known.constFind (primary);
    if (changed.size () != 1 || changed.first () != primary || old == known.constEnd ()
        || !old->exists || old->size <= 0 || !ends_at_record_boundary (old->anchor)) return result;
    result.known = *old;
    auto const current = signature_of (primary);
    if (!current.exists || current.size <= old->size) return result;
    // A large addition is parsed more cheaply, and off the GUI thread, by a full reload.
    if (current.size - old->size > max_append_bytes) return result;
    QFile file {primary};
    auto const start = old->size - old->anchor.size ();
    if (!file.open (QIODevice::ReadOnly) || !file.seek (start)) return result;
    auto const data = file.read (current.size - start);
    if (data.size () != current.size - start || !data.startsWith (old->anchor)) return result;
    result.appended = data.mid (old->anchor.size ());
    if (!ends_at_record_boundary (result.appended))
      {
        // Another program is partway through a record: look again at the next check.
        result.what = poll_result::action::None;
        return result;
      }
    result.current = current;
    result.current.anchor = data.right (anchor_size);
    result.what = poll_result::action::Append;
    return result;
  }
}

class WorkedBefore::impl final
{
public:
  impl (Configuration const * configuration)
    : construction_trace_ {"logbook.construct"}
    , configuration_ {configuration}
    , path_ {configuration->adif_log_file_path ()}
    , prefixes_ {configuration}
  {
    construction_trace_.finish ();
  }

  bool stopping () const {return stop_requested_.load (std::memory_order_relaxed);}

  void request_stop ()
  {
    stop_requested_.store (true, std::memory_order_relaxed);
#ifdef WSJT_WORKED_BEFORE_LOAD_CHECKPOINT
    WSJT_WORKED_BEFORE_LOAD_CHECKPOINT ("stop_requested", stop_requested_);
#endif
  }

  QStringList watched_paths () const
  {
    // A history file that is the same file as the log (a link, another spelling) must
    // not be watched on its own: our own appends would then look like foreign changes.
    auto key = [] (QString const& path) {
      auto const canonical = QFileInfo {path}.canonicalFilePath ();
      return canonical.isEmpty () ? QDir::cleanPath (path) : canonical;
    };
    QStringList paths {path_};
    QStringList keys {key (path_)};
    for (auto const& file : configuration_->supplemental_history_files ())
      {
        auto const k = key (file);
        if (keys.contains (k, Qt::CaseSensitive)) continue;
        keys.append (k);
        paths.append (file);
      }
    return paths;
  }

  // Look for changes to the log and history files on a worker thread: a file
  // on an unreachable network drive must not stall the GUI every few seconds.
  void start_poll ()
  {
    poll_pending_ = true;
    poll_future_ = QtConcurrent::run ([paths = watched_paths (), known = watched_] {
        return check_sources (paths, known);
      });
    poll_watcher_.setFuture (poll_future_);
  }

  void start_loader ()
  {
    if (stopping ()) return;
    loader_run_ = PerformanceTrace::current_run ();
    {
      PerformanceTrace::Phase prefixes_reload {loader_run_, "logbook.prefixes_reload"};
      prefixes_.reload (configuration_);
    }
    PerformanceTrace::milestone (loader_run_, "logbook.load_started");
    async_loader_ = QtConcurrent::run ([primary = path_, files = configuration_->supplemental_history_files (),
                                       folders = configuration_->supplemental_history_folders (), prefixes = &prefixes_,
                                       run = loader_run_, stop = &stop_requested_] {
        // Baseline for the change check, taken before anything is read so that a
        // write made during the load is noticed afterwards.
        QHash<QString, file_signature> signatures;
        signatures.insert (primary, signature_of (primary, true));
        for (auto const& file : files) signatures.insert (file, signature_of (file));
        auto result = load_sources (primary, files, folders, prefixes, run, *stop);
        result.signatures = signatures;
        return result;
      });
    loader_watcher_.setFuture (async_loader_);
  }

  PerformanceTrace::Phase construction_trace_;
  Configuration const * configuration_;
  QString path_;
  QHash<QString, file_signature> watched_;
  QTimer * poll_timer_ {nullptr};
  QFutureWatcher<poll_result> poll_watcher_;
  QFuture<poll_result> poll_future_;
  bool poll_pending_ {false};
  AD1CCty prefixes_;
  QFutureWatcher<load_result> loader_watcher_;
  QFuture<load_result> async_loader_;
  std::atomic_bool stop_requested_ {false};
  worked_before_database_type worked_;
  worked_before_database_type added_during_load_;
  QString load_status_;
  QString load_errors_;
  WorkedBeforeLoadState load_state_;
  PerformanceTrace::RunId loader_run_ {0};
};

WorkedBefore::WorkedBefore (Configuration const * configuration)
  : m_ {configuration}
{
  Q_ASSERT (configuration);
  connect (&m_->loader_watcher_, &QFutureWatcher<load_result>::finished,
           this, &WorkedBefore::handle_loader_finished);
  connect (configuration, &Configuration::supplemental_history_sources_changed, this, &WorkedBefore::reload);
  connect (QCoreApplication::instance (), &QCoreApplication::aboutToQuit, this, [this] {m_->request_stop ();});
  m_->poll_timer_ = new QTimer {this};
  connect (m_->poll_timer_, &QTimer::timeout, this, [this] {
      // A running load snapshots the sources when it starts, so changes made
      // meanwhile are noticed by the first check after it completes.
      if (!m_->stopping () && !m_->load_state_.active () && !m_->poll_pending_) m_->start_poll ();
    });
  connect (&m_->poll_watcher_, &QFutureWatcher<poll_result>::finished, this, [this] {
      m_->poll_pending_ = false;
      if (m_->stopping () || m_->load_state_.active ()) return;
      auto const result = m_->poll_watcher_.result ();
      if (poll_result::action::Reload == result.what)
        {
          reload ();
        }
      else if (poll_result::action::Append == result.what)
        {
          // Ignore the result if our own logging or a reload moved the baseline meanwhile;
          // the next check starts again from the current state.
          auto const known = m_->watched_.find (m_->path_);
          if (result.primary != m_->path_ || known == m_->watched_.end () || !(*known == result.known)) return;
          try
            {
              auto parsed = loader (m_->path_, &m_->prefixes_, m_->loader_run_, m_->stop_requested_, &result.appended, true);
              if (parsed.unfinished_tails) return;
              if (parsed.status != source_status::Complete)
                {
                  reload ();
                  return;
                }
              for (auto const& entry : parsed.worked) insert_worked (m_->worked_, entry);
              *known = result.current;
            }
          catch (LoadCancelled const&)
            {
            }
        }
    });
  set_source_poll_interval (2000);
  reload ();
}

void WorkedBefore::set_source_poll_interval (int milliseconds)
{
  if (milliseconds > 0) m_->poll_timer_->start (milliseconds);
  else m_->poll_timer_->stop ();
}

void WorkedBefore::handle_loader_finished ()
{
  if (m_->stopping ()) return;
  m_->load_state_.begin_completion ();
  if (m_->load_state_.reload_pending ())
    {
      m_->load_state_.finish_completion ();
      m_->start_loader ();
      return;
    }
  load_result result;
  try
    {
      result = m_->loader_watcher_.result ();
    }
  catch (LoaderException const& e)
    {
      result.errors.append (e.error ());
    }
  if (result.cancelled) return;
  for (auto const& entry : m_->added_during_load_) insert_worked (result.worked, entry);
  m_->watched_ = result.signatures;
  m_->worked_.swap (result.worked);
  auto n = m_->worked_.size ();
  m_->load_errors_ = result.errors.join ('\n');
  if (result.errors.isEmpty ())
    {
      m_->load_status_ = tr ("History loaded: %1 unique worked-before entries; %2 files loaded.").arg (n).arg (result.files_read);
    }
  else if (result.files_read && !result.files_incomplete && !result.files_failed && !result.directory_failures)
    {
      m_->load_status_ = tr ("History loaded with warnings: %1 warnings; %2 contacts skipped; %3 partially imported; %4 unique worked-before entries.")
        .arg (result.warnings).arg (result.rejected_records).arg (result.partial_records).arg (n);
    }
  else if (result.files_read || result.files_incomplete)
    {
      m_->load_status_ = tr ("Incomplete history: %1 unique worked-before entries; %2 of %3 files loaded completely; %4 incomplete; %5 failed.")
        .arg (n).arg (result.files_read).arg (result.files_expected).arg (result.files_incomplete).arg (result.files_failed);
    }
  else
    {
      m_->load_status_ = tr ("History load failed: %1 unique worked-before entries; no files loaded; %2 failed.")
        .arg (n).arg (result.files_failed);
    }
  if (result.primary_absent)
    {
      m_->load_status_ += tr (" The primary log was absent when scanned.");
    }
  if (result.directory_failures)
    m_->load_status_ += tr (" Directory discovery failures: %1.").arg (result.directory_failures);
  if (!result.folder_summaries.isEmpty ())
    {
      m_->load_status_ += "\n\n" + result.folder_summaries.join ('\n');
      m_->load_status_ += "\n" + tr ("Folder matches may overlap; the overall loaded count represents unique files.");
    }
  QString cty_ver = m_->prefixes_.version();
  auto const error = m_->load_errors_;
  LOG_DEBUG(QString{"WorkedBefore::reload: CTY.DAT version %1"}.arg (cty_ver));
  QPointer<WorkedBefore> guard {this};
  Q_EMIT finished_loading (n, cty_ver, error);
  if (!guard || m_->stopping ())
    {
      return;
    }
  if (WorkedBeforeLoadState::CompletionResult::Restart == m_->load_state_.finish_completion ())
    {
      m_->load_status_ = tr ("Loading history… Previous results remain in use until loading completes.");
      m_->start_loader ();
      Q_EMIT loading_started ();
    }
  else
    {
      m_->added_during_load_.clear ();
    }
}

QString WorkedBefore::cty_version () const
{
  return m_->prefixes_.version ();
}

void WorkedBefore::reload ()
{
  if (m_->stopping ()) return;
  auto const path = m_->configuration_->adif_log_file_path ();
  if (path != m_->path_)
    {
      m_->path_ = path;
      m_->added_during_load_.clear ();
    }
  auto action = m_->load_state_.request_reload ();
  m_->load_status_ = tr ("Loading history… Previous results remain in use until loading completes.");
  m_->load_errors_.clear ();
  if (WorkedBeforeLoadState::ReloadResult::Start == action)
    {
      m_->added_during_load_.clear ();
      m_->start_loader ();
    }
  Q_EMIT loading_started ();
}

WorkedBefore::~WorkedBefore ()
{
  PerformanceTrace::Phase shutdown_wait {"logbook.shutdown_wait"};
  m_->request_stop ();
  disconnect (&m_->loader_watcher_, nullptr, this, nullptr);
  disconnect (&m_->poll_watcher_, nullptr, this, nullptr);
  // The load worker borrows the prefix database and stop flag until it has exited.
  // The poll worker only works on copies, so a poll stuck on an unreachable drive is left to finish.
  m_->async_loader_.waitForFinished ();
}

QString const& WorkedBefore::path () const
{
  return m_->path_;
}

QString const& WorkedBefore::load_status () const
{
  return m_->load_status_;
}

QString const& WorkedBefore::load_errors () const
{
  return m_->load_errors_;
}

QStringList WorkedBefore::normalize_history_files (QStringList const& files, QString const& primary_path)
{
  QStringList result;
  QStringList seen {history_path_key (QDir::cleanPath (QFileInfo {primary_path}.absoluteFilePath ()))};
  for (auto const& path : files)
    {
      if (path.isEmpty ()) continue;
      auto absolute = QDir::cleanPath (QFileInfo {path}.absoluteFilePath ());
      auto key = history_path_key (absolute);
      if (!seen.contains (key))
        {
          seen.append (key);
          result.append (absolute);
        }
    }
  return result;
}

HistoryFolders WorkedBefore::normalize_history_folders (HistoryFolders const& folders)
{
  HistoryFolders result;
  QSet<QString> seen;
  for (auto const& folder : folders)
    {
      if (folder.path.isEmpty ()) continue;
      auto absolute = QDir::cleanPath (QFileInfo {folder.path}.absoluteFilePath ());
      auto key = history_path_key (absolute);
      if (!seen.contains (key))
        {
          seen.insert (key);
          result.append ({absolute, folder.include_subfolders});
        }
    }
  return result;
}

AD1CCty const * WorkedBefore::countries () const
{
  return &m_->prefixes_;
}

bool WorkedBefore::erase_log ()
{
  QLockFile lock {m_->path_ + ".lock"};
  lock.setStaleLockTime (30000);
  if (!lock.tryLock (2000))
    {
      LOG_WARN ("Could not lock ADIF log file to erase it: " << m_->path_);
    }
  bool const erased = !QFile::exists (m_->path_) || QFile::remove (m_->path_);
  if (!erased) LOG_ERROR ("Could not erase ADIF log file: " << m_->path_);
  lock.unlock ();
  if (erased) m_->added_during_load_.clear ();
  reload ();
  return erased;
}

bool WorkedBefore::add (QString const& call
                        , QString const& grid
                        , QString const& band
                        , QString const& mode
                        , QByteArray const& ADIF_record)
{
  if (call.size ())
    {
      auto const& entity = m_->prefixes_.lookup (call);
      QFile file {m_->path_};
      ensure_parent_directory (file.fileName ());
      // Other WSJT-X instances may share this file. Appends are serialised
      // between instances that use the lock; the record is written in one call.
      QLockFile lock {m_->path_ + ".lock"};
      lock.setStaleLockTime (30000);
      if (!lock.tryLock (2000))
        {
          LOG_WARN ("Could not lock ADIF log file, appending anyway: " << m_->path_);
        }
      if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        {
          LOG_ERROR ("Error opening ADIF log file for append: " << file.fileName ()
                     << " - " << file.errorString ());
          return false;
        }
      else
        {
          auto const size_at_open = file.size ();
          QByteArray bytes;
          QTextStream out {&bytes};
          if (!size_at_open)
            {
              auto ts = QDateTime::currentDateTimeUtc ().toString ("yyyyMMdd HHmmss");
              auto ver = version (true);
              out <<            // new file
                QString {
                  "ADIF Export\n"
                  "<adif_ver:5>3.1.8\n"
                  "<created_timestamp:15>%0\n"
                  "<programid:6>WSJT-X\n"
                  "<programversion:%1>%2\n"
                  "<eoh>"
                    }.arg (ts).arg (ver.size ()).arg (ver)
                  <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl
#else
                 Qt::endl
#endif
                 ;
            }
          out << ADIF_record << " <eor>" <<
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
                 endl
#else
                 Qt::endl
#endif
                 ;
          out.flush ();
#ifdef Q_OS_WIN
          bytes.replace ("\n", "\r\n");
#endif
          file.seek (file.size ()); // on Windows Qt's append mode is fixed when the file is opened, so another writer may have extended it since
          if (file.write (bytes) != bytes.size () || !file.flush ())
            {
              LOG_ERROR ("Error writing ADIF log file: " << file.fileName ()
                         << " - " << file.errorString ());
              // Do not leave half a record for the next one, unless another writer may have appended.
              if (lock.isLocked ()) file.resize (size_at_open);
              return false;
            }
          file.close ();
          // Our own append must not look like a change made by someone else, unless
          // another writer also appended since the last read. Judge that from the
          // size seen on the open file, not from possibly cached directory metadata.
          auto const known = m_->watched_.find (m_->path_);
          if (known != m_->watched_.end () && known->size == size_at_open && (known->exists || !size_at_open))
            {
              auto const after = signature_of (m_->path_);
              known->exists = true;
              known->size = size_at_open + bytes.size ();
              known->modified = after.modified;
              known->anchor = (known->anchor + bytes).right (anchor_size);
            }
        }
      worked_entry entry {call.toUpper (), grid.left (4).toUpper (), band.toUpper (), mode.toUpper ()
                          , entity, adif_record_date (ADIF_record)};
      insert_worked (m_->worked_, entry);
      if (m_->load_state_.active ())
        {
          insert_worked (m_->added_during_load_, entry);
        }
    }
  return true;
}

bool WorkedBefore::country_worked (QString const& country, QString const& mode, QString const& band,
                                   AD1CCty::EntityView view) const
{
  if (country.isEmpty ()) return false;
  auto lookup = [&] (auto const& mode_index, auto const& band_index) {
    if (!mode.isEmpty ())
      {
        if (!band.isEmpty ())
          return mode_index.end () != mode_index.find (std::make_tuple (country, mode.toUpper (), band.toUpper ()));
        return mode_index.end () != mode_index.find (std::make_tuple (country, mode.toUpper ()));
      }
    if (!band.isEmpty ())
      return band_index.end () != band_index.find (std::make_tuple (country, band.toUpper ()));
    return band_index.end () != band_index.find (country);
  };
  return view == AD1CCty::EntityView::WAE
    ? lookup (m_->worked_.get<wae_entity_mode_band> (), m_->worked_.get<wae_entity_band> ())
    : lookup (m_->worked_.get<dxcc_entity_mode_band> (), m_->worked_.get<dxcc_entity_band> ());
}

bool WorkedBefore::grid_worked (QString const& grid, QString const& mode, QString const& band) const
{
  auto gridsquare = grid.left (4).toUpper ();
  if (m_->configuration_->highlight_only_fields ())
    {
      // can't use a direct set find operation or a set operation with
      // a (CompatibleKey, CompatibleCompare) concept so we must
      // partially scan the index
      auto range = boost::make_iterator_range (
                                               m_->worked_.get<grid_mode_band> ().lower_bound (gridsquare.left (2))
                                               , m_->worked_.get<grid_mode_band> ().upper_bound (gridsquare.left (2) + "99"));
      for (worked_entry const& worked : range)
        {
          if ((!mode.size () || mode.toUpper () == worked.mode_)
              && (!band.size () || worked.band_ == band.toUpper ()))
            {
              return true;
            }
        }
    }
  else
    {
      if (mode.size ())
        {
          if (band.size ())
            {
              return m_->worked_.get<grid_mode_band> ().end ()
                != m_->worked_.get<grid_mode_band> ().find (std::make_tuple (gridsquare, mode.toUpper (), band.toUpper ()));
            }
          else
            {
              // partial key lookup
              return m_->worked_.get<grid_mode_band> ().end ()
                != m_->worked_.get<grid_mode_band> ().find (std::make_tuple (gridsquare, mode.toUpper ()));
            }
        }
      else
        {
          if (band.size ())
            {
              return m_->worked_.get<grid_band> ().end ()
                != m_->worked_.get<grid_band> ().find (std::make_tuple (gridsquare, band.toUpper ()));
            }
          else
            {
              // partial key lookup
              return m_->worked_.get<grid_band> ().end ()
                != m_->worked_.get<grid_band> ().find (gridsquare);
            }
        }
    }
  return false;
}

bool WorkedBefore::call_worked (QString const& call, QString const& mode, QString const& band) const
{
  if (mode.size ())
    {
      if (band.size ())
        {
          return m_->worked_.get<call_mode_band> ().end ()
            != m_->worked_.get<call_mode_band> ().find (std::make_tuple (call.toUpper (), mode.toUpper (), band.toUpper ()));
        }
      else
        {
          // partial key lookup
          return m_->worked_.get<call_mode_band> ().end ()
            != m_->worked_.get<call_mode_band> ().find (std::make_tuple (call.toUpper (), mode.toUpper ()));
        }
    }
  else
    {
      if (band.size ())
        {
          return m_->worked_.get<call_band> ().end ()
            != m_->worked_.get<call_band> ().find (std::make_tuple (call.toUpper (), band.toUpper ()));
        }
      else
        {
          // partial key lookup
          return m_->worked_.get<call_band> ().end ()
            != m_->worked_.get<call_band> ().find (std::make_tuple (call.toUpper ()));
        }
    }
}

bool WorkedBefore::call_worked_on_days (QString const& call, QString const& mode, QString const& band,
                                        QSet<QDate> const& days) const
{
  if (days.isEmpty ()) return call_worked (call, mode, band);
  auto any_on_days = [&days] (auto const& range) {
    for (auto it = range.first; it != range.second; ++it)
      {
        if (it->worked_on (days)) return true;
      }
    return false;
  };
  if (mode.size ())
    {
      auto const& index = m_->worked_.get<call_mode_band> ();
      return band.size ()
        ? any_on_days (index.equal_range (std::make_tuple (call.toUpper (), mode.toUpper (), band.toUpper ())))
        : any_on_days (index.equal_range (std::make_tuple (call.toUpper (), mode.toUpper ())));
    }
  auto const& index = m_->worked_.get<call_band> ();
  return band.size ()
    ? any_on_days (index.equal_range (std::make_tuple (call.toUpper (), band.toUpper ())))
    : any_on_days (index.equal_range (std::make_tuple (call.toUpper ())));
}

int WorkedBefore::calls_worked_on_days (QString const& band, QSet<QDate> const& days) const
{
  auto const wanted_band = band.toUpper ();
  int count {0};
  QString last_counted;
  // Ordered by call, then band: each call's entries are adjacent.
  for (auto const& entry : m_->worked_.get<call_band> ())
    {
      if (entry.call_ == last_counted) continue;
      if (!wanted_band.isEmpty () && entry.band_ != wanted_band) continue;
      if (!entry.worked_on (days)) continue;
      last_counted = entry.call_;
      ++count;
    }
  return count;
}

bool WorkedBefore::continent_worked (Continent continent, QString const& mode, QString const& band) const
{
  if (mode.size ())
    {
      if (band.size ())
        {
          return m_->worked_.get<continent_mode_band> ().end ()
            != m_->worked_.get<continent_mode_band> ().find (std::make_tuple (continent, mode.toUpper (), band.toUpper ()));
        }
      else
        {
          // partial key lookup
          return m_->worked_.get<continent_mode_band> ().end ()
            != m_->worked_.get<continent_mode_band> ().find (std::make_tuple (continent, mode.toUpper ()));
        }
    }
  else
    {
      if (band.size ())
        {
          return m_->worked_.get<continent_band> ().end ()
            != m_->worked_.get<continent_band> ().find (std::make_tuple (continent, band.toUpper ()));
        }
      else
        {
          // partial key lookup
          return m_->worked_.get<continent_band> ().end ()
            != m_->worked_.get<continent_band> ().find (continent);
        }
    }
}

bool WorkedBefore::CQ_zone_worked (int CQ_zone, QString const& mode, QString const& band) const
{
  if (mode.size ())
    {
      if (band.size ())
        {
          return m_->worked_.get<CQ_zone_mode_band> ().end ()
            != m_->worked_.get<CQ_zone_mode_band> ().find (std::make_tuple (CQ_zone, mode.toUpper (), band.toUpper ()));
        }
      else
        {
          // partial key lookup
          return m_->worked_.get<CQ_zone_mode_band> ().end ()
            != m_->worked_.get<CQ_zone_mode_band> ().find (std::make_tuple (CQ_zone, mode.toUpper ()));
        }
    }
  else
    {
      if (band.size ())
        {
          return m_->worked_.get<CQ_zone_band> ().end ()
            != m_->worked_.get<CQ_zone_band> ().find (std::make_tuple (CQ_zone, band.toUpper ()));
        }
      else
        {
          // partial key lookup
          return m_->worked_.get<CQ_zone_band> ().end ()
            != m_->worked_.get<CQ_zone_band> ().find (CQ_zone);
        }
    }
}

bool WorkedBefore::ITU_zone_worked (int ITU_zone, QString const& mode, QString const& band) const
{
  if (mode.size ())
    {
      if (band.size ())
        {
          return m_->worked_.get<ITU_zone_mode_band> ().end ()
            != m_->worked_.get<ITU_zone_mode_band> ().find (std::make_tuple (ITU_zone, mode.toUpper (), band.toUpper ()));
        }
      else
        {
          // partial key lookup
          return m_->worked_.get<ITU_zone_mode_band> ().end ()
            != m_->worked_.get<ITU_zone_mode_band> ().find (std::make_tuple (ITU_zone, mode.toUpper ()));
        }
    }
  else
    {
      if (band.size ())
        {
          return m_->worked_.get<ITU_zone_band> ().end ()
            != m_->worked_.get<ITU_zone_band> ().find (std::make_tuple (ITU_zone, band.toUpper ()));
        }
      else
        {
          // partial key lookup
          return m_->worked_.get<ITU_zone_band> ().end ()
            != m_->worked_.get<ITU_zone_band> ().find (ITU_zone);
        }
    }
}
