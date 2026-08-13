#include <QtTest>

#include <QCoreApplication>
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
    QCOMPARE (request.fifo_session_id, qint64 {0});
    QVERIFY (!request.tuning);
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
    source.fifo_session_id = 909;
    source.tuning = true;

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
    QCOMPARE (copy.fifo_session_id, qint64 {909});
    QVERIFY (copy.tuning);
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
    request.fifo_session_id = 56;
    Q_EMIT relay.requestReady (request);

    QTRY_VERIFY (relay.delivered);
    QCOMPARE (relay.received.mode, QString {"FT4"});
    QCOMPARE (relay.received.session_id, TxEvidence::TxSessionId {12});
    QCOMPARE (relay.received.generation, TxEvidence::TxGeneration {34});
    QCOMPARE (relay.received.fifo_session_id, qint64 {56});
  }
};

QTEST_GUILESS_MAIN (TestTxRequest)

#include "test_tx_request.moc"
