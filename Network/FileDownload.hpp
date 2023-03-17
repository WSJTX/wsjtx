#ifndef WSJTX_FILEDOWNLOAD_H
#define WSJTX_FILEDOWNLOAD_H

#include <QObject>
#include <QString>
#include <QPointer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QTemporaryFile>
#include <QSaveFile>

class FileDownload : public QObject {
    Q_OBJECT

public:
    explicit FileDownload();
    ~FileDownload();

    void configure(QNetworkAccessManager *network_manager, const QString& source_url, const QString& destination_filename, const QString& user_agent);

private:
    QNetworkAccessManager *manager_;
    QString source_url_;
    QString destination_filename_;
    QString user_agent_;
    QPointer<QNetworkReply> reply_;
    QNetworkRequest request_;
    QSaveFile destfile_;
    bool url_valid_;
    int redirect_count_;
signals:
            void complete(QString filename);
            void progress(QString filename);
            void load_finished() const;
            void download_error (QString const& reason) const;
            void error(QString const& reason) const;


public slots:
    void start_download();
    void download(QUrl url);
    void store();
    void abort();
    void downloadComplete(QNetworkReply* data);
    void downloadProgress(qint64 recieved, qint64 total);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    void errorOccurred(QNetworkReply::NetworkError code);
#else
    void obsoleteError();
#endif
    void replyComplete();
};

#endif //WSJTX_FILEDOWNLOAD_H
