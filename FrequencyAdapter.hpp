#ifndef FREQUENCY_ADAPTER_HPP
#define FREQUENCY_ADAPTER_HPP

#include <array>
#include <cmath>
#include <optional>
#include "FrequencyControl.hpp"

class FrequencyAdapter
{
public:
  struct Projection
  {
    Radio::FrequencyDelta offset = 0;
    double intercept = 0.;
    double slope_ppm = 0.;
    bool calibrated = true;

    Radio::Frequency encode (Radio::Frequency value) const
    {
      if (!value) return 0;
      auto const intermediate = static_cast<double> (value) + offset;
      return std::llround (calibrated ? intercept + (1. + slope_ppm / 1.e6) * intermediate : intermediate);
    }
    Radio::Frequency decode (Radio::Frequency value) const
    {
      if (!value) return 0;
      auto const intermediate = calibrated ? (value - intercept) / (1. + slope_ppm / 1.e6) : value;
      return std::llround (intermediate - offset);
    }
  };

  struct Mapping
  {
    RigFrequency::Request request;
    RigFrequency::Request command;
    Projection projection;
    RigFrequency::Basis basis = RigFrequency::Basis::Retained;
  };

  struct Snapshot
  {
    std::optional<Mapping> established;
    std::optional<Mapping> pending;
    std::optional<Mapping> queued;
  };

  quint64 generation () const { return generation_; }
  static Radio::Frequency resolution_hz (int code)
  {
    auto const magnitude = std::abs (code);
    return magnitude >= 3 ? 100 : magnitude == 2 ? 20 : magnitude == 1 ? 10 : 1;
  }
  void new_connection ()
  {
    ++generation_;
    for (auto& path : paths_)
      {
        auto const baseline = path.state.established
          ? std::optional<Projection> {path.state.established->projection} : path.baseline;
        path = {};
        path.baseline = baseline;
      }
    failed_ = false;
  }
  void resolution (Radio::Frequency hz) { resolution_ = hz ? hz : 1; }
  void default_projection (Projection const& projection) { default_projection_ = projection; }

  bool submit (RigFrequency::Request const& request, Projection const& projection)
  {
    if (failed_ || !request || request.generation != generation_) return false;
    auto& path = slot (request.path);
    if (path.state.pending && RigFrequency::same_operation (path.state.pending->request, request))
      return path.state.pending->request.target == request.target
        && path.state.pending->command.target == projection.encode (request.target);
    if (path.state.queued && RigFrequency::same_operation (path.state.queued->request, request))
      return path.state.queued->request.target == request.target
        && path.state.queued->command.target == projection.encode (request.target);
    if (path.state.established && RigFrequency::same_operation (path.state.established->request, request)) return false;
    Mapping mapping {request, request, projection};
    mapping.command.target = projection.encode (request.target);
    if (path.state.pending) path.state.queued = mapping;
    else { path.state.pending = mapping; path.sent = false; }
    return true;
  }

  std::optional<RigFrequency::Request> take_dispatch (RigFrequency::Path which)
  {
    auto& path = slot (which);
    if (failed_ || !path.state.pending || path.sent || path.deferred) return {};
    path.sent = true;
    return path.state.pending->command;
  }

  std::optional<RigFrequency::Result> complete (RigFrequency::Result const& result)
  {
    if (failed_) return {};
    auto& path = slot (result.request.path);
    if (result.request.generation != generation_) return {};
    if (result.outcome == RigFrequency::Outcome::Failed && path.state.established
        && RigFrequency::same_operation (result.request, path.state.established->command))
      {
        auto translated = result;
        translated.request = path.state.established->request;
        translated.applied = path.state.established->projection.decode (result.applied);
        failed_ = true;
        return translated;
      }
    if (!path.state.pending || path.deferred
        || !RigFrequency::same_operation (result.request, path.state.pending->command)) return {};
    auto translated = result;
    translated.request = path.state.pending->request;
    translated.applied = path.state.pending->projection.decode (result.applied);
    if (result.outcome == RigFrequency::Outcome::Pending) return translated;
    if (result.outcome == RigFrequency::Outcome::Deferred)
      {
        path.sent = false;
        path.deferred = true;
        path.ambiguous = false;
        return translated;
      }
    auto const target = path.state.pending->command.target;
    auto const delta = result.applied > target ? result.applied - target : target - result.applied;
    if (result.outcome == RigFrequency::Outcome::Established && delta < resolution_)
      translated.applied = translated.request.target;
    if (result.outcome == RigFrequency::Outcome::Established
        && (path.ambiguous || delta >= resolution_ || result.basis == RigFrequency::Basis::Retained))
      {
        translated.outcome = RigFrequency::Outcome::Failed;
      }
    else if (result.outcome == RigFrequency::Outcome::Established)
      {
        path.state.pending->basis = result.basis;
        path.state.established = path.state.pending;
      }
    path.state.pending = path.state.queued;
    path.state.queued.reset ();
    path.sent = false;
    path.ambiguous = false;
    if (translated.outcome == RigFrequency::Outcome::Failed
        || translated.outcome == RigFrequency::Outcome::Rejected) failed_ = true;
    return translated;
  }

  void resume_deferred ()
  {
    if (failed_) return;
    for (auto& path : paths_)
      if (path.deferred)
        {
          if (path.state.queued) path.state.pending = path.state.queued;
          path.state.queued.reset ();
          path.deferred = false;
        }
  }

  std::optional<RigFrequency::Evidence> observe (RigFrequency::Evidence const& evidence)
  {
    if (evidence.generation != generation_ || evidence.basis == RigFrequency::Basis::Retained) return {};
    auto& path = slot (evidence.path);
    if (evidence.serial && evidence.serial <= path.serial) return {};
    path.serial = evidence.serial;
    auto translated = evidence;
    Mapping const * mapping = nullptr;
    if (evidence.operation)
      {
        if (path.state.pending && RigFrequency::same_operation (evidence.operation, path.state.pending->command))
          mapping = &*path.state.pending;
        else if (path.state.established && RigFrequency::same_operation (evidence.operation, path.state.established->command))
          mapping = &*path.state.established;
        else return {};
        translated.operation = mapping->request;
        auto const delta = evidence.value > mapping->command.target
          ? evidence.value - mapping->command.target : mapping->command.target - evidence.value;
        if (mapping == (path.state.pending ? &*path.state.pending : nullptr)
            && evidence.basis == RigFrequency::Basis::Readback && delta < resolution_)
          path.ambiguous = false;
      }
    else
      {
        // A report during an operation cannot distinguish its echo from dial tuning.
        if (path.state.pending)
          {
            if (evidence.value != path.state.pending->command.target
                && (!path.state.established || evidence.value != path.state.established->command.target))
              path.ambiguous = true;
            return {};
          }
        if (path.state.established) mapping = &*path.state.established;
      }
    if (mapping)
      {
        auto const delta = evidence.value > mapping->command.target
          ? evidence.value - mapping->command.target : mapping->command.target - evidence.value;
        translated.value = delta < resolution_ ? mapping->request.target : mapping->projection.decode (evidence.value);
      }
    else translated.value = path.baseline.value_or (default_projection_).decode (evidence.value);
    return translated;
  }

  Radio::Frequency decode (RigFrequency::Path which, Radio::Frequency value) const
  {
    auto const& path = slot (which);
    if (!path.state.established) return path.baseline.value_or (default_projection_).decode (value);
    auto const& mapping = *path.state.established;
    auto const delta = value > mapping.command.target ? value - mapping.command.target : mapping.command.target - value;
    return delta < resolution_ ? mapping.request.target : mapping.projection.decode (value);
  }
  Snapshot snapshot (RigFrequency::Path which) const { return slot (which).state; }

private:
  struct PathState
  {
    Snapshot state;
    std::optional<Projection> baseline;
    quint64 serial = 0;
    bool sent = false;
    bool ambiguous = false;
    bool deferred = false;
  };
  PathState& slot (RigFrequency::Path path) { return paths_[path == RigFrequency::Path::Rx ? 0 : 1]; }
  PathState const& slot (RigFrequency::Path path) const { return paths_[path == RigFrequency::Path::Rx ? 0 : 1]; }
  quint64 generation_ = 0;
  Radio::Frequency resolution_ = 1;
  bool failed_ = false;
  Projection default_projection_;
  std::array<PathState, 2> paths_ {};
};

#endif
