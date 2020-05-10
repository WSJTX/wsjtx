#ifndef LOTW_USERS_HPP_
#define LOTW_USERS_HPP_

#include <boost/core/noncopyable.hpp>
#include <QObject>
#include "pimpl_h.hpp"

class QString;
class QDate;
class QNetworkAccessManager;

//
// LotWUsers - Lookup Logbook of the World users
//
class LotWUsers final
  : public QObject
{
  Q_OBJECT

public:
  explicit LotWUsers (QNetworkAccessManager *, QObject * parent = 0);
  ~LotWUsers ();

  void set_local_file_path (QString const&);

  Q_SLOT void load (QString const& url, bool fetch = true, bool force_download = false);
  Q_SLOT void set_age_constraint (qint64 uploaded_since_days);

  // returns true if the specified call sign 'call' has uploaded their
  // log to LotW in the last 'age_constraint_days' days
  bool user (QString const& call) const;

  Q_SIGNAL void LotW_users_error (QString const& reason) const;
  Q_SIGNAL void load_finished () const;

private:
  class impl;
  pimpl<impl> m_;
};

#endif
