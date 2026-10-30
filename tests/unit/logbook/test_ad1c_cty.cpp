#include "wsjtx_config.h"

#include <QtTest>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <memory>
#include <sstream>
#include <boost/log/utility/setup/console.hpp>

#include "Configuration.cpp"
#include "logbook/AD1CCty.hpp"
#include "widgets/itoneAndicw.h"

int volatile itone[MAX_NUM_SYMBOLS] {};
float gran () {return 0;}

class LogCapture
{
public:
  LogCapture () : sink_ {boost::log::add_console_log (output_, boost::log::keywords::format = "%Message%")} {}
  ~LogCapture () {boost::log::core::get ()->remove_sink (sink_);}
  QString text () const {return QString::fromStdString (output_.str ());}

private:
  std::ostringstream output_;
  boost::shared_ptr<boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend>> sink_;
};

class TestAD1CCty final : public QObject
{
  Q_OBJECT

  QTemporaryDir settings_dir_;
  QNetworkAccessManager network_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Configuration> configuration_;

private Q_SLOTS:
  void initTestCase ();
  void cleanupTestCase ();
  void init ();
  void cleanup ();
  void loads_bundled_country_file ();
  void ignores_truncated_final_entity ();
  void survives_country_file_damage ();
  void bounds_diagnostic_examples ();
  void recovers_at_next_entity_header ();
  void accepts_optional_commas_and_whitespace ();
  void handles_missing_us_entity_data ();
  void handles_missing_us_entity ();
  void keeps_prefix_with_invalid_override ();
  void accepts_boundary_entity_fields ();
  void rejects_invalid_entity_fields_data ();
  void rejects_invalid_entity_fields ();
  void applies_valid_overrides ();
  void handles_invalid_overrides_data ();
  void handles_invalid_overrides ();
  void loads_bundled_grid_file ();
  void bounds_grid_prefixes_and_indices ();
  void discards_incomplete_grid_blocks ();
  void requires_two_digit_grid_lookup ();
  void ignores_duplicate_grid_blocks ();
  void accepts_grid_line_separators ();

private:
  QString data_directory () const;
  bool write_data_file (QString const& name, QByteArray const& contents);
};

QString TestAD1CCty::data_directory () const
{
  return QStandardPaths::writableLocation (QStandardPaths::DataLocation);
}

bool TestAD1CCty::write_data_file (QString const& name, QByteArray const& contents)
{
  QFile file {QDir {data_directory ()}.filePath (name)};
  if (!file.open (QIODevice::WriteOnly | QIODevice::Text)) return false;
  return file.write (contents) == contents.size ();
}

void TestAD1CCty::initTestCase ()
{
  register_types ();
  QStandardPaths::setTestModeEnabled (true);
  QCoreApplication::setOrganizationName ("WSJT-X");
  QCoreApplication::setApplicationName ("test_ad1c_cty");
  QVERIFY (settings_dir_.isValid ());
  QVERIFY (QDir {}.mkpath (data_directory ()));
}

void TestAD1CCty::cleanupTestCase ()
{
  configuration_.reset ();
  QDir {data_directory ()}.removeRecursively ();
}

void TestAD1CCty::init ()
{
  QVERIFY (write_data_file ("cty.dat", QByteArray {
    "Baseline Entity:5:8:NA:37:-96:-5:B\n"
    "=BASELINE;\n"}));
  settings_.reset (new QSettings {settings_dir_.filePath ("settings.ini"), QSettings::IniFormat});
  settings_->clear ();
  configuration_.reset (new Configuration {&network_, QDir {settings_dir_.path ()}, settings_.get (), nullptr});
}

void TestAD1CCty::cleanup ()
{
  configuration_.reset ();
  settings_.reset ();
  QFile::remove (QDir {data_directory ()}.filePath ("cty.dat"));
  QFile::remove (QDir {data_directory ()}.filePath ("grid.dat"));
}

void TestAD1CCty::loads_bundled_country_file ()
{
  QFile source {QFINDTESTDATA ("../../../cty.dat")};
  QVERIFY (source.open (QIODevice::ReadOnly | QIODevice::Text));
  QVERIFY (write_data_file ("cty.dat", source.readAll ()));

  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  QCOMPARE (countries.lookup ("K1ABC").entity_name, QString {"United States"});
  QCOMPARE (countries.version (), QString {"VER20250115"});
  QVERIFY2 (!log.text ().contains ("malformed"), qPrintable (log.text ()));
}

void TestAD1CCty::recovers_at_next_entity_header ()
{
  QVERIFY (write_data_file ("cty.dat", QByteArray {
    "Incomplete:5:8:NA:37:-96:-5:I\n=INCOMPLETE,\n"
    "Invalid:5:999:NA:37:-96:-5:B\n=BAD;\n"
    "Good:14:28:EU:50:-10:-1:G\n=GOOD1,=GOOD2;\n"}));
  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  QVERIFY (countries.lookup ("INCOMPLETE").entity_name.isEmpty ());
  QVERIFY (countries.lookup ("BAD").entity_name.isEmpty ());
  QCOMPARE (countries.lookup ("GOOD1").entity_name, QString {"Good"});
  QCOMPARE (countries.lookup ("GOOD2").entity_name, QString {"Good"});
  QVERIFY (log.text ().contains ("line 1: incomplete entity"));
  QVERIFY (log.text ().contains ("line 3: invalid entity ITU zone"));
}

void TestAD1CCty::accepts_optional_commas_and_whitespace ()
{
  QVERIFY (write_data_file ("cty.dat", QByteArray {
    "Good:14:28:eu:50:-10:-1:G/a\n"
    "  =FIRST\n\n  =SECOND,\n  =THIRD;  \n"}));
  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  for (auto const * call : {"FIRST", "SECOND", "THIRD"})
    QCOMPARE (countries.lookup (call).entity_name, QString {"Good"});
  QVERIFY2 (!log.text ().contains ("malformed"), qPrintable (log.text ()));
}

void TestAD1CCty::handles_missing_us_entity_data ()
{
  QTest::addColumn<QByteArray> ("us_header");
  QTest::newRow ("absent") << QByteArray {};
  QTest::newRow ("invalid") << QByteArray {"United States:5:999:NA:37:-96:-5:K\nK,W;\n"};
}

void TestAD1CCty::handles_missing_us_entity ()
{
  QFETCH (QByteArray, us_header);
  QVERIFY (write_data_file ("cty.dat", us_header
    + "Guantanamo:8:11:NA:20:75:5:KG4\nKG4;\n"));
  AD1CCty countries {configuration_.get ()};
  QVERIFY (countries.lookup ("KG4ABC").entity_name.isEmpty ());
  QCOMPARE (countries.lookup ("KG4AA").entity_name, QString {"Guantanamo"});
}

void TestAD1CCty::keeps_prefix_with_invalid_override ()
{
  QVERIFY (write_data_file ("cty.dat", QByteArray {
    "Broad:5:8:NA:37:-96:-5:B\nA;\n"
    "Specific:14:28:EU:50:-10:-1:S\nAB(40)[999]~5.75~;\n"}));
  AD1CCty countries {configuration_.get ()};
  auto const record = countries.lookup ("AB1XYZ");
  QCOMPARE (record.entity_name, QString {"Specific"});
  QCOMPARE (record.CQ_zone, 40);
  QCOMPARE (record.ITU_zone, 28);
  QCOMPARE (record.UTC_offset, 20700);
}

void TestAD1CCty::ignores_truncated_final_entity ()
{
  auto const contents = QByteArray {
    "Good Entity:5:8:NA:37:-96:-5:G\n"
    "=GOOD;\n"
    "Incomplete Entity:14:28:EU:50:-10:-1:I\n"
    "=INCOMPLETE"};
  QVERIFY (write_data_file ("cty.dat", contents));

  AD1CCty countries {configuration_.get ()};
  QCOMPARE (countries.lookup ("GOOD").entity_name, QString {"Good Entity"});
  QVERIFY (countries.lookup ("INCOMPLETE").entity_name.isEmpty ());
}

void TestAD1CCty::accepts_boundary_entity_fields ()
{
  auto const contents = QByteArray {
    "Minimum Entity:1:1:AN:-90:-180:-14:M\n"
    "=MINIMUM;\n"
    "Maximum Entity:40:90:EU:90:180:14:X\n"
    "=MAXIMUM;\n"
    "Fraction Entity:14:28:NA:50:-10:5.75:F\n"
    "=FRACTION;\n"};
  QVERIFY (write_data_file ("cty.dat", contents));

  AD1CCty countries {configuration_.get ()};
  auto minimum = countries.lookup ("MINIMUM");
  QCOMPARE (minimum.CQ_zone, 1);
  QCOMPARE (minimum.ITU_zone, 1);
  QCOMPARE (minimum.latitude, -90.f);
  QCOMPARE (minimum.longtitude, -180.f);
  QCOMPARE (minimum.UTC_offset, -14 * 60 * 60);

  auto maximum = countries.lookup ("MAXIMUM");
  QCOMPARE (maximum.CQ_zone, 40);
  QCOMPARE (maximum.ITU_zone, 90);
  QCOMPARE (maximum.latitude, 90.f);
  QCOMPARE (maximum.longtitude, 180.f);
  QCOMPARE (maximum.UTC_offset, 14 * 60 * 60);
  QCOMPARE (countries.lookup ("FRACTION").UTC_offset, 5 * 60 * 60 + 45 * 60);
}

void TestAD1CCty::survives_country_file_damage ()
{
  QFile source {QFINDTESTDATA ("../../../cty.dat")};
  QVERIFY (source.open (QIODevice::ReadOnly));
  auto const original = source.readAll ();
  for (int i = 0; i < 16; ++i)
    {
      auto const offset = (original.size () - 1) * i / 15;
      for (bool truncate : {true, false})
        {
          auto damaged = truncate ? original.left (offset) : original;
          if (!truncate) damaged[offset] = char (damaged[offset] ^ 0x40);
          QVERIFY (write_data_file ("cty.dat", damaged));
          AD1CCty countries {configuration_.get ()};
          for (auto const * call : {"K1ABC", "KG4ABC", "RC4I", "RA9J", "EF6ABC", "WH7KABC"})
            {
              auto const record = countries.lookup (call);
              QVERIFY (record.CQ_zone >= 0 && record.CQ_zone <= 40);
              QVERIFY (record.ITU_zone >= 0 && record.ITU_zone <= 90);
              QVERIFY (record.UTC_offset >= -50400 && record.UTC_offset <= 50400);
            }
        }
    }
}

void TestAD1CCty::bounds_diagnostic_examples ()
{
  QByteArray contents;
  for (int i = 0; i < 8; ++i) contents += "Bad:5:999:NA:37:-96:-5:B\nB;\n";
  QVERIFY (write_data_file ("cty.dat", contents));
  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  QVERIFY (log.text ().contains ("8 malformed records/fields"));
  QVERIFY (log.text ().contains ("line 9: invalid entity ITU zone"));
  QVERIFY (!log.text ().contains ("line 11:"));
}

void TestAD1CCty::rejects_invalid_entity_fields_data ()
{
  QTest::addColumn<QByteArray> ("header");

  QTest::newRow ("continent") << QByteArray {"Bad:14:28:XX:50:-10:-1:B"};
  QTest::newRow ("cq-text") << QByteArray {"Bad:text:28:EU:50:-10:-1:B"};
  QTest::newRow ("cq-low") << QByteArray {"Bad:0:28:EU:50:-10:-1:B"};
  QTest::newRow ("cq-high") << QByteArray {"Bad:41:28:EU:50:-10:-1:B"};
  QTest::newRow ("itu-low") << QByteArray {"Bad:14:0:EU:50:-10:-1:B"};
  QTest::newRow ("itu-high") << QByteArray {"Bad:14:91:EU:50:-10:-1:B"};
  QTest::newRow ("latitude-low") << QByteArray {"Bad:14:28:EU:-91:-10:-1:B"};
  QTest::newRow ("latitude-high") << QByteArray {"Bad:14:28:EU:91:-10:-1:B"};
  QTest::newRow ("latitude-nan") << QByteArray {"Bad:14:28:EU:nan:-10:-1:B"};
  QTest::newRow ("longitude-low") << QByteArray {"Bad:14:28:EU:50:-181:-1:B"};
  QTest::newRow ("longitude-high") << QByteArray {"Bad:14:28:EU:50:181:-1:B"};
  QTest::newRow ("longitude-inf") << QByteArray {"Bad:14:28:EU:50:inf:-1:B"};
  QTest::newRow ("utc-low") << QByteArray {"Bad:14:28:EU:50:-10:-15:B"};
  QTest::newRow ("utc-high") << QByteArray {"Bad:14:28:EU:50:-10:15:B"};
  QTest::newRow ("utc-overflow") << QByteArray {"Bad:14:28:EU:50:-10:1e30:B"};
  QTest::newRow ("utc-nan") << QByteArray {"Bad:14:28:EU:50:-10:nan:B"};
  QTest::newRow ("utc-inf") << QByteArray {"Bad:14:28:EU:50:-10:inf:B"};
}

void TestAD1CCty::rejects_invalid_entity_fields ()
{
  QFETCH (QByteArray, header);
  auto const contents = QByteArray {
    "Good Entity:5:8:NA:37:-96:-5:G\n"
    "=GOOD;\n"} + header + "\n=BAD;\n"
    "Last Entity:14:28:EU:50:-10:-1:L\n"
    "=LAST;\n";
  QVERIFY (write_data_file ("cty.dat", contents));

  AD1CCty countries {configuration_.get ()};
  QCOMPARE (countries.lookup ("GOOD").entity_name, QString {"Good Entity"});
  QVERIFY (countries.lookup ("BAD").entity_name.isEmpty ());
  QCOMPARE (countries.lookup ("LAST").entity_name, QString {"Last Entity"});
}

void TestAD1CCty::applies_valid_overrides ()
{
  auto const contents = QByteArray {
    "Override Entity:5:8:NA:37:-96:-5:O\n"
    "=OVERRIDE(40)[90]<90/180>{SA}~14~,=FRACTION~-5.75~;\n"};
  QVERIFY (write_data_file ("cty.dat", contents));

  AD1CCty countries {configuration_.get ()};
  auto override = countries.lookup ("OVERRIDE");
  QCOMPARE (override.CQ_zone, 40);
  QCOMPARE (override.ITU_zone, 90);
  QCOMPARE (override.continent, AD1CCty::Continent::SA);
  QCOMPARE (override.latitude, 90.f);
  QCOMPARE (override.longtitude, 180.f);
  QCOMPARE (override.UTC_offset, 14 * 60 * 60);
  QCOMPARE (countries.lookup ("FRACTION").UTC_offset, -(5 * 60 * 60 + 45 * 60));
}

void TestAD1CCty::handles_invalid_overrides_data ()
{
  QTest::addColumn<QByteArray> ("override");
  QTest::addColumn<bool> ("recoverable");

  QTest::newRow ("continent") << QByteArray {"{XX}"} << true;
  QTest::newRow ("cq-number") << QByteArray {"(not-a-zone)"} << true;
  QTest::newRow ("cq-low") << QByteArray {"(0)"} << true;
  QTest::newRow ("cq-high") << QByteArray {"(41)"} << true;
  QTest::newRow ("itu-low") << QByteArray {"[0]"} << true;
  QTest::newRow ("itu-high") << QByteArray {"[91]"} << true;
  QTest::newRow ("coordinate-pair") << QByteArray {"<12>"} << true;
  QTest::newRow ("latitude-low") << QByteArray {"<-91/0>"} << true;
  QTest::newRow ("longitude-high") << QByteArray {"<0/181>"} << true;
  QTest::newRow ("coordinate-nan") << QByteArray {"<nan/0>"} << true;
  QTest::newRow ("utc-low") << QByteArray {"~-15~"} << true;
  QTest::newRow ("utc-high") << QByteArray {"~15~"} << true;
  QTest::newRow ("utc-overflow") << QByteArray {"~1e30~"} << true;
  QTest::newRow ("utc-nan") << QByteArray {"~nan~"} << true;
  QTest::newRow ("missing-cq-close") << QByteArray {"(5"} << false;
  QTest::newRow ("missing-itu-close") << QByteArray {"[28"} << false;
  QTest::newRow ("missing-coordinate-close") << QByteArray {"<12/34"} << false;
  QTest::newRow ("missing-continent-close") << QByteArray {"{NA"} << false;
  QTest::newRow ("missing-utc-close") << QByteArray {"~5"} << false;
  QTest::newRow ("stray-cq-close") << QByteArray {")"} << false;
  QTest::newRow ("duplicate-cq") << QByteArray {"(5)(6)"} << false;
  QTest::newRow ("duplicate-utc") << QByteArray {"~5~~6~"} << false;
  QTest::newRow ("trailing-junk") << QByteArray {"(5)junk"} << false;
}

void TestAD1CCty::handles_invalid_overrides ()
{
  QFETCH (QByteArray, override);
  QFETCH (bool, recoverable);
  auto const contents = QByteArray {
    "Version Entity:5:8:NA:37:-96:-5:V\n"
    "=VERSION"} + override + ",=GOOD;\n";
  QVERIFY (write_data_file ("cty.dat", contents));

  AD1CCty countries {configuration_.get ()};
  auto const record = countries.lookup ("VERSION");
  QCOMPARE (!record.entity_name.isEmpty (), recoverable);
  if (recoverable)
    {
      QCOMPARE (record.CQ_zone, 5);
      QCOMPARE (record.ITU_zone, 8);
      QCOMPARE (record.continent, AD1CCty::Continent::NA);
      QCOMPARE (record.latitude, 37.f);
      QCOMPARE (record.longtitude, -96.f);
      QCOMPARE (record.UTC_offset, -18000);
    }
  QCOMPARE (countries.lookup ("GOOD").entity_name, QString {"Version Entity"});
}

void TestAD1CCty::bounds_grid_prefixes_and_indices ()
{
  QByteArray contents;
  for (int i = 0; i < 26; ++i)
    {
      auto const prefix = QByteArray {"A"} + static_cast<char> ('A' + i);
      contents += prefix + "<\n";
      if (i == 0)
        {
          contents += "-1:NEGATIVE,\n";
          contents += "100:TOO_LARGE,\n";
          contents += "A0:NONNUMERIC,\n";
          contents += "0:TOO_SHORT,\n";
        }
      contents += "00:STATE" + QByteArray::number (i) + ">\n";
    }
  QVERIFY (write_data_file ("grid.dat", contents));

  AD1CCty countries {configuration_.get ()};
  QCOMPARE (countries.findState ("AA00"), QString {"STATE0"});
  QCOMPARE (countries.findState ("AA99"), QString {"**"});
  QCOMPARE (countries.findState ("AAAA"), QString {"**"});
  QCOMPARE (countries.findState ("AA-1"), QString {"**"});
  QCOMPARE (countries.findState ("AY00"), QString {"STATE24"});
  QCOMPARE (countries.findState ("AZ00"), QString {"**"});
}

void TestAD1CCty::loads_bundled_grid_file ()
{
  QFile source {QFINDTESTDATA ("../../../grid.dat")};
  QVERIFY (source.open (QIODevice::ReadOnly | QIODevice::Text));
  QVERIFY (write_data_file ("grid.dat", source.readAll ()));

  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  QCOMPARE (countries.findState ("AL91"), QString {"HI"});
  QCOMPARE (countries.findState ("BK19"), QString {"HI"});
  QVERIFY (source.seek (0));
  QString prefix;
  int blocks = 0;
  int cells = 0;
  while (!source.atEnd ())
    {
      auto const line = QString::fromUtf8 (source.readLine ()).trimmed ();
      if (line.contains ('<'))
        {
          prefix = line.section ('<', 0, 0);
          ++blocks;
        }
      if (!line.contains (':')) continue;
      auto const index = line.section (':', 0, 0).trimmed ();
      auto const state = line.section (':', 1).section (',', 0, 0).section ('>', 0, 0).trimmed ();
      QCOMPARE (countries.findState (prefix + index), state);
      ++cells;
    }
  QCOMPARE (blocks, 21);
  QCOMPARE (cells, 695);
  QVERIFY2 (!log.text ().contains ("malformed"), qPrintable (log.text ()));
}

void TestAD1CCty::discards_incomplete_grid_blocks ()
{
  auto const contents = QByteArray {
    "AA<\n"
    "00:DISCARDED,\n"
    "BB<\n"
    "01:KEPT>\n"
    "CC<\n"
    "02:INCOMPLETE,\n"
    "dD<\n"
    "03:INVALID>\n"
    "EE<\n"
    "04:EOF-INCOMPLETE,\n"};
  QVERIFY (write_data_file ("grid.dat", contents));

  AD1CCty countries {configuration_.get ()};
  QCOMPARE (countries.findState ("AA00"), QString {"**"});
  QCOMPARE (countries.findState ("BB01"), QString {"KEPT"});
  QCOMPARE (countries.findState ("CC02"), QString {"**"});
  QCOMPARE (countries.findState ("dD03"), QString {"**"});
  QCOMPARE (countries.findState ("EE04"), QString {"**"});
}

void TestAD1CCty::requires_two_digit_grid_lookup ()
{
  QVERIFY (write_data_file ("grid.dat", QByteArray {
    "FN<\n"
    "03:STATE>\n"}));

  AD1CCty countries {configuration_.get ()};
  QCOMPARE (countries.findState ("FN03"), QString {"STATE"});
  QCOMPARE (countries.findState ("FN03AB"), QString {"STATE"});
  QCOMPARE (countries.findState ("FN3"), QString {"**"});
  QCOMPARE (countries.findState ("FN+3"), QString {"**"});
  QCOMPARE (countries.findState ("FN 3"), QString {"**"});
  QCOMPARE (countries.findState ("FNAA"), QString {"**"});
}

void TestAD1CCty::ignores_duplicate_grid_blocks ()
{
  QByteArray contents {"AA<\n00:FIRST>\nAA<\n01:SECOND>\n"};
  for (int i = 1; i < 25; ++i)
    contents += QByteArray {"A"} + char ('A' + i) + "<\n00:KEPT>\n";
  QVERIFY (write_data_file ("grid.dat", contents));
  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  QCOMPARE (countries.findState ("AA00"), QString {"FIRST"});
  QCOMPARE (countries.findState ("AA01"), QString {"**"});
  QCOMPARE (countries.findState ("AY00"), QString {"KEPT"});
  QVERIFY (log.text ().contains ("line 3: duplicate grid prefix"));
}

void TestAD1CCty::accepts_grid_line_separators ()
{
  QVERIFY (write_data_file ("grid.dat", QByteArray {"FN<\n00: CA\n01: NV\n>\n"}));
  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  QCOMPARE (countries.findState ("FN00"), QString {"CA"});
  QCOMPARE (countries.findState ("FN01"), QString {"NV"});
  QVERIFY2 (!log.text ().contains ("malformed"), qPrintable (log.text ()));
}
QTEST_MAIN (TestAD1CCty)

#include "test_ad1c_cty.moc"
