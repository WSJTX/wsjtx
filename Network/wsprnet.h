#ifndef WSPRNET_H
#define WSPRNET_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <QList>
#include <QUrlQuery>
#include <QQueue>

class QNetworkAccessManager;
class QNetworkReply;

class WSPRNet : public QObject
{
  Q_OBJECT

  using SpotQueue = QQueue<QUrlQuery>;

public:
  explicit WSPRNet (QNetworkAccessManager *, QObject *parent = nullptr);
  void upload (QString const& call, QString const& grid, QString const& rfreq, QString const& tfreq,
               QString const& mode, float TR_peirod, QString const& tpct, QString const& dbm,
               QString const& version, QString const& fileName);
  void post (QString const& call, QString const& grid, QString const& rfreq, QString const& tfreq,
             QString const& mode, float TR_period, QString const& tpct, QString const& dbm,
             QString const& version, QString const& decode_text = QString {});
signals:
  void uploadStatus (QString);

public slots:
  void networkReply (QNetworkReply *);
  void work ();
  void abortOutstandingRequests ();

private:
  bool decodeLine (QString const& line, SpotQueue::value_type& query) const;
  SpotQueue::value_type urlEncodeNoSpot () const;
  SpotQueue::value_type urlEncodeSpot (SpotQueue::value_type& spot) const;
  QString encode_mode () const;

  QNetworkAccessManager * network_manager_;
  QList<QNetworkReply *> m_outstandingRequests;
  QString m_call;
  QString m_grid;;
  QString m_rfreq;
  QString m_tfreq;
  QString m_mode;
  QString m_tpct;
  QString m_dbm;
  QString m_vers;
  QString m_file;
  float TR_period_;
  int spots_to_send_;
  SpotQueue spot_queue_;
  QTimer upload_timer_;
  int m_uploadType;
};

#endif // WSPRNET_H
