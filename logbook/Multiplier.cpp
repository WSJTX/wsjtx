#include "Multiplier.hpp"

#include <QSet>
#include <QString>
#include <QDebug>
#include "models/CabrilloLog.hpp"
#include "pimpl_impl.hpp"

class Multiplier::impl
{
public:
  impl (AD1CCty const * countries)
    : countries_ {countries}
  {
  }

  AD1CCty const * countries_;
  worked_set entities_worked_;
  worked_set grids_worked_;
};

Multiplier::Multiplier (AD1CCty const * countries)
  : m_ {countries}
{
}

Multiplier::~Multiplier ()
{
}

void Multiplier::reload (CabrilloLog const * log)
{
  m_->entities_worked_ = log->unique_DXCC_entities (m_->countries_);
}

auto Multiplier::entities_worked () const -> worked_set const&
{
  return m_->entities_worked_;
}

auto Multiplier::grids_worked () const -> worked_set const&
{
  return m_->grids_worked_;
}
