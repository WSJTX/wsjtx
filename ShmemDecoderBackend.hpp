// -*- Mode: C++ -*-
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHMEM_DECODER_BACKEND_HPP
#define SHMEM_DECODER_BACKEND_HPP

#include "DecoderBackend.hpp"
#include "DecoderIpc.hpp"
#include "DecoderOutputFramer.hpp"
#include "DecoderProcessSupervisor.hpp"

class QMutex;

// The jt9 worker over DecoderIpc::Session. The worker reads the GUI's decode
// buffer, so settings and audio travel in the shared request, not through
// applySettings () or acceptAudio ().
class ShmemDecoderBackend final
  : public DecoderBackend
  , private DecoderProcessSupervisor::Transport
{
  Q_OBJECT

public:
  ShmemDecoderBackend (DecoderIpc::Session&, dec_data_t& data, QMutex& dataMutex,
                       DecoderDiagnostics::LineSink = logDecoderDiagnostic,
                       QObject * parent = nullptr);

  bool schedulesDecodes () const override {return false;}

  void start (LaunchSettings const&) override;
  void restart () override;
  void prepareForClose () override;
  void shutdownAtClose () override;

  bool ready () const override;
  bool running () const override;
  bool restartInProgress () const override;

  void beginInput () override;
  void acceptAudio (short const * samples, int end) override;
  void applySettings (decoder_params_t const&) override;

  bool submit (Audio) override;
  std::unique_ptr<CapturedDecode> capture () override;
  bool submit (CapturedDecode const&) override;
  void abandonDecode () override;
  void discardRetainedAudio () override;
  RetainedAudio retainedAudio () const override;
  qint64 activeRequest () const override;

  bool hasPendingOutput () const override;
  void deliverOutput (OutputHandler const&) override;
  bool activeProgress (qint32& progress) const override;

  QString failureDetail (QString const& reason) override;
  QByteArray diagnosticsTail () override;
  QString diagnostics () const override;

#if defined (WSJT_ENABLE_LIVE_AUDIO_TEST)
  // For the live-audio test observers: what the last submit (Audio) put in the
  // shared segment, and what a capture holds. samples is borrowed from the
  // decode buffer or the capture and is valid until either changes; it is null
  // for a capture another backend made.
  struct Published
  {
    decoder_params_t params {};
    decoder_input_metadata_t metadata {};
    short const * samples {nullptr};
  };
  Published lastSubmission () const {return m_lastSubmission;}
  static Published describe (CapturedDecode const&);
#endif

private:
  DecoderProcessSupervisor::Command command () override;
  void prepareStart () override;
  void requestStop () override;
  bool prepareReplacement () override;
  void startupFailed () override;

  void route (DecoderOutputFramer::Event const&, OutputHandler const&);

  DecoderIpc::Session& m_session;
  dec_data_t& m_data;
  QMutex& m_dataMutex;
  DecoderIpc::InputState m_input;
  DecoderOutputFramer m_framer;
  LaunchSettings m_launch;
  qint32 m_submitted {0};
  qint32 m_submittedPeriod {0};
#if defined (WSJT_ENABLE_LIVE_AUDIO_TEST)
  Published m_lastSubmission;
#endif
  DecoderProcessSupervisor m_supervisor;
};

#endif
