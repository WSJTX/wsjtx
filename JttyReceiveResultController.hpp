#ifndef JTTY_RECEIVE_RESULT_CONTROLLER_HPP
#define JTTY_RECEIVE_RESULT_CONTROLLER_HPP

#include "DecodeOperatingContext.hpp"
#include "JttyDecoder.hpp"
#include "JttyReceiveLine.hpp"
#include <QHash>
#include <deque>
#include <functional>

namespace Jtty {

class ReceiveResultController
{
public:
  enum class Source { Live, Wav, Review };
  enum class Pane { All, Qso };

  struct Reception {
    Source source {Source::Live};
    quint64 contextId {};
    quint64 displayGroup {};
    QDateTime anchor, displayAnchor;
    DecodeOperatingContext context;
    float center {1500}, tolerance {50};
  };

  struct Selection {
    bool jttyActive {};
    quint64 contextId {};
    float center {1500}, tolerance {50};
  };

  struct EchoOptions { bool enabled {}, lowerCase {}; };

  struct Inputs {
    std::function<JttyReceiveLine::Options()> display;
    std::function<Selection()> selection;
    std::function<EchoOptions()> echo;
    std::function<bool()> reporting;
  };

  struct Spot {
    QString sender, grid;
    Radio::Frequency frequency {};
    QString mode;
    int snr {};
    QDateTime time;
  };

  // Effects and handle references are used only during synchronous invocation.
  struct Effects {
    std::function<void(Pane)> reviewHeading;
    std::function<void(Pane, JttyReceiveLine&, JttyReceiveLine::Presentation const&,
                       JttyReceiveLine::Options const&)> render;
    std::function<void(QString const&)> echo;
    std::function<void(qint64, qint64)> noteDecoded;
    std::function<void(QString const&, DecodeOperatingContext const&)> log;
    std::function<void(Spot const&)> spot;
    std::function<void(qint64, QDateTime const&, quint32, qint32, QString const&, bool, bool)> jttyText;
  };

  void apply(QVector<ReceiveUpdate> const&, Reception const&, Inputs const&, Effects const&);
  void resetReplay();
  int snrForSelectedWord(QString const&, Pane) const;

private:
  struct Line {
    QString text;
    JttyReceiveLine allLine, qsoLine;
    DecodeOperatingContext context;
    bool admitted {};
  };
  struct SnrEntry { QString text; int snr; bool admitted; };
  QHash<qint64, Line> liveLines_, replayLines_;
  std::deque<SnrEntry> snrHistory_;
  bool reviewHeading_ {};
};

}

#endif
