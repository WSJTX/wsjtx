#include <QtTest>

#include <functional>

#include <QCoreApplication>
#include <QList>
#include <QPair>
#include <QMetaType>

#include "Audio/TxRequest.hpp"

class TxRequestRelay : public QObject
{
  Q_OBJECT

public:
  TxEvidence::TxRequest received;
  bool delivered {false};

  Q_SIGNAL void requestReady (TxEvidence::TxRequest request);

  Q_SLOT void receive (TxEvidence::TxRequest request)
  {
    received = request;
    delivered = true;
  }
};

class TestTxRequest : public QObject
{
  Q_OBJECT

private:
  Q_SLOT void has_ft8_defaults ()
  {
    TxEvidence::TxRequest const request;
    QCOMPARE (request.mode, QString {"FT8"});
    QCOMPARE (request.symbols_length, 79u);
    QCOMPARE (request.frames_per_symbol, 1920.0);
    QCOMPARE (request.frequency_hz, 1500.0);
    QCOMPARE (request.tone_spacing, -3.0);
    QCOMPARE (request.channel, AudioDevice::Mono);
    QVERIFY (request.synchronize);
    QVERIFY (!request.fast_mode);
    QCOMPARE (request.snr_db, 99.0);
    QCOMPARE (request.tr_period_s, 60.0);
    QCOMPARE (request.session_id, TxEvidence::TxSessionId::invalid ());
    QCOMPARE (request.generation, TxEvidence::TxGeneration::invalid ());
    QCOMPARE (request.queue_epoch, TxAudioQueueEpoch::invalid ());
    QVERIFY (!request.tuning);
    QVERIFY (!request.silent);
    QVERIFY (request.cw_id.isEmpty ());
    QCOMPARE (request.start_window_open_ms, qint64 {-1});
    QCOMPARE (request.start_window_close_ms, qint64 {-1});
  }

  Q_SLOT void copies_every_distinct_field ()
  {
    TxEvidence::TxRequest source;
    source.mode = "JTTY";
    source.symbols_length = 101;
    source.frames_per_symbol = 202.5;
    source.frequency_hz = 303.5;
    source.tone_spacing = 404.5;
    source.channel = AudioDevice::Right;
    source.synchronize = false;
    source.fast_mode = true;
    source.snr_db = 505.5;
    source.tr_period_s = 606.5;
    source.session_id = TxEvidence::TxSessionId {707};
    source.generation = TxEvidence::TxGeneration {808};
    source.queue_epoch = TxAudioQueueEpoch {909};
    source.tuning = true;
    source.silent = true;
    source.cw_id = {1, 0, 1};
    source.start_window_open_ms = 1010;
    source.start_window_close_ms = 1111;

    auto const copy = source;
    source = {};

    QCOMPARE (copy.mode, QString {"JTTY"});
    QCOMPARE (copy.symbols_length, 101u);
    QCOMPARE (copy.frames_per_symbol, 202.5);
    QCOMPARE (copy.frequency_hz, 303.5);
    QCOMPARE (copy.tone_spacing, 404.5);
    QCOMPARE (copy.channel, AudioDevice::Right);
    QVERIFY (!copy.synchronize);
    QVERIFY (copy.fast_mode);
    QCOMPARE (copy.snr_db, 505.5);
    QCOMPARE (copy.tr_period_s, 606.5);
    QCOMPARE (copy.session_id, TxEvidence::TxSessionId {707});
    QCOMPARE (copy.generation, TxEvidence::TxGeneration {808});
    QCOMPARE (copy.queue_epoch, TxAudioQueueEpoch {909});
    QVERIFY (copy.tuning);
    QVERIFY (copy.silent);
    QCOMPARE (copy.cw_id, QVector<int> ({1, 0, 1}));
    QCOMPARE (copy.start_window_open_ms, qint64 {1010});
    QCOMPARE (copy.start_window_close_ms, qint64 {1111});
  }

  Q_SLOT void equality_compares_every_field ()
  {
    TxEvidence::TxRequest const base;
    QVERIFY (base == TxEvidence::TxRequest {});

    using Change = std::function<void (TxEvidence::TxRequest&)>;
    QList<QPair<char const *, Change>> const changes {
      {"mode", [] (TxEvidence::TxRequest& r) {r.mode = "JTTY";}},
      {"symbols_length", [] (TxEvidence::TxRequest& r) {r.symbols_length = 101;}},
      {"frames_per_symbol", [] (TxEvidence::TxRequest& r) {r.frames_per_symbol = 202.5;}},
      {"frequency_hz", [] (TxEvidence::TxRequest& r) {r.frequency_hz = 303.5;}},
      {"tone_spacing", [] (TxEvidence::TxRequest& r) {r.tone_spacing = 404.5;}},
      {"channel", [] (TxEvidence::TxRequest& r) {r.channel = AudioDevice::Right;}},
      {"synchronize", [] (TxEvidence::TxRequest& r) {r.synchronize = false;}},
      {"fast_mode", [] (TxEvidence::TxRequest& r) {r.fast_mode = true;}},
      {"snr_db", [] (TxEvidence::TxRequest& r) {r.snr_db = 505.5;}},
      {"tr_period_s", [] (TxEvidence::TxRequest& r) {r.tr_period_s = 606.5;}},
      {"session_id", [] (TxEvidence::TxRequest& r) {r.session_id = TxEvidence::TxSessionId {707};}},
      {"generation", [] (TxEvidence::TxRequest& r) {r.generation = TxEvidence::TxGeneration {808};}},
      {"queue_epoch", [] (TxEvidence::TxRequest& r) {r.queue_epoch = TxAudioQueueEpoch {909};}},
      {"tuning", [] (TxEvidence::TxRequest& r) {r.tuning = true;}},
      {"silent", [] (TxEvidence::TxRequest& r) {r.silent = true;}},
      {"cw_id", [] (TxEvidence::TxRequest& r) {r.cw_id = {1, 0, 1};}},
      {"start_window_open_ms", [] (TxEvidence::TxRequest& r) {r.start_window_open_ms = 1010;}},
      {"start_window_close_ms", [] (TxEvidence::TxRequest& r) {r.start_window_close_ms = 1111;}},
    };
    for (auto const& change : changes)
      {
        auto changed = base;
        change.second (changed);
        QVERIFY2 (changed != base, change.first);
        QVERIFY2 (!(changed == base), change.first);
      }
  }

  Q_SLOT void registers_and_delivers_queued_requests ()
  {
    TxEvidence::register_tx_request_type ();
    QCOMPARE (QMetaType::type ("TxEvidence::TxRequest"),
              qMetaTypeId<TxEvidence::TxRequest> ());

    TxRequestRelay relay;
    QVERIFY (connect (&relay, &TxRequestRelay::requestReady,
                      &relay, &TxRequestRelay::receive,
                      Qt::QueuedConnection));

    TxEvidence::TxRequest request;
    request.mode = "FT4";
    request.session_id = TxEvidence::TxSessionId {12};
    request.generation = TxEvidence::TxGeneration {34};
    request.queue_epoch = TxAudioQueueEpoch {56};
    request.cw_id = {0, 1, 0};
    Q_EMIT relay.requestReady (request);

    QTRY_VERIFY (relay.delivered);
    QCOMPARE (relay.received.mode, QString {"FT4"});
    QCOMPARE (relay.received.session_id, TxEvidence::TxSessionId {12});
    QCOMPARE (relay.received.generation, TxEvidence::TxGeneration {34});
    QCOMPARE (relay.received.queue_epoch, request.queue_epoch);
    QCOMPARE (relay.received.cw_id, QVector<int> ({0, 1, 0}));
  }
};

QTEST_GUILESS_MAIN (TestTxRequest)

#include "test_tx_request.moc"
