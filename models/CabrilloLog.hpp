#ifndef CABRILLO_LOG_HPP_
#define CABRILLO_LOG_HPP_

#include <QObject>
#include <QSet>
#include <QPair>
#include <QString>
#include "Radio.hpp"
#include "pimpl_h.hpp"

class Configuration;
class QDateTime;
class QSqlTableModel;
class QTextStream;
class AD1CCty;

class CabrilloLog final
  : public QObject
{
  Q_OBJECT

public:
  using Frequency = Radio::Frequency;
  using worked_item = QPair<QString, QString>;
  using worked_set = QSet<worked_item>;

  explicit CabrilloLog (Configuration const *, QObject * parent = nullptr);
  ~CabrilloLog ();

  // returns false if insert fails
  bool add_QSO (Frequency, QString const& mode, QDateTime const&, QString const& call
                , QString const& report_sent, QString const& report_received);
  bool dupe (Frequency, QString const& call) const;

  QSqlTableModel * model ();
  void reset ();
  void export_qsos (QTextStream&) const;
  worked_set unique_DXCC_entities (AD1CCty const *) const;

  Q_SIGNAL void data_changed () const;

private:
  class impl;
  pimpl<impl> m_;
};

#endif
