#include <QtTest>
#include <QFile>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <memory>

#include "commons.h"
#include "Configuration.hpp"
#include "DecodeOutputPlan.hpp"
#include "MessageFilterLogic.hpp"
#include "MetaDataRegistry.hpp"
#include "logbook/logbook.h"
#include "widgets/itoneAndicw.h"

namespace
{
  dec_data_t test_dec_data {};
}

dec_data_t& dec_data = test_dec_data;
int volatile itone[MAX_NUM_SYMBOLS] {};
int volatile icw[NUM_CW_SYMBOLS] {};
float gran () {return 0;}

class TestCountryFilters final : public QObject
{
  Q_OBJECT

  QTemporaryDir settings_dir_;
  QNetworkAccessManager network_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Configuration> configuration_;
  std::unique_ptr<LogBook> logbook_;

  QString data_directory () const
  {
    return QStandardPaths::writableLocation (QStandardPaths::DataLocation);
  }

private Q_SLOTS:
  void initTestCase ()
  {
    register_types ();
    QStandardPaths::setTestModeEnabled (true);
    QCoreApplication::setOrganizationName ("WSJT-X");
    QCoreApplication::setApplicationName ("test_country_filters");
    QVERIFY (settings_dir_.isValid ());
    QVERIFY (QDir {}.mkpath (data_directory ()));
    QFile countries {QDir {data_directory ()}.filePath ("cty.dat")};
    QVERIFY (countries.open (QIODevice::WriteOnly | QIODevice::Text));
    QByteArray const contents {
      "Scotland:14:27:EU:56.82:4.18:0:GM\n2M,GM,=G0FBJ;\n"
      "Shetland Islands:14:27:EU:60.5:1.5:0:*GM/s\n=2M0BDR,=G0FBJ;\n"
      "Asiatic Turkey:20:39:AS:39:-35:-3:TA\nTA;\n"
      "European Turkey:20:39:EU:41:-28:-3:*TA1\nTA1;\n"};
    QCOMPARE (countries.write (contents), qint64 {contents.size ()});
    countries.close ();
    settings_.reset (new QSettings {settings_dir_.filePath ("settings.ini"), QSettings::IniFormat});
    configuration_.reset (new Configuration {&network_, QDir {settings_dir_.path ()}, settings_.get (), nullptr});
    logbook_.reset (new LogBook {configuration_.get ()});
  }

  void cleanupTestCase ()
  {
    logbook_.reset ();
    configuration_.reset ();
    QDir {data_directory ()}.removeRecursively ();
  }

  void territoryFiltersUseCanonicalIdentity_data ()
  {
    QTest::addColumn<bool> ("include_wae");
    QTest::addColumn<QString> ("call");
    QTest::addColumn<QString> ("territory");
    QTest::addColumn<bool> ("expected");
    for (bool include_wae : {false, true})
      {
        for (auto const * call : {"TA1ABC", "TA2ABC"})
          for (auto const * territory : {"Turkey", "AS Turkey", "EU Turkey", "turkey"})
            {
              QString const name = !include_wae || QString {call}.startsWith ("TA2") ? "AS Turkey" : "EU Turkey";
              auto const row = QString {"%1-%2-%3"}.arg (include_wae ? "WAE" : "DXCC").arg (call).arg (territory);
              QTest::newRow (qPrintable (row)) << include_wae << QString {call} << QString {territory}
                                             << name.contains (territory);
            }
        for (auto const * call : {"2M0BDR", "G0FBJ"})
          for (auto const * territory : {"Scotland", "Shetland"})
            {
              auto const row = QString {"%1-%2-%3"}.arg (include_wae ? "WAE" : "DXCC").arg (call).arg (territory);
              QTest::newRow (qPrintable (row)) << include_wae << QString {call} << QString {territory}
                                             << (include_wae == (QString {territory} == "Shetland"));
            }
      }
  }

  void territoryFiltersUseCanonicalIdentity ()
  {
    QFETCH (bool, include_wae);
    QFETCH (QString, call);
    QFETCH (QString, territory);
    QFETCH (bool, expected);
    auto const view = include_wae ? AD1CCty::EntityView::WAE : AD1CCty::EntityView::DXCC;
    DecodedText decoded {QString {"060522 -10  0.3  815 # CQ %1 KN40"}.arg (call)};
    QString sender;
    QString grid;
    decoded.deCallAndGrid (sender, grid);
    QCOMPARE (sender, call);

    MessageFilterLogic::FilterContext msk_context;
    msk_context.entityView = view;
    msk_context.hideTerritory1 = true;
    msk_context.territory1 = territory;
    QCOMPARE (MessageFilterLogic::evaluateMSK144 (decoded, msk_context, logbook_.get ()).filtered, expected);

    DecodeOutputPlan::VisibilityFilterContext visibility_context;
    visibility_context.hideTerritory1 = true;
    visibility_context.territory1 = territory;
    visibility_context.countryName = [this, view] (QString const& sender_call) {
      return logbook_->countries ()->lookup (sender_call).identity (view).abbreviated_name;
    };
    QCOMPARE (DecodeOutputPlan::decideVisibilityFilter (decoded, visibility_context).filtered, expected);
  }
};

QTEST_MAIN (TestCountryFilters)
#include "test_country_filters.moc"
