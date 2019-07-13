#ifndef AD1C_CTY_HPP_
#define AD1C_CTY_HPP_

#include <QObject>
#include <QDebug>
#include "pimpl_h.hpp"

class QString;
class Configuration;

//
// AD1CCty  - Fast  access database  of Jim  Reisert, AD1C's,  cty.dat
// 						entity and entity override information file.
// 
class AD1CCty final
  : public QObject
{
  Q_OBJECT

public:
  //
  // Continent enumeration
  // 
  enum class Continent {UN, AF, AN, AS, EU, NA, OC, SA};
  static Continent continent (QString const& continent_id);
  static char const * continent (Continent);
  Q_ENUM (Continent)

  struct Record
  {
    explicit Record ();

    Continent continent;
    int CQ_zone;
    int ITU_zone;
    QString entity_name;
    bool WAE_only;
    float latitude;
    float longtitude;
    int UTC_offset;
    QString primary_prefix;
  };

  explicit AD1CCty (Configuration const *);
  ~AD1CCty ();
  Record lookup (QString const& call) const;

private:
  class impl;
  pimpl<impl> m_;
};

#if !defined (QT_NO_DEBUG_STREAM)
QDebug operator << (QDebug, AD1CCty::Record const&);
#endif

#endif
