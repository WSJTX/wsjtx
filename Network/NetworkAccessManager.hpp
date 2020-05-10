#ifndef NETWORK_ACCESS_MANAGER_HPP__
#define NETWORK_ACCESS_MANAGER_HPP__

#include <QNetworkAccessManager>
#include <QList>
#include <QSslError>
#include <QNetworkReply>
#include <QString>

#include "widgets/MessageBox.hpp"

class QNetworkRequest;
class QIODevice;
class QWidget;

// sub-class QNAM to keep a list of accepted SSL errors and allow
// them in future replies
class NetworkAccessManager
  : public QNetworkAccessManager
{
public:
  NetworkAccessManager (QWidget * parent)
    : QNetworkAccessManager (parent)
  {
    // handle SSL errors that have not been cached as allowed
    // exceptions and offer them to the user to add to the ignored
    // exception cache
    connect (this, &QNetworkAccessManager::sslErrors, [this, &parent] (QNetworkReply * reply, QList<QSslError> const& errors) {
        QString message;
        QList<QSslError> new_errors;
        for (auto const& error: errors)
          {
            if (!allowed_ssl_errors_.contains (error))
              {
                new_errors << error;
                message += '\n' + reply->request ().url ().toDisplayString () + ": "
                  + error.errorString ();
              }
          }
        if (new_errors.size ())
          {
            QString certs;
            for (auto const& cert : reply->sslConfiguration ().peerCertificateChain ())
              {
                certs += cert.toText () + '\n';
              }
            if (MessageBox::Ignore == MessageBox::query_message (parent, tr ("Network SSL Errors"), message, certs, MessageBox::Abort | MessageBox::Ignore))
              {
                // accumulate new SSL error exceptions that have been allowed
                allowed_ssl_errors_.append (new_errors);
                reply->ignoreSslErrors (allowed_ssl_errors_);
              }
          }
        else
          {
            // no new exceptions so silently ignore the ones already allowed
            reply->ignoreSslErrors (allowed_ssl_errors_);
          }
      });
  }

protected:
  QNetworkReply * createRequest (Operation operation, QNetworkRequest const& request, QIODevice * outgoing_data = nullptr) override
  {
    auto reply = QNetworkAccessManager::createRequest (operation, request, outgoing_data);
    // errors are usually certificate specific so passing all cached
    // exceptions here is ok
    reply->ignoreSslErrors (allowed_ssl_errors_);
    return reply;
  }

private:
  QList<QSslError> allowed_ssl_errors_;
};

#endif
