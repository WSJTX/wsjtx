#ifndef CABRILLO_LOG_HPP_
#define CABRILLO_LOG_HPP_

#include <boost/core/noncopyable.hpp>
#include <QSet>
#include <QString>
#include "Radio.hpp"
#include "pimpl_h.hpp"

class Configuration;
class QDateTime;
class QSqlTableModel;
class QTextStream;
class AD1CCty;

class CabrilloLog final
  : private boost::noncopyable
{
public:
  using Frequency = Radio::Frequency;

  explicit CabrilloLog (Configuration const *);
  ~CabrilloLog ();

  // returns false if insert fails
  bool add_QSO (Frequency, QDateTime const&, QString const& call
                , QString const& report_sent, QString const& report_received);
  bool dupe (Frequency, QString const& call) const;

  QSqlTableModel * model ();
  void reset ();
  void export_qsos (QTextStream&) const;
  QSet<QString> unique_DXCC_entities (AD1CCty const&) const;

private:
  class impl;
  pimpl<impl> m_;
};

#endif
