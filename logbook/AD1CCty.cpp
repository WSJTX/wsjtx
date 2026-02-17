#include "AD1CCty.hpp"

#include <string>
#include <stdexcept>
#include <algorithm>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/key_extractors.hpp>
#include <boost/lambda/lambda.hpp>
#include <boost/lexical_cast.hpp>
#include <QString>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QDebugStateSaver>
#include <QRegularExpression>
#include "Configuration.hpp"
#include "Radio.hpp"
#include "pimpl_impl.hpp"
#include "Logger.hpp"

#include "moc_AD1CCty.cpp"

using namespace boost::multi_index;

namespace
{
  auto const file_name = "cty.dat";
  auto const grid_file_name = "grid.dat";    // NJ0A
//  auto const logFileName = "wsjtx_log.adi";  // NJ0A
}

struct entity
{
  using Continent = AD1CCty::Continent;

  explicit entity (int id
                   , QString const& name
                   , QString const& abbreviated_name
                   , bool WAE_only
                   , int CQ_zone
                   , int ITU_zone
                   , Continent continent
                   , float latitude
                   , float longtitude
                   , int UTC_offset
                   , QString const& primary_prefix)
    : id_ {id}
    , name_ {name}
    , abbreviated_name_ {abbreviated_name}
    , WAE_only_ {WAE_only}
    , CQ_zone_ {CQ_zone}
    , ITU_zone_ {ITU_zone}
    , continent_ {continent}
    , lat_ {latitude}
    , long_ {longtitude}
    , UTC_offset_ {UTC_offset}
    , primary_prefix_ {primary_prefix}
  {
  }

  int id_;
  QString name_;
  QString abbreviated_name_;
  bool WAE_only_;               // DARC WAE only, not valid for ARRL awards
  int CQ_zone_;
  int ITU_zone_;
  Continent continent_;
  float lat_;                   // degrees + is North
  float long_;                  // degrees + is West
  int UTC_offset_;              // seconds
  QString primary_prefix_;
};

#define maxPrefix 25        //NJ0A
#define maxIndex 100        //NJ0A

QString gridPrefix [maxPrefix];        //NJ0A
QString gridState [maxPrefix] [maxIndex];   //NJ0A
int gridNumPrefix;          //NJ0A

#if !defined (QT_NO_DEBUG_STREAM)
QDebug operator << (QDebug dbg, entity const& e)
{
  QDebugStateSaver saver {dbg};
  dbg.nospace () << "entity("
                 << e.id_ << ", "
                 << e.name_ << ", "
                 << e.abbreviated_name_ << ", "
                 << e.WAE_only_ << ", "
                 << e.CQ_zone_ << ", "
                 << e.ITU_zone_ << ", "
                 << e.continent_ << ", "
                 << e.lat_ << ", "
                 << e.long_ << ", "
                 << (e.UTC_offset_ / (60. * 60.)) << ", "
                 << e.primary_prefix_ << ')';
  return dbg;
}
#endif

// tags
struct id {};
struct primary_prefix {};

// hash operation for QString object instances
struct hash_QString
{
  std::size_t operator () (QString const& qs) const
  {
    return qHash (qs);
  }
};

// set with hashed unique index that allow for efficient lookup of
// entity by internal id
typedef multi_index_container<
  entity,
  indexed_by<
    hashed_unique<tag<id>, member<entity, int, &entity::id_> >,
    hashed_unique<tag<primary_prefix>, member<entity, QString, &entity::primary_prefix_>, hash_QString> >
  > entities_type;

struct prefix
{
  explicit prefix (QString const& prefix, bool exact_match_only, int entity_id)
    : prefix_ {prefix}
    , exact_ {exact_match_only}
    , entity_id_ {entity_id}
  {
  }

  // extract key which is the prefix ignoring the trailing override
  // information
  QString prefix_key () const
  {
    auto const& prefix = prefix_.toStdString ();
    return QString::fromStdString (prefix.substr (0, prefix.find_first_of ("({[<~")));
  }
    
  QString prefix_;              // call or prefix with optional
                                // trailing override information
  bool exact_;
  int entity_id_;
};

#if !defined (QT_NO_DEBUG_STREAM)
QDebug operator << (QDebug dbg, prefix const& p)
{
  QDebugStateSaver saver {dbg};
  dbg.nospace () << "prefix("
                 << p.prefix_ << ", "
                 << p.exact_ << ", "
                 << p.entity_id_ << ')';
  return dbg;
}
#endif

// set with ordered unique index that allow for efficient
// determination of entity and entity overrides for a call or call
// prefix
typedef multi_index_container<
  prefix,
  indexed_by<
    ordered_unique<const_mem_fun<prefix, QString, &prefix::prefix_key> > >
  > prefixes_type;

class AD1CCty::impl final
{
public:
  using entity_by_id = entities_type::index<id>::type;

  explicit impl (Configuration const * configuration)
    : configuration_ {configuration}
  {
  }
  QString abbreviated_entity_name (QString const& name) const
  {
    auto result = name;
    result.replace ("Islands", "Is.");
    result.replace ("Island", "Is.");
    result.replace ("North ", "N. ");
    result.replace ("Northern ", "N. ");
    result.replace ("South ", "S. ");
    result.replace ("East ", "E. ");
    result.replace ("Eastern ", "E. ");
    result.replace ("West ", "W. ");
    result.replace ("Western ", "W. ");
    result.replace ("Central ", "C. ");
    result.replace (" and ", " & ");
    result.replace ("Republic", "Rep.");
    result.replace ("United States of America", "U.S.A.");
    result.replace ("United States", "U.S.A.");
    result.replace ("Fed. Rep. of ", "");
    result.replace ("French ", "Fr.");
    result.replace ("Asiatic", "AS");
    result.replace ("European", "EU");
    result.replace ("African", "AF");
    return result;
  }

  QString get_cty_path(const Configuration *configuration);
  void load_cty(QFile &file);

  entity_by_id::iterator lookup_entity (QString call, prefix const& p) const
  {
    call = call.toUpper ();
    entity_by_id::iterator e;   // iterator into entity set

    //
    // deal with special rules that cty.dat does not cope with
    //
    if (call.startsWith ("KG4") && call.size () != 5 && call.size () != 3)
      {
        // KG4 2x1 and 2x3 calls that map to Gitmo are mainland US not Gitmo
        return entities_.project<id> (entities_.get<primary_prefix> ().find ("K"));
      }
    else
      {
        return entities_.get<id> ().find (p.entity_id_);
      }
  }

  Record fixup (prefix const& p, entity const& e) const
  {
    Record result;
    result.continent = e.continent_;
    result.CQ_zone = e.CQ_zone_;
    result.ITU_zone = e.ITU_zone_;
    result.entity_name = e.name_;
    result.abbreviated_entity_name = e.abbreviated_name_;
    result.WAE_only = e.WAE_only_;
    result.latitude = e.lat_;
    result.longtitude = e.long_;
    result.UTC_offset = e.UTC_offset_;
    result.primary_prefix = e.primary_prefix_;

    // check for overrides
    bool ok1 {true}, ok2 {true}, ok3 {true}, ok4 {true}, ok5 {true};
    QString value;
    if (override_value (p.prefix_, '(', ')', value)) result.CQ_zone = value.toInt (&ok1);
    if (override_value (p.prefix_, '[', ']', value)) result.ITU_zone = value.toInt (&ok2);
    if (override_value (p.prefix_, '<', '>', value))
      {
        auto const& fix = value.split ('/');
        result.latitude = fix[0].toFloat (&ok3);
        result.longtitude = fix[1].toFloat (&ok4);
      }
    if (override_value (p.prefix_, '{', '}', value)) result.continent = continent (value);
    if (override_value (p.prefix_, '~', '~', value)) result.UTC_offset = static_cast<int> (value.toFloat (&ok5) * 60 * 60);
    if (!(ok1 && ok2 && ok3 && ok4 && ok5))
      {
        throw std::domain_error {"Invalid number in cty.dat for override of " + p.prefix_.toStdString ()};
      }
    return result;
  }

  static bool override_value (QString const& s, QChar lb, QChar ub, QString& v)
  {
    auto pos = s.indexOf (lb);
    if (pos >= 0)
      {
        v = s.mid (pos + 1, s.indexOf (ub, pos + 1) - pos - 1);
        return true;
      }
    return false;
  }

  Configuration const * configuration_;
  QString path_;
  QString cty_version_;
  QString cty_version_date_;

  entities_type entities_;
  prefixes_type prefixes_;
};

AD1CCty::Record::Record ()
  : continent {Continent::UN}
  , CQ_zone {0}
  , ITU_zone {0}
  , WAE_only {false}
  , latitude {NAN}
  , longtitude {NAN}
  , UTC_offset {0}
{
}

#if !defined (QT_NO_DEBUG_STREAM)
  QDebug operator << (QDebug dbg, AD1CCty::Record const& r)
  {
    QDebugStateSaver saver {dbg};
    dbg.nospace () << "AD1CCty::Record("
                   << r.continent << ", "
                   << r.CQ_zone << ", "
                   << r.ITU_zone << ", "
                   << r.entity_name << ", "
                   << r.WAE_only << ", "
                   << r.latitude << ", "
                   << r.longtitude << ", "
                   << (r.UTC_offset / (60. * 60.)) << ", "
                   << r.primary_prefix << ')';
    return dbg;
  }
#endif

auto AD1CCty::continent (QString const& continent_id) -> Continent
{
  Continent continent;
  if ("AF" == continent_id)
    {
      continent = Continent::AF;
    }
  else if ("AN" == continent_id)
    {
      continent = Continent::AN;
    }
  else if ("AS" == continent_id)
    {
      continent = Continent::AS;
    }
  else if ("EU" == continent_id)
    {
      continent = Continent::EU;
    }
  else if ("NA" == continent_id)
    {
      continent = Continent::NA;
    }
  else if ("OC" == continent_id)
    {
      continent = Continent::OC;
    }
  else if ("SA" == continent_id)
    {
      continent = Continent::SA;
    }
  else
    {
      throw std::domain_error {"Invalid continent id: " + continent_id.toStdString ()};
    }
  return continent;
}

char const * AD1CCty::continent (Continent c)
{
  switch (c)
    {
    case Continent::AF: return "AF";
    case Continent::AN: return "AN";
    case Continent::AS: return "AS";
    case Continent::EU: return "EU";
    case Continent::NA: return "NA";
    case Continent::OC: return "OC";
    case Continent::SA: return "SA";
    default: return "UN";
    }
}

QString AD1CCty::impl::get_cty_path(Configuration const * configuration)
{
  QDir dataPath {QStandardPaths::writableLocation (QStandardPaths::DataLocation)};
  auto path = dataPath.exists (file_name)
              ? dataPath.absoluteFilePath (file_name) // user override
              : configuration->data_dir ().absoluteFilePath (file_name); // or original
  return path;
}

void AD1CCty::impl::load_cty(QFile &file)
{
  QRegularExpression version_pattern{R"(VER\d{8})"};
  int entity_id = 0;
  int line_number{0};

  entities_.clear();
  prefixes_.clear();
  cty_version_ = QString{};
  cty_version_date_ = QString{};

  QTextStream in{&file};
  while (!in.atEnd())
  {
    auto const &entity_line = in.readLine();
    ++line_number;
    if (!in.atEnd())
    {
      auto const &entity_parts = entity_line.split(':');
      if (entity_parts.size() >= 8)
      {
        auto primary_prefix = entity_parts[7].trimmed();
        bool WAE_only{false};
        if (primary_prefix.startsWith('*'))
        {
          primary_prefix = primary_prefix.mid(1);
          WAE_only = true;
        }
        bool ok1, ok2, ok3, ok4, ok5;
        entities_.emplace(++entity_id, entity_parts[0].trimmed(), abbreviated_entity_name(entity_parts[0].trimmed()),WAE_only, entity_parts[1].trimmed().toInt(&ok1),
                          entity_parts[2].trimmed().toInt(&ok2), continent(entity_parts[3].trimmed()),
                          entity_parts[4].trimmed().toFloat(&ok3), entity_parts[5].trimmed().toFloat(&ok4),
                          static_cast<int> (entity_parts[6].trimmed().toFloat(&ok5) * 60 * 60), primary_prefix);
        if (!(ok1 && ok2 && ok3 && ok4 && ok5))
        {
          throw std::domain_error{"Invalid number in cty.dat line " + boost::lexical_cast<std::string>(line_number)};
        }
        QString line;
        QString detail;
        do
        {
          in.readLineInto(&line);
          ++line_number;
        } while (detail += line, !detail.endsWith(';'));
        for (auto prefix: detail.left(detail.size() - 1).split(','))
        {
          prefix = prefix.trimmed();
          bool exact{false};
          if (prefix.startsWith('='))
          {
            prefix = prefix.mid(1);
            exact = true;
            // match version pattern to prefix
            if (version_pattern.match(prefix).hasMatch())
            {
              cty_version_date_ = prefix;
            }
          }
          prefixes_.emplace(prefix, exact, entity_id);
        }
      }
    }
  }
}

AD1CCty::AD1CCty (Configuration const * configuration)
  : m_ {configuration}
{
  Q_ASSERT (configuration);
  // TODO: G4WJS - consider doing the following asynchronously to
  // speed up startup. Not urgent as it takes less than 0.5s on a Core
  // i7 reading BIG CTY.DAT.
  AD1CCty::reload (configuration);

  //NJ0A
  gridNumPrefix = 0;

  for (int i = 0; i < maxPrefix ; i ++) {
      for (int j = 0; j < maxIndex; j ++) {
          gridState[i] [j] = "**";
      }
  }

  QDir dataPath {QStandardPaths::writableLocation (QStandardPaths::DataLocation)};
  m_->path_ = dataPath.exists (file_name)
    ? dataPath.absoluteFilePath (file_name) // user override
    : configuration->data_dir ().absoluteFilePath (file_name); // or original

  QString path = dataPath.exists (grid_file_name)
   ? dataPath.absoluteFilePath (grid_file_name) // user override
   : configuration->data_dir ().absoluteFilePath (grid_file_name);   // or original in the resources FS


  QFile file1 {path};

  if (file1.open (QFile::ReadOnly))
    {
       int line_number [[maybe_unused]] {0};
       QTextStream in {&file1};
       while (!in.atEnd ())
       {
          auto const& entity_line = in.readLine ();
          ++line_number;
          if (!in.atEnd () && entity_line.length() > 0 && entity_line.contains("<"))
          {
              //std::cout << entity_line.toStdString() << '\n';
              if (entity_line.contains("<")) {
                auto const& entity_parts = entity_line.split ('<');
                //std::cout << "Grid prefix: " << entity_parts[0].toStdString() << '\n';
                gridPrefix[gridNumPrefix] = entity_parts[0];
                gridNumPrefix++;
                while (!in.atEnd()) {
                    auto const& entity_grid_line = in.readLine();
                    if (entity_grid_line.length()  > 1 &&
                            entity_grid_line.contains(":") &&
                            entity_grid_line.contains(",")) {
                        auto const& entity_parts = entity_grid_line.split(":");
                        //std::cout << "Grid Indxes: " << entity_parts[0].trimmed().toStdString() << " " ;

                        auto const& entity_temp = entity_parts[1].split(",");
                        auto const & entity_state = entity_temp[0];
                        //std::cout << "State: " << entity_state.toStdString() << '\n';
                        gridState[gridNumPrefix - 1] [entity_parts[0].toInt()] = entity_state;

                    } else if (entity_grid_line.length()  > 1 &&
                               entity_grid_line.contains(":") &&
                               entity_grid_line.contains(">")) {
                        auto const& entity_parts = entity_grid_line.split(":");
                        //std::cout << "Grid Indxes: " <<  entity_parts[0].trimmed().toStdString() << " ";

                        auto const& entity_temp = entity_parts[1].split(">");
                        auto const & entity_state = entity_temp[0];
                        //std::cout << "State: " << entity_state.toStdString() << '\n';
                        gridState[gridNumPrefix - 1] [entity_parts[0].toInt()] = entity_state;

                        break;
                    }
                }
              }
          }
       }
    }
  }

void AD1CCty::reload(Configuration const * configuration)
{
  m_->path_ = m_->impl::get_cty_path(configuration);
  QFile file {m_->path_};

  LOG_INFO(QString{"Loading CTY.DAT from %1"}.arg (m_->path_));

  if (file.open (QFile::ReadOnly))
  {
    m_->impl::load_cty(file);
    m_->cty_version_ = AD1CCty::lookup("VERSION").entity_name;
    Q_EMIT cty_loaded(m_->cty_version_);
    LOG_INFO(QString{"Loaded CTY.DAT version %1, %2"}.arg (m_->cty_version_date_).arg (m_->cty_version_));
  }
}

AD1CCty::~AD1CCty ()
{
}

auto AD1CCty::lookup (QString const& call) const -> Record
{
  auto const& exact_search = call.toUpper ();
  if (!(exact_search.endsWith ("/MM") || exact_search.endsWith ("/AM")))
    {
      auto search_prefix = Radio::effective_prefix (exact_search);
      if (search_prefix != exact_search)
        {
          auto p = m_->prefixes_.find (exact_search);
          if (p != m_->prefixes_.end () && p->exact_)
            {
              return m_->fixup (*p, *m_->lookup_entity (call, *p));
            }
        }
      while (search_prefix.size ())
        {
          auto p = m_->prefixes_.find (search_prefix);
          if (p != m_->prefixes_.end ())
            {
              impl::entity_by_id::iterator e = m_->lookup_entity (call, *p);
              // always lookup WAE entities, we substitute them later in displaytext.cpp if "Include extra WAE entites" is not selected
              if (!p->exact_ || call.size () == search_prefix.size ())
                {
                  return m_->fixup (*p, *e);
                }
            }
          search_prefix = search_prefix.left (search_prefix.size () - 1);
        }
    }
  return Record {};
}

auto AD1CCty::version () const -> QString
{
  return m_->cty_version_date_;
}

// NJ0A
auto AD1CCty::findState ( QString const& grid) const -> QString
{


    auto const& prefix = grid.left(2);
    int gridIndex = grid.mid(2,2).toInt();
    //if (gridIndex < 0 || gridIndex > maxIndex) {
        //std::cout << "Prefix: " << prefix.toStdString() << " Index " << gridIndex << '\n';
    //}
    for (int i = 0; i < gridNumPrefix + 1; i ++) {
        if (gridPrefix[i] == prefix) {
            QString state = gridState[i] [gridIndex];
            //if (state.length() < 2 ) {
               // std::cout << "Prefix: " << prefix.toStdString() << " Index " << gridIndex << '\n';
            //}
            return state;
        }
    }

    return "**";
} // NJ0A
