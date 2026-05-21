#ifndef CLOUDLOG_HPP_
#define CLOUDLOG_HPP_

#include <boost/core/noncopyable.hpp>
#include <QObject>
#include "pimpl_h.hpp"

class QString;
class QNetworkAccessManager;
class Configuration;

//
// Cloudlog
//
class Cloudlog final
  : public QObject
{
  Q_OBJECT

public:
  explicit Cloudlog (Configuration const * config, QObject * parent = nullptr);
  ~Cloudlog ();

  void logQso(QByteArray ADIF);
  Q_SLOT void testApi (QString const& url, QString const& apiKey);

  Q_SIGNAL void apikey_ok () const;
  Q_SIGNAL void apikey_ro () const;
  Q_SIGNAL void apikey_invalid () const;

private:
  class impl;
  pimpl<impl> m_;
};

#endif
