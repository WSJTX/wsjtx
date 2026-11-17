#ifndef WORKWED_BEFORE_HPP_
#define WORKWED_BEFORE_HPP_

#include <QObject>
#include <QStringList>
#include <QSet>
#include <QDate>
#include "AD1CCty.hpp"
#include "HistoryFolder.hpp"
#include "pimpl_h.hpp"

class Configuration;
class CountryDat;
class QString;
class QByteArray;

class WorkedBefore final
  : public QObject
{
  Q_OBJECT

public:
  using Continent = AD1CCty::Continent;

  explicit WorkedBefore (Configuration const *);
  ~WorkedBefore ();

  Q_SLOT void reload ();
  // How often the log and history source files are checked for changes made by
  // other programs or other WSJT-X instances; zero or less disables the check.
  // Tests use this to make the check quick or to turn it off.
  void set_source_poll_interval (int milliseconds);
  Q_SLOT bool add (QString const& call
                   , QString const& grid
                   , QString const& band
                   , QString const& mode
                   , QByteArray const& ADIF_record);

  // Remove the log file (under the same lock as appends) and rescan.
  bool erase_log ();

  QString const& path () const;
  QString const& load_status () const;
  QString const& load_errors () const;
  static QStringList normalize_history_files (QStringList const&, QString const& primary_path);
  static HistoryFolders normalize_history_folders (HistoryFolders const&);
  AD1CCty const * countries () const;
  bool country_worked (QString const& country, QString const& mode, QString const& band,
                       AD1CCty::EntityView view) const;
  bool grid_worked (QString const& grid, QString const& mode, QString const& band) const;
  bool call_worked (QString const& call, QString const& mode, QString const& band) const;
  // As call_worked, counting only QSOs made on one of days (their QSO_DATE,
  // UTC); an empty set counts every QSO.
  bool call_worked_on_days (QString const& call, QString const& mode, QString const& band,
                            QSet<QDate> const& days) const;
  // Number of different calls worked on band (any band if empty), any mode,
  // counting only QSOs made on one of days when the set is not empty.
  int calls_worked_on_days (QString const& band, QSet<QDate> const& days) const;
  bool continent_worked (Continent continent, QString const& mode, QString const& band) const;
  bool CQ_zone_worked (int CQ_zone, QString const& mode, QString const& band) const;
  bool ITU_zone_worked (int ITU_zone, QString const& mode, QString const& band) const;
  QString cty_version () const;

  Q_SIGNAL void finished_loading (int worked_before_record_count, QString const, QString const& error) const;
  Q_SIGNAL void loading_started () const;

private:
  Q_SLOT void handle_loader_finished ();

  class impl;
  pimpl<impl> m_;
};

#endif
