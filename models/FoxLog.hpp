#ifndef FOX_LOG_HPP_
#define FOX_LOG_HPP_

#include <boost/core/noncopyable.hpp>
#include "pimpl_h.hpp"

class QDateTime;
class QString;
class QSqlTableModel;
class QTextStream;
class Configuration;

class FoxLog final
  : private boost::noncopyable
{
public:
  explicit FoxLog (Configuration const *);
  ~FoxLog ();

  // returns false if insert fails, dupe call+band
  bool add_QSO (QDateTime const&, QString const& call, QString const& grid
                , QString const& report_sent, QString const& report_received
                , QString const& band);
  bool dupe (QString const& call, QString const& band) const;

  QSqlTableModel * model ();
  void reset ();
  void export_qsos (QTextStream&) const;
  QString rate();
  double rate_last_n(int n);  // get the rate for the last n
  int rate_60m();

private:
  class impl;
  pimpl<impl> m_;
};

#endif
