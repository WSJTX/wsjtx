// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BEST_SP_CONTROLLER_HPP
#define BEST_SP_CONTROLLER_HPP

#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include <QDateTime>

#include "DecodeOperatingContext.hpp"
#include "Decoder/decodedtext.h"

class BestSPController final
{
public:
  struct Policy
  {
    bool newDxcc {true};
    bool newCallOnBand {true};
  };

  enum class Reason {NewDXCC, CallBand};

  struct Request
  {
    quint64 generation {0};
    quint64 backendRequest {0};
    DecodeOperatingContext context;
  };

  struct Selection
  {
    DecodedText message;
    Reason reason;
  };

  using Eligibility = std::function<std::optional<Reason> (
    DecodedText const&, Policy const&)>;

  bool armed () const {return armed_;}
  std::optional<Request> const& activeRequest () const {return request_;}

  void arm (QDateTime const& now)
  {
    ++revision_;
    armed_ = true;
    armedAt_ = now;
    requestRevision_ = revision_;
  }

  void cancel ()
  {
    ++revision_;
    armed_ = false;
    armedAt_ = {};
  }

  bool timeout (QDateTime const& now)
  {
    if (!armed_ || !armedAt_.isValid () || armedAt_.msecsTo (now) < 120000)
      return false;
    cancel ();
    return true;
  }

  void beginRequest (Request const& request)
  {
    if (request_ && sameRequest (*request_, request)) return;
    clearRequest ();
    if (request.context.mode != "FT4") return;
    request_ = request;
    requestRevision_ = revision_;
  }

  void observe (Request const& request, DecodedText const& message)
  {
    if (!request_ || !sameRequest (*request_, request)
        || !message.isStandardMessage ()) return;
    auto const fields = parseDecodedMessage (message.message ());
    if (fields.cq && !fields.sender.isEmpty ()) observations_.push_back (message);
  }

  void failRequest () {clearRequest ();}

  std::optional<Selection> complete (
    Request const& request, QDateTime const& now,
    Policy const& policy, Eligibility const& eligibility)
  {
    timeout (now);
    if (!request_ || !sameRequest (*request_, request)) return {};
    auto observations = std::move (observations_);
    auto const requestRevision = requestRevision_;
    clearRequest ();
    if (!armed_ || revision_ != requestRevision) return {};

    std::optional<Selection> selected;
    for (auto const& message : observations)
      {
        auto const reason = eligibility (message, policy);
        if (!armed_ || revision_ != requestRevision) return {};
        if (!reason || (*reason == Reason::NewDXCC && !policy.newDxcc)
            || (*reason == Reason::CallBand && !policy.newCallOnBand)) continue;
        if (!selected || (*reason == Reason::NewDXCC
                          && selected->reason == Reason::CallBand))
          selected = Selection {message, *reason};
      }
    if (selected)
      {
        armed_ = false;
        armedAt_ = {};
      }
    return selected;
  }

private:
  static bool sameContext (DecodeOperatingContext const& first,
                           DecodeOperatingContext const& second)
  {
    return first.hasSameDecodeIdentity (second)
      && first.sequenceStart == second.sequenceStart;
  }

  static bool sameRequest (Request const& first, Request const& second)
  {
    return first.generation == second.generation
      && first.backendRequest == second.backendRequest
      && sameContext (first.context, second.context);
  }

  void clearRequest ()
  {
    request_.reset ();
    observations_.clear ();
  }

  bool armed_ {false};
  quint64 revision_ {0};
  quint64 requestRevision_ {0};
  QDateTime armedAt_;
  std::optional<Request> request_;
  std::vector<DecodedText> observations_;
};

#endif
