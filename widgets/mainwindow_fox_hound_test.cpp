#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "BestSPApplicationTestController.hpp"
#include "DecoderEngine.h"
#include "Radio.hpp"
#include "commons.h"
#include "wsjtx_config.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

extern bool programStart;
extern dec_data_t& dec_data;

extern "C" {
  void genft8_ (char *, int *, int *, char *, char *, int *,
                fortran_charlen_t, fortran_charlen_t);
  void gen_ft8wave_ (int *, int *, int *, float *, float *, float *,
                     float *, float *, int *, int *);
}

namespace
{
  QString const foxCall {"K1ABC"};
  QString const standardCall {"W9XYZ"};
  QString const compoundCall {"PJ2/W1AW"};

  constexpr int sampleRate = 12000;
  constexpr int periodSamples = 15 * sampleRate;
  constexpr int waveformSamples = 79 * 1920;
  constexpr int foxFrequency = 500;
  using AudioPeriod = std::vector<int16_t>;

  void require (bool condition, QString const& failure)
  {
    if (!condition) throw std::runtime_error {failure.toStdString ()};
  }

  bool waitFor (std::function<bool ()> const& ready)
  {
    if (ready ()) return true;
    QEventLoop loop;
    QTimer check;
    QTimer watchdog;
    QObject::connect (&check, &QTimer::timeout, &loop, [&] {
      if (ready ()) loop.quit ();
    });
    QObject::connect (&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    watchdog.setSingleShot (true);
    watchdog.start (15000);
    check.start (10);
    loop.exec ();
    return ready ();
  }

  void sendWaveform (QString const& path, float const * waveform, int stride)
  {
    AudioPeriod pcm (periodSamples);
    // The low-frequency Fox waveform is generated at 48 kHz and decoded at 12 kHz.
    for (int i = 0; i < waveformSamples; ++i)
      pcm[sampleRate / 2 + i] = static_cast<int16_t> (std::lround (waveform[i * stride] * 4096));
    auto const bytes = static_cast<qint64> (pcm.size () * sizeof (pcm.front ()));
    QSaveFile file {path};
    require (file.open (QIODevice::WriteOnly)
             && file.write (reinterpret_cast<char const *> (pcm.data ()), bytes) == bytes
             && file.commit (), "Cannot publish audio period: " + path);
  }

  void sendFt8Message (QString const& path, QString const& message, float frequency)
  {
    QByteArray text = message.toLatin1 ().leftJustified (37, ' ');
    char sent[37], bits[77];
    int tones[79], type = 0, subtype = 0;
    genft8_ (text.data (), &type, &subtype, sent, bits, tones, sizeof sent, sizeof sent);
    int symbols = 79, samplesPerSymbol = 1920, count = waveformSamples, complexOutput = 0;
    float bandwidth = 2.f, rate = sampleRate, unused[2] {};
    std::vector<float> waveform (count);
    gen_ft8wave_ (tones, &symbols, &samplesPerSymbol, &bandwidth, &rate, &frequency,
                  unused, waveform.data (), &complexOutput, &count);
    sendWaveform (path, waveform.data (), 1);
  }

  AudioPeriod awaitWaveform (QString const& path)
  {
    QFile file {path};
    require (waitFor ([&] {return file.exists ();}), "Timed out waiting for audio period: " + path);
    AudioPeriod pcm (periodSamples);
    auto const bytes = static_cast<qint64> (pcm.size () * sizeof (pcm.front ()));
    require (file.open (QIODevice::ReadOnly) && file.size () == bytes
             && file.read (reinterpret_cast<char *> (pcm.data ()), bytes) == bytes,
             "Cannot read audio period: " + path);
    return pcm;
  }

  class Ft8Receiver final
  {
  public:
    Ft8Receiver ()
    {
      decoder_engine_options const options {DECODER_ENGINE_ABI};
      require (decoder_engine_create (&options, &engine_) == DECODER_OK,
               "Cannot create the station's FT8 decoder.");
    }

    ~Ft8Receiver () {decoder_engine_destroy (engine_);}

    QStringList decode (AudioPeriod const& pcm, QString const& myCall, QString const& partner,
                        int utc)
    {
      decoder_attempt_request request {};
      request.input_id = request.analysis_id = ++period_;
      request.attempt_no = 1;
      request.mode = DECODER_MODE_FT8;
      request.phase = DECODER_PHASE_NORMAL;
      request.source = DECODER_SOURCE_FILE;
      auto& ft8 = request.ft8;
      ft8.utc = utc;
      ft8.half_symbol_stage = 50;
      ft8.receive_frequency_hz = ft8.transmit_frequency_hz = foxFrequency;
      ft8.search_low_hz = 200;
      ft8.search_high_hz = 3000;
      ft8.tolerance_hz = 20;
      ft8.depth = 1;
      ft8.ap_width_hz = 75;
      ft8.cycles = ft8.threads = 1;
      ft8.date = 261120;
      setCall (ft8.mycall, myCall);
      setCall (ft8.my_base_call, Radio::base_callsign (myCall));
      setCall (ft8.hiscall, partner);
      setCall (ft8.his_base_call, Radio::base_callsign (partner));

      decoder_audio_view const audio {pcm.data (), periodSamples, sampleRate};
      decoder_attempt_outcome outcome {};
      Decodes decodes {utc, {}};
      require (decoder_engine_decode (engine_, &request, &audio, observe, &decodes, &outcome) == DECODER_OK
               && outcome.status == DECODER_OK, "FT8 audio decode failed.");
      require (decoder_engine_release_input (engine_, request.input_id) == DECODER_OK,
               "Cannot release the decoded FT8 period.");
      return decodes.lines;
    }

  private:
    Q_DISABLE_COPY (Ft8Receiver)
    struct Decodes { int utc; QStringList lines; };

    static void setCall (char (&destination)[12], QString const& call)
    {
      auto const padded = call.toLatin1 ().leftJustified (sizeof destination, ' ');
      std::copy_n (padded.constData (), sizeof destination, destination);
    }

    static void observe (decoder_observation const * observation, void * opaque)
    {
      auto& decodes = *static_cast<Decodes *> (opaque);
      decodes.lines.append (QString {"%1 %2 %3 %4 ~  %5\n"}
        .arg (decodes.utc, 6, 10, QLatin1Char {'0'}).arg (observation->snr_db, 3)
        .arg (observation->dt_seconds, 4, 'f', 1).arg (std::lround (observation->frequency_hz), 4)
        .arg (QString::fromLatin1 (observation->message)));
    }

    decoder_engine_handle engine_ {};
    int period_ {0};
  };
}

QString MainWindow::checkFoxHoundExchangeTest (QString const& station)
{
  try
    {
      m_guiTimer.stop ();
      bool const fox = station == "fox";
      bool const compound = station == "compound";
      auto const myCall = fox ? foxCall : compound ? compoundCall : standardCall;
      require (m_automated_test && m_config.is_dummy_rig () && m_config.my_callsign () == myCall
               && m_mode == "FT8" && !m_config.superFox (),
               "Classic FT8 Fox/Hound fixture settings are incorrect.");
      require (m_config.transceiver_online () && waitFor ([&] {
        auto const radio = m_operatingFrequency.snapshot ();
        return !programStart && m_rigState.online () && radio.rx
          && !radio.pendingRx && !radio.pendingTx && !m_modeEntries.active ();
      }), "The dummy radio did not become ready.");
      if (!fox) configureHound (true, ModeEntry::RetunePolicy::Preserve);
      require (m_specOp == (fox ? SpecOp::FOX : SpecOp::HOUND), "Incorrect Fox/Hound activity.");
      require (requestNominalFrequencyChange (14090000, FrequencyRequestOrigin::User)
               && waitFor ([&] {
        auto const radio = m_operatingFrequency.snapshot ();
        return radio.rx == 14090000 && !radio.pendingRx && !radio.pendingTx
          && !m_modeEntries.active () && !m_pendingModeRetune.pending ();
      }), "The dummy radio did not reach the FT8 test frequency.");

      Ft8Receiver receiver;
      // Exercise the application's normal text receive path using the native decoder's observations.
      auto receive = [&] (QString const& path, int utc) {
        auto const lines = receiver.decode (awaitWaveform (path), myCall, fox ? QString {} : foxCall, utc);
        require (!lines.isEmpty (), "FT8 decoder produced no messages for " + path);
        dec_data.params = {};
        dec_data.params.nmode = 8;
        dec_data.params.ntrperiod = 15;
        dec_data.params.newdat = true;
        require (publishDecodeRequest (true) == DecodePublishResult::Published,
                 "MainWindow did not accept the receive period.");
        for (auto const& line : lines)
          deliverScriptedTestDecode (m_decoderBackend.get (), line);
        completeScriptedTestDecode (m_decoderBackend.get ());
        return lines;
      };

      auto readLog = [&] {
        QFile adif {m_logBook.path ()};
        return adif.open (QIODevice::ReadOnly) ? adif.readAll ().toUpper () : QByteArray {};
      };
      auto logged = [] (QByteArray const& adif, QString const& call,
                        QString const& sent, QString const& received) {
        auto field = [] (QByteArray const& name, QString const& value) {
          return '<' + name + ':' + QByteArray::number (value.size ()) + '>' + value.toLatin1 ();
        };
        int records = 0;
        for (auto const& record : adif.split ('\n'))
          if (record.contains (field ("CALL", call)) && record.contains (field ("RST_SENT", sent))
              && record.contains (field ("RST_RCVD", received)) && record.contains ("<EOR>"))
            ++records;
        return records == 1;
      };

      if (fox)
        {
          QTemporaryDir audio;
          require (audio.isValid (), "Cannot create the exchange audio directory.");
          QProcess hound, compoundHound;
          for (auto * child : {&hound, &compoundHound})
            {
              auto const role = child == &hound ? QString {"hound"} : QString {"compound"};
              auto environment = QProcessEnvironment::systemEnvironment ();
              environment.insert ("WSJTX_FOX_HOUND_AUDIO", audio.path ());
              environment.insert ("WSJT_QMAP_SHARED_MEMORY_KEY", "mem_qmap-test_" + role);
              child->setProcessEnvironment (environment);
              child->setProcessChannelMode (QProcess::ForwardedChannels);
              child->start (QCoreApplication::applicationFilePath (),
                            {"--fox-hound-exchange-test", role, "--rig-name", "CTEST-" + role});
              require (child->waitForStarted (), "Cannot launch " + role + " station.");
            }
          auto transmit = [&] (QString const& name) {
            require (foxTxSequencer (), "Classic Fox could not prepare " + name);
            sendWaveform (audio.filePath (name), foxcom_.wave, 4);
          };
          auto queueCaller = [&] (QString const& name, QString const& call, QString const& report, int utc) {
            auto const lines = receive (audio.filePath (name), utc);
            auto const found = std::any_of (lines.cbegin (), lines.cend (), [&](QString const& line) {
              return DecodedText {line}.fields ().sender == call;
            });
            require (found, "Fox did not decode caller " + call + ": " + lines.join (" | "));
            selectHound (call + " FN42 " + report, false);
            require (!m_houndQueue.isEmpty (), "Fox could not queue " + call);
          };
          ui->TxFreqSpinBox->setValue (foxFrequency);
          ui->cbMoreCQs->setChecked (false);
          ui->cbSendMsg->setChecked (false);
          ui->sbNslots->setValue (1);
          transmit ("cq.pcm");
          queueCaller ("hound-call.pcm", standardCall, "-12", 120015);
          transmit ("report.pcm");
          receive (audio.filePath ("hound-ack.pcm"), 120045);
          auto const first = m_foxQSO.value (standardCall);
          require (first.tFoxRrpt > 0, "The first Hound's acknowledgement did not reach its slot.");
          sendFt8Message (audio.filePath ("wrong-fox.pcm"), standardCall + " K1ABD RR73", foxFrequency);
          queueCaller ("compound-call.pcm", compoundCall, "-18", 120115);
          transmit ("combined.pcm");
          auto compoundLines = receive (audio.filePath ("compound-ack.pcm"), 120145);
          require (std::any_of (compoundLines.cbegin (), compoundLines.cend (), [] (QString const& line) {
            auto const fields = DecodedText {line}.fields ();
            return fields.sender == compoundCall && fields.senderState == DecodedCallState::ResolvedHash;
          }), "Fox did not resolve the compound Hound's acknowledgement: " + compoundLines.join (" | "));
          auto const second = m_foxQSO.value (compoundCall);
          require (second.tFoxRrpt > 0, "The compound Hound's acknowledgement did not reach its slot: "
                   + compoundLines.join (" | "));
          transmit ("final.pcm");
          require (waitFor ([&] {
            return hound.state () == QProcess::NotRunning && compoundHound.state () == QProcess::NotRunning;
          }) && hound.exitStatus () == QProcess::NormalExit && hound.exitCode () == 0
             && compoundHound.exitStatus () == QProcess::NormalExit && compoundHound.exitCode () == 0,
                   "Both Hounds must complete and log.");
          require (waitFor ([&] {
            auto const adif = readLog ();
            return adif.count ("<EOR>") == 2 && logged (adif, standardCall, "-12", first.rcvd)
              && logged (adif, compoundCall, "-18", second.rcvd);
          }), "Fox ADIF must contain both full calls and their corresponding reports.");
        }
      else
        {
          auto const directory = qEnvironmentVariable ("WSJTX_FOX_HOUND_AUDIO");
          require (!directory.isEmpty (), "A Hound fixture must be started by the Fox fixture.");
          QDir const audio {directory};
          auto const cq = receive (audio.filePath ("cq.pcm"), 120000);
          require (std::any_of (cq.cbegin (), cq.cend (), [] (QString const& line) {
            auto const fields = DecodedText {line}.fields ();
            return fields.cq && fields.sender == foxCall;
          }), "Hound did not decode the Fox CQ.");
          ui->dxCallEntry->setText (foxCall);
          ui->dxGridEntry->setText ("FN42");
          m_hisCall = foxCall;
          m_hisGrid = "FN42";
          genStdMsgs ("-10");
          setTxMsg (1);
          sendFt8Message (audio.filePath (station + "-call.pcm"), ui->tx1->text (),
                            compound ? 2000 : 1500);
          auto const report = compound ? QString {"-18"} : QString {"-12"};
          auto const lines = receive (audio.filePath (compound ? "combined.pcm" : "report.pcm"),
                                      compound ? 120130 : 120030);
          require (ui->txrb3->isChecked () && m_rptRcvd == report && !readLog ().contains ("<EOR>"),
                   "The Fox report must select Tx3 without logging: " + lines.join (" | "));
          auto const sent = m_rptSent;
          sendFt8Message (audio.filePath (station + "-ack.pcm"), ui->tx3->text (),
                            ui->TxFreqSpinBox->value ());
          if (!compound)
            {
              auto const wrongFox = receive (audio.filePath ("wrong-fox.pcm"), 120100);
              require (std::any_of (wrongFox.cbegin (), wrongFox.cend (), [] (QString const& line) {
                return DecodedText {line}.payload () == standardCall + " K1ABD RR73";
              }), "Hound did not decode the wrong Fox's RR73: " + wrongFox.join (" | "));
              require (m_auto && m_pendingLogQsos.empty () && !readLog ().contains ("<EOR>"),
                       "A different Fox's RR73 stopped or logged the active contact.");
            }
          auto const final = receive (audio.filePath (compound ? "final.pcm" : "combined.pcm"),
                                      compound ? 120200 : 120130);
          require (!m_auto && waitFor ([&] {
            auto const adif = readLog ();
            return adif.count ("<EOR>") == 1 && logged (adif, foxCall, sent, report);
          }), "Hound must stop and log the selected Fox with both reports: " + final.join (" | "));
        }
      return {};
    }
  catch (std::exception const& error)
    {
      return QString::fromUtf8 (error.what ());
    }
}
