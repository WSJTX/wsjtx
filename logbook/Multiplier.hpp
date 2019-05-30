#ifndef MULTIPLIER_HPP_
#define MULTIPLIER_HPP_

#include <boost/core/noncopyable.hpp>
#include <QSet>
#include "pimpl_h.hpp"

class QString;
class AD1CCty;
class CabrilloLog;

class Multiplier final
  : private boost::noncopyable
{
public:
  using worked_item = QPair<QString, QString>;
  using worked_set = QSet<worked_item>;

  explicit Multiplier (AD1CCty const *);
  ~Multiplier ();
  void reload (CabrilloLog const *);
  worked_set const& entities_worked () const;
  worked_set const& grids_worked () const;

private:
  class impl;
  pimpl<impl> m_;
};

#endif
