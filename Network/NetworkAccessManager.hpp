#ifndef NETWORK_ACCESS_MANAGER_HPP__
#define NETWORK_ACCESS_MANAGER_HPP__

#include <QNetworkAccessManager>
#include <QList>
#include <QSslError>

#include "widgets/MessageBox.hpp"

class QNetworkRequest;
class QIODevice;
class QWidget;

// sub-class QNAM to keep a list of accepted SSL errors and allow
// them in future replies
class NetworkAccessManager
  : public QNetworkAccessManager
{
  Q_OBJECT

public:
  explicit NetworkAccessManager (QWidget * parent);

protected:
  QNetworkReply * createRequest (Operation, QNetworkRequest const&, QIODevice * = nullptr) override;

private:
  void filter_SSL_errors (QNetworkReply * reply, QList<QSslError> const& errors);

  QWidget * parent_widget_;
  QList<QSslError> allowed_ssl_errors_;
};

#endif
