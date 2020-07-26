#include "Network/NetworkAccessManager.hpp"

#include <QString>
#include <QNetworkReply>

#include "moc_NetworkAccessManager.cpp"

NetworkAccessManager::NetworkAccessManager (QWidget * parent)
  : QNetworkAccessManager (parent)
  , parent_widget_ {parent}
{
  // handle SSL errors that have not been cached as allowed
  // exceptions and offer them to the user to add to the ignored
  // exception cache
  connect (this, &QNetworkAccessManager::sslErrors, this, &NetworkAccessManager::filter_SSL_errors);
}

void NetworkAccessManager::filter_SSL_errors (QNetworkReply * reply, QList<QSslError> const& errors)
{
  QString message;
  QList<QSslError> new_errors;
  for (auto const& error: errors)
    {
      if (!allowed_ssl_errors_.contains (error))
        {
          new_errors << error;
          message += '\n' + reply->request ().url ().toDisplayString () + ": " + error.errorString ();
        }
    }
  if (new_errors.size ())
    {
      QString certs;
      for (auto const& cert : reply->sslConfiguration ().peerCertificateChain ())
        {
          certs += cert.toText () + '\n';
        }
      if (MessageBox::Ignore == MessageBox::query_message (parent_widget_
                                                           , tr ("Network SSL/TLS Errors")
                                                           , message, certs
                                                           , MessageBox::Abort | MessageBox::Ignore))
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
}

QNetworkReply * NetworkAccessManager::createRequest (Operation operation, QNetworkRequest const& request
                                                     , QIODevice * outgoing_data)
{
  auto reply = QNetworkAccessManager::createRequest (operation, request, outgoing_data);
  // errors are usually certificate specific so passing all cached
  // exceptions here is ok
  reply->ignoreSslErrors (allowed_ssl_errors_);
  return reply;
}
