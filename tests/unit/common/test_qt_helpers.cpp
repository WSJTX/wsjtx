#include <QtTest>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "qt_helpers.hpp"

class TestQtHelpers
  : public QObject
{
  Q_OBJECT

public:

private:
  Q_SLOT void round_15s_date_time_up ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 500}};
    QCOMPARE (qt_round_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 30)));
  }

  Q_SLOT void truncate_15s_date_time_up ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 500}};
    QCOMPARE (qt_truncate_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void round_15s_date_time_down ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 499}};
    QCOMPARE (qt_round_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void truncate_15s_date_time_down ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 499}};
    QCOMPARE (qt_truncate_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void round_15s_date_time_on ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 15}};
    QCOMPARE (qt_round_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void truncate_15s_date_time_on ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 15}};
    QCOMPARE (qt_truncate_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void round_15s_date_time_under ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 14, 999}};
    QCOMPARE (qt_round_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void truncate_15s_date_time_under ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 14, 999}};
    QCOMPARE (qt_truncate_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15)));
  }

  Q_SLOT void round_15s_date_time_over ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 15, 1}};
    QCOMPARE (qt_round_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void truncate_15s_date_time_over ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 15, 1}};
    QCOMPARE (qt_truncate_date_time_to (dt, 15000), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void round_7p5s_date_time_up ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 26, 250}};
    QCOMPARE (qt_round_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 30)));
  }

  Q_SLOT void truncate_7p5s_date_time_up ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 26, 250}};
    QCOMPARE (qt_truncate_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }

  Q_SLOT void round_7p5s_date_time_down ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 26, 249}};
    QCOMPARE (qt_round_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }

  Q_SLOT void truncate_7p5s_date_time_down ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 26, 249}};
    QCOMPARE (qt_truncate_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }

  Q_SLOT void round_7p5s_date_time_on ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 500}};
    QCOMPARE (qt_round_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }

  Q_SLOT void truncate_7p5s_date_time_on ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 500}};
    QCOMPARE (qt_truncate_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }

  Q_SLOT void round_7p5s_date_time_under ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 499}};
    QCOMPARE (qt_round_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }

  Q_SLOT void truncate_7p5s_date_time_under ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 499}};
    QCOMPARE (qt_truncate_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 15)));
  }

  Q_SLOT void round_7p5s_date_time_over ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 501}};
    QCOMPARE (qt_round_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }

  Q_SLOT void truncate_7p5s_date_time_over ()
  {
    QDateTime dt {QDate {2020, 8, 6}, QTime {14, 15, 22, 501}};
    QCOMPARE (qt_truncate_date_time_to (dt, 7500), QDateTime (QDate (2020, 8, 6), QTime (14, 15, 22, 500)));
  }

  Q_SLOT void next_cyclic_index_advances_through_all_items ()
  {
    QCOMPARE (next_cyclic_index (0, 5), 1);
    QCOMPARE (next_cyclic_index (1, 5), 2);
    QCOMPARE (next_cyclic_index (2, 5), 3);
    QCOMPARE (next_cyclic_index (3, 5), 4);
    QCOMPARE (next_cyclic_index (4, 5), 0);
  }

  Q_SLOT void next_cyclic_index_starts_from_invalid_index ()
  {
    QCOMPARE (next_cyclic_index (-1, 5), 0);
  }

  Q_SLOT void next_cyclic_index_has_no_empty_index ()
  {
    QCOMPARE (next_cyclic_index (0, 0), -1);
  }

  Q_SLOT void next_cyclic_index_wraps_by_count ()
  {
    QCOMPARE (next_cyclic_index (2, 3), 0);
  }

  Q_SLOT void app_sounds_subdirectory_accepts_relative_children ()
  {
    QVERIFY (app_sounds_subdirectory_is_safe (""));
    QVERIFY (app_sounds_subdirectory_is_safe ("/English"));
    QVERIFY (app_sounds_subdirectory_is_safe ("voices/English"));
  }

  Q_SLOT void app_sounds_subdirectory_rejects_traversal ()
  {
    QVERIFY (!app_sounds_subdirectory_is_safe (".."));
    QVERIFY (!app_sounds_subdirectory_is_safe ("../outside"));
    QVERIFY (!app_sounds_subdirectory_is_safe ("voices/../../outside"));
    QVERIFY (!app_sounds_subdirectory_is_safe ("..\\outside"));
  }

  Q_SLOT void app_sounds_directory_falls_back_for_traversal ()
  {
    QCOMPARE (app_sounds_directory ("../outside"), app_sounds_directory ());
  }

  Q_SLOT void app_voice_entry_parsing_data ()
  {
    QTest::addColumn<QString> ("record");
    QTest::addColumn<bool> ("valid");
    QTest::addColumn<QString> ("subdirectory");
    QTest::addColumn<QString> ("display_name");

    QTest::newRow ("legacy leading slash") << "/German|Deutsch" << true << "German" << "Deutsch";
    QTest::newRow ("nested and trimmed") << " voices/German | Deutsch " << true << "voices/German" << "Deutsch";
    QTest::newRow ("blank") << "" << false << "" << "";
    QTest::newRow ("missing delimiter") << "German" << false << "" << "";
    QTest::newRow ("empty directory") << "|Deutsch" << false << "" << "";
    QTest::newRow ("root directory") << "/|Deutsch" << false << "" << "";
    QTest::newRow ("empty name") << "German|" << false << "" << "";
    QTest::newRow ("extra delimiter") << "German|Deutsch|extra" << false << "" << "";
    QTest::newRow ("parent traversal") << "../outside|Bad" << false << "" << "";
    QTest::newRow ("nested traversal") << "voices/../../outside|Bad" << false << "" << "";
    QTest::newRow ("backslash traversal") << "..\\outside|Bad" << false << "" << "";
  }

  Q_SLOT void app_voice_entry_parsing ()
  {
    QFETCH (QString, record);
    QFETCH (bool, valid);
    QFETCH (QString, subdirectory);
    QFETCH (QString, display_name);

    QString parsed_subdirectory;
    QString parsed_display_name;
    QCOMPARE (::parse_app_voice_entry (record, parsed_subdirectory, parsed_display_name), valid);
    if (valid)
      {
        QCOMPARE (parsed_subdirectory, subdirectory);
        QCOMPARE (parsed_display_name, display_name);
      }
  }

  Q_SLOT void writable_file_path_uses_writable_dir ()
  {
    QTemporaryDir writable;
    QVERIFY (writable.isValid ());

    QCOMPARE (writable_file_path (QDir {writable.path ()}, "wsjtx.log"),
              QDir {writable.path ()}.absoluteFilePath ("wsjtx.log"));
  }

  Q_SLOT void writable_override_or_installed_file_path_uses_installed_default ()
  {
    QTemporaryDir writable;
    QTemporaryDir installed;
    QVERIFY (writable.isValid ());
    QVERIFY (installed.isValid ());
    QFile file {QDir {installed.path ()}.absoluteFilePath ("cty.dat")};
    QVERIFY (file.open (QIODevice::WriteOnly));
    file.close ();

    QCOMPARE (writable_override_or_installed_file_path (QDir {writable.path ()}, QDir {installed.path ()}, "cty.dat"),
              QDir {installed.path ()}.absoluteFilePath ("cty.dat"));
  }

  Q_SLOT void writable_override_or_installed_file_path_uses_installed_default_when_missing ()
  {
    QTemporaryDir writable;
    QTemporaryDir installed;
    QVERIFY (writable.isValid ());
    QVERIFY (installed.isValid ());

    QCOMPARE (writable_override_or_installed_file_path (QDir {writable.path ()}, QDir {installed.path ()}, "cty.dat"),
              QDir {installed.path ()}.absoluteFilePath ("cty.dat"));
  }

  Q_SLOT void writable_override_or_installed_file_path_prefers_writable_override ()
  {
    QTemporaryDir writable;
    QTemporaryDir installed;
    QVERIFY (writable.isValid ());
    QVERIFY (installed.isValid ());
    QFile installed_file {QDir {installed.path ()}.absoluteFilePath ("cty.dat")};
    QVERIFY (installed_file.open (QIODevice::WriteOnly));
    installed_file.close ();
    QFile writable_file {QDir {writable.path ()}.absoluteFilePath ("cty.dat")};
    QVERIFY (writable_file.open (QIODevice::WriteOnly));
    writable_file.close ();

    QCOMPARE (writable_override_or_installed_file_path (QDir {writable.path ()}, QDir {installed.path ()}, "cty.dat"),
              QDir {writable.path ()}.absoluteFilePath ("cty.dat"));
  }

  Q_SLOT void ensure_parent_directory_creates_append_parent ()
  {
    QTemporaryDir writable;
    QVERIFY (writable.isValid ());
    auto const& file_path = QDir {writable.path ()}.absoluteFilePath ("nested/wsjtx_log.adi");

    QVERIFY (ensure_parent_directory (file_path));
    QVERIFY (QDir {writable.path ()}.exists ("nested"));
  }

  Q_SLOT void is_multicast_address_data ()
  {
    QTest::addColumn<QString> ("addr");
    QTest::addColumn<bool> ("result");

    QTest::newRow ("loopback") << "127.0.0.1" << false;
    QTest::newRow ("looback IPv6") << "::1" << false;
    QTest::newRow ("lowest-") << "223.255.255.255" << false;
    QTest::newRow ("lowest") << "224.0.0.0" << true;
    QTest::newRow ("lowest- IPv6") << "feff:ffff:ffff:ffff:ffff:ffff:ffff:ffff" << false;
    QTest::newRow ("lowest IPv6") << "ff00::" << true;
    QTest::newRow ("highest") << "239.255.255.255" << true;
    QTest::newRow ("highest+") << "240.0.0.0" << false;
    QTest::newRow ("highest IPv6") << "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff" << true;
  }

  Q_SLOT void is_multicast_address ()
  {
    QFETCH (QString, addr);
    QFETCH (bool, result);

    QCOMPARE (::is_multicast_address (QHostAddress {addr}), result);
  }

  Q_SLOT void is_MAC_ambiguous_multicast_address_data ()
  {
    QTest::addColumn<QString> ("addr");
    QTest::addColumn<bool> ("result");

    QTest::newRow ("loopback") << "127.0.0.1" << false;
    QTest::newRow ("looback IPv6") << "::1" << false;

    QTest::newRow ("lowest- R1") << "223.255.255.255" << false;
    QTest::newRow ("lowest R1") << "224.0.0.0" << false;
    QTest::newRow ("highest R1") << "224.0.0.255" << false;
    QTest::newRow ("highest+ R1") << "224.0.1.0" << false;
    QTest::newRow ("lowest- R1A") << "224.127.255.255" << false;
    QTest::newRow ("lowest R1A") << "224.128.0.0" << true;
    QTest::newRow ("highest R1A") << "224.128.0.255" << true;
    QTest::newRow ("highest+ R1A") << "224.128.1.0" << false;

    QTest::newRow ("lowest- R2") << "224.255.255.255" << false;
    QTest::newRow ("lowest R2") << "225.0.0.0" << true;
    QTest::newRow ("highest R2") << "225.0.0.255" << true;
    QTest::newRow ("highest+ R2") << "225.0.1.0" << false;
    QTest::newRow ("lowest- R2A") << "225.127.255.255" << false;
    QTest::newRow ("lowest R2A") << "225.128.0.0" << true;
    QTest::newRow ("highest R2A") << "225.128.0.255" << true;
    QTest::newRow ("highest+ R2A") << "225.128.1.0" << false;

    QTest::newRow ("lowest- R3") << "238.255.255.255" << false;
    QTest::newRow ("lowest R3") << "239.0.0.0" << true;
    QTest::newRow ("highest R3") << "239.0.0.255" << true;
    QTest::newRow ("highest+ R3") << "239.0.1.0" << false;
    QTest::newRow ("lowest- R3A") << "239.127.255.255" << false;
    QTest::newRow ("lowest R3A") << "239.128.0.0" << true;
    QTest::newRow ("highest R3A") << "239.128.0.255" << true;
    QTest::newRow ("highest+ R3A") << "239.128.1.0" << false;

    QTest::newRow ("lowest- IPv6") << "feff:ffff:ffff:ffff:ffff:ffff:ffff:ffff" << false;
    QTest::newRow ("lowest IPv6") << "ff00::" << false;
    QTest::newRow ("highest") << "239.255.255.255" << false;
    QTest::newRow ("highest+") << "240.0.0.0" << false;
    QTest::newRow ("highest IPv6") << "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff" << false;
  }

  Q_SLOT void is_MAC_ambiguous_multicast_address ()
  {
    QFETCH (QString, addr);
    QFETCH (bool, result);

    QCOMPARE (::is_MAC_ambiguous_multicast_address (QHostAddress {addr}), result);
  }
};

QTEST_MAIN (TestQtHelpers);

#include "test_qt_helpers.moc"
