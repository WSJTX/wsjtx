#include "LotWUsers.hpp"

#include <exception>
#include <utility>

#include <QHash>
#include <QString>
#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDebug>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include "qt_helpers.hpp"
#include "Logger.hpp"
#include "FileDownload.hpp"
#include "pimpl_impl.hpp"

#include "moc_LotWUsers.cpp"

namespace
{
  // Dictionary mapping call sign to date of last upload to LotW
  using dictionary = QHash<QString, QDate>;

  struct load_result
  {
    dictionary users;
    QString error;
  };

  // Upload times and any additional fields are not needed for date lookups.
  load_result load_dictionary (QString const& lotw_csv_file)
  {
    load_result result;
    try
      {
        QFile f {lotw_csv_file};
        if (!f.open (QFile::ReadOnly | QFile::Text))
          {
            result.error = QObject::tr ("Failed to open LotW users CSV file: '%1'").arg (f.fileName ());
            return result;
          }
        QTextStream s {&f};
        for (auto l = s.readLine (); !l.isNull (); l = s.readLine ())
          {
            auto const pos = l.indexOf (',');
            if (pos <= 0) continue;
            auto const date_end = l.indexOf (',', pos + 1);
            auto const date_field = date_end < 0 ? l.mid (pos + 1) : l.mid (pos + 1, date_end - pos - 1);
            auto const call = l.left (pos).trimmed ();
            auto const date = QDate::fromString (date_field.trimmed (), "yyyy-MM-dd");
            if (!call.isEmpty () && date.isValid ()) result.users[call] = date;
          }
        if (f.error () != QFileDevice::NoError)
          {
            result.error = QObject::tr ("Failed to read LotW users CSV file: '%1'").arg (f.fileName ());
          }
      }
    catch (std::exception const& e)
      {
        result.error = QString::fromUtf8 (e.what ());
      }
    return result;
  }
}

class LotWUsers::impl final
  : public QObject
{
  Q_OBJECT

public:
  impl (LotWUsers * self, QNetworkAccessManager * network_manager)
    : QObject {self}
    , self_ {self}
    , network_manager_ {network_manager}
    , url_valid_ {false}
    , redirect_count_ {0}
    , age_constraint_ {365}
    , connected_ {false}
  {
    lotw_downloader_.setParent (this);
    connect (&loader_watcher_, &QFutureWatcher<load_result>::finished, this, [this] {
      loading_ = false;
      if (!pending_file_.isEmpty ())
        {
          if (active_generation_ == load_generation_)
            {
              auto result = loader_watcher_.result ();
              if (result.error.isEmpty ())
                {
                  // Keep the cached snapshot available while the downloaded file is parsed.
                  last_uploaded_ = std::move (result.users);
                }
            }
          auto const next_file = std::exchange (pending_file_, QString {});
          start_load (next_file);
          return;
        }
      if (active_generation_ != load_generation_)
        {
          if (!download_active_) Q_EMIT self_->load_finished ();
          return;
        }

      auto result = loader_watcher_.result ();
      auto const should_finish = !download_active_;
      QPointer<LotWUsers> owner {self_};
      if (result.error.isEmpty ())
        {
          last_uploaded_ = std::move (result.users);
          LOG_INFO (QString {"LotWUsers: Loaded %1 records from %2"}.arg (last_uploaded_.size ()).arg (active_file_));
          Q_EMIT owner->progress (QString {"Loaded %1 records from LotW."}.arg (last_uploaded_.size ()));
        }
      else
        {
          Q_EMIT owner->LotW_users_error (result.error);
        }
      if (owner && should_finish) Q_EMIT owner->load_finished ();
    });
  }

  void load (QString const& url, bool fetch, bool forced_fetch)
  {
    auto csv_file_name = csv_file_.fileName ();
    auto exists = QFileInfo::exists (csv_file_name);
    auto const downloading = fetch && (!exists || forced_fetch);
    auto const preserve_cache_load = downloading && exists && loading_ && active_file_ == csv_file_name
      && active_generation_ == load_generation_;
    if (!preserve_cache_load) ++load_generation_;
    pending_file_.clear ();
    download_active_ = false;
    abort ();                   // abort any active download
    if (downloading)
    {
      current_url_.setUrl(url);
      if (current_url_.isValid() && !QSslSocket::supportsSsl())
      {
        current_url_.setScheme("http");
      }
      redirect_count_ = 0;

      Q_EMIT self_->progress (QString("Starting download from %1").arg(url));

      lotw_downloader_.configure(network_manager_,
                                 url,
                                 csv_file_name,
                                 "WSJT-X LotW User Downloader");
      if (!connected_)
      {
        connect(&lotw_downloader_, &FileDownload::complete, this, [this] (QString const& file_name) {
            download_active_ = false;
            request_load (file_name);
        });
        connect(&lotw_downloader_, &FileDownload::download_error, this, [this] (QString const& msg) {
            LOG_INFO(QString{"LotWUsers: Error downloading LotW file: %1"}.arg(msg));
            Q_EMIT self_->LotW_users_error (msg);
        });
        connect(&lotw_downloader_, &FileDownload::load_finished, this, [this] {
            if (!download_active_) return;
            download_active_ = false;
            if (!loading_) Q_EMIT self_->load_finished ();
        });
        connect( &lotw_downloader_, &FileDownload::progress, [this] (QString const& msg) {
            Q_EMIT self_->progress (msg);
        });
        connected_ = true;
      }
        download_active_ = true;
        lotw_downloader_.start_download();
      }
    else
      {
        if (exists)
          {
            request_load (csv_file_name);
          }
      }
  }

  void abort ()
  {
    lotw_downloader_.abort();
  }

  void request_load (QString const& file_name)
  {
    if (loading_)
      {
        pending_file_ = file_name;
      }
    else
      {
        start_load (file_name);
      }
  }

  void start_load (QString const& file_name)
  {
    loading_ = true;
    active_generation_ = load_generation_;
    active_file_ = file_name;
    loader_watcher_.setFuture (QtConcurrent::run ([file_name] { return load_dictionary (file_name); }));
  }

  LotWUsers * self_;
  QNetworkAccessManager * network_manager_;
  QSaveFile csv_file_;
  bool url_valid_;
  QUrl current_url_;            // may be a redirect
  int redirect_count_;
  QPointer<QNetworkReply> reply_;
  QFutureWatcher<load_result> loader_watcher_ {this};
  bool loading_ {false};
  bool download_active_ {false};
  quint64 load_generation_ {0};
  quint64 active_generation_ {0};
  QString active_file_;
  QString pending_file_;
  dictionary last_uploaded_;
  qint64 age_constraint_;       // days
  FileDownload lotw_downloader_;
  bool connected_;
};

#include "LotWUsers.moc"

LotWUsers::LotWUsers (QNetworkAccessManager * network_manager, QObject * parent)
  : QObject {parent}
  , m_ {this, network_manager}
{

}

LotWUsers::~LotWUsers ()
{
}

void LotWUsers::set_local_file_path (QString const& path)
{
  m_->csv_file_.setFileName (path);
}

void LotWUsers::load (QString const& url, bool fetch, bool force_download)
{
  m_->load (url, fetch, force_download);
}

void LotWUsers::set_age_constraint (qint64 uploaded_since_days)
{
  m_->age_constraint_ = uploaded_since_days;
}

bool LotWUsers::user (QString const& call) const
{
  if (m_->last_uploaded_.size ())
    {
      auto p = m_->last_uploaded_.constFind (call);
      if (p != m_->last_uploaded_.end ())
        {
          auto const age = p.value ().daysTo (QDateTime::currentDateTimeUtc ().date ());
          return p.value ().isValid () && age >= 0 && age <= m_->age_constraint_;
        }
    }
  return false;
}
