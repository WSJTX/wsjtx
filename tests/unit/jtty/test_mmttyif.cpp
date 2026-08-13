#include <QtTest>

#include <memory>

#include <QCoreApplication>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "widgets/MMTTYIF.hpp"

namespace
{
  int constexpr maximumHeaderBytes {64};
  int constexpr maximumPayloadBytes {64 * 1024};
}

class TestMMTTYIF final
  : public QObject
{
  Q_OBJECT

private slots:
  void init ()
  {
    server_.reset (new QTcpServer);
    // MMTTYIF connects to 127.0.0.1 (IPv4). Bind the same family so Windows
    // does not leave the server on ::1 while the client dials IPv4.
    QVERIFY (server_->listen (QHostAddress {QStringLiteral ("127.0.0.1")}, 0));
    QCoreApplication::processEvents ();

    interface_.reset (new MMTTYIF);
    interface_->initialize (server_->serverPort ());

    QTRY_VERIFY_WITH_TIMEOUT (server_->hasPendingConnections (), 5000);
    peer_ = server_->nextPendingConnection ();
    QVERIFY (peer_);
    QTRY_VERIFY_WITH_TIMEOUT (interface_->isConnected (), 5000);
  }

  void cleanup ()
  {
    interface_.reset ();
    peer_ = nullptr;
    server_.reset ();
    QCoreApplication::processEvents ();
  }

  void parsesFragmentedFrame ()
  {
    QSignalSpy text {interface_.get (), &MMTTYIF::app_tx_string};

    QVERIFY (writeToInterface ("<TXTEXT:11>HELLO"));
    QTest::qWait (20);
    QCOMPARE (text.count (), 0);

    QVERIFY (writeToInterface (" WORLD"));
    QTRY_COMPARE_WITH_TIMEOUT (text.count (), 1, 1000);
    QCOMPARE (text.at (0).at (0).toString (), QString {"HELLO WORLD"});
  }

  void parsesCoalescedFrames ()
  {
    QSignalSpy text {interface_.get (), &MMTTYIF::app_tx_string};
    QSignalSpy start {interface_.get (), &MMTTYIF::app_start_tx};

    QVERIFY (writeToInterface ("<TXTEXT:5>A<B>C<XMIT:2>ON"));

    QTRY_COMPARE_WITH_TIMEOUT (text.count (), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT (start.count (), 1, 1000);
    QCOMPARE (text.at (0).at (0).toString (), QString {"A<B>C"});
  }

  void dispatchesControlCommands ()
  {
    QSignalSpy stop {interface_.get (), &MMTTYIF::app_stop_tx};
    QSignalSpy abort {interface_.get (), &MMTTYIF::app_abort_tx};
    QSignalSpy close {interface_.get (), &MMTTYIF::app_is_quitting};

    QVERIFY (writeToInterface ("<XMIT:3>OFF<ABORT>ABORT<CLOSE>CLOSE"));

    QTRY_COMPARE_WITH_TIMEOUT (stop.count (), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT (abort.count (), 1, 1000);
    QTRY_COMPARE_WITH_TIMEOUT (close.count (), 1, 1000);
  }

  void recoversAfterInvalidLength ()
  {
    QSignalSpy start {interface_.get (), &MMTTYIF::app_start_tx};
    QSignalSpy logs {interface_.get (), &MMTTYIF::log_message};

    QVERIFY (writeToInterface ("<TXTEXT:-1><TXTEXT:nope><XMIT:2>ON"));

    QTRY_COMPARE_WITH_TIMEOUT (start.count (), 1, 1000);
    QTRY_VERIFY_WITH_TIMEOUT (logs.count () >= 2, 1000);
  }

  void acceptsMaximumDeclaredPayload ()
  {
    QSignalSpy text {interface_.get (), &MMTTYIF::app_tx_string};
    QByteArray const payload (maximumPayloadBytes, 'a');
    QByteArray frame {"<TXTEXT:"};
    frame += QByteArray::number (payload.size ());
    frame += '>';
    frame += payload;

    QVERIFY (writeToInterface (frame));

    QTRY_COMPARE_WITH_TIMEOUT (text.count (), 1, 2000);
    QCOMPARE (text.at (0).at (0).toString (), QString (maximumPayloadBytes, 'A'));
  }

  void rejectsOversizedDeclaredPayload ()
  {
    QSignalSpy start {interface_.get (), &MMTTYIF::app_start_tx};
    QSignalSpy logs {interface_.get (), &MMTTYIF::log_message};

    QByteArray frame {"<TXTEXT:"};
    frame += QByteArray::number (maximumPayloadBytes + 1);
    frame += "><XMIT:2>ON";
    QVERIFY (writeToInterface (frame));

    QTRY_COMPARE_WITH_TIMEOUT (start.count (), 1, 1000);
    QTRY_VERIFY_WITH_TIMEOUT (hasLogContaining (logs, "payload length exceeds"), 1000);
  }

  void rejectsSignedMaximumLength ()
  {
    QSignalSpy start {interface_.get (), &MMTTYIF::app_start_tx};
    QSignalSpy logs {interface_.get (), &MMTTYIF::log_message};

    QVERIFY (writeToInterface ("<TXTEXT:2147483647><XMIT:2>ON"));

    QTRY_COMPARE_WITH_TIMEOUT (start.count (), 1, 1000);
    QTRY_VERIFY_WITH_TIMEOUT (hasLogContaining (logs, "payload length exceeds"), 1000);
  }

  void rejectsOversizedUndeclaredPayload ()
  {
    QSignalSpy start {interface_.get (), &MMTTYIF::app_start_tx};
    QSignalSpy abort {interface_.get (), &MMTTYIF::app_abort_tx};
    QSignalSpy logs {interface_.get (), &MMTTYIF::log_message};

    QByteArray frame {"<ABORT>"};
    frame += QByteArray (maximumPayloadBytes + 1, 'A');
    frame += "<XMIT:2>ON";
    QVERIFY (writeToInterface (frame));

    QTRY_COMPARE_WITH_TIMEOUT (start.count (), 1, 2000);
    QCOMPARE (abort.count (), 0);
    QTRY_VERIFY_WITH_TIMEOUT (hasLogContaining (logs, "payload exceeds"), 1000);
  }

  void rejectsOverlongIncompleteHeader ()
  {
    QSignalSpy start {interface_.get (), &MMTTYIF::app_start_tx};
    QSignalSpy logs {interface_.get (), &MMTTYIF::log_message};

    QByteArray header {"<"};
    header += QByteArray (maximumHeaderBytes, 'A');
    QVERIFY (writeToInterface (header));
    QTRY_VERIFY_WITH_TIMEOUT (hasLogContaining (logs, "header exceeds"), 1000);

    QVERIFY (writeToInterface ("<XMIT:2>ON"));
    QTRY_COMPARE_WITH_TIMEOUT (start.count (), 1, 1000);
  }

  void rejectsOverlongTerminatedHeader ()
  {
    QSignalSpy start {interface_.get (), &MMTTYIF::app_start_tx};
    QSignalSpy logs {interface_.get (), &MMTTYIF::log_message};

    QByteArray header {"<"};
    header += QByteArray (maximumHeaderBytes - 1, 'A');
    header += '>';
    QVERIFY (writeToInterface (header));
    QTRY_VERIFY_WITH_TIMEOUT (hasLogContaining (logs, "header exceeds"), 1000);

    QVERIFY (writeToInterface ("<XMIT:2>ON"));
    QTRY_COMPARE_WITH_TIMEOUT (start.count (), 1, 1000);
  }

private:
  static bool hasLogContaining (QSignalSpy const& logs, QString const& text)
  {
    for (auto const& arguments : logs) {
      if (arguments.at (0).toString ().contains (text)) return true;
    }
    return false;
  }

  bool writeToInterface (QByteArray const& data)
  {
    if (!peer_ || peer_->write (data) != data.size ()) return false;
    return !peer_->bytesToWrite () || peer_->waitForBytesWritten (1000);
  }

  std::unique_ptr<QTcpServer> server_;
  std::unique_ptr<MMTTYIF> interface_;
  QTcpSocket * peer_ {nullptr};
};

QTEST_GUILESS_MAIN (TestMMTTYIF)

#include "test_mmttyif.moc"
