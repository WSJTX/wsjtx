#ifndef WSPRNET_H
#define WSPRNET_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <QHash>
#include <QUrlQuery>
#include <QQueue>
#include <QDateTime>
#include <QVector>
#include <QByteArray>
#include <QSet>

class QNetworkAccessManager;
class QNetworkReply;

class WSPRNet : public QObject
{
  Q_OBJECT

  using SpotQueue = QQueue<QUrlQuery>;

public:
  struct RetryPolicy
  {
    int max_attempts = 7;
    int max_pending = 1024;
    int ttl_ms = 60 * 60 * 1000;
    double jitter_fraction = 0.25;
    QVector<int> retry_delays_ms {15000, 60000, 120000, 300000, 600000, 1200000};
  };

  struct StationContext
  {
    StationContext () = default;
    StationContext (QString const& call, QString const& grid, QString const& rfreq, QString const& tfreq,
                    QString const& mode, float TR_period, QString const& tpct, QString const& dbm,
                    QString const& version)
      : call {call}
      , grid {grid}
      , rfreq {rfreq}
      , tfreq {tfreq}
      , mode {mode}
      , TR_period {TR_period}
      , tpct {tpct}
      , dbm {dbm}
      , version {version}
    {
    }

    QString call;
    QString grid;
    QString rfreq;
    QString tfreq;
    QString mode;
    float TR_period = 0.F;
    QString tpct;
    QString dbm;
    QString version;
  };

  explicit WSPRNet (QObject *parent = nullptr);
  WSPRNet (QNetworkAccessManager *network_manager, RetryPolicy retry_policy,
           bool take_network_manager_ownership, QObject *parent = nullptr);
  void upload (QString const& call, QString const& grid, QString const& rfreq, QString const& tfreq,
               QString const& mode, float TR_period, QString const& tpct, QString const& dbm,
               QString const& version, QString const& fileName);
  void post (QString const& call, QString const& grid, QString const& rfreq, QString const& tfreq,
             QString const& mode, float TR_period, QString const& tpct, QString const& dbm,
             QString const& version, QString const& decode_text = QString {});
  void queueWsprFile (StationContext const& context, QString const& file_name);
  void queueFst4wDecode (StationContext const& context, QString const& decode_text);
  void flush (StationContext const& context);
signals:
  void uploadStatus (QString);

public slots:
  void networkReply (QNetworkReply *);
  void work ();
  void abortOutstandingRequests ();

private:
  enum class UploadSource {Direct, File};
  enum class PayloadKind {Spot, Status};

  struct PendingUpload
  {
    QUrlQuery query;
    UploadSource source;
    PayloadKind kind;
    QString source_file;
    int file_batch_id;
    QDateTime queued_at;
    QDateTime expires_at;
    QDateTime next_attempt_at;
    int attempts;
    QString url;
    int logical_upload_id;
  };

  struct FileSnapshot
  {
    QString path;
    qint64 size = -1;
    QByteArray hash;
  };

  struct FileUploadState
  {
    FileSnapshot snapshot;
    int total = 0;
    QSet<int> accepted;
    QSet<int> failed;
  };

  void applyContext (StationContext const& context);
  FileSnapshot snapshotFile (QString const& file_name) const;
  bool fileMatchesSnapshot (FileSnapshot const& snapshot) const;
  bool hasPendingFileLeg (int file_batch_id, int logical_upload_id) const;
  bool decodeLine (QString const& line, SpotQueue::value_type& query) const;
  SpotQueue::value_type urlEncodeNoSpot () const;
  SpotQueue::value_type urlEncodeSpot (SpotQueue::value_type& spot) const;
  QString encode_mode () const;
  void enqueueUpload (QUrlQuery const& query, UploadSource source, PayloadKind kind, QString const& source_file = QString {}, int file_batch_id = 0);
  int beginFileBatch (FileSnapshot const& snapshot);
  void startUploadSession ();
  void scheduleWork ();
  void pruneExpiredUploads (QDateTime const& now);
  void enforcePendingLimit ();
  void sendUpload (PendingUpload upload);
  bool replyAccepted (PendingUpload const& upload, QNetworkReply *reply, QString& server_response) const;
  bool canRetry (PendingUpload const& upload, QDateTime const& now, int retry_delay_ms) const;
  int retryDelayMs (PendingUpload const& upload) const;
  int uploadsToSend () const;
  void retryUpload (PendingUpload upload, QDateTime const& now, int retry_delay_ms);
  void markAccepted (PendingUpload const& upload);
  void markFailed (PendingUpload const& upload);
  void maybeRemoveFailedFileBatch (int file_batch_id);
  void maybeRemoveCompletedFile (int file_batch_id);
  void maybeFinalize ();

  QNetworkAccessManager * network_manager_;
  RetryPolicy retry_policy_;
  QHash<QNetworkReply *, PendingUpload> outstanding_requests_;
  QHash<int, FileUploadState> file_uploads_;
  QString m_call;
  QString m_grid;
  QString m_rfreq;
  QString m_tfreq;
  QString m_mode;
  QString m_tpct;
  QString m_dbm;
  QString m_vers;
  float TR_period_;
  int uploads_started_;
  int next_file_batch_id_;
  int next_logical_upload_id_;
  QQueue<PendingUpload> pending_uploads_;
  QTimer upload_timer_;
  bool upload_session_active_;
};

#endif // WSPRNET_H
