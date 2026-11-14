#include <QtTest>

#include <cmath>
#include <limits>
#include <memory>

#include <QComboBox>
#include <QDateTime>
#include <QFocusEvent>
#include <QLineEdit>
#include <QModelIndex>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlTableModel>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QString>
#include <QTranslator>

#include "Radio.hpp"
#include "item_delegates/ForeignKeyDelegate.hpp"
#include "models/Bands.hpp"
#include "models/FrequencyList.hpp"
#include "models/IARURegions.hpp"
#include "models/Modes.hpp"
#include "models/StationList.hpp"
#include "validators/LiveFrequencyValidator.hpp"
#include "widgets/FrequencyDeltaLineEdit.hpp"
#include "widgets/FrequencyLineEdit.hpp"

namespace
{
  constexpr Radio::Frequency preferred_3cm_frequency {10368200000ULL};
  constexpr Radio::Frequency entered_3cm_frequency {10368799000ULL};
  constexpr Radio::Frequency preferred_20m_frequency {14074000ULL};

  class DuplicateRegionTranslator : public QTranslator
  {
  public:
    bool isEmpty () const override { return false; }
    QString translate (char const * context, char const *, char const * = nullptr,
                       int = -1) const override
    {
      return QString::fromLatin1 (context) == "IARURegions"
        ? QStringLiteral ("Translated region") : QString {};
    }
  };

  std::unique_ptr<QComboBox> key_editor (QAbstractItemDelegate& delegate, QModelIndex const& index)
  {
    auto editor = std::unique_ptr<QComboBox> {
      qobject_cast<QComboBox *> (delegate.createEditor (nullptr, QStyleOptionViewItem {}, index))};
    if (editor) delegate.setEditorData (editor.get (), index);
    return editor;
  }

  class LiveFrequencyEditorFixture
  {
  public:
    LiveFrequencyEditorFixture ()
      : frequencies {&bands}
    {
      frequencies.add ({preferred_3cm_frequency, Modes::Q65, IARURegions::ALL,
                        {}, {}, {}, {}, true});
      frequencies.add ({preferred_20m_frequency, Modes::FT8, IARURegions::ALL,
                        {}, {}, {}, {}, true});
      band_editor.setEditable (true);
      band_editor.setModel (&frequencies);
      band_editor.setModelColumn (FrequencyList_v2_101::frequency_mhz_column);
      validator = std::make_unique<LiveFrequencyValidator> (
        &band_editor, &bands, &frequencies, &nominal_frequency, false);
      band_editor.setValidator (validator.get ());
      band_editor.setCurrentText (bands.find (nominal_frequency));

      QObject::connect (validator.get (), &LiveFrequencyValidator::valid,
                        [&] (Radio::Frequency frequency) {
                          requests.append (frequency);
                          bands_at_request.append (band_editor.currentText ());
                          if (accept_requests)
                            {
                              nominal_frequency = frequency;
                            }
                          else
                            {
                              band_editor.setCurrentText (bands.find (nominal_frequency));
                            }
                        });
    }

    void enter (QString const& text, QString const& completed_text = {})
    {
      band_editor.setFocus ();
      band_editor.lineEdit ()->selectAll ();
      QTest::keyClicks (band_editor.lineEdit (), text);
      if (!completed_text.isEmpty ()) band_editor.lineEdit ()->setText (completed_text);
      QTest::keyClick (band_editor.lineEdit (), Qt::Key_Return);
      QCoreApplication::processEvents ();
    }

    void send_focus_out ()
    {
      QFocusEvent event {QEvent::FocusOut, Qt::MouseFocusReason};
      QCoreApplication::sendEvent (band_editor.lineEdit (), &event);
      QCoreApplication::processEvents ();
    }

    Bands bands;
    FrequencyList_v2_101 frequencies;
    Radio::Frequency nominal_frequency {preferred_3cm_frequency};
    QComboBox band_editor;
    QVector<Radio::Frequency> requests;
    QVector<QString> bands_at_request;
    bool accept_requests {true};
    std::unique_ptr<LiveFrequencyValidator> validator;
  };
}

class TestFrequencyEditors
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void japanese_region_editor_preserves_key ()
  {
    QTranslator translator;
    QVERIFY (translator.load (QString::fromUtf8 (WSJT_JAPANESE_QM)));
    QCoreApplication::installTranslator (&translator);
    Bands bands;
    IARURegions regions;
    FrequencyList_v2_101 frequencies {&bands};
    auto const added = frequencies.add ({14080000, Modes::FT4, IARURegions::R2,
                                         {}, {}, {}, {}, false});
    auto const index = frequencies.index (added.row (), FrequencyList_v2_101::region_column);
    ForeignKeyDelegate delegate {&regions, 0};
    QAbstractItemDelegate& base = delegate;
    auto editor = key_editor (base, index);
    QVERIFY (editor);
    QVERIFY (editor->currentText () != "Region 2");
    QCOMPARE (editor->currentData (Qt::EditRole).value<IARURegions::Region> (), IARURegions::R2);
    base.setModelData (editor.get (), &frequencies, index);
    QCOMPARE (index.data (Qt::EditRole).value<IARURegions::Region> (), IARURegions::R2);
    editor->setCurrentIndex (editor->findData (QVariant::fromValue (IARURegions::R3), Qt::EditRole));
    base.setModelData (editor.get (), &frequencies, index);
    QCOMPARE (index.data (Qt::EditRole).value<IARURegions::Region> (), IARURegions::R3);
    QCoreApplication::removeTranslator (&translator);
  }

  void frequency_line_edit_requires_positive_frequency ();
  void frequency_line_edit_accepts_frequency_above_signed_range ();
  void frequency_conversion_rejects_unsigned_limit ();
  void frequency_delta_line_edit_accepts_signed_offsets ();
  void station_list_rejects_invalid_offset_variants ();
  void frequency_list_rejects_invalid_frequency_variants ();
  void translated_region_editor_preserves_and_changes_keys ();
  void mode_editor_preserves_and_changes_keys ();
  void frequency_list_rejects_invalid_enum_variants ();
  void foreign_key_editor_ignores_missing_keys ();
  void foreign_key_editor_honors_roles_and_columns ();
  void station_band_editor_filters_used_keys ();
  void sql_band_editor_preserves_string_keys ();
  void live_frequency_entry_requests_one_qsy_data ();
  void live_frequency_entry_requests_one_qsy ();
  void live_frequency_band_entry_requests_working_frequency ();
  void live_frequency_rejected_request_preserves_selection ();
  void live_frequency_focus_loss_without_edit_is_harmless ();
};

void TestFrequencyEditors::frequency_line_edit_requires_positive_frequency ()
{
  FrequencyLineEdit editor;

  bool ok {true};
  editor.setText ("0");
  QCOMPARE (editor.frequency (&ok), Radio::Frequency {0});
  QVERIFY (!ok);

  editor.setText ("0.000001");
  QCOMPARE (editor.frequency (&ok), Radio::Frequency {1});
  QVERIFY (ok);
}

void TestFrequencyEditors::frequency_line_edit_accepts_frequency_above_signed_range ()
{
  FrequencyLineEdit editor;
  editor.setText ("9223372036864.000000");

  QVERIFY (editor.hasAcceptableInput ());
  bool ok {false};
  QCOMPARE (editor.frequency (&ok), Radio::Frequency {9223372036864000000ULL});
  QVERIFY (ok);
}

void TestFrequencyEditors::frequency_conversion_rejects_unsigned_limit ()
{
  auto const limit = std::ldexp (1., std::numeric_limits<Radio::Frequency>::digits);
  bool ok {true};

  QCOMPARE (Radio::frequency (limit, 0, &ok), Radio::Frequency {0});
  QVERIFY (!ok);
}

void TestFrequencyEditors::frequency_delta_line_edit_accepts_signed_offsets ()
{
  FrequencyDeltaLineEdit editor;

  bool ok {false};
  editor.setText ("0");
  QCOMPARE (editor.frequency_delta (&ok), Radio::FrequencyDelta {0});
  QVERIFY (ok);

  editor.setText ("-0.001");
  QCOMPARE (editor.frequency_delta (&ok), Radio::FrequencyDelta {-1000});
  QVERIFY (ok);
}

void TestFrequencyEditors::station_list_rejects_invalid_offset_variants ()
{
  Bands bands;
  StationList stations {&bands, {{"20m", 1000, "Yagi"}}};
  auto const offset_index = stations.index (0, StationList::offset_column);

  QVERIFY (!stations.setData (offset_index, QString {"abc"}, Qt::EditRole));
  QCOMPARE (stations.data (offset_index, Qt::EditRole).value<Radio::FrequencyDelta> (),
            Radio::FrequencyDelta {1000});

  QVERIFY (stations.setData (offset_index, Radio::FrequencyDelta {-2000}, Qt::EditRole));
  QCOMPARE (stations.data (offset_index, Qt::EditRole).value<Radio::FrequencyDelta> (),
            Radio::FrequencyDelta {-2000});
}

void TestFrequencyEditors::frequency_list_rejects_invalid_frequency_variants ()
{
  Bands bands;
  FrequencyList_v2_101 frequencies {&bands};
  auto const added_index = frequencies.add ({14074000, Modes::FT8, IARURegions::ALL,
        QString {}, QString {}, QDateTime {}, QDateTime {}, false});
  auto const frequency_index = frequencies.index (added_index.row (), FrequencyList_v2_101::frequency_column);

  QVERIFY (!frequencies.setData (QModelIndex {}, Radio::Frequency {14075000}, Qt::EditRole));
  QVERIFY (!frequencies.setData (frequency_index, QString {"abc"}, Qt::EditRole));
  QCOMPARE (frequencies.data (frequency_index, Qt::EditRole).value<Radio::Frequency> (),
            Radio::Frequency {14074000});

  QVERIFY (!frequencies.setData (frequency_index, Radio::Frequency {0}, Qt::EditRole));
  QCOMPARE (frequencies.data (frequency_index, Qt::EditRole).value<Radio::Frequency> (),
            Radio::Frequency {14074000});

  QVERIFY (frequencies.setData (frequency_index, Radio::Frequency {14075000}, Qt::EditRole));
  QCOMPARE (frequencies.data (frequency_index, Qt::EditRole).value<Radio::Frequency> (),
            Radio::Frequency {14075000});
}

void TestFrequencyEditors::translated_region_editor_preserves_and_changes_keys ()
{
  DuplicateRegionTranslator translator;
  QVERIFY (QCoreApplication::installTranslator (&translator));
  IARURegions regions;
  Bands bands;
  FrequencyList_v2_101 frequencies {&bands};
  auto const added = frequencies.add ({14074000, Modes::FT4, IARURegions::R2,
        {}, {}, {}, {}, false});
  auto const index = frequencies.index (added.row (), FrequencyList_v2_101::region_column);
  QCOMPARE (index.data (Qt::EditRole).userType (), qMetaTypeId<IARURegions::Region> ());
  QCOMPARE (regions.index (IARURegions::R2, 0).data (Qt::EditRole).userType (),
            qMetaTypeId<IARURegions::Region> ());
  QCOMPARE (index.data ().toString (), QString {"Translated region"});
  ForeignKeyDelegate delegate {&regions, 0};
  QAbstractItemDelegate& base = delegate;
  auto editor = key_editor (base, index);
  QVERIFY (editor);
  QCOMPARE (editor->currentData (Qt::EditRole).value<IARURegions::Region> (), IARURegions::R2);
  QSignalSpy changes {&frequencies, &QAbstractItemModel::dataChanged};
  base.setModelData (editor.get (), &frequencies, index);
  QCOMPARE (index.data (Qt::EditRole).value<IARURegions::Region> (), IARURegions::R2);
  QCOMPARE (changes.count (), 0);
  for (auto region : {IARURegions::R3, IARURegions::ALL})
    {
      editor->setCurrentIndex (editor->findData (QVariant::fromValue (region), Qt::EditRole));
      base.setModelData (editor.get (), &frequencies, index);
      QCOMPARE (index.data (Qt::EditRole).value<IARURegions::Region> (), region);
    }
  QCOMPARE (changes.count (), 2);
}

void TestFrequencyEditors::mode_editor_preserves_and_changes_keys ()
{
  Modes modes;
  Bands bands;
  FrequencyList_v2_101 frequencies {&bands};
  auto const added = frequencies.add ({14074000, Modes::FT4, IARURegions::R2,
        {}, {}, {}, {}, false});
  auto const index = frequencies.index (added.row (), FrequencyList_v2_101::mode_column);
  QCOMPARE (index.data (Qt::EditRole).userType (), qMetaTypeId<Modes::Mode> ());
  QCOMPARE (modes.index (Modes::FT4, 0).data (Qt::EditRole).userType (), qMetaTypeId<Modes::Mode> ());
  ForeignKeyDelegate delegate {&modes, 0};
  QAbstractItemDelegate& base = delegate;
  auto editor = key_editor (base, index);
  QVERIFY (editor);
  QCOMPARE (editor->currentData (Qt::EditRole).value<Modes::Mode> (), Modes::FT4);
  base.setModelData (editor.get (), &frequencies, index);
  QCOMPARE (index.data (Qt::EditRole).value<Modes::Mode> (), Modes::FT4);
  for (auto mode : {Modes::FT8, Modes::ALL})
    {
      editor->setCurrentIndex (editor->findData (QVariant::fromValue (mode), Qt::EditRole));
      base.setModelData (editor.get (), &frequencies, index);
      QCOMPARE (index.data (Qt::EditRole).value<Modes::Mode> (), mode);
    }
}

void TestFrequencyEditors::frequency_list_rejects_invalid_enum_variants ()
{
  Bands bands;
  FrequencyList_v2_101 frequencies {&bands};
  auto const added = frequencies.add ({14074000, Modes::FT4, IARURegions::R2,
        {}, {}, {}, {}, false});
  auto const region = frequencies.index (added.row (), FrequencyList_v2_101::region_column);
  auto const mode = frequencies.index (added.row (), FrequencyList_v2_101::mode_column);
  QSignalSpy changes {&frequencies, &QAbstractItemModel::dataChanged};
  for (auto const& value : {QVariant {}, QVariant {2}, QVariant {"Region 2"},
                           QVariant::fromValue (Modes::JT9),
                           QVariant {-1},
                           QVariant::fromValue (IARURegions::SENTINAL)})
    QVERIFY (!frequencies.setData (region, value, Qt::EditRole));
  for (auto const& value : {QVariant {}, QVariant {9}, QVariant {"FT4"},
                           QVariant::fromValue (IARURegions::R2),
                           QVariant {-1},
                           QVariant::fromValue (Modes::MODES_END_SENTINAL_AND_COUNT)})
    QVERIFY (!frequencies.setData (mode, value, Qt::EditRole));
  QCOMPARE (region.data (Qt::EditRole).value<IARURegions::Region> (), IARURegions::R2);
  QCOMPARE (mode.data (Qt::EditRole).value<Modes::Mode> (), Modes::FT4);
  QCOMPARE (changes.count (), 0);
}

void TestFrequencyEditors::foreign_key_editor_ignores_missing_keys ()
{
  Bands bands;
  QStandardItemModel target {1, 1};
  auto const index = target.index (0, 0);
  target.setData (index, QString {"Unknown band"}, Qt::EditRole);
  ForeignKeyDelegate delegate {&bands, 0};
  QAbstractItemDelegate& base = delegate;
  auto editor = key_editor (base, index);
  QVERIFY (editor);
  QCOMPARE (editor->currentIndex (), -1);
  QSignalSpy changes {&target, &QAbstractItemModel::dataChanged};
  base.setModelData (editor.get (), &target, index);
  QCOMPARE (index.data (Qt::EditRole).toString (), QString {"Unknown band"});
  QCOMPARE (changes.count (), 0);
  target.setData (index, QVariant {}, Qt::EditRole);
  base.setEditorData (editor.get (), index);
  QCOMPARE (editor->currentIndex (), -1);
  changes.clear ();
  base.setModelData (editor.get (), &target, index);
  QCOMPARE (changes.count (), 0);
}

void TestFrequencyEditors::foreign_key_editor_honors_roles_and_columns ()
{
  int constexpr referenced_role = Qt::UserRole + 1;
  int constexpr referencing_role = Qt::UserRole + 2;
  QStandardItemModel referenced {3, 2};
  for (int row = 0; row < referenced.rowCount (); ++row)
    {
      referenced.setData (referenced.index (row, 1), QString {"Duplicate label"});
      referenced.setData (referenced.index (row, 1), (row + 1) * 10, referenced_role);
    }
  QStandardItemModel target {2, 2};
  auto const index = target.index (0, 1);
  target.setData (index, 10, referencing_role);
  target.setData (target.index (1, 1), 20, referencing_role);
  ForeignKeyDelegate delegate {&referenced, &target, 1, 1, nullptr,
                                referenced_role, referencing_role};
  QAbstractItemDelegate& base = delegate;
  auto editor = key_editor (base, index);
  QVERIFY (editor);
  QCOMPARE (editor->modelColumn (), 1);
  QCOMPARE (editor->count (), 2);
  QCOMPARE (editor->currentData (referenced_role).toInt (), 10);
  QCOMPARE (editor->findData (20, referenced_role), -1);
  editor->setCurrentIndex (editor->findData (30, referenced_role));
  base.setModelData (editor.get (), &target, index);
  QCOMPARE (index.data (referencing_role).toInt (), 30);
  QVERIFY (!index.data (Qt::EditRole).isValid ());
}

void TestFrequencyEditors::station_band_editor_filters_used_keys ()
{
  Bands bands;
  StationList stations {&bands, {{"20m", 0, "Yagi"}, {"40m", 0, "Dipole"}}};
  auto const index = stations.index (0, StationList::band_column);
  ForeignKeyDelegate delegate {&bands, &stations, 0, StationList::band_column};
  QAbstractItemDelegate& base = delegate;
  auto editor = key_editor (base, index);
  QVERIFY (editor);
  QCOMPARE (editor->currentData (Qt::EditRole).toString (), QString {"20m"});
  QCOMPARE (editor->findData (QString {"40m"}, Qt::EditRole), -1);
  base.setModelData (editor.get (), &stations, index);
  QCOMPARE (index.data (Qt::EditRole).toString (), QString {"20m"});
  editor->setCurrentIndex (editor->findData (QString {"80m"}, Qt::EditRole));
  base.setModelData (editor.get (), &stations, index);
  QCOMPARE (index.data (Qt::EditRole).toString (), QString {"80m"});
}

void TestFrequencyEditors::sql_band_editor_preserves_string_keys ()
{
  QString const connection {"frequency_editor_keys"};
  {
    auto database = QSqlDatabase::addDatabase ("QSQLITE", connection);
    database.setDatabaseName (":memory:");
    QVERIFY (database.open ());
    QSqlQuery query {database};
    QVERIFY (query.exec ("CREATE TABLE log (band TEXT)"));
    QVERIFY (query.exec ("INSERT INTO log VALUES ('20m')"));
    QSqlTableModel model {nullptr, database};
    model.setEditStrategy (QSqlTableModel::OnManualSubmit);
    model.setTable ("log");
    QVERIFY (model.select ());
    auto const index = model.index (0, 0);
    Bands bands;
    ForeignKeyDelegate delegate {&bands, 0};
    QAbstractItemDelegate& base = delegate;
    auto editor = key_editor (base, index);
    QVERIFY (editor);
    QCOMPARE (editor->currentData (Qt::EditRole).toString (), QString {"20m"});
    base.setModelData (editor.get (), &model, index);
    QVERIFY (model.submitAll ());
    QCOMPARE (index.data (Qt::EditRole).toString (), QString {"20m"});
    editor->setCurrentIndex (editor->findData (QString {"40m"}, Qt::EditRole));
    base.setModelData (editor.get (), &model, index);
    QVERIFY (model.submitAll ());
    QVERIFY (query.exec ("SELECT band FROM log"));
    QVERIFY (query.next ());
    QCOMPARE (query.value (0).toString (), QString {"40m"});
  }
  QSqlDatabase::removeDatabase (connection);
}

void TestFrequencyEditors::live_frequency_entry_requests_one_qsy_data ()
{
  QTest::addColumn<QString> ("entry");
  QTest::addColumn<QString> ("completed_text");
  QTest::addColumn<Radio::Frequency> ("expected_frequency");

  QTest::newRow ("full-frequency")
    << QString {"10368.799000"} << QString {} << entered_3cm_frequency;
  QTest::newRow ("relative-khz")
    << QString {"799k"} << QString {} << entered_3cm_frequency;
  QTest::newRow ("completed-working-frequency")
    << QString {"14.074"} << QString {"14.074000"} << preferred_20m_frequency;
}

void TestFrequencyEditors::live_frequency_entry_requests_one_qsy ()
{
  QFETCH (QString, entry);
  QFETCH (QString, completed_text);
  QFETCH (Radio::Frequency, expected_frequency);
  LiveFrequencyEditorFixture fixture;

  fixture.enter (entry, completed_text);

  QCOMPARE (fixture.requests, QVector<Radio::Frequency> {expected_frequency});
  QCOMPARE (fixture.bands_at_request,
            QVector<QString> {fixture.bands.find (expected_frequency)});
  QCOMPARE (fixture.band_editor.currentText (), fixture.bands.find (expected_frequency));

  fixture.send_focus_out ();
  QCOMPARE (fixture.requests, QVector<Radio::Frequency> {expected_frequency});

  fixture.send_focus_out ();
  QCOMPARE (fixture.requests, QVector<Radio::Frequency> {expected_frequency});
}

void TestFrequencyEditors::live_frequency_band_entry_requests_working_frequency ()
{
  LiveFrequencyEditorFixture fixture;
  fixture.nominal_frequency = 14074000;
  fixture.band_editor.setCurrentText (fixture.bands.find (fixture.nominal_frequency));

  fixture.enter ("3cm");

  QCOMPARE (fixture.requests, QVector<Radio::Frequency> {preferred_3cm_frequency});
  QCOMPARE (fixture.bands_at_request, QVector<QString> {QString {"3cm"}});
  QCOMPARE (fixture.band_editor.currentText (), QString {"3cm"});
}

void TestFrequencyEditors::live_frequency_rejected_request_preserves_selection ()
{
  LiveFrequencyEditorFixture fixture;
  fixture.accept_requests = false;

  fixture.enter ("14.074", "14.074000");

  QCOMPARE (fixture.requests, QVector<Radio::Frequency> {preferred_20m_frequency});
  QCOMPARE (fixture.bands_at_request, QVector<QString> {QString {"20m"}});
  QCOMPARE (fixture.band_editor.currentText (), QString {"3cm"});

  fixture.send_focus_out ();
  QCOMPARE (fixture.requests, QVector<Radio::Frequency> {preferred_20m_frequency});
  QCOMPARE (fixture.band_editor.currentText (), QString {"3cm"});
}

void TestFrequencyEditors::live_frequency_focus_loss_without_edit_is_harmless ()
{
  LiveFrequencyEditorFixture fixture;
  fixture.band_editor.lineEdit ()->setText ("10368.200000");
  fixture.band_editor.setFocus ();
  QCoreApplication::processEvents ();

  fixture.send_focus_out ();
  QCOMPARE (fixture.band_editor.currentText (), QString {"3cm"});
  QVERIFY (fixture.requests.isEmpty ());
}

QTEST_MAIN (TestFrequencyEditors)

#include "test_frequency_editors.moc"
