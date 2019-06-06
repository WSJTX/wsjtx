// -*- Mode: C++ -*-
#ifndef EXPORTCABRILLO_H
#define EXPORTCABRILLO_H

#include <QDialog>
#include <QScopedPointer>

class QSettings;
class Configuration;
class CabrilloLog;
namespace Ui {
  class ExportCabrillo;
}

class ExportCabrillo final
  : public QDialog
{
  Q_OBJECT

public:
  explicit ExportCabrillo (QSettings *, Configuration const *
                           , CabrilloLog const *, QWidget * parent = nullptr);
  ~ExportCabrillo ();
  
private:
  void read_settings();
  void write_settings();
  void save_log ();

  QSettings * settings_;
  Configuration const * configuration_;
  CabrilloLog const * log_;
  QScopedPointer<Ui::ExportCabrillo> ui;
};

#endif
