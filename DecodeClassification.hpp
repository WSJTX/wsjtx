// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DECODE_CLASSIFICATION_HPP
#define DECODE_CLASSIFICATION_HPP

#include <optional>

#include "Decoder/decodedtext.h"
#include "Network/LotWUsers.hpp"
#include "Radio.hpp"
#include "logbook/logbook.h"
#include "models/DecodeHighlightingModel.hpp"

struct DecodeClassification
{
  struct WorkedBefore
  {
    bool call;
    bool entity;
    bool grid;
    bool continent;
    bool cqZone;
    bool ituZone;
  };

  QString sender;
  QString grid;
  std::optional<AD1CCty::Record> entity;
  std::optional<WorkedBefore> worked;
  std::optional<WorkedBefore> workedOnBand;
  DecodeHighlightingModel::HighlightTypes highlights;
};

inline DecodeClassification classifyDecode (DecodedText const& decoded, QString const& mode,
                                             QString const& band, LogBook const& logbook,
                                             LotWUsers const * lotw = nullptr)
{
  DecodeClassification result;
  decoded.deCallAndGrid (result.sender, result.grid);
  if (!Radio::is_callsign (result.sender))
    {
      result.sender.clear ();
      result.grid.clear ();
      return result;
    }
  if (!result.grid.contains (Radio::decoded_grid_pattern ())) result.grid.clear ();
  result.entity = logbook.countries ()->lookup (result.sender);
  auto match = [&] (QString const& onBand) {
    DecodeClassification::WorkedBefore facts;
    logbook.match (result.sender, mode, result.grid, *result.entity,
                    facts.call, facts.entity, facts.grid, facts.continent,
                    facts.cqZone, facts.ituZone, onBand);
    if (result.grid.isEmpty ()) facts.grid = true;
    return facts;
  };
  result.worked = match ({});
  if (!band.isEmpty ()) result.workedOnBand = match (band);
  using Highlight = DecodeHighlightingModel::Highlight;
  auto add = [&] (bool worked, Highlight type) {
    if (!worked) result.highlights.push_back (type);
  };
  add (result.worked->entity, Highlight::DXCC);
  add (result.worked->grid, Highlight::Grid);
  add (result.worked->call, Highlight::Call);
  add (result.worked->continent, Highlight::Continent);
  add (result.worked->cqZone, Highlight::CQZone);
  add (result.worked->ituZone, Highlight::ITUZone);
  if (result.workedOnBand)
    {
      add (result.workedOnBand->entity, Highlight::DXCCBand);
      add (result.workedOnBand->grid, Highlight::GridBand);
      add (result.workedOnBand->call, Highlight::CallBand);
      add (result.workedOnBand->continent, Highlight::ContinentBand);
      add (result.workedOnBand->cqZone, Highlight::CQZoneBand);
      add (result.workedOnBand->ituZone, Highlight::ITUZoneBand);
    }
  if (lotw && lotw->user (result.sender)) result.highlights.push_back (Highlight::LotW);
  result.highlights.push_back (Highlight::CQ);
  return result;
}

#endif
