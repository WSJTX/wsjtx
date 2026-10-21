// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShmemDecoderBackend.hpp"

#include <algorithm>
#include <utility>

#include <QDebug>
#include <QDir>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

namespace
{
  class ShmemCapturedDecode final
    : public CapturedDecode
  {
  public:
    decoder_params_t const& settings () const override {return payload.params;}
    DecoderIpc::Ft8MtdPayload payload;
  };
}

ShmemDecoderBackend::ShmemDecoderBackend (DecoderIpc::Session& session, dec_data_t& data,
                                          QMutex& dataMutex, DecoderDiagnostics::LineSink sink,
                                          QObject * parent)
  : DecoderBackend {parent}
  , m_session {session}
  , m_data {data}
  , m_dataMutex {dataMutex}
  , m_supervisor {*this, std::move (sink)}
{
  connect (&m_supervisor, &DecoderProcessSupervisor::processStarted,
           this, &DecoderBackend::processStarted);
  connect (&m_supervisor, &DecoderProcessSupervisor::launched,
           this, &DecoderBackend::launched);
  connect (&m_supervisor, &DecoderProcessSupervisor::becameReady,
           this, &DecoderBackend::becameReady);
  connect (&m_supervisor, &DecoderProcessSupervisor::restartInitiated,
           this, &DecoderBackend::restartInitiated);
  connect (&m_supervisor, &DecoderProcessSupervisor::outputAvailable,
           this, &DecoderBackend::outputAvailable);
  connect (&m_supervisor, &DecoderProcessSupervisor::failed,
           this, &DecoderBackend::failed);
}

void ShmemDecoderBackend::start (LaunchSettings const& settings)
{
  m_launch = settings;
  m_supervisor.start ();
}

void ShmemDecoderBackend::restart ()
{
  m_supervisor.restart ();
}

void ShmemDecoderBackend::prepareForClose ()
{
  m_supervisor.prepareForClose ();
}

void ShmemDecoderBackend::shutdownAtClose ()
{
  m_supervisor.shutdownAtClose ();
  m_session.detach ();
}

bool ShmemDecoderBackend::ready () const
{
  return m_supervisor.ready ();
}

bool ShmemDecoderBackend::running () const
{
  return m_supervisor.running ();
}

bool ShmemDecoderBackend::restartInProgress () const
{
  return m_supervisor.restartInProgress ();
}

void ShmemDecoderBackend::beginInput ()
{
  m_input.beginInput ();
}

void ShmemDecoderBackend::acceptAudio (short const *, int)
{
}

void ShmemDecoderBackend::applySettings (decoder_params_t const&)
{
}

bool ShmemDecoderBackend::submit (Audio audio)
{
  DecoderIpc::Submission submitted;
  qint32 period {0};
#if defined (WSJT_ENABLE_LIVE_AUDIO_TEST)
  Published published;
#endif
  {
    QMutexLocker payloadLock {&m_dataMutex};
    auto inputs = m_input;
    auto const request = m_session.prepareRequest (m_data, Audio::Current == audio, inputs);
    submitted = m_session.submit (request);
    if (submitted) m_input = inputs;
    period = request.options ().nutc;
#if defined (WSJT_ENABLE_LIVE_AUDIO_TEST)
    published = {request.options (), request.metadata (), m_data.d2};
#endif
  }
  if (submitted)
    {
      m_submitted = submitted.generation.value ();
      m_submittedPeriod = period;
#if defined (WSJT_ENABLE_LIVE_AUDIO_TEST)
      m_lastSubmission = published;
#endif
    }
  return static_cast<bool> (submitted);
}

std::unique_ptr<CapturedDecode> ShmemDecoderBackend::capture ()
{
  auto captured = std::make_unique<ShmemCapturedDecode> ();
  auto& payload = captured->payload;
  QMutexLocker payloadLock {&m_dataMutex};
  payload.params = m_data.params;
  payload.metadata = m_input.nextMetadata (m_data.params);
  payload.metadata.valid_samples = std::min (
    payload.metadata.valid_samples, static_cast<qint32> (DecoderIpc::Ft8SampleCount));
  Q_ASSERT (8 == payload.params.nmode);
  Q_ASSERT (payload.params.lmultift8);
  std::copy_n (m_data.d2, DecoderIpc::Ft8SampleCount, payload.samples.begin ());
  return captured;
}

bool ShmemDecoderBackend::submit (CapturedDecode const& captured)
{
  auto const * ours = dynamic_cast<ShmemCapturedDecode const *> (&captured);
  if (!ours) return false;
  auto const submitted = m_session.submit (DecoderIpc::Request::ft8 (ours->payload));
  if (submitted)
    {
      m_submitted = submitted.generation.value ();
      m_submittedPeriod = ours->payload.params.nutc;
    }
  return static_cast<bool> (submitted);
}

#if defined (WSJT_ENABLE_LIVE_AUDIO_TEST)
ShmemDecoderBackend::Published ShmemDecoderBackend::describe (CapturedDecode const& captured)
{
  auto const * ours = dynamic_cast<ShmemCapturedDecode const *> (&captured);
  if (!ours) return {};
  return {ours->payload.params, ours->payload.metadata, ours->payload.samples.data ()};
}
#endif

void ShmemDecoderBackend::abandonDecode ()
{
  m_framer.reset ();
  m_session.abort ();
}

void ShmemDecoderBackend::discardRetainedAudio ()
{
  m_session.abort ();
}

DecoderBackend::RetainedAudio ShmemDecoderBackend::retainedAudio () const
{
  auto const& retained = m_session.completedSnapshot ();
  return {static_cast<bool> (retained), retained.mode, retained.trPeriod,
          m_input.inputId () == retained.metadata.input_id};
}

qint64 ShmemDecoderBackend::activeRequest () const
{
  return m_session.accepts (m_submitted) ? m_submitted : 0;
}

bool ShmemDecoderBackend::hasPendingOutput () const
{
  return m_supervisor.channel ().canReadLine ();
}

void ShmemDecoderBackend::deliverOutput (OutputHandler const& handler)
{
  m_framer.drain (m_supervisor.channel (),
                  [this, &handler] (DecoderOutputFramer::Event const& event) {
                    route (event, handler);
                  });
}

bool ShmemDecoderBackend::activeProgress (qint32& progress) const
{
  auto const ipc = m_session.diagnostics ();
  if (!m_session.accepts (ipc.generation)) return false;
  progress = ipc.progress;
  return true;
}

QString ShmemDecoderBackend::failureDetail (QString const& reason)
{
  return m_supervisor.failureDetail (reason);
}

QByteArray ShmemDecoderBackend::diagnosticsTail ()
{
  return m_supervisor.diagnosticsTail ();
}

QString ShmemDecoderBackend::diagnostics () const
{
  auto const ipc = m_session.diagnostics ();
  return QString {"process_phase=%1 active_generation=%2 ipc_state=%3 "
                  "ipc_generation=%4 ipc_progress=%5"}
    .arg (static_cast<int> (m_supervisor.phase ()))
    .arg (activeRequest ())
    .arg (ipc.state)
    .arg (ipc.generation)
    .arg (ipc.progress);
}

DecoderProcessSupervisor::Command ShmemDecoderBackend::command ()
{
  QStringList arguments {
    "-s", m_session.name (),
    "--ipc-lock", m_session.workerLockPath (),
    "-w", "1", // FFTW planning patience
    // The number  of threads for  FFTW specified here is  chosen as
    // three because  that gives  the best  throughput of  the large
    // FFTs used  in jt9.  The count  is the minimum of  (the number
    // available CPU threads less one) and three.  This ensures that
    // there is always at least one free CPU thread to run the other
    // mode decoder in parallel.
    "-m", QString::number (qMin (qMax (QThread::idealThreadCount () - 1, 1), 3)),
    "-e", QDir::toNativeSeparators (m_launch.programDirectory),
    "-a", QDir::toNativeSeparators (m_launch.writeableDataDirectory),
    "-t", QDir::toNativeSeparators (m_launch.temporaryDirectory),
    // -r: read-only shipped-data dir (cty.dat, ALLCALL7.TXT, ...) for the
    // Fortran decoder; resolves to Contents/Resources/wsjtx on macOS.
    "-r", QDir::toNativeSeparators (m_launch.dataDirectory)
  };
  QProcessEnvironment environment {m_launch.environment};
  environment.insert ("OMP_STACKSIZE", "10M");
  return {QDir::toNativeSeparators (QDir {m_launch.programDirectory}.filePath ("jt9")),
          arguments, environment};
}

void ShmemDecoderBackend::prepareStart ()
{
  m_framer.reset ();
  m_session.beginStartup ();
}

void ShmemDecoderBackend::requestStop ()
{
  m_session.shutdown ();
}

bool ShmemDecoderBackend::prepareReplacement ()
{
  return m_session.reset ();
}

void ShmemDecoderBackend::startupFailed ()
{
  m_session.beginStartup ();
}

void ShmemDecoderBackend::route (DecoderOutputFramer::Event const& event,
                                 OutputHandler const& handler)
{
  using EventType = DecoderOutputFramer::EventType;
  using FailureKind = Failure::Kind;
  if (m_supervisor.stopped ()) return;
  auto const starting = m_supervisor.starting ();
  if (EventType::Ready == event.type)
    {
      if (!starting || DecoderIpc::Status::Ok != m_session.acceptReady (event.protocolVersion))
        {
          m_supervisor.failStartup (FailureKind::HandshakeInvalid, {}, event.protocolVersion);
          return;
        }
      m_supervisor.markReady ();
      return;
    }
  if (EventType::Error == event.type)
    {
      m_supervisor.failStartup (FailureKind::HandshakeRefused,
                                QString::fromLatin1 (event.errorStatus));
      return;
    }
  if ((EventType::Malformed == event.type
       && (event.rawLine.startsWith ("<DecoderReady")
           || event.rawLine.startsWith ("<DecoderError")))
      || (starting && EventType::Malformed != event.type))
    {
      m_supervisor.failStartup (FailureKind::HandshakeMalformed);
      return;
    }

  Output output;
  output.current = m_session.accepts (event.generation);
  output.request = event.generation;
  output.period = event.generation == m_submitted ? m_submittedPeriod : 0;
  output.text = event.rawLine;
  switch (event.type)
    {
    case EventType::Record:
      output.kind = Output::Kind::Record;
      break;
    case EventType::Rejected:
      output.kind = Output::Kind::Rejected;
      break;
    case EventType::Started:
      output.kind = Output::Kind::CycleStarted;
      break;
    case EventType::Finished:
      {
        auto const& completion = event.completion;
        output.kind = Output::Kind::CycleFinished;
        output.counts = {completion.synchronized, completion.decoded, completion.average};
        if (output.current)
          {
            auto const ipc = m_session.diagnostics ();
            if (DecoderIpc::Status::Ok != m_session.complete (completion))
              {
                qWarning () << "Unable to consume matching decoder completion"
                            << "generation:" << completion.generation
                            << "state:" << ipc.state;
                output.kind = Output::Kind::CycleLost;
              }
          }
      }
      break;
    case EventType::Malformed:
      output.kind = event.rawLine.startsWith ("<DecodeStarted>")
        || event.rawLine.startsWith ("<DecodeFinished>")
        ? Output::Kind::MalformedBoundary : Output::Kind::Unframed;
      break;
    case EventType::Ready:
    case EventType::Error:
      return;
    }
  handler (output);
}
