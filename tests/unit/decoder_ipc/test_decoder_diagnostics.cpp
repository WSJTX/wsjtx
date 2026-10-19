#include <QtTest>
#include <QVector>

#include <sstream>
#include <boost/make_shared.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/keywords/channel.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>

#include "DecoderDiagnostics.hpp"
#include "Logger.hpp"
#include "WSJTXLogging.hpp"

class TestDecoderDiagnostics final : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void fragmentedLines ();
  void finalPartialLine ();
  void boundedRetention ();
  void reset ();
  void defaultLoggingVisibility ();
};

void TestDecoderDiagnostics::fragmentedLines ()
{
  QVector<QByteArray> lines;
  DecoderDiagnostics diagnostics {[&] (QByteArray const& line) { lines.append (line); }};
  diagnostics.append ("first\r");
  QVERIFY (lines.isEmpty ());
  diagnostics.append ("\nsec");
  QCOMPARE (lines, QVector<QByteArray> ({"first"}));
  diagnostics.append ("ond\n\nthird\n");
  QCOMPARE (lines, QVector<QByteArray> ({"first", "second", "third"}));
  QCOMPARE (diagnostics.tail (), QByteArray {"first\r\nsecond\n\nthird\n"});
}

void TestDecoderDiagnostics::finalPartialLine ()
{
  QVector<QByteArray> lines;
  DecoderDiagnostics diagnostics {[&] (QByteArray const& line) { lines.append (line); }};
  diagnostics.append ("complete\nlast fragment");
  QCOMPARE (lines.size (), 1);
  diagnostics.finish ();
  QCOMPARE (lines, QVector<QByteArray> ({"complete", "last fragment"}));
  diagnostics.finish ();
  QCOMPARE (lines.size (), 2);
  QCOMPARE (diagnostics.tail (), QByteArray {"complete\nlast fragment"});
}

void TestDecoderDiagnostics::boundedRetention ()
{
  QVector<QByteArray> lines;
  DecoderDiagnostics diagnostics {[&] (QByteArray const& line) { lines.append (line); }};
  QByteArray const large (DecoderDiagnostics::retentionLimit * 3, 'x');
  diagnostics.append (large);
  QCOMPARE (diagnostics.tail ().size (), DecoderDiagnostics::retentionLimit);
  QVERIFY (lines.isEmpty ());
  diagnostics.append ("recent diagnostic");
  diagnostics.finish ();
  QCOMPARE (lines.size (), 1);
  QCOMPARE (lines[0].size (), DecoderDiagnostics::retentionLimit);
  QVERIFY (lines[0].endsWith ("recent diagnostic"));
  QCOMPARE (diagnostics.tail (), (large + "recent diagnostic").right (DecoderDiagnostics::retentionLimit));
  diagnostics.append ("\nnext\n");
  QCOMPARE (lines.size (), 2);
  QCOMPARE (lines[1], QByteArray {"next"});
  QCOMPARE (diagnostics.tail (), (large + "recent diagnostic\nnext\n").right (DecoderDiagnostics::retentionLimit));
}

void TestDecoderDiagnostics::reset ()
{
  QVector<QByteArray> lines;
  DecoderDiagnostics diagnostics {[&] (QByteArray const& line) { lines.append (line); }};
  diagnostics.append ("previous process fragment");
  diagnostics.reset ();
  QVERIFY (diagnostics.tail ().isEmpty ());
  diagnostics.finish ();
  QVERIFY (lines.isEmpty ());
  diagnostics.append ("replacement process\n");
  QCOMPARE (lines, QVector<QByteArray> ({"replacement process"}));
  QCOMPARE (diagnostics.tail (), QByteArray {"replacement process\n"});
}

void TestDecoderDiagnostics::defaultLoggingVisibility ()
{
  namespace logging = boost::log;
  using severity = logging::trivial::severity_level;
  using sink_type = logging::sinks::synchronous_sink<logging::sinks::text_ostream_backend>;
  auto output = boost::make_shared<std::ostringstream> ();
  auto sink = boost::make_shared<sink_type> ();
  sink->locked_backend ()->add_stream (output);
  sink->set_filter (WSJTXLogging::default_filter ());
  sink->set_formatter (logging::expressions::stream << logging::expressions::smessage);
  auto core = logging::core::get ();
  core->add_sink (sink);
  logging::sources::severity_channel_logger_mt<severity>
    system {logging::keywords::channel = "SYSLOG"},
    rig {logging::keywords::channel = "RIGCTRL"},
    data {logging::keywords::channel = "DATALOG"},
    other {logging::keywords::channel = "OTHER"};
  logDecoderDiagnostic ("decoder warning");
  BOOST_LOG_SEV (system, logging::trivial::warning) << "hidden system warning";
  BOOST_LOG_SEV (system, logging::trivial::error) << "system error";
  BOOST_LOG_SEV (rig, logging::trivial::warning) << "rig warning";
  BOOST_LOG_SEV (data, logging::trivial::warning) << "hidden data warning";
  BOOST_LOG_SEV (data, logging::trivial::error) << "data error";
  BOOST_LOG_SEV (other, logging::trivial::fatal) << "other fatal";
  core->flush ();
  core->remove_sink (sink);
  QCOMPARE (QString::fromStdString (output->str ()),
            QString {"decoder warning\nsystem error\nrig warning\ndata error\nother fatal\n"});
}

QTEST_GUILESS_MAIN (TestDecoderDiagnostics)
#include "test_decoder_diagnostics.moc"
