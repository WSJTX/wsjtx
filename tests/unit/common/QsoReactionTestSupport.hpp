#ifndef QSOREACTIONTESTSUPPORT_HPP
#define QSOREACTIONTESTSUPPORT_HPP

#include "DecodedMessageReaction.hpp"
#include "Decoder/decodedtext.h"

namespace QsoReactionTestSupport
{
  using Effect = DecodedMessageReaction::QsoReactionEffect;
  using Plan = DecodedMessageReaction::QsoReactionPlan;
  using Snapshot = DecodedMessageReaction::QsoReactionSnapshot;

  inline DecodedText decode(QString const& payload, QString const& marker = "~",
                            QString const& time = "0605", int frequency = 1500)
  {
    return DecodedText {
      QString {"%1 -10  0.3 %2 %3  %4"}
        .arg(time).arg(frequency, 4, 10, QChar {'0'}).arg(marker).arg(payload)};
  }

  inline Snapshot neutralStationSnapshot()
  {
    Snapshot snapshot;
    snapshot.myCall = "K1ABC";
    snapshot.baseCall = "K1ABC";
    snapshot.dxCall = "W1AW";
    snapshot.hisCall = "W1AW";
    snapshot.hisGrid = "FN31";
    snapshot.trPeriod = 15.0;
    snapshot.nominalFrequency = 14074000;
    snapshot.rxFrequency = 1500;
    snapshot.txFrequency = 1500;
    return snapshot;
  }

  inline int effectIndex(Plan const& plan, Effect::Kind kind, int from = 0)
  {
    for (int i = from; i < plan.effects.size(); ++i) {
      if (plan.effects[i].kind == kind) return i;
    }
    return -1;
  }

  inline int effectCount(Plan const& plan, Effect::Kind kind)
  {
    int count = 0;
    for (auto const& effect : plan.effects) {
      if (effect.kind == kind) ++count;
    }
    return count;
  }

  inline bool hasEffect(Plan const& plan, Effect::Kind kind)
  {
    return effectIndex(plan, kind) >= 0;
  }

  inline int intEffect(Plan const& plan, Effect::Kind kind, int defaultValue = -1)
  {
    int const index = effectIndex(plan, kind);
    return index >= 0 ? plan.effects[index].intValue : defaultValue;
  }

  inline QString textEffect(Plan const& plan, Effect::Kind kind)
  {
    int const index = effectIndex(plan, kind);
    return index >= 0 ? plan.effects[index].text : QString {};
  }
}

#endif // QSOREACTIONTESTSUPPORT_HPP
