#include "WorkedBefore.hpp"

#include <atomic>
#include <functional>
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
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDateTime>
#include <QPointer>
#include <QSet>
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
                         , QString const& mode, AD1CCty::Record const& entity)
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
                 << e.ITU_zone_ << ')';
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
  auto const logFileName = "wsjtx_log.adi";

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

  struct adif_field
  {
    QString name;
    int data_start;
    int end;
    bool valid_length {false};
    bool needs_more {false};
    bool recover_boundary {false};
  };

  adif_field read_adif_field (QString const& input, int position, int closing, bool at_eof,
                            std::atomic_bool const& stop, bool character_lengths = false)
  {
    check_load_cancelled (stop, character_lengths ? "field_characters" : "field");
    auto descriptor = input.mid (position + 1, closing - position - 1);
    auto delimiter = descriptor.indexOf (QChar {':'});
    adif_field field {descriptor.left (delimiter).toUpper (), closing + 1, closing + 1};
    if (delimiter < 0) return field;
    auto type_delimiter = descriptor.indexOf (QChar {':'}, delimiter + 1);
    if (type_delimiter >= 0)
      {
        if (descriptor.size () != type_delimiter + 2) return field;
        auto type = descriptor[type_delimiter + 1].toUpper ();
        if (type < QChar {'A'} || type > QChar {'Z'}) return field;
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

    quint64 bytes {0};
    int end = field.data_start;
    characters = 0;
    while (!character_lengths && end < input.size () && bytes < length)
      {
        if (!(characters++ % 4096)) check_load_cancelled (stop, "field_data");
        auto character = input[end++].unicode ();
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
                    if (characters.needs_more || !characters.recover_boundary) return characters;
                  }
                field.recover_boundary = true;
                return field;
              }
          }
      }
    field.end = end;
    return field;
  }

  QString extractField (QString const& record, QString const& fieldName, std::atomic_bool const& stop,
                        bool character_lengths = false)
  {
    check_load_cancelled (stop, "extract_field");
    int position {0};
    while ((position = record.indexOf (QChar {'<'}, position)) >= 0)
      {
        check_load_cancelled (stop, "extract_field");
        auto closing = record.indexOf (QChar {'>'}, position + 1);
        if (closing < 0) break;
        auto field = read_adif_field (record, position, closing, true, stop, character_lengths);
        if (field.name.compare (fieldName, Qt::CaseInsensitive) == 0)
          {
            if (!field.valid_length || field.recover_boundary)
              throw LoaderException (std::runtime_error {QCoreApplication::translate (
                "WorkedBefore", "Invalid ADIF field length: %1").arg (fieldName).toLocal8Bit ()});
            return record.mid (field.data_start, field.end - field.data_start);
          }
        position = field.recover_boundary ? field.data_start : field.end;
      }
    return {};
  }

  int find_adif_tag (QString const& input, QString const& tag, int start, int& tag_end, bool at_eof,
                    std::atomic_bool const& stop, bool character_lengths = false)
  {
    check_load_cancelled (stop, character_lengths ? "tag_characters" : "tag");
    int position {start};
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
        if (closing < 0) return -1;
        auto field = read_adif_field (input, position, closing, at_eof, stop, character_lengths);
        if (input.mid (position + 1, closing - position - 1).compare (tag, Qt::CaseInsensitive) == 0)
          {
            tag_end = closing + 1;
            return position;
          }
        if (field.needs_more) return -1;
        position = field.recover_boundary ? field.data_start : field.end;
      }
    return -1;
  }

  enum class source_status {Complete, Incomplete, Failed};

  struct source_result
  {
    worked_before_database_type worked;
    QString error;
    source_status status {source_status::Complete};
  };

  source_result loader (QString const& path, AD1CCty const * prefixes,
                        PerformanceTrace::RunId run, std::atomic_bool const& stop)
  {
    check_load_cancelled (stop, "file");
    PerformanceTrace::Phase parse {run, "logbook.parse"};
    source_result result;
    auto& worked = result.worked;
    try
      {
        QFile inputFile {path};
        if (inputFile.open (QFile::ReadOnly))
          {
            check_load_cancelled (stop, "file_opened");
            if (inputFile.size ())
              {
                QTextStream in {&inputFile};
                in.setCodec ("UTF-8");
                QString buffer;
                constexpr int read_chunk_size {64 * 1024};
                bool eof {false};
                auto read_more = [&in, &buffer, &eof, &stop] ()
                  {
                    check_load_cancelled (stop, "read");
                    auto chunk = in.read (read_chunk_size);
                    check_load_cancelled (stop, "read_complete");
                    buffer += chunk;
                    eof = chunk.isEmpty ();
                    return !eof;
                  };
                while (true)
                  {
                    int first_character {0};
                    while (first_character < buffer.size () && buffer[first_character].isSpace ())
                      {
                        if (!(first_character % 4096)) check_load_cancelled (stop, "whitespace");
                        ++first_character;
                      }
                    if (first_character == buffer.size ())
                      {
                        buffer.clear ();
                        if (!read_more ()) break;
                        continue;
                      }
                    if (buffer[first_character] != QChar {'<'})
                      {
                        int header_end {0};
                        while (find_adif_tag (buffer, "EOH", 0, header_end, eof, stop) < 0)
                          {
                            if (eof)
                              {
                                throw LoaderException (std::runtime_error {QCoreApplication::translate ("WorkedBefore", "Invalid ADIF header").toLocal8Bit ()});
                              }
                            read_more ();
                          }
                        buffer.remove (0, header_end);
                      }
                    else
                      {
                        while (true)
                          {
                            int header_end {0};
                            int record_end {0};
                            auto header_position = find_adif_tag (buffer, "EOH", 0, header_end, eof, stop);
                            auto record_position = find_adif_tag (buffer, "EOR", 0, record_end, eof, stop);
                            if (header_position >= 0 && (record_position < 0 || header_position < record_position))
                              {
                                buffer.remove (0, header_end);
                                break;
                              }
                            if (record_position >= 0 || eof) break;
                            read_more ();
                          }
                      }
                    break;
                  }

                auto append_record = [&] (QString const& record, bool character_lengths) {
                    check_load_cancelled (stop, character_lengths ? "record_characters" : "record");
                    auto call = extractField (record, "CALL", stop, character_lengths);
                    if (call.isEmpty ()) return;
                    auto const& entity = prefixes->lookup (call);
                    auto mode = extractField (record, "MODE", stop, character_lengths).toUpper ();
                    if (mode.isEmpty () || mode == "MFSK")
                      mode = extractField (record, "SUBMODE", stop, character_lengths).toUpper ();
                    worked.emplace (call.toUpper ()
                                    , extractField (record, "GRIDSQUARE", stop, character_lengths).left (4).toUpper ()
                                    , extractField (record, "BAND", stop, character_lengths).toUpper ()
                                    , mode, entity);
                  };
                int buffer_start {0};
                while (true)
                  {
                    int record_end {0};
                    bool character_lengths {false};
                    auto end_position = find_adif_tag (buffer, "EOR", buffer_start, record_end, eof, stop);
                    if (end_position < 0 && eof)
                      {
                        end_position = find_adif_tag (buffer, "EOR", buffer_start, record_end, eof, stop, true);
                        character_lengths = true;
                      }
                    if (end_position >= 0)
                      {
                        auto record = buffer.mid (buffer_start, record_end - buffer_start);
                        try
                          {
                            append_record (record, character_lengths);
                          }
                        catch (LoaderException const&)
                          {
                            if (character_lengths || record.toLatin1 () == record.toUtf8 ()) throw;
                            auto fallback = find_adif_tag (buffer, "EOR", buffer_start, record_end, eof, stop, true);
                            if (fallback < 0)
                              {
                                if (eof) throw;
                                read_more ();
                                continue;
                              }
                            append_record (buffer.mid (buffer_start, record_end - buffer_start), true);
                          }
                        buffer_start = record_end;
                        if (buffer_start >= read_chunk_size)
                          {
                            buffer.remove (0, buffer_start);
                            buffer_start = 0;
                          }
                      }
                    else
                      {
                        if (buffer_start)
                          {
                            buffer.remove (0, buffer_start);
                            buffer_start = 0;
                          }
                        if (eof) break;
                        read_more ();
                      }
                  }
                if (buffer.contains (QRegularExpression {QStringLiteral ("<[A-Za-z][A-Za-z0-9_]*:")}))
                  {
                    throw LoaderException (std::runtime_error {QCoreApplication::translate ("WorkedBefore", "Incomplete ADIF record (missing <EOR>)").toLocal8Bit ()});
                  }
                if (inputFile.error () != QFile::NoError || in.status () == QTextStream::ReadCorruptData)
                  {
                    throw LoaderException (std::runtime_error {QCoreApplication::translate ("WorkedBefore", "Error reading ADIF file: %1").arg (inputFile.errorString ()).toLocal8Bit ()});
                  }
              }
          }
        else
          {
            throw LoaderException (std::runtime_error {QCoreApplication::translate ("WorkedBefore", "Error opening ADIF file: %1").arg (inputFile.errorString ()).toLocal8Bit ()});
          }
      }
    catch (LoaderException const& e)
      {
        result.error = e.error ();
        result.status = worked.empty () ? source_status::Failed : source_status::Incomplete;
      }
    check_load_cancelled (stop, "file_complete");
    parse.finish (QString {"unique_entries=%1"}.arg (worked.size ()));
    return result;
  }

  struct load_result
  {
    worked_before_database_type worked;
    QStringList errors;
    QStringList folder_summaries;
    int directory_failures {0};
    int files_read {0};
    int files_incomplete {0};
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
            bool recovered = path == primary && source.status == source_status::Incomplete;
            if (source.status == source_status::Complete || recovered)
              {
                if (result.worked.empty ()) result.worked.swap (source.worked);
                else
                  {
                    unsigned merged {0};
                    for (auto const& entry : source.worked)
                      {
                        if (!(merged++ % 256)) check_load_cancelled (stop, "merge");
                        result.worked.insert (entry);
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

class WorkedBefore::impl final
{
public:
  impl (Configuration const * configuration)
    : construction_trace_ {"logbook.construct"}
    , configuration_ {configuration}
    , path_ {writable_file_path (configuration->writeable_data_dir (), logFileName)}
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
        return load_sources (primary, files, folders, prefixes, run, *stop);
      });
    loader_watcher_.setFuture (async_loader_);
  }

  PerformanceTrace::Phase construction_trace_;
  Configuration const * configuration_;
  QString path_;
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
  reload ();
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
  result.worked.insert (m_->added_during_load_.begin (), m_->added_during_load_.end ());
  m_->worked_.swap (result.worked);
  auto n = m_->worked_.size ();
  m_->load_errors_ = result.errors.join ('\n');
  if (result.errors.isEmpty ())
    {
      m_->load_status_ = tr ("History loaded: %1 unique worked-before entries; %2 files loaded.").arg (n).arg (result.files_read);
    }
  else if (result.files_read || result.files_incomplete)
    {
      m_->load_status_ = tr ("Incomplete history: %1 unique worked-before entries; %2 of %3 files loaded completely; %4 partially recovered; %5 failed.")
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
  // The worker borrows the prefix database and stop flag until it has exited.
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
      if (!file.open(QIODevice::Text | QIODevice::Append))
        {
          LOG_ERROR ("Error opening ADIF log file for append: " << file.fileName ()
                     << " - " << file.errorString ());
          return false;
        }
      else
        {
          QTextStream out {&file};
          if (!file.size ())
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
        }
      worked_entry entry {call.toUpper (), grid.left (4).toUpper (), band.toUpper (), mode.toUpper ()
                          , entity};
      m_->worked_.insert (entry);
      if (m_->load_state_.active ())
        {
          m_->added_during_load_.insert (entry);
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
