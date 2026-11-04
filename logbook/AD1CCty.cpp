#include "AD1CCty.hpp"

#include <string>
#include <stdexcept>
#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <optional>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/key_extractors.hpp>
#include <boost/lambda/lambda.hpp>
#include <QString>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QTextStream>
#include <QDebug>
#include <QDebugStateSaver>
#include <QRegularExpression>
#include "Configuration.hpp"
#include "Radio.hpp"
#include "pimpl_impl.hpp"
#include "Logger.hpp"
#include "qt_helpers.hpp"

#include "moc_AD1CCty.cpp"

using namespace boost::multi_index;

namespace
{
  auto const file_name = "cty.dat";
  auto const grid_file_name = "grid.dat";    // NJ0A
//  auto const logFileName = "wsjtx_log.adi";  // NJ0A

  constexpr int min_cq_zone = 1;
  constexpr int max_cq_zone = 40;
  constexpr int min_itu_zone = 1;
  constexpr int max_itu_zone = 90;
  constexpr double min_latitude = -90.;
  constexpr double max_latitude = 90.;
  constexpr double min_longitude = -180.;
  constexpr double max_longitude = 180.;
  constexpr double min_utc_offset = -14.;
  constexpr double max_utc_offset = 14.;

  class parse_diagnostics
  {
  public:
    void add (int line, QString const& reason)
    {
      ++count_;
      if (examples_.size () < 5) examples_ << QString {"line %1: %2"}.arg (line).arg (reason);
    }

    void report (QString const& path) const
    {
      if (count_) LOG_WARN(QString {"Found %1 data issues while loading %2 (%3)"}
                           .arg (count_).arg (path).arg (examples_.join ("; ")));
    }

  private:
    int count_ = 0;
    QStringList examples_;
  };

  bool parse_bounded_int (QString const& text, int minimum, int maximum, int& value)
  {
    bool ok;
    auto const parsed = text.trimmed ().toInt (&ok);
    if (!ok || parsed < minimum || parsed > maximum) return false;
    value = parsed;
    return true;
  }

  bool parse_bounded_float (QString const& text, double minimum, double maximum, float& value)
  {
    bool ok;
    auto const parsed = text.trimmed ().toDouble (&ok);
    if (!ok || !std::isfinite (parsed) || parsed < minimum || parsed > maximum) return false;
    value = static_cast<float> (parsed);
    return true;
  }

  bool parse_utc_offset (QString const& text, int& seconds)
  {
    bool ok;
    auto const hours = text.trimmed ().toDouble (&ok);
    if (!ok || !std::isfinite (hours) || hours < min_utc_offset || hours > max_utc_offset) return false;
    seconds = static_cast<int> (std::lround (hours * 60. * 60.));
    return true;
  }

  bool parse_continent (QString const& id, AD1CCty::Continent& continent)
  {
    if ("AF" == id) continent = AD1CCty::Continent::AF;
    else if ("AN" == id) continent = AD1CCty::Continent::AN;
    else if ("AS" == id) continent = AD1CCty::Continent::AS;
    else if ("EU" == id) continent = AD1CCty::Continent::EU;
    else if ("NA" == id) continent = AD1CCty::Continent::NA;
    else if ("OC" == id) continent = AD1CCty::Continent::OC;
    else if ("SA" == id) continent = AD1CCty::Continent::SA;
    else return false;
    return true;
  }

  bool parse_grid_index (QString const& text, int& index)
  {
    if (text.size () != 2
        || text[0] < QLatin1Char {'0'} || text[0] > QLatin1Char {'9'}
        || text[1] < QLatin1Char {'0'} || text[1] > QLatin1Char {'9'}) return false;
    index = (text[0].unicode () - '0') * 10 + text[1].unicode () - '0';
    return true;
  }

  bool valid_grid_prefix (QString const& prefix)
  {
    return prefix.size () == 2
      && prefix[0] >= QLatin1Char {'A'} && prefix[0] <= QLatin1Char {'Z'}
      && prefix[1] >= QLatin1Char {'A'} && prefix[1] <= QLatin1Char {'Z'};
  }
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
  explicit prefix (QString const& prefix, int entity_id)
    : prefix_ {prefix}
    , entity_id_ {entity_id}
  {
  }

  QString prefix_;
  int entity_id_;
  std::optional<int> CQ_zone_;
  std::optional<int> ITU_zone_;
  std::optional<AD1CCty::Continent> continent_;
  std::optional<std::pair<float, float>> coordinates_;
  std::optional<int> UTC_offset_;
};

bool same_definition (prefix const& lhs, prefix const& rhs)
{
  return lhs.entity_id_ == rhs.entity_id_
    && lhs.CQ_zone_ == rhs.CQ_zone_ && lhs.ITU_zone_ == rhs.ITU_zone_
    && lhs.continent_ == rhs.continent_ && lhs.coordinates_ == rhs.coordinates_
    && lhs.UTC_offset_ == rhs.UTC_offset_;
}

#if !defined (QT_NO_DEBUG_STREAM)
QDebug operator << (QDebug dbg, prefix const& p)
{
  QDebugStateSaver saver {dbg};
  dbg.nospace () << "prefix("
                 << p.prefix_ << ", "
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
    ordered_unique<member<prefix, QString, &prefix::prefix_> > >
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

  struct match
  {
    entity const * country {};
    prefix const * alias {};
  };

  match lookup_match (prefix const& p, QString const& effective_prefix = {}) const
  {
    if (p.prefix_ == "KG4" && effective_prefix.startsWith ("KG4")
        && effective_prefix.size () != 5 && effective_prefix.size () != 3)
      {
        // Exact calls and foreign operating prefixes take precedence over this exception.
        auto const us = entities_.get<primary_prefix> ().find ("K");
        if (us == entities_.get<primary_prefix> ().end () || us->WAE_only_) return {};
        return {&*us, nullptr};
      }
    auto const e = entities_.get<id> ().find (p.entity_id_);
    return e == entities_.get<id> ().end () ? match {} : match {&*e, &p};
  }

  static EntityIdentity identity_for (match const& found)
  {
    if (!found.country) return {};
    auto const& e = *found.country;
    return {e.name_, e.abbreviated_name_, e.primary_prefix_, e.WAE_only_};
  }

  static void set_geography (Record& result, match const& found)
  {
    if (!found.country) return;
    auto const& e = *found.country;
    result.continent = e.continent_;
    result.CQ_zone = e.CQ_zone_;
    result.ITU_zone = e.ITU_zone_;
    result.latitude = e.lat_;
    result.longtitude = e.long_;
    result.UTC_offset = e.UTC_offset_;
    if (!found.alias) return;
    auto const& p = *found.alias;
    if (p.CQ_zone_) result.CQ_zone = *p.CQ_zone_;
    if (p.ITU_zone_) result.ITU_zone = *p.ITU_zone_;
    if (p.continent_) result.continent = *p.continent_;
    if (p.coordinates_)
      {
        result.latitude = p.coordinates_->first;
        result.longtitude = p.coordinates_->second;
      }
    if (p.UTC_offset_) result.UTC_offset = *p.UTC_offset_;
  }

  prefix const * find_alias (QString const& key, EntityView view, bool exact) const
  {
    if (view == EntityView::WAE)
      {
        auto const& index = exact ? wae_exact_calls_ : wae_prefixes_;
        auto const found = index.find (key);
        if (found != index.end ()) return &*found;
      }
    auto const& index = exact ? exact_calls_ : prefixes_;
    auto const found = index.find (key);
    return found == index.end () ? nullptr : &*found;
  }

  match resolve (QString const& call, EntityView view, bool use_wae_parent = true) const
  {
    auto exact_search = call.trimmed ().toUpper ();
    while (true)
      {
        if (exact_search.endsWith ("/MM") || exact_search.endsWith ("/AM")) return {};
        if (auto const exact = find_alias (exact_search, view, true)) return lookup_match (*exact);
        if (view == EntityView::DXCC && use_wae_parent)
          {
            auto const wae = wae_exact_calls_.find (exact_search);
            if (wae != wae_exact_calls_.end ())
              {
                auto const parent = wae_parents_.constFind (wae->entity_id_);
                if (parent != wae_parents_.cend () && parent->entity_id)
                  return {&*entities_.get<id> ().find (parent->entity_id), nullptr};
              }
          }
        auto const slash = exact_search.lastIndexOf ('/');
        if (slash < 0 || !Radio::is_operating_suffix (exact_search.mid (slash + 1))) break;
        exact_search.truncate (slash);
      }

    auto search_prefix = Radio::effective_prefix (exact_search);
    auto const other_half = Radio::base_callsign (exact_search);
    if (search_prefix != other_half && search_prefix.size () == other_half.size ()
        && !find_alias (search_prefix, EntityView::WAE, false)
        && find_alias (other_half, EntityView::WAE, false))
      search_prefix = other_half;
    return resolve_prefix (search_prefix, view);
  }

  match resolve_prefix (QString search_prefix, EntityView view) const
  {
    auto const effective_prefix = search_prefix;
    while (!search_prefix.isEmpty ())
      {
        if (auto const found = find_alias (search_prefix, view, false))
          return lookup_match (*found, effective_prefix);
        search_prefix.chop (1);
      }
    return {};
  }

  void derive_wae_parents ()
  {
    for (auto const& e : entities_)
      if (e.WAE_only_) wae_parents_.insert (e.id_, {});
    // Shared exact aliases identify the parent more reliably than prefix fall-through.
    for (auto const& p : wae_exact_calls_)
      {
        auto const exact = exact_calls_.find (p.prefix_);
        if (exact != exact_calls_.end ())
          {
            auto& parent = wae_parents_[p.entity_id_];
            parent.shared_exact = true;
            ++parent.votes[exact->entity_id_];
          }
      }
    for (auto const * index : {&wae_prefixes_, &wae_exact_calls_})
      for (auto const& p : *index)
        {
          auto& parent = wae_parents_[p.entity_id_];
          if (parent.shared_exact) continue;
          auto const found = resolve (p.prefix_, EntityView::DXCC, false);
          if (found.country) ++parent.votes[found.country->id_];
          else ++parent.unmatched;
        }
    for (auto& parent : wae_parents_)
      {
        if (parent.shared_exact && parent.votes.size () != 1) continue;
        int total = 0;
        for (auto count : parent.votes) total += count;
        for (auto vote = parent.votes.cbegin (); vote != parent.votes.cend (); ++vote)
          if (*vote > total / 2) parent.entity_id = vote.key ();
      }
  }

  void diagnose_wae_parents (parse_diagnostics& diagnostics, QMap<int, int> const& header_lines) const
  {
    QMap<int, QStringList> prefix_mismatches;
    QMap<int, int> mismatch_counts;
    for (auto const& p : wae_prefixes_)
      {
        auto const parent_id = wae_parents_.value (p.entity_id_).entity_id;
        if (!parent_id) continue;
        auto const found = resolve_prefix (p.prefix_, EntityView::DXCC).country;
        if (found && found->id_ == parent_id) continue;
        ++mismatch_counts[p.entity_id_];
        auto& examples = prefix_mismatches[p.entity_id_];
        if (examples.size () < 5)
          examples << p.prefix_ + " -> " + (found ? found->primary_prefix_ : "no DXCC match");
      }
    for (auto parent = wae_parents_.cbegin (); parent != wae_parents_.cend (); ++parent)
      {
        auto const& wae = *entities_.get<id> ().find (parent.key ());
        if (!parent->entity_id)
          {
            QStringList votes;
            for (auto vote = parent->votes.cbegin (); vote != parent->votes.cend (); ++vote)
              if (votes.size () < 5)
                votes << QString {"%1=%2"}.arg (entities_.get<id> ().find (vote.key ())->primary_prefix_).arg (*vote);
            if (parent->votes.size () > 5) votes << QString {"and %1 other entities"}.arg (parent->votes.size () - 5);
            diagnostics.add (header_lines.value (parent.key ()),
                             QString {"WAE entity %1: ambiguous DXCC parent (%2 votes: %3; unmatched=%4)"}
                             .arg (wae.name_).arg (parent->shared_exact ? "shared exact" : "alias")
                             .arg (votes.isEmpty () ? "none" : votes.join (", ")).arg (parent->unmatched));
          }
        else if (mismatch_counts.contains (parent.key ()))
          diagnostics.add (header_lines.value (parent.key ()),
                           QString {"WAE entity %1: %2 prefixes differ from DXCC parent %3 (%4)"}
                           .arg (wae.name_).arg (mismatch_counts.value (parent.key ()))
                           .arg (entities_.get<id> ().find (parent->entity_id)->name_)
                           .arg (prefix_mismatches.value (parent.key ()).join (", ")));
      }
  }

  static bool parse_prefix (prefix& result, int& ignored_fields)
  {
    auto const text = result.prefix_.trimmed ();
    int pos = 0;
    while (pos < text.size () && !QStringLiteral ("([{<~").contains (text[pos])) ++pos;
    result.prefix_ = text.left (pos).trimmed ().toUpper ();
    if (result.prefix_.isEmpty ()) return false;
    for (auto const c : result.prefix_)
      if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/')) return false;

    QString seen;
    while (pos < text.size ())
      {
        if (text[pos].isSpace ()) {++pos; continue;}
        auto const opening = text[pos];
        auto const kind = QStringLiteral ("([<{~").indexOf (opening);
        if (kind < 0 || seen.contains (opening)) return false;
        seen += opening;
        auto const close = text.indexOf (QStringLiteral (")]>}~")[kind], pos + 1);
        if (close < 0) return false;
        auto const value = text.mid (pos + 1, close - pos - 1).trimmed ();
        for (auto const c : value) if (QStringLiteral ("()[]<>{}~").contains (c)) return false;
        bool valid = false;
        int number;
        float latitude;
        float longitude;
        Continent continent;
        switch (kind)
          {
          case 0:
            valid = parse_bounded_int (value, min_cq_zone, max_cq_zone, number);
            if (valid) result.CQ_zone_ = number;
            break;
          case 1:
            valid = parse_bounded_int (value, min_itu_zone, max_itu_zone, number);
            if (valid) result.ITU_zone_ = number;
            break;
          case 2:
            {
              auto const pair = value.split ('/', Qt::KeepEmptyParts);
              valid = pair.size () == 2
                && parse_bounded_float (pair[0], min_latitude, max_latitude, latitude)
                && parse_bounded_float (pair[1], min_longitude, max_longitude, longitude);
              if (valid) result.coordinates_ = std::make_pair (latitude, longitude);
              break;
            }
          case 3:
            valid = parse_continent (value.toUpper (), continent);
            if (valid) result.continent_ = continent;
            break;
          case 4:
            valid = parse_utc_offset (value, number);
            if (valid) result.UTC_offset_ = number;
            break;
          }
        if (!valid) ++ignored_fields;
        pos = close + 1;
      }
    return true;
  }

  Configuration const * configuration_;
  QString path_;
  QString cty_version_;
  QString cty_version_date_;

  entities_type entities_;
  prefixes_type prefixes_;
  prefixes_type exact_calls_;
  prefixes_type wae_prefixes_;
  prefixes_type wae_exact_calls_;
  struct wae_parent
  {
    int entity_id = 0;
    bool shared_exact = false;
    QMap<int, int> votes;
    int unmatched = 0;
  };
  QMap<int, wae_parent> wae_parents_;
};

AD1CCty::Record::Record ()
  : continent {Continent::UN}
  , CQ_zone {0}
  , ITU_zone {0}
  , latitude {NAN}
  , longtitude {NAN}
  , UTC_offset {0}
{
}

auto AD1CCty::Record::identity (EntityView view) const -> EntityIdentity const&
{
  return view == EntityView::WAE || dxcc.name.isEmpty () ? wae : dxcc;
}

QString AD1CCty::Record::display_name (EntityView view) const
{
  auto const& selected = identity (view);
  return view == EntityView::DXCC && selected.primary_prefix == "TA"
    ? QString {"Turkey"} : selected.abbreviated_name;
}

#if !defined (QT_NO_DEBUG_STREAM)
  QDebug operator << (QDebug dbg, AD1CCty::Record const& r)
  {
    QDebugStateSaver saver {dbg};
    dbg.nospace () << "AD1CCty::Record("
                   << r.continent << ", "
                   << r.CQ_zone << ", "
                   << r.ITU_zone << ", "
                   << r.dxcc.name << ", "
                   << r.wae.name << ", "
                   << r.latitude << ", "
                   << r.longtitude << ", "
                   << (r.UTC_offset / (60. * 60.)) << ", "
                   << r.dxcc.primary_prefix << ", "
                   << r.wae.primary_prefix << ')';
    return dbg;
  }
#endif

auto AD1CCty::continent (QString const& continent_id) -> Continent
{
  Continent continent;
  if (!parse_continent (continent_id, continent))
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
  return writable_override_or_installed_file_path (configuration->writeable_data_dir (),
                                                  configuration->data_dir (), file_name);
}

void AD1CCty::impl::load_cty(QFile &file)
{
  QRegularExpression version_pattern{R"(VER\d{8})"};
  int entity_id = 0;
  int line_number = 0;
  parse_diagnostics diagnostics;
  QMap<int, int> wae_header_lines;
  QString pending_header;

  entities_.clear();
  prefixes_.clear();
  exact_calls_.clear();
  wae_prefixes_.clear ();
  wae_exact_calls_.clear ();
  wae_parents_.clear ();
  cty_version_ = QString{};
  cty_version_date_ = QString{};

  QTextStream in{&file};
  while (!pending_header.isEmpty () || !in.atEnd())
  {
    QString entity_line;
    if (!pending_header.isEmpty ())
      {
        entity_line = pending_header;
        pending_header.clear ();
      }
    else
      {
        entity_line = in.readLine ();
        ++line_number;
      }
    auto const header_line = line_number;
    auto const &entity_parts = entity_line.split(':');
    if (entity_parts.size() >= 8)
    {
      QString line;
      QList<QPair<int, QString>> alias_lines;
      bool complete = false;
      while (in.readLineInto (&line))
      {
        ++line_number;
        if (line.count (':') >= 7)
          {
            pending_header = line;
            break;
          }
        auto const terminator = line.indexOf (';');
        alias_lines.append ({line_number, terminator >= 0 ? line.left (terminator) : line});
        if (terminator >= 0)
          {
            complete = true;
            if (!line.mid (terminator + 1).trimmed ().isEmpty ())
              diagnostics.add (line_number, "text after entity terminator");
            break;
          }
      }
      if (!complete)
      {
        diagnostics.add (header_line, "incomplete entity");
        continue;
      }

      auto primary_prefix = entity_parts[7].trimmed();
      bool WAE_only{false};
      if (primary_prefix.startsWith('*'))
      {
        primary_prefix = primary_prefix.mid(1).trimmed ();
        WAE_only = true;
      }
      if (entity_parts[0].trimmed ().isEmpty () || primary_prefix.isEmpty ())
        {
          diagnostics.add (header_line, "missing entity name or primary prefix");
          continue;
        }
      int cq_zone;
      int itu_zone;
      float latitude;
      float longitude;
      int utc_offset;
      Continent entity_continent;
      auto check = [&] (bool valid, QString const& field) {
        if (!valid) diagnostics.add (header_line, "invalid entity " + field);
        return valid;
      };
      if (!check (parse_bounded_int (entity_parts[1], min_cq_zone, max_cq_zone, cq_zone), "CQ zone")
          || !check (parse_bounded_int (entity_parts[2], min_itu_zone, max_itu_zone, itu_zone), "ITU zone")
          || !check (parse_continent (entity_parts[3].trimmed ().toUpper (), entity_continent), "continent")
          || !check (parse_bounded_float (entity_parts[4], min_latitude, max_latitude, latitude), "latitude")
          || !check (parse_bounded_float (entity_parts[5], min_longitude, max_longitude, longitude), "longitude")
          || !check (parse_utc_offset (entity_parts[6], utc_offset), "UTC offset"))
      {
        continue;
      }

      auto const entity_result = entities_.emplace(++entity_id, entity_parts[0].trimmed(),
                                                   abbreviated_entity_name(entity_parts[0].trimmed()),
                                                   WAE_only, cq_zone, itu_zone, entity_continent, latitude, longitude,
                                                   utc_offset, primary_prefix);
      if (!entity_result.second)
      {
        --entity_id;
        diagnostics.add (header_line, "duplicate entity primary prefix");
        continue;
      }
      if (WAE_only) wae_header_lines.insert (entity_id, header_line);
      for (auto const& aliases : alias_lines)
      for (auto prefix: aliases.second.split (',', Qt::SkipEmptyParts))
      {
        prefix = prefix.trimmed();
        if (prefix.isEmpty ()) continue;
        bool exact{false};
        if (prefix.startsWith('='))
        {
          prefix = prefix.mid(1);
          exact = true;
        }
        auto candidate = ::prefix {prefix, entity_id};
        int ignored_fields = 0;
        if (!parse_prefix (candidate, ignored_fields))
        {
          diagnostics.add (aliases.first, "invalid prefix or override syntax");
          continue;
        }
        if (ignored_fields) diagnostics.add (aliases.first, "invalid override values; inherited entity defaults");
        // CTY aliases are ordered: the first entry in each namespace wins.
        auto& index = WAE_only ? (exact ? wae_exact_calls_ : wae_prefixes_)
                              : (exact ? exact_calls_ : prefixes_);
        auto const prefix_result = index.emplace(candidate);
        if (!prefix_result.second)
        {
          if (!same_definition (*prefix_result.first, candidate))
            diagnostics.add (aliases.first, QString {"conflicting %1 %2; first definition kept"}
                             .arg (exact ? "exact call" : "prefix").arg (candidate.prefix_));
          continue;
        }
        if (exact && version_pattern.match(candidate.prefix_).hasMatch()) cty_version_date_ = candidate.prefix_;
      }
    }
    else if (!entity_line.trimmed ().isEmpty () && !entity_line.front ().isSpace ())
    {
      diagnostics.add (header_line, "invalid entity header");
    }
  }

  derive_wae_parents ();
  diagnose_wae_parents (diagnostics, wae_header_lines);
  diagnostics.report (file.fileName ());
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

  m_->path_ = m_->impl::get_cty_path (configuration);

  QString path = writable_override_or_installed_file_path (configuration->writeable_data_dir (),
                                                          configuration->data_dir (), grid_file_name);


  QFile file1 {path};

  if (file1.open (QFile::ReadOnly))
    {
      parse_diagnostics diagnostics;
      int line_number = 0;
      int block_line = 0;
      bool block_open = false;
      bool block_valid = false;
      bool block_rejected = false;
      QString pending_prefix;
      std::array<QString, maxIndex> pending_states;
      std::bitset<maxIndex> populated_cells;
      auto finish_block = [&] (bool terminated) {
        int kept = 0;
        if (block_valid)
          {
            auto const duplicate = std::find (std::begin (gridPrefix), std::begin (gridPrefix) + gridNumPrefix, pending_prefix);
            if (duplicate != std::begin (gridPrefix) + gridNumPrefix)
              diagnostics.add (block_line, "duplicate grid prefix");
            else if (gridNumPrefix < maxPrefix)
              {
                gridPrefix[gridNumPrefix] = pending_prefix;
                for (int i = 0; i < maxIndex; ++i) gridState[gridNumPrefix][i] = pending_states[i];
                ++gridNumPrefix;
                kept = static_cast<int> (populated_cells.count ());
              }
            else diagnostics.add (block_line, "excess grid prefix");
          }
        if (!terminated && !block_rejected)
          diagnostics.add (block_line, QString {"grid block missing '>'; kept %1 cells"}.arg (kept));
        block_open = false;
      };
      QTextStream in {&file1};
      while (!in.atEnd ())
        {
          auto const line = in.readLine ().trimmed ();
          ++line_number;
          if (line.isEmpty ()) continue;

          auto const separator = line.indexOf (':');
          auto const header = line.indexOf ('<');
          if (header >= 0 && separator < 0)
            {
              if (block_open) finish_block (false);
              block_line = line_number;
              pending_prefix = line.left (header).trimmed ();
              block_valid = valid_grid_prefix (pending_prefix);
              block_rejected = !block_valid;
              if (block_rejected) diagnostics.add (block_line, "invalid grid prefix");
              block_open = true;
              pending_states.fill (QString {"**"});
              populated_cells.reset ();
              if (!line.mid (header + 1).contains ('>')) continue;
            }

          if (!block_open)
            {
              diagnostics.add (line_number, "grid cell outside a block");
              continue;
            }

          auto const end = line.indexOf ('>');
          auto const comma = line.indexOf (',');
          auto const delimiter = end >= 0 && comma >= 0 ? std::min (end, comma) : std::max (end, comma);
          auto const terminator = delimiter >= 0 ? delimiter : line.size ();
          if (separator >= 0 && terminator > separator)
            {
              int grid_index;
              auto const index_text = line.left (separator).trimmed ();
              auto const state = line.mid (separator + 1, terminator - separator - 1).trimmed ();
              if (block_valid && parse_grid_index (index_text, grid_index) && !state.isEmpty ())
                {
                  pending_states[grid_index] = state;
                  populated_cells.set (grid_index);
                }
              else
                {
                  diagnostics.add (line_number, "invalid grid cell");
                }
            }
          else if (header < 0 && end < 0)
            {
              diagnostics.add (line_number, "invalid grid cell");
            }

          if (end >= 0) finish_block (true);
        }
      if (block_open) finish_block (false);
      diagnostics.report (file1.fileName ());
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
    m_->cty_version_ = AD1CCty::lookup("VERSION").identity (EntityView::DXCC).name;
    Q_EMIT cty_loaded(m_->cty_version_);
    LOG_INFO(QString{"Loaded CTY.DAT version %1, %2"}.arg (m_->cty_version_date_).arg (m_->cty_version_));
  }
}

AD1CCty::~AD1CCty ()
{
}

auto AD1CCty::lookup (QString const& call) const -> Record
{
  auto const wae = m_->resolve (call, EntityView::WAE);
  auto const dxcc = m_->resolve (call, EntityView::DXCC);
  Record result;
  result.dxcc = impl::identity_for (dxcc);
  result.wae = impl::identity_for (wae);
  impl::set_geography (result, wae);
  return result;
}

auto AD1CCty::version () const -> QString
{
  return m_->cty_version_date_;
}

// NJ0A
auto AD1CCty::findState ( QString const& grid) const -> QString
{


    auto const& prefix = grid.left(2);
    int gridIndex;
    if (grid.size () < 4 || !parse_grid_index (grid.mid (2, 2), gridIndex)) return "**";
    for (int i = 0; i < gridNumPrefix; i ++) {
        if (gridPrefix[i] == prefix) {
            QString state = gridState[i] [gridIndex];
            return state;
        }
    }

    return "**";
} // NJ0A
