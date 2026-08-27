#ifndef TX_PLAYBACK_DIAGNOSTICS_HPP_
#define TX_PLAYBACK_DIAGNOSTICS_HPP_

#include <QVector>

#include "Audio/TxPlaybackEvidence.hpp"

namespace TxEvidence
{
  class TxPlaybackDiagnostics
  {
  public:
    TxPlaybackDiagnostics ();
    ~TxPlaybackDiagnostics ();

    static TxSessionId allocateSessionId ();
    static TxGeneration allocateGeneration ();
    static TxStopDecision decisionFor (TxStopReason reason, bool zeroTailMode);

    TxSessionId beginSession (QString const& mode, int sampleRateHz,
                              qint64 committedEndSample = -1,
                              bool targetKnown = false,
                              QString const& diagnostic = QString ());
    TxGeneration beginGeneration (TxSessionId sessionId);
    bool commitStart (TxStartSnapshot const& snapshot);
    bool commitTarget (TxSessionId sessionId, TxGeneration generation,
                       qint64 committedEndSample, bool targetKnown = true,
                       QString const& diagnostic = QString ());
    bool observeSourceProgress (TxSessionId sessionId, TxGeneration generation,
                                qint64 servedSamples, qint64 totalSamples,
                                QString const& diagnostic = QString ());
    bool observe (TxSessionId sessionId, TxGeneration generation,
                  TxRawPlayoutSnapshot const& raw);
    TxStopDecision stop (TxSessionId sessionId, TxGeneration generation,
                         TxStopReason reason, int legacyTailMs = 0);

    QVector<TxTerminalSession> terminalSessions () const;
    QString diagnosticDump () const;

  private:
    struct ActiveSession
    {
      TxStartSnapshot start;
      TxRawPlayoutSnapshot raw;
      TxPlayoutSnapshot playout;
      qint64 source_served_frames {-1};
      qint64 source_total_frames {-1};
      QString source_progress_diagnostic;
      bool claimed {true};
      bool bound {false};
      bool underrun {false};
      qint64 backend_start_sequence {0};
      bool stopped {false};
      TxStopDecision stop;
    };

    ActiveSession * findActive (TxSessionId sessionId,
                                TxGeneration generation);
    ActiveSession const * findActive (TxSessionId sessionId,
                                      TxGeneration generation) const;
    static TxPlayoutSnapshot normalize (TxStartSnapshot const& start,
                                        TxRawPlayoutSnapshot const& raw);
    static TxTerminalSession terminal (ActiveSession const& active);

    QVector<ActiveSession> active_;
    QVector<TxTerminalSession> terminal_;
  };
}

#endif
