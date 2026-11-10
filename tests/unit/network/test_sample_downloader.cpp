#include "wsjtx_config.h"

#include <QtTest>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>

#include "SampleDownloader.hpp"
#include "SampleDownloader/EntryName.hpp"
#include "SampleDownloader/RemoteFile.hpp"

namespace
{
  class UnsentReply final
    : public QNetworkReply
  {
  public:
    UnsentReply (QNetworkAccessManager::Operation operation, QNetworkRequest const& request, QObject * parent)
      : QNetworkReply {parent}
    {
      setOperation (operation);
      setRequest (request);
      setUrl (request.url ());
      setError (QNetworkReply::OperationCanceledError, "not sent");
      setFinished (true);
    }

    void abort () override {}

  private:
    qint64 readData (char *, qint64) override { return -1; }
  };

  class NoTlsNetworkAccessManager final
    : public QNetworkAccessManager
  {
    Q_OBJECT

  public:
    QList<QUrl> requested;

  protected:
    QNetworkReply * createRequest (Operation operation, QNetworkRequest const& request, QIODevice *) override
    {
      requested << request.url ();
      return new UnsentReply {operation, request, this};
    }

  protected Q_SLOTS:
    QStringList supportedSchemesImplementation () const
    {
      auto schemes = QNetworkAccessManager::supportedSchemesImplementation ();
      schemes.removeAll ("https");
      return schemes;
    }
  };

  class Listener final
    : public RemoteFile::ListenerInterface
  {
  public:
    void error (QString const& title, QString const& message) override
    {
      errors << title + ": " + message;
    }

    void download_finished (bool success) override
    {
      finished << success;
    }

    QStringList errors;
    QList<bool> finished;
  };
}

class TestSampleDownloader final
  : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void entry_name_validation_data ()
  {
    QTest::addColumn<QString> ("name");
    QTest::addColumn<bool> ("valid");

    QTest::newRow ("file") << "FT8.wav" << true;
    QTest::newRow ("space") << "Fox sample.wav" << true;
    QTest::newRow ("leading dots") << "..sample.wav" << true;
    QTest::newRow ("hash") << "sample#1.wav" << true;
    QTest::newRow ("percent") << "sample%2F1.wav" << true;
    QTest::newRow ("unicode") << QString::fromUtf8 ("échantillon.wav") << true;
    QTest::newRow ("empty") << "" << false;
    QTest::newRow ("current directory") << "." << false;
    QTest::newRow ("parent directory") << ".." << false;
    QTest::newRow ("forward slash") << "../outside.wav" << false;
    QTest::newRow ("backslash") << "..\\outside.wav" << false;
    QTest::newRow ("absolute path") << "/tmp/outside.wav" << false;
    QTest::newRow ("drive path") << "C:\\outside.wav" << false;
    QTest::newRow ("colon") << "sample:name.wav" << false;
    QTest::newRow ("semicolon") << "sample;name.wav" << false;
    QTest::newRow ("question mark") << "sample?.wav" << false;
    QTest::newRow ("angle bracket") << "sample<1>.wav" << false;
    QTest::newRow ("quote") << "sample\"1.wav" << false;
    QTest::newRow ("pipe") << "sample|1.wav" << false;
    QTest::newRow ("asterisk") << "sample*.wav" << false;
    QTest::newRow ("control") << QString {"sample\n1.wav"} << false;
    QTest::newRow ("delete") << QString {"sample"} + QChar {0x7f} + ".wav" << false;
    QTest::newRow ("trailing dot") << "sample.wav." << false;
    QTest::newRow ("trailing space") << "sample.wav " << false;
    QTest::newRow ("device") << "NUL" << false;
    QTest::newRow ("device extension") << "con.wav" << false;
    QTest::newRow ("spaced device extension") << "NUL .wav" << false;
    QTest::newRow ("serial device") << "CoM9.log" << false;
    QTest::newRow ("parallel device") << "lpt1.txt" << false;
    QTest::newRow ("superscript serial one") << QString::fromUtf8 ("COM¹") << false;
    QTest::newRow ("superscript serial two") << QString::fromUtf8 ("com².wav") << false;
    QTest::newRow ("superscript serial three") << QString::fromUtf8 ("CoM³.log") << false;
    QTest::newRow ("superscript parallel one") << QString::fromUtf8 ("LPT¹") << false;
    QTest::newRow ("superscript parallel two") << QString::fromUtf8 ("lpt².txt") << false;
    QTest::newRow ("superscript parallel three") << QString::fromUtf8 ("LpT³.wav") << false;
    QTest::newRow ("non-device serial") << "COM10.wav" << true;
    QTest::newRow ("non-device superscript") << QString::fromUtf8 ("COM⁴.wav") << true;
    QTest::newRow ("non-device superscript suffix") << QString::fromUtf8 ("COM¹0.wav") << true;
    QTest::newRow ("ordinary superscript") << QString::fromUtf8 ("sample¹.wav") << true;
  }

  void entry_name_validation ()
  {
    QFETCH (QString, name);
    QFETCH (bool, valid);

    QCOMPARE (sample_downloader::valid_entry_name (name), valid);
  }

  void relative_entry_paths_stay_in_samples ()
  {
    QCOMPARE (sample_downloader::relative_entry_path (QDir {"samples"}, "FT8.wav")
              , QString {"samples/FT8.wav"});
    QCOMPARE (sample_downloader::relative_entry_path (QDir {"samples/Q65"}, "sample#1.wav")
              , QString {"samples/Q65/sample#1.wav"});
    QVERIFY (sample_downloader::relative_entry_path (QDir {"samples/../outside"}, "sample.wav").isEmpty ());
    QVERIFY (sample_downloader::relative_entry_path (QDir {"samples"}, "../outside.wav").isEmpty ());
  }

  void entry_urls_encode_path_metacharacters ()
  {
    QUrl const root {"https://mirror.example/wsjtx/"};

    auto const hash_url = sample_downloader::entry_url (root, "samples/sample#1.wav");
    QCOMPARE (hash_url.toString (QUrl::FullyEncoded)
              , QString {"https://mirror.example/wsjtx/samples/sample%231.wav"});
    QVERIFY (hash_url.query ().isEmpty ());
    QVERIFY (hash_url.fragment ().isEmpty ());

    auto const percent_url = sample_downloader::entry_url (root, "samples/%2e%2e.wav");
    QCOMPARE (percent_url.toString (QUrl::FullyEncoded)
              , QString {"https://mirror.example/wsjtx/samples/%252e%252e.wav"});
    QCOMPARE (percent_url.path (QUrl::FullyDecoded), QString {"/wsjtx/samples/%2e%2e.wav"});
  }

  void unsaved_url_reads_as_pages ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
    QCOMPARE (saved_samples_url (settings), QString {"https://wsjtx.github.io/wsjtx/"});
  }

  void saved_sourceforge_default_reads_as_pages ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
    settings.setValue ("SamplesURL", "http://downloads.sourceforge.net/project/wsjt/");
    QCOMPARE (saved_samples_url (settings), QString {"https://wsjtx.github.io/wsjtx/"});
  }

  void custom_url_is_kept ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
    settings.setValue ("SamplesURL", "https://mirror.example.org/wsjt/");
    QCOMPARE (saved_samples_url (settings), QString {"https://mirror.example.org/wsjt/"});
  }

  void default_url_is_not_saved ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
    settings.setValue ("SamplesURL", "http://downloads.sourceforge.net/project/wsjt/");
    save_samples_url (settings, "https://wsjtx.github.io/wsjtx/");
    QVERIFY (!settings.contains ("SamplesURL"));
  }

  void custom_url_is_saved ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    QSettings settings {directory.filePath ("settings.ini"), QSettings::IniFormat};
    save_samples_url (settings, "https://mirror.example.org/wsjt/");
    QCOMPARE (settings.value ("SamplesURL").toString (), QString {"https://mirror.example.org/wsjt/"});
  }

  void https_without_tls_reports_tls_required ()
  {
    QTemporaryDir directory;
    QVERIFY (directory.isValid ());
    NoTlsNetworkAccessManager network;
    QVERIFY (!network.supportedSchemes ().contains ("https"));
    Listener listener;
    RemoteFile file {&listener, &network, directory.filePath ("samples/contents_3.3.json")};
    file.sync (QUrl {"https://wsjtx.github.io/wsjtx/samples/contents_3.3.json"});
    QCOMPARE (network.requested, QList<QUrl> {});
    QCOMPARE (listener.finished, QList<bool> {false});
    QCOMPARE (listener.errors.size (), 1);
    QVERIFY2 (listener.errors.first ().contains ("SSL/TLS"), qPrintable (listener.errors.first ()));
    QVERIFY (!file.local ());
  }
};

QTEST_GUILESS_MAIN (TestSampleDownloader)

#include "test_sample_downloader.moc"
