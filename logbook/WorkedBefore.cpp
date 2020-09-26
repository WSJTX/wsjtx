#include "WorkedBefore.hpp"

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
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include "Configuration.hpp"
#include "revision_utils.hpp"
#include "qt_helpers.hpp"
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
                         , QString const& mode, QString const& country, AD1CCty::Continent continent
                         , int CQ_zone, int ITU_zone)
    : call_ {call}
    , grid_ {grid}
    , band_ {band}
    , mode_ {mode}
    , country_ {country}
    , continent_ {continent}
    , CQ_zone_ {CQ_zone}
    , ITU_zone_ {ITU_zone}
  {
  }

  QString call_;
  QString grid_;
  QString band_;
  QString mode_;
  QString country_;
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
    && lhs.country_ == rhs.country_   // differences as quickly as possible
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
  boost::hash_combine (seed, we.country_);
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
                 << e.country_ << ", "
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
struct entity_mode_band {};
struct entity_band {};
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
    // country+mode+band
    ordered_non_unique<tag<entity_mode_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, QString, &worked_entry::country_>,
                                     member<worked_entry, QString, &worked_entry::mode_>,
                                     member<worked_entry, QString, &worked_entry::band_> > >,
    // country+band
    ordered_non_unique<tag<entity_band>,
                       composite_key<worked_entry,
                                     member<worked_entry, QString, &worked_entry::country_>,
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

  // Expception class suitable for using with QtConcurrent across
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

  QString extractField (QString const& record, QString const& fieldName)
  {
    int fieldNameIndex = record.indexOf ('<' + fieldName + ':', 0, Qt::CaseInsensitive);
    if (fieldNameIndex >=0)
      {
        int closingBracketIndex = record.indexOf('>',fieldNameIndex);
        int fieldLengthIndex = record.indexOf(':',fieldNameIndex);  // find the size delimiter
        int dataTypeIndex = -1;
        if (fieldLengthIndex >= 0)
          {
            dataTypeIndex = record.indexOf(':',fieldLengthIndex+1);  // check for a second : indicating there is a data type
            if (dataTypeIndex > closingBracketIndex)
              dataTypeIndex = -1; // second : was found but it was beyond the closing >
          }
        else
          {
            throw LoaderException (std::runtime_error {QCoreApplication::translate ("WorkedBefore", "Invalid ADIF field %0: %1").arg (fieldName).arg (record).toLocal8Bit ()});
          }

        if (closingBracketIndex > fieldNameIndex && fieldLengthIndex > fieldNameIndex && fieldLengthIndex < closingBracketIndex)
          {
            int fieldLengthCharCount = closingBracketIndex - fieldLengthIndex -1;
            if (dataTypeIndex >= 0)
              fieldLengthCharCount -= 2; // data type indicator is always a colon followed by a single character
            QString fieldLengthString = record.mid(fieldLengthIndex+1,fieldLengthCharCount);
            int fieldLength = fieldLengthString.toInt();
            if (fieldLength > 0)
              {
                return record.mid(closingBracketIndex+1,fieldLength);
              }
          }
        else
          {
            throw LoaderException (std::runtime_error {QCoreApplication::translate ("WorkedBefore", "Malformed ADIF field %0: %1").arg (fieldName).arg (record).toLocal8Bit ()});
          }
      }
    return QString {};
  }

  worked_before_database_type loader (QString const& path, AD1CCty const * prefixes)
  {
    worked_before_database_type worked;
    QFile inputFile {path};
    if (inputFile.exists ())
      {
        if (inputFile.open (QFile::ReadOnly))
          {
            QTextStream in {&inputFile};
            QString buffer;
            bool pre_read {false};
            int end_position {-1};

            // skip optional header record
            do
              {
                buffer += in.readLine () + '\n';
                if (buffer.startsWith (QChar {'<'})) // denotes no header
                  {
                    pre_read = true;
                  }
                else
                  {
                    end_position = buffer.indexOf ("<EOH>", 0, Qt::CaseInsensitive);
                  }
              }
            while (!in.atEnd () && !pre_read && end_position < 0);
            if (!pre_read)            // found header
              {
                if (end_position < 0)
                  {
                    throw LoaderException (std::runtime_error {QCoreApplication::translate ("WorkedBefore", "Invalid ADIF header").toLocal8Bit ()});
                  }
                buffer.remove (0, end_position + 5);
              }
            while (!in.atEnd ())
              {
                end_position = buffer.indexOf ("<EOR>", 0, Qt::CaseInsensitive);
                do
                  {
                    if (!in.atEnd () && end_position < 0)
                      {
                        buffer += in.readLine () + '\n';
                      }
                  }
                while ((end_position = buffer.indexOf ("<EOR>", 0, Qt::CaseInsensitive)) < 0 && !in.atEnd ());
                if (end_position >= 0) // require valid ADIF record
                                       // with terminator
                  {
                    auto record = buffer.left (end_position + 5).trimmed ();
                    auto next_record = buffer.indexOf (QChar {'<'}, end_position + 5);
                    buffer.remove (0, next_record >=0 ? next_record : buffer.size ());
                    record = record.mid (record.indexOf (QChar {'<'}));
                    auto call = extractField (record, "CALL");
                    if (call.size ()) // require CALL field before we
                                      // will parse a record
                      {
                        auto const& entity = prefixes->lookup (call);
                        auto mode = extractField (record, "MODE").toUpper ();
                        if (!mode.size () || "MFSK" == mode)
                          {
                            mode = extractField (record, "SUBMODE").toUpper ();
                          }
                        worked.emplace (call.toUpper ()
                                        , extractField (record, "GRIDSQUARE").left (4).toUpper () // not interested in 6-digit grids
                                        , extractField (record, "BAND").toUpper ()
                                        , mode
                                        , entity.entity_name
                                        , entity.continent
                                        , entity.CQ_zone
                                        , entity.ITU_zone);
                      }
                  }
              }
          }
        else
          {
            throw LoaderException (std::runtime_error {QCoreApplication::translate ("WorkedBefore", "Error opening ADIF log file for read: %0").arg (inputFile.errorString ()).toLocal8Bit ()});
          }
      }
    return worked;
  }
}

class WorkedBefore::impl final
{
public:
  impl (Configuration const * configuration)
    : configuration_ {configuration}
    , path_ {QDir {QStandardPaths::writableLocation (QStandardPaths::DataLocation)}.absoluteFilePath (logFileName)}
    , prefixes_ {configuration}
  {
  }

  void reload ()
  {
    async_loader_ = QtConcurrent::run (loader, path_, &prefixes_);
    loader_watcher_.setFuture (async_loader_);
  }

  Configuration const * configuration_;
  QString path_;
  AD1CCty prefixes_;
  QFutureWatcher<worked_before_database_type> loader_watcher_;
  QFuture<worked_before_database_type> async_loader_;
  worked_before_database_type worked_;
};

WorkedBefore::WorkedBefore (Configuration const * configuration)
  : m_ {configuration}
{
  Q_ASSERT (configuration);
  connect (&m_->loader_watcher_, &QFutureWatcher<worked_before_database_type>::finished, [this] () {
      QString error;
      size_t n {0};
      try
        {
          m_->worked_ = m_->loader_watcher_.result ();
          n = m_->worked_.size ();
        }
      catch (LoaderException const& e)
        {
          error = e.error ();
        }
      Q_EMIT finished_loading (n, error);
    });
  reload ();
}

void WorkedBefore::reload ()
{
  m_->reload ();
}

WorkedBefore::~WorkedBefore ()
{
}

QString const& WorkedBefore::path () const
{
  return m_->path_;
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
      if (!file.open(QIODevice::Text | QIODevice::Append))
        {
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
                  "<adif_ver:5>3.1.1\n"
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
      m_->worked_.emplace (call.toUpper (), grid.left (4).toUpper (), band.toUpper (), mode.toUpper ()
                           , entity.entity_name, entity.continent, entity.CQ_zone, entity.ITU_zone);
    }
  return true;
}

bool WorkedBefore::country_worked (QString const& country, QString const& mode, QString const& band) const
{
  if (mode.size ())
    {
      if (band.size ())
        {
          return
            country.size ()
            && m_->worked_.get<entity_mode_band> ().end ()
            != m_->worked_.get<entity_mode_band> ().find (std::make_tuple (country, mode.toUpper (), band.toUpper ()));
        }
      else
        {
          // partial key lookup
          return
            country.size ()
            && m_->worked_.get<entity_mode_band> ().end ()
            != m_->worked_.get<entity_mode_band> ().find (std::make_tuple (country, mode.toUpper ()));
        }
    }
  else
    {
      if (band.size ())
        {
          return
            country.size ()
            && m_->worked_.get<entity_band> ().end ()
            != m_->worked_.get<entity_band> ().find (std::make_tuple (country, band.toUpper ()));
        }
      else
        {
          // partial key lookup
          return
            country.size ()
            && m_->worked_.get<entity_band> ().end ()
            != m_->worked_.get<entity_band> ().find (country);
        }
    }
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
