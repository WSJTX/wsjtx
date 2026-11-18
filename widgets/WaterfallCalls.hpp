// -*- Mode: C++ -*-
// Callsign labels for the Wide Graph: which call a decoded JTTY message
// puts on the waterfall, how long it stays, and where the labels go.
// Header only, Qt core only, so the rules can be unit tested.

#ifndef WATERFALL_CALLS_HPP_
#define WATERFALL_CALLS_HPP_

#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <algorithm>
#include <functional>

namespace WaterfallCalls
{
  // Highest priority first: a message that names our call, our own
  // transmission (seen through Duplex or the Moon), a CQ, the rest.
  enum class Kind {Me, Own, Cq, Other};

  struct Label
  {
    QString call;
    float freq {};              // Hz, lowest tone of the signal
    float width {};             // Hz, from the lowest tone to the top of the signal
    Kind kind {Kind::Other};
    qint64 id {};               // message id (JTTY), 0 when none
    qint64 heardMs {};          // last update
    qint64 firstMs {};          // first time this message (call) was labelled
    qint64 untilMs {};          // gone after this time
  };

  // JTTY: 4-GFSK, 31.25 baud.
  constexpr float jttySignalWidth = 127.0f;
  // A label is dropped this long after the last update of its message.
  constexpr qint64 holdMs = 60000;

  // A callsign: 3 to 11 characters of A-Z 0-9 /, with at least one digit
  // and one letter, and not a 4-character grid square such as JN68.
  inline bool isCallsign (QString const& word)
  {
    static QRegularExpression const re {
      QStringLiteral ("^(?=.*\\d)(?=.*[A-Z])[A-Z0-9/]{3,11}$")};
    static QRegularExpression const grid {QStringLiteral ("^[A-R]{2}\\d{2}$")};
    return re.match (word).hasMatch () && !grid.match (word).hasMatch ();
  }

  inline QString stripBrackets (QString word)
  {
    if (word.startsWith ('<')) word.remove (0, 1);
    if (word.endsWith ('>')) word.chop (1);
    return word;
  }

  struct JttyCall
  {
    QString call;
    Kind kind {Kind::Other};
    bool valid () const {return !call.isEmpty ();}
  };

  // Only two forms are labelled, not every message that names a call:
  //   "CQ [DX] CALL ..."  the first call after CQ
  //   "CALL: text"        the call in front of the colon
  // A call counts only once a word follows it or the message has stopped
  // growing, so a call cut off by the end of the text so far is never shown.
  inline JttyCall jttyCall (QString const& message, bool growing, QString const& myCall)
  {
    auto const text = message.toUpper ().simplified ();
    auto const words = text.isEmpty () ? QStringList {} : text.split (' ');
    JttyCall result;
    if (words.isEmpty ()) return result;
    auto const mine = stripBrackets (myCall.toUpper ());
    bool const cq = words.first () == QLatin1String ("CQ");
    if (cq) {
      for (int i = 1; i < words.size (); ++i) {
        auto const w = stripBrackets (words[i]);
        if (!isCallsign (w)) continue;
        if (i < words.size () - 1 || !growing) result.call = w;
        break;
      }
    } else {
      static QRegularExpression const re {
        QStringLiteral ("^<?([A-Z0-9/]+)>?:")};
      auto const m = re.match (words.first ());
      // "CALL:" alone is the whole text so far: wait for a word after it
      if (m.hasMatch () && isCallsign (m.captured (1))
          && (words.size () > 1 || !growing)) {
        result.call = m.captured (1);
        // addressed to us: our call somewhere after the colon
        for (int i = 1; i < words.size () && !mine.isEmpty (); ++i) {
          if (stripBrackets (words[i]) == mine) {
            result.kind = Kind::Me;
            break;
          }
        }
      }
    }
    if (!result.valid ()) return result;
    if (!mine.isEmpty () && result.call == mine) result.kind = Kind::Own;
    else if (cq) result.kind = Kind::Cq;
    return result;
  }

  // The labels now on show. Kept per message id: a message that grows
  // replaces its own label, and a label is gone holdMs after its last update.
  class Board
  {
  public:
    // Returns true if what is shown may have changed.
    bool update (qint64 id, QString const& message, float freq, bool growing,
                 QString const& myCall, qint64 nowMs, float width = jttySignalWidth)
    {
      auto const found = jttyCall (message, growing, myCall);
      if (!found.valid ()) return false;       // an older label of this id stays
      Label label;
      label.call = found.call;
      label.freq = freq;
      label.width = width;
      label.kind = found.kind;
      label.id = id;
      label.heardMs = nowMs;
      auto const old = entries_.constFind (id);
      label.firstMs = old != entries_.constEnd () ? old->firstMs : nowMs;
      label.untilMs = nowMs + holdMs;
      entries_[id] = label;
      return true;
    }

    // Drops what has expired; true if anything went.
    bool expire (qint64 nowMs)
    {
      bool any = false;
      for (auto it = entries_.begin (); it != entries_.end ();) {
        if (it->untilMs < nowMs) {it = entries_.erase (it); any = true;}
        else ++it;
      }
      return any;
    }

    // Priority order: our call, our own transmission, CQs, the rest. One label
    // per call (the newest decode). Within a kind the order is that of first
    // appearance, so two labels that compete for the same place do not swap
    // every time one of their messages is updated.
    QVector<Label> labels (qint64 nowMs)
    {
      expire (nowMs);
      QVector<Label> all;
      for (auto const& e : entries_) all.append (e);
      std::sort (all.begin (), all.end (), [] (Label const& a, Label const& b) {
          if (a.heardMs != b.heardMs) return a.heardMs > b.heardMs;
          return a.id > b.id;
        });                                         // newest first, for one per call
      QVector<Label> out;
      QHash<QString, qint64> first;                 // a call's earliest appearance
      for (auto const& l : all) {
        auto it = first.find (l.call);
        if (it == first.end ()) {first.insert (l.call, l.firstMs); out.append (l);}
        else if (l.firstMs < *it) *it = l.firstMs;
      }
      for (auto& l : out) l.firstMs = first.value (l.call);
      std::stable_sort (out.begin (), out.end (), [] (Label const& a, Label const& b) {
          if (a.kind != b.kind) return int (a.kind) < int (b.kind);
          if (a.firstMs != b.firstMs) return a.firstMs < b.firstMs;
          return a.freq < b.freq;
        });
      return out;
    }

    void clear () {entries_.clear ();}
    bool isEmpty () const {return entries_.isEmpty ();}

  private:
    QHash<qint64, Label> entries_;
  };

  struct Placed
  {
    int index {};               // into the label list
    int row {};
    double xCentre {};
  };

  // Where the labels go. Labels in priority order (the first wins a
  // place); x(freq) -> pixel; textWidth(call) -> pixels. A label that would
  // overlap one already placed in every row, or whose signal lies off the
  // strip, is left out; one that sticks out at an edge is pushed back in.
  inline QVector<Placed> place (QVector<Label> const& labels,
                                std::function<double (double)> const& x,
                                std::function<double (QString const&)> const& textWidth,
                                double width, int rows = 2, double gap = 4.0)
  {
    QVector<QVector<QPair<double, double>>> taken (rows);
    QVector<Placed> out;
    for (int i = 0; i < labels.size (); ++i) {
      auto const& l = labels[i];
      double xc = x (l.freq + l.width / 2.0);
      double const half = textWidth (l.call) / 2.0;
      double a = xc - half, b = xc + half;
      if (xc < 0 || xc > width) continue;            // signal off the strip
      double const shift = std::max (0.0, -a) - std::max (0.0, b - width);
      a += shift; b += shift; xc += shift;
      for (int r = 0; r < rows; ++r) {
        bool free = true;
        for (auto const& t : taken[r]) {
          if (!(b + gap <= t.first || a - gap >= t.second)) {free = false; break;}
        }
        if (free) {
          taken[r].append ({a, b});
          out.append ({i, r, xc});
          break;
        }
      }
    }
    return out;
  }
}

#endif
