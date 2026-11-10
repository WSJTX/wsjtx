#include "wsjtx_config.h"

#include <QtTest>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <memory>
#include <cmath>
#include <sstream>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/expressions.hpp>

#include "Configuration.cpp"
#include "logbook/AD1CCty.hpp"
#include "widgets/itoneAndicw.h"

int volatile itone[MAX_NUM_SYMBOLS] {};
float gran () {return 0;}

class LogCapture
{
public:
  LogCapture () : sink_ {boost::log::add_console_log (output_, boost::log::keywords::format = "%Message%")}
  {
    sink_->set_filter (boost::log::trivial::severity >= boost::log::trivial::warning);
  }
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
  void ignores_truncated_final_entity ();
  void survives_country_file_damage ();
  void bounds_diagnostic_examples ();
  void diagnoses_conflicting_aliases ();
  void checks_prefix_consistency_without_exact_call_shadowing ();
  void diagnoses_inconsistent_wae_parents_data ();
  void diagnoses_inconsistent_wae_parents ();
  void recovers_at_next_entity_header ();
  void accepts_optional_commas_and_whitespace ();
  void handles_missing_us_entity_data ();
  void handles_missing_us_entity ();
  void keeps_prefix_with_invalid_override ();
  void separates_exact_calls_and_prefixes_data ();
  void separates_exact_calls_and_prefixes ();
  void separates_dxcc_and_wae_aliases_data ();
  void separates_dxcc_and_wae_aliases ();
  void selects_identity_without_changing_geography ();
  void falls_back_to_wae_identity ();
  void resolves_operating_prefix_in_both_views ();
  void resolves_wae_event_calls_from_agreed_parent ();
  void derives_parents_from_shared_exacts_or_majority ();
  void derives_historical_shetland_parent ();
  void resolves_lookup_exceptions ();
  void validates_country_file_aliases_data ();
  void validates_country_file_aliases ();
  void accepts_boundary_entity_fields ();
  void rejects_invalid_entity_fields_data ();
  void rejects_invalid_entity_fields ();
  void applies_valid_overrides ();
  void handles_invalid_overrides_data ();
  void handles_invalid_overrides ();
  void loads_bundled_grid_file ();
  void bounds_grid_prefixes_and_indices ();
  void recovers_unterminated_grid_blocks ();
  void bounds_recovered_grid_blocks ();
  void keeps_first_recovered_grid_block ();
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
  settings_.reset (new QSettings {settings_dir_.filePath ("settings.ini"), QSettings::IniFormat});
  settings_->clear ();
  configuration_.reset (new Configuration {&network_, QDir {settings_dir_.path ()}, settings_.get (), nullptr});
}

void TestAD1CCty::cleanupTestCase ()
{
  configuration_.reset ();
  settings_.reset ();
  QDir {data_directory ()}.removeRecursively ();
}

void TestAD1CCty::init ()
{
  QVERIFY (write_data_file ("cty.dat", QByteArray {
    "Baseline Entity:5:8:NA:37:-96:-5:B\n"
    "=BASELINE;\n"}));
}

void TestAD1CCty::cleanup ()
{
  QFile::remove (QDir {data_directory ()}.filePath ("cty.dat"));
  QFile::remove (QDir {data_directory ()}.filePath ("grid.dat"));
}

void TestAD1CCty::recovers_at_next_entity_header ()
{
  QVERIFY (write_data_file ("cty.dat", QByteArray {
    "Incomplete:5:8:NA:37:-96:-5:I\n=INCOMPLETE,\n"
    "Invalid:5:999:NA:37:-96:-5:B\n=BAD;\n"
    "Good:14:28:EU:50:-10:-1:G\n=GOOD1,=GOOD2;\n"}));
  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  QVERIFY (countries.lookup ("INCOMPLETE").wae.name.isEmpty ());
  QVERIFY (countries.lookup ("BAD").wae.name.isEmpty ());
  QCOMPARE (countries.lookup ("GOOD1").wae.name, QString {"Good"});
  QCOMPARE (countries.lookup ("GOOD2").wae.name, QString {"Good"});
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
    QCOMPARE (countries.lookup (call).wae.name, QString {"Good"});
  QVERIFY2 (log.text ().isEmpty (), qPrintable (log.text ()));
}

void TestAD1CCty::handles_missing_us_entity_data ()
{
  QTest::addColumn<QByteArray> ("us_header");
  QTest::newRow ("absent") << QByteArray {};
  QTest::newRow ("invalid") << QByteArray {"United States:5:999:NA:37:-96:-5:K\nK,W;\n"};
  QTest::newRow ("wae-only") << QByteArray {"United States:5:8:NA:37:-96:-5:*K\nK,W;\n"};
}

void TestAD1CCty::handles_missing_us_entity ()
{
  QFETCH (QByteArray, us_header);
  QVERIFY (write_data_file ("cty.dat", us_header
    + "Guantanamo:8:11:NA:20:75:5:KG4\nKG4;\n"));
  AD1CCty countries {configuration_.get ()};
  QVERIFY (countries.lookup ("KG4ABC").wae.name.isEmpty ());
  QVERIFY (countries.lookup ("KG4ABC").dxcc.name.isEmpty ());
  QCOMPARE (countries.lookup ("KG4AA").wae.name, QString {"Guantanamo"});
}

void TestAD1CCty::keeps_prefix_with_invalid_override ()
{
  QVERIFY (write_data_file ("cty.dat", QByteArray {
    "Broad:5:8:NA:37:-96:-5:B\nA;\n"
    "Specific:14:28:EU:50:-10:-1:S\nAB(40)[999]~5.75~;\n"}));
  AD1CCty countries {configuration_.get ()};
  auto const record = countries.lookup ("AB1XYZ");
  QCOMPARE (record.wae.name, QString {"Specific"});
  QCOMPARE (record.CQ_zone, 40);
  QCOMPARE (record.ITU_zone, 28);
  QCOMPARE (record.UTC_offset, 20700);
}

void TestAD1CCty::separates_exact_calls_and_prefixes_data ()
{
  QTest::addColumn<bool> ("exact_first");
  QTest::newRow ("exact-first") << true;
  QTest::newRow ("prefix-first") << false;
}

void TestAD1CCty::separates_exact_calls_and_prefixes ()
{
  QFETCH (bool, exact_first);
  QByteArray const exact {"Exact:14:37:EU:40:4:-1:E\n=EF6;\n"};
  QByteArray const broad {"Prefix:14:37:EU:40:4:-1:P\nEF6;\n"};
  QVERIFY (write_data_file ("cty.dat", exact_first ? exact + broad : broad + exact));
  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  QCOMPARE (countries.lookup ("EF6").wae.name, QString {"Exact"});
  QCOMPARE (countries.lookup ("EF6ABC").wae.name, QString {"Prefix"});
  QVERIFY2 (log.text ().isEmpty (), qPrintable (log.text ()));
}

void TestAD1CCty::separates_dxcc_and_wae_aliases_data ()
{
  QTest::addColumn<bool> ("wae_first");
  QTest::newRow ("dxcc-first") << false;
  QTest::newRow ("wae-first") << true;
}

void TestAD1CCty::separates_dxcc_and_wae_aliases ()
{
  QFETCH (bool, wae_first);
  QByteArray const dxcc {"DXCC:14:28:EU:50:-10:-1:D\n=TEST[29];\n"};
  QByteArray const wae {"WAE:14:28:EU:50:-10:-1:*D/w\n=TEST[30];\n"};
  QVERIFY (write_data_file ("cty.dat", wae_first ? wae + dxcc : dxcc + wae));
  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  auto const record = countries.lookup ("TEST");
  QCOMPARE (record.wae.name, QString {"WAE"});
  QCOMPARE (record.dxcc.name, QString {"DXCC"});
  QVERIFY (record.wae.WAE_only);
  QVERIFY (!record.dxcc.WAE_only);
  QCOMPARE (record.ITU_zone, 30);
  QVERIFY2 (log.text ().isEmpty (), qPrintable (log.text ()));
}

void TestAD1CCty::selects_identity_without_changing_geography ()
{
  QVERIFY (write_data_file ("cty.dat", QByteArray {
    "Italy:15:28:EU:42:-12:-1:I\nI;\n"
    "African Italy:33:37:AF:35:-12:-1:*IG9\nIG9,=IG9OVERRIDE(34)[38]<36/-13>~2~;\n"
    "Asiatic Turkey:20:39:AS:39:-35:-2:TA\nTA;\n"
    "European Turkey:20:39:EU:41:-29:-2:*TA1\nTA1;\n"
    "Asiatic Russia:17:30:AS:56:-84:-7:UA9\nUA9;\n"}));
  AD1CCty countries {configuration_.get ()};
  auto const italy = countries.lookup ("IG9ABC");
  QCOMPARE (italy.identity (AD1CCty::EntityView::DXCC).name, QString {"Italy"});
  QCOMPARE (italy.identity (AD1CCty::EntityView::WAE).name, QString {"African Italy"});
  QCOMPARE (italy.continent, AD1CCty::Continent::AF);
  QCOMPARE (italy.CQ_zone, 33);
  QCOMPARE (italy.ITU_zone, 37);
  auto const overridden = countries.lookup ("IG9OVERRIDE");
  QCOMPARE (overridden.dxcc.name, QString {"Italy"});
  QCOMPARE (overridden.CQ_zone, 34);
  QCOMPARE (overridden.ITU_zone, 38);
  QCOMPARE (overridden.latitude, 36.f);
  QCOMPARE (overridden.longtitude, -13.f);
  QCOMPARE (overridden.UTC_offset, 7200);
  for (auto const * call : {"TA1ABC", "TA2ABC"})
    QCOMPARE (countries.lookup (call).display_name (AD1CCty::EntityView::DXCC), QString {"Turkey"});
  QCOMPARE (countries.lookup ("TA1ABC").display_name (AD1CCty::EntityView::WAE), QString {"EU Turkey"});
  QCOMPARE (countries.lookup ("TA1ABC").identity (AD1CCty::EntityView::DXCC).abbreviated_name, QString {"AS Turkey"});
  QCOMPARE (countries.lookup ("TA1ABC").continent, AD1CCty::Continent::EU);
  QCOMPARE (countries.lookup ("TA2ABC").display_name (AD1CCty::EntityView::WAE), QString {"AS Turkey"});
  for (auto view : {AD1CCty::EntityView::DXCC, AD1CCty::EntityView::WAE})
    QCOMPARE (countries.lookup ("UA9ABC").display_name (view), QString {"AS Russia"});
}

void TestAD1CCty::falls_back_to_wae_identity ()
{
  QVERIFY (write_data_file ("cty.dat", QByteArray {"Kosovo:15:28:EU:42:-21:-1:*Z6\nZ6;\n"}));
  AD1CCty countries {configuration_.get ()};
  auto const record = countries.lookup ("Z61ABC");
  QVERIFY (record.dxcc.name.isEmpty ());
  QVERIFY (record.dxcc.primary_prefix.isEmpty ());
  QCOMPARE (record.identity (AD1CCty::EntityView::DXCC).name, QString {"Kosovo"});
  QCOMPARE (record.identity (AD1CCty::EntityView::WAE).name, QString {"Kosovo"});
  QCOMPARE (record.continent, AD1CCty::Continent::EU);
}

void TestAD1CCty::resolves_operating_prefix_in_both_views ()
{
  QVERIFY (write_data_file ("cty.dat", QByteArray {
    "Italy:15:28:EU:42:-12:-1:I\nI,=IT9SPECIAL;\n"
    "Sicily:15:28:EU:37:-14:-1:*IT9\nIT9;\n"
    "United States:5:8:NA:37:96:5:K\nK,W;\n"}));
  AD1CCty countries {configuration_.get ()};
  for (auto const * call : {"W1A/IT9", "IT9/W1A", "W1A/IT9/P/QRP"})
    {
      auto const record = countries.lookup (call);
      QCOMPARE (record.dxcc.name, QString {"Italy"});
      QCOMPARE (record.wae.name, QString {"Sicily"});
    }
  QCOMPARE (countries.lookup ("IT9SPECIAL").wae.name, QString {"Italy"});
  QCOMPARE (countries.lookup ("IT9SPECIAL/P/QRP").wae.name, QString {"Italy"});
}

void TestAD1CCty::resolves_wae_event_calls_from_agreed_parent ()
{
  QByteArray const dxcc {
    "Italy:15:28:EU:42:-12:-1:I\nI,=IT9PARENT;\n"
    "Chile:12:14:SA:-30:71:4:CE\nCA,CE,=IT9EVENT/P,=IT9DIRECT;\n"
    "United States:5:8:NA:37:96:5:K\nK,N;\n"};
  QByteArray const wae {"Sicily:15:28:EU:37:-14:-1:*IT9\nIT9,=IT9CHU/J,=IT9CKA/CA[29],=IT9DTU/N,=IT9EVENT,=IT9DIRECT/P,=IT9PARENT;\n"};
  for (auto const& contents : {dxcc + wae, wae + dxcc})
    {
      QVERIFY (write_data_file ("cty.dat", contents));
      AD1CCty countries {configuration_.get ()};
      for (auto const * call : {"IT9CHU/J", "IT9CKA/CA", "IT9DTU/N", "IT9CKA/CA/P/QRP"})
        {
          auto const record = countries.lookup (call);
          QCOMPARE (record.dxcc.name, QString {"Italy"});
          QCOMPARE (record.wae.name, QString {"Sicily"});
        }
      QCOMPARE (countries.lookup ("IT9CKA/CA").ITU_zone, 29);
      QCOMPARE (countries.lookup ("IT9EVENT/P").dxcc.name, QString {"Chile"});
      QCOMPARE (countries.lookup ("IT9DIRECT/P/QRP").dxcc.name, QString {"Italy"});
      QCOMPARE (countries.lookup ("IT9OTHER/CA").dxcc.name, QString {"Chile"});
      QVERIFY (countries.lookup ("IT9CKA/CA/MM").dxcc.name.isEmpty ());
      QVERIFY (write_data_file ("cty.dat", dxcc + "Sicily:15:28:EU:37:-14:-1:*IT9\n=IT9CKA/CA;\n"));
      countries.reload (configuration_.get ());
      QCOMPARE (countries.lookup ("IT9CKA/CA").dxcc.name, QString {"Chile"});
    }
}

void TestAD1CCty::derives_parents_from_shared_exacts_or_majority ()
{
  QByteArray const countries {
    "Italy:15:28:EU:42:-12:-1:I\nI,=4U1TEST;\n"
    "Chile:12:14:SA:-30:71:4:CE\nCA,CE,=4U1OTHER;\n"};
  QMap<QByteArray, QString> const cases {
    {"IT9,=IT9HOME,=ZZ1,=ZZ2,=ZZ3", "Italy"},
    {"=IT9HOME", "Chile"},
    {"=4U1TEST,CE,CA", "Italy"},
    {"=4U1TEST,=4U1OTHER,IT9,=IT9HOME", "Chile"},
    {"IT9,CE,CE,CE,CE,CE,=IT9HOME,=IT9MORE", "Italy"}
  };
  for (auto i = cases.cbegin (); i != cases.cend (); ++i)
    {
      QVERIFY (write_data_file ("cty.dat", countries + "WAE:15:28:EU:37:-14:-1:*X\n" + i.key () + ",=IT9GUEST/CA;\n"));
      AD1CCty cty {configuration_.get ()};
      QVERIFY2 (cty.lookup ("IT9GUEST/CA").dxcc.name == i.value (), i.key ().constData ());
    }
  QVERIFY (write_data_file ("cty.dat", countries + "WAE:15:28:EU:37:-14:-1:*X\n=4U1TEST,=4U1EVENT/CA;\n"));
  AD1CCty cty {configuration_.get ()};
  QCOMPARE (cty.lookup ("4U1EVENT/CA").dxcc.name, QString {"Italy"});
}

void TestAD1CCty::derives_historical_shetland_parent ()
{
  // These entities from 43664a2e0 have misleading prefixes and no shared exact aliases.
  QFile source {QFINDTESTDATA ("data/cty_wae_2014.dat")};
  QVERIFY (source.open (QIODevice::ReadOnly));
  auto const contents = source.readAll ();
  QVERIFY (write_data_file ("cty.dat", contents));
  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  auto const shetland = QString::fromUtf8 (contents).section ("*GM/s:", 1);
  QRegularExpression const aliases {"=([A-Z0-9/]+)"};
  auto matches = aliases.globalMatch (shetland);
  QVERIFY (matches.hasNext ());
  while (matches.hasNext ())
    {
      auto const call = matches.next ().captured (1);
      auto const record = countries.lookup (call);
      QVERIFY2 (record.dxcc.name == "Scotland", qPrintable (call));
      QCOMPARE (record.wae.name, QString {"Shetland and Fair Isle"});
    }
  QCOMPARE (countries.lookup ("GZ1ABC").dxcc.name, QString {"England"});
  QVERIFY2 (log.text ().contains ("3 prefixes differ from DXCC parent Scotland (2Z -> G, GZ -> G, MZ -> G)"),
            qPrintable (log.text ()));
}

void TestAD1CCty::resolves_lookup_exceptions ()
{
  // Historical aliases keep lookup-rule regressions independent of country-file updates.
  QFile source {QFINDTESTDATA ("data/cty_lookup.dat")};
  QVERIFY (source.open (QIODevice::ReadOnly));
  QVERIFY (write_data_file ("cty.dat", source.readAll ()));
  AD1CCty countries {configuration_.get ()};
  for (auto const * call : {"RC4I", "RJ4I", "RM4I", "RU4I", "UA4H", "UC4I", "UG4I", "UI4I"})
    {
      QCOMPARE (countries.lookup (call).ITU_zone, 29);
      QCOMPARE (countries.lookup (QString {call} + "XYZ").ITU_zone, 30);
    }
  for (auto const * call : {"RA9J", "RC9J", "RT9J", "UA8J"})
    {
      QCOMPARE (countries.lookup (call).ITU_zone, 21);
      QCOMPARE (countries.lookup (QString {call} + "/P/QRP").ITU_zone, 21);
      QCOMPARE (countries.lookup (QString {call} + "XYZ").ITU_zone, 20);
    }
  QMap<QString, int> const suffix_zones {
    {"RC4I/P", 29}, {"RA9J/P/QRP", 21}, {"UG4I/P", 30}, {"UG4I/P/QRP", 30}
  };
  for (auto i = suffix_zones.cbegin (); i != suffix_zones.cend (); ++i)
    QVERIFY2 (countries.lookup (i.key ()).ITU_zone == i.value (), qPrintable (i.key ()));
  QMap<QString, QString> const expected {
    {"K1ABC", "United States"},
    {"EF6", "Spain"}, {"EF6ABC", "Balearic Islands"},
    {"WH7K", "Hawaii"}, {"WH7KABC", "Kure Island"},
    {"G0FBJ", "Shetland Islands"}, {"4U1VIC", "Vienna Intl Ctr"},
    {"KG44WW", "Guantanamo Bay"}, {"KG4BKW", "Guam"},
    {"KG4NBL/P", "Alaska"}, {"KG4NBL/P/QRP", "Alaska"},
    {"KG4BKW/P", "Guam"}, {"KG4AA/P", "Guantanamo Bay"},
    {"KG4ABC/P", "United States"}, {"KG4ABC/VP9", "Bermuda"},
    {"KG4ABC/KG4", "Guantanamo Bay"}, {"W1A/VP9", "Bermuda"},
    {"EA8/K1A", "Canary Islands"}, {"EA8/VP9", "Canary Islands"}
  };
  for (auto i = expected.cbegin (); i != expected.cend (); ++i)
    {
      auto const actual = countries.lookup (i.key ());
      QVERIFY2 (actual.wae.name == i.value (), qPrintable (i.key () + ": " + actual.wae.name));
      auto const dxcc = i.key () == "G0FBJ" ? QString {"Scotland"}
        : i.key () == "4U1VIC" ? QString {"Austria"} : i.value ();
      QVERIFY2 (actual.dxcc.name == dxcc, qPrintable (i.key () + ": " + actual.dxcc.name));
    }
  for (auto const * call : {"N2NL/MM", "NQ4I/AM", "N2NL/MM/QRP"})
    {
      QVERIFY (countries.lookup (call).wae.name.isEmpty ());
      QVERIFY (countries.lookup (call).dxcc.name.isEmpty ());
    }
}

void TestAD1CCty::validates_country_file_aliases_data ()
{
  QTest::addColumn<QString> ("path");
  QTest::newRow ("bundled") << QFINDTESTDATA ("../../../cty.dat");
  auto const downloaded = qEnvironmentVariable ("WSJTX_TEST_CTY_FILE");
  if (!downloaded.isEmpty ()) QTest::newRow ("downloaded") << downloaded;
}

void TestAD1CCty::validates_country_file_aliases ()
{
  QFETCH (QString, path);
  QFile source {path};
  QVERIFY (source.open (QIODevice::ReadOnly | QIODevice::Text));
  auto const bytes = source.readAll ();
  QVERIFY (write_data_file ("cty.dat", bytes));
  QMap<QString, AD1CCty::Record> prefixes;
  QMap<QString, AD1CCty::Record> exact_calls;
  QMap<QString, AD1CCty::Record> dxcc_prefixes;
  QMap<QString, AD1CCty::Record> dxcc_exact_calls;
  QSet<QString> text_keys;
  int entities = 0;
  int aliases = 0;
  int text_collisions = 0;
  int duplicates = 0;
  QString expected_version;
  AD1CCty::Record parent;
  QRegularExpression const version_pattern {R"(^VER\d{8}$)"};
  QRegularExpression const alias_pattern {R"(^\s*(=?)([A-Z0-9/]+)(.*)$)"};
  QRegularExpression const override_pattern {R"(\(([^)]*)\)|\[([^\]]*)\]|<([^>]*)>|\{([^}]*)\}|~([^~]*)~)"};
  for (auto const& line : QString::fromUtf8 (bytes).split ('\n'))
    {
      auto const fields = line.split (':');
      if (fields.size () >= 8)
        {
          ++entities;
          parent.wae.name = fields[0].trimmed ();
          parent.CQ_zone = fields[1].toInt ();
          parent.ITU_zone = fields[2].toInt ();
          parent.continent = AD1CCty::continent (fields[3].trimmed ());
          parent.latitude = fields[4].toFloat ();
          parent.longtitude = fields[5].toFloat ();
          parent.UTC_offset = std::lround (fields[6].toDouble () * 3600.);
          parent.wae.primary_prefix = fields[7].trimmed ();
          parent.wae.WAE_only = parent.wae.primary_prefix.startsWith ('*');
          if (parent.wae.WAE_only) parent.wae.primary_prefix.remove (0, 1);
          continue;
        }
      for (auto const& token : line.section (';', 0, 0).split (',', Qt::SkipEmptyParts))
        {
          if (token.trimmed ().isEmpty ()) continue;
          auto const parsed = alias_pattern.match (token.trimmed ());
          QVERIFY2 (parsed.hasMatch (), qPrintable (token));
          auto const key = parsed.captured (2);
          if (!parsed.captured (1).isEmpty () && version_pattern.match (key).hasMatch ())
            expected_version = key;
          auto expected = parent;
          auto overrides = override_pattern.globalMatch (parsed.captured (3));
          while (overrides.hasNext ())
            {
              auto const item = overrides.next ();
              if (!item.captured (1).isNull ()) expected.CQ_zone = item.captured (1).toInt ();
              if (!item.captured (2).isNull ()) expected.ITU_zone = item.captured (2).toInt ();
              if (!item.captured (3).isNull ())
                {
                  auto const coordinates = item.captured (3).split ('/');
                  QCOMPARE (coordinates.size (), 2);
                  expected.latitude = coordinates[0].toFloat ();
                  expected.longtitude = coordinates[1].toFloat ();
                }
              if (!item.captured (4).isNull ()) expected.continent = AD1CCty::continent (item.captured (4));
              if (!item.captured (5).isNull ()) expected.UTC_offset = std::lround (item.captured (5).toDouble () * 3600.);
            }
          ++aliases;
          if (text_keys.contains (key)) ++text_collisions;
          text_keys.insert (key);
          auto& index = parsed.captured (1).isEmpty () ? prefixes : exact_calls;
          if (index.contains (key)) ++duplicates;
          if (!index.contains (key) || (expected.wae.WAE_only && !index[key].wae.WAE_only))
            index.insert (key, expected);
          auto& dxcc_index = parsed.captured (1).isEmpty () ? dxcc_prefixes : dxcc_exact_calls;
          if (!expected.wae.WAE_only && !dxcc_index.contains (key)) dxcc_index.insert (key, expected);
        }
    }
  QVERIFY (entities > 0);
  QVERIFY (!prefixes.isEmpty ());
  QVERIFY (!exact_calls.isEmpty ());
  QVERIFY (!expected_version.isEmpty ());
  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  QCOMPARE (countries.version (), expected_version);
  auto dxcc_exact_identity = [&] (QString key) {
    while (true)
      {
        auto const exact = dxcc_exact_calls.constFind (key);
        if (exact != dxcc_exact_calls.cend ()) return exact->wae;
        auto const slash = key.lastIndexOf ('/');
        if (slash < 0 || !Radio::is_operating_suffix (key.mid (slash + 1))) break;
        key.truncate (slash);
      }
    return AD1CCty::EntityIdentity {};
  };
  auto dxcc_prefix_identity = [&] (QString key) {
    while (!key.isEmpty ())
      {
        auto const found = dxcc_prefixes.constFind (key);
        if (found != dxcc_prefixes.cend ()) return found->wae;
        key.chop (1);
      }
    return AD1CCty::EntityIdentity {};
  };
  QMap<QString, QMap<QString, int>> parent_votes;
  QSet<QString> shared_exacts;
  for (auto i = exact_calls.cbegin (); i != exact_calls.cend (); ++i)
    if (i->wae.WAE_only && dxcc_exact_calls.contains (i.key ()))
      {
        shared_exacts.insert (i->wae.primary_prefix);
        ++parent_votes[i->wae.primary_prefix][dxcc_exact_calls[i.key ()].wae.primary_prefix];
      }
  for (auto const * index : {&prefixes, &exact_calls})
    for (auto i = index->cbegin (); i != index->cend (); ++i)
      if (i->wae.WAE_only && !shared_exacts.contains (i->wae.primary_prefix))
        {
          auto parent = dxcc_exact_identity (i.key ());
          if (parent.name.isEmpty ()) parent = dxcc_prefix_identity (Radio::effective_prefix (i.key ()));
          if (!parent.name.isEmpty ()) ++parent_votes[i->wae.primary_prefix][parent.primary_prefix];
        }
  QMap<QString, QString> derived_parents;
  for (auto i = parent_votes.cbegin (); i != parent_votes.cend (); ++i)
    {
      if (shared_exacts.contains (i.key ()) && i->size () != 1) continue;
      int total = 0;
      for (int count : *i) total += count;
      for (auto vote = i->cbegin (); vote != i->cend (); ++vote)
        if (*vote > total / 2) derived_parents.insert (i.key (), vote.key ());
    }
  QMap<QString, AD1CCty::EntityIdentity> dxcc_entities;
  for (auto const& record : dxcc_prefixes) dxcc_entities.insert (record.wae.primary_prefix, record.wae);
  for (auto const& record : dxcc_exact_calls) dxcc_entities.insert (record.wae.primary_prefix, record.wae);
  QMap<QString, QString> wae_parents;
  auto check = [&] (QString const& key, AD1CCty::Record const& expected) {
    auto const actual = countries.lookup (key);
    auto expected_dxcc = dxcc_exact_calls.value (key).wae;
    if (expected_dxcc.name.isEmpty ())
      {
        if (expected.wae.WAE_only && exact_calls.contains (key))
          expected_dxcc = dxcc_entities.value (derived_parents.value (expected.wae.primary_prefix));
        if (expected_dxcc.name.isEmpty ()) expected_dxcc = dxcc_exact_identity (key);
        if (expected_dxcc.name.isEmpty ()) expected_dxcc = dxcc_prefix_identity (Radio::effective_prefix (key));
      }
    QVERIFY2 (actual.dxcc.name == expected_dxcc.name, qPrintable (key + " DXCC identity"));
    QVERIFY2 (actual.dxcc.primary_prefix == expected_dxcc.primary_prefix, qPrintable (key));
    QVERIFY (!actual.dxcc.WAE_only);
    QVERIFY2 (actual.wae.name == expected.wae.name, qPrintable (key + ": " + actual.wae.name));
    QVERIFY2 (actual.wae.primary_prefix == expected.wae.primary_prefix, qPrintable (key));
    QVERIFY2 (actual.wae.WAE_only == expected.wae.WAE_only, qPrintable (key));
    QVERIFY2 (actual.CQ_zone == expected.CQ_zone, qPrintable (key + " CQ zone"));
    QVERIFY2 (actual.ITU_zone == expected.ITU_zone, qPrintable (key + " ITU zone"));
    QVERIFY2 (actual.continent == expected.continent, qPrintable (key + " continent"));
    QVERIFY2 (actual.latitude == expected.latitude, qPrintable (key + " latitude"));
    QVERIFY2 (actual.longtitude == expected.longtitude, qPrintable (key + " longitude"));
    QVERIFY2 (actual.UTC_offset == expected.UTC_offset, qPrintable (key + " UTC offset"));
    if (actual.dxcc.name.isEmpty ())
      {
        QCOMPARE (actual.identity (AD1CCty::EntityView::DXCC).name, actual.wae.name);
      }
    if (expected.wae.WAE_only)
      {
        QVERIFY2 (!actual.dxcc.name.isEmpty (), qPrintable (key + " has no DXCC parent"));
        if (!wae_parents.contains (expected.wae.primary_prefix))
          wae_parents.insert (expected.wae.primary_prefix, actual.dxcc.primary_prefix);
        QCOMPARE (actual.dxcc.primary_prefix, wae_parents.value (expected.wae.primary_prefix));
      }
  };
  for (auto i = prefixes.cbegin (); i != prefixes.cend (); ++i)
    {
      if (!exact_calls.contains (i.key ())) check (i.key (), i.value ());
      if (QTest::currentTestFailed ()) return;
    }
  for (auto i = exact_calls.cbegin (); i != exact_calls.cend (); ++i)
    {
      if (i.key ().endsWith ("/MM") || i.key ().endsWith ("/AM")) continue;
      check (i.key (), i.value ());
      if (QTest::currentTestFailed ()) return;
    }
  QVERIFY2 (log.text ().isEmpty (), qPrintable (log.text ()));
  qInfo () << "Validated" << entities << "entities and" << aliases << "aliases;"
           << duplicates << "same-namespace duplicates," << text_collisions - duplicates << "exact/prefix overlaps";
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
  QCOMPARE (countries.lookup ("GOOD").wae.name, QString {"Good Entity"});
  QVERIFY (countries.lookup ("INCOMPLETE").wae.name.isEmpty ());
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
  QVERIFY (log.text ().contains ("8 data issues"));
  QVERIFY (log.text ().contains ("line 9: invalid entity ITU zone"));
  QVERIFY (!log.text ().contains ("line 11:"));
}

void TestAD1CCty::diagnoses_conflicting_aliases ()
{
  QVERIFY (write_data_file ("cty.dat", QByteArray {
    "First:14:28:EU:50:-10:-1:F\nF,F(14),=CALL[29],=CALL[29],=CALL[30];\n"
    "Second:15:28:EU:40:-12:-1:S\nF,=CALL;\n"}));
  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  QCOMPARE (countries.lookup ("F1ABC").dxcc.name, QString {"First"});
  QCOMPARE (countries.lookup ("CALL").ITU_zone, 29);
  QVERIFY2 (log.text ().contains ("4 data issues"), qPrintable (log.text ()));
  QVERIFY (log.text ().contains ("line 2: conflicting prefix F; first definition kept"));
  QVERIFY (log.text ().contains ("line 2: conflicting exact call CALL; first definition kept"));
  QVERIFY (log.text ().contains ("line 4: conflicting prefix F; first definition kept"));
  QVERIFY (log.text ().contains ("line 4: conflicting exact call CALL; first definition kept"));
}

void TestAD1CCty::checks_prefix_consistency_without_exact_call_shadowing ()
{
  QVERIFY (write_data_file ("cty.dat", QByteArray {
    "Italy:15:28:EU:42:-12:-1:I\nI,=IT9PARENT;\n"
    "Chile:12:14:SA:-30:71:4:CE\nCE,=IT9;\n"
    "Sicily:15:28:EU:37:-14:-1:*IT9\nIT9,=IT9PARENT;\n"}));
  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  QCOMPARE (countries.lookup ("IT9").dxcc.name, QString {"Chile"});
  QCOMPARE (countries.lookup ("IT9ABC").dxcc.name, QString {"Italy"});
  QVERIFY2 (log.text ().isEmpty (), qPrintable (log.text ()));
}

void TestAD1CCty::diagnoses_inconsistent_wae_parents_data ()
{
  QTest::addColumn<QByteArray> ("source");
  QTest::addColumn<QString> ("diagnostic");
  QTest::addColumn<QString> ("wae_call");
  QTest::addColumn<QString> ("dxcc_name");
  QByteArray const first {"First:14:28:EU:50:-10:-1:F\nF;\n"};
  QByteArray const second {"Second:15:28:EU:40:-12:-1:S\nS;\n"};
  QTest::newRow ("prefix-mismatch")
    << first + second + "WAE:14:28:EU:50:-10:-1:*X\nF,S,=F1,=F2;\n"
    << QString {"line 5: WAE entity WAE: 1 prefixes differ from DXCC parent First (S -> S)"}
    << QString {"S1"} << QString {"Second"};
  QTest::newRow ("tie-is-ambiguous")
    << second + first + "WAE:14:28:EU:50:-10:-1:*X\n=F1,=S1;\n"
    << QString {"line 5: WAE entity WAE: ambiguous DXCC parent (alias votes: S=1, F=1; unmatched=0)"}
    << QString {"F1"} << QString {"First"};
  QTest::newRow ("no-parent")
    << QByteArray {"WAE:14:28:EU:50:-10:-1:*X\n=ZZ1,=ZZ2;\n"}
    << QString {"line 1: WAE entity WAE: ambiguous DXCC parent (alias votes: none; unmatched=2)"}
    << QString {"ZZ1"} << QString {};
  QTest::newRow ("bounded-examples")
    << first + "WAE:14:28:EU:50:-10:-1:*X\n=F1,=F2,ZZ1,ZZ2,ZZ3,ZZ4,ZZ5,ZZ6;\n"
    << QString {"line 3: WAE entity WAE: 6 prefixes differ from DXCC parent First (ZZ1 -> no DXCC match, ZZ2 -> no DXCC match, ZZ3 -> no DXCC match, ZZ4 -> no DXCC match, ZZ5 -> no DXCC match)"}
    << QString {"ZZ6"} << QString {};
  QTest::newRow ("conflicting-shared-exacts")
    << QByteArray {"First:14:28:EU:50:-10:-1:F\nF,=F1,=F2;\nSecond:15:28:EU:40:-12:-1:S\nS,=S1;\nWAE:14:28:EU:50:-10:-1:*X\n=F1,=F2,=S1;\n"}
    << QString {"line 5: WAE entity WAE: ambiguous DXCC parent (shared exact votes: F=2, S=1; unmatched=0)"}
    << QString {"S1"} << QString {"Second"};
}

void TestAD1CCty::diagnoses_inconsistent_wae_parents ()
{
  QFETCH (QByteArray, source);
  QFETCH (QString, diagnostic);
  QFETCH (QString, wae_call);
  QFETCH (QString, dxcc_name);
  QVERIFY (write_data_file ("cty.dat", source));
  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  QVERIFY2 (log.text ().contains (diagnostic), qPrintable (log.text ()));
  QVERIFY (log.text ().contains ("1 data issues"));
  auto const record = countries.lookup (wae_call);
  QCOMPARE (record.dxcc.name, dxcc_name);
  QCOMPARE (record.wae.name, QString {"WAE"});
  if (dxcc_name.isEmpty ()) QCOMPARE (record.identity (AD1CCty::EntityView::DXCC).name, QString {"WAE"});
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
  QCOMPARE (countries.lookup ("GOOD").wae.name, QString {"Good Entity"});
  QVERIFY (countries.lookup ("BAD").wae.name.isEmpty ());
  QCOMPARE (countries.lookup ("LAST").wae.name, QString {"Last Entity"});
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
  QCOMPARE (!record.wae.name.isEmpty (), recoverable);
  if (recoverable)
    {
      QCOMPARE (record.CQ_zone, 5);
      QCOMPARE (record.ITU_zone, 8);
      QCOMPARE (record.continent, AD1CCty::Continent::NA);
      QCOMPARE (record.latitude, 37.f);
      QCOMPARE (record.longtitude, -96.f);
      QCOMPARE (record.UTC_offset, -18000);
    }
  QCOMPARE (countries.lookup ("GOOD").wae.name, QString {"Version Entity"});
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
  QVERIFY (blocks > 0);
  QVERIFY (cells > 0);
  QVERIFY2 (log.text ().isEmpty (), qPrintable (log.text ()));
  qInfo () << "Validated" << blocks << "grid blocks and" << cells << "cells";
}

void TestAD1CCty::recovers_unterminated_grid_blocks ()
{
  auto const contents = QByteArray {
    "AA<\n"
    "00:KEPT,\n"
    "01:,\n"
    "02:ALSO-KEPT\n"
    "BB<\n"
    "01:KEPT>\n"
    "CC<\n"
    "02:INCOMPLETE,\n"
    "dD<\n"
    "03:INVALID>\n"
    "EE<\n"
    "04:EOF-INCOMPLETE,\n"};
  QVERIFY (write_data_file ("grid.dat", contents));

  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  QCOMPARE (countries.findState ("AA00"), QString {"KEPT"});
  QCOMPARE (countries.findState ("AA01"), QString {"**"});
  QCOMPARE (countries.findState ("AA02"), QString {"ALSO-KEPT"});
  QCOMPARE (countries.findState ("BB01"), QString {"KEPT"});
  QCOMPARE (countries.findState ("CC02"), QString {"INCOMPLETE"});
  QCOMPARE (countries.findState ("dD03"), QString {"**"});
  QCOMPARE (countries.findState ("EE04"), QString {"EOF-INCOMPLETE"});
  QVERIFY (log.text ().contains ("line 1: grid block missing '>'; kept 2 cells"));
  QVERIFY (log.text ().contains ("line 3: invalid grid cell"));
}

void TestAD1CCty::bounds_recovered_grid_blocks ()
{
  QByteArray contents;
  for (int i = 0; i < 26; ++i)
    contents += QByteArray {"A"} + char ('A' + i) + "<\n00:KEPT\n";
  QVERIFY (write_data_file ("grid.dat", contents));
  AD1CCty countries {configuration_.get ()};
  QCOMPARE (countries.findState ("AY00"), QString {"KEPT"});
  QCOMPARE (countries.findState ("AZ00"), QString {"**"});
}

void TestAD1CCty::keeps_first_recovered_grid_block ()
{
  QVERIFY (write_data_file ("grid.dat", QByteArray {"AA<\n00:FIRST,\nAA<\n01:SECOND>\n"}));
  LogCapture log;
  AD1CCty countries {configuration_.get ()};
  // Recovering the partial first block still reserves its prefix.
  QCOMPARE (countries.findState ("AA00"), QString {"FIRST"});
  QCOMPARE (countries.findState ("AA01"), QString {"**"});
  QVERIFY (log.text ().contains ("line 3: duplicate grid prefix"));
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
  QVERIFY2 (log.text ().isEmpty (), qPrintable (log.text ()));
}
QTEST_MAIN (TestAD1CCty)

#include "test_ad1c_cty.moc"
