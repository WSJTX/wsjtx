// -*- Mode: C++ -*-
#ifndef WSJTX_WIDGETS_EME_CABRILLO_DIALOG_HPP
#define WSJTX_WIDGETS_EME_CABRILLO_DIALOG_HPP

#include <QDate>
#include <QDialog>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>

#include "logbook/EmeCabrillo.hpp"

class QSettings;
class QLabel;
class QToolButton;
class QHBoxLayout;
class QGroupBox;
class QGridLayout;
class QLineEdit;
class QRadioButton;
class QPlainTextEdit;
class QPushButton;
class QCheckBox;

//! A Cabrillo log for the ARRL International EME Contest, made from an ADIF
//! log (WSJT-X's own, or any other file, e.g. from a contest PC): the QSOs on
//! the chosen days, on the bands ticked, one per call, band and grid. QSOs
//! logged without a grid are listed so their grids can be typed in; the
//! ADIF file is only read.
class EmeCabrilloDialog final
  : public QDialog
{
  Q_OBJECT

public:
  EmeCabrilloDialog (QSettings *, QString const& adif_path, QString const& my_call,
                     QString const& my_grid, QSet<QDate> const& days,
                     QString const& save_directory, QWidget * parent = nullptr);
  ~EmeCabrilloDialog ();

  QSet<QDate> days () const {return days_;}
  QString log_path () const;
  //! The Cabrillo text as it would be saved now.
  QString cabrillo_text () const {return text_;}
  EmeCabrillo::Selection const& selection () const {return selection_;}
  //! Write the Cabrillo file; false (and a message) if it cannot.
  bool save_to (QString const& path);

  //! The chosen days changed (Active Stations uses the same days).
  Q_SIGNAL void days_changed (QSet<QDate> const&);

private:
  void read_settings ();
  void write_settings ();
  void choose_file ();
  void use_own_log ();
  void load ();
  void refresh ();
  void show_bands ();
  void show_grid_rows ();
  void grid_typed (QString const& key);
  void save ();
  EmeCabrillo::Header header () const;

  QSettings * settings_;
  QString own_log_;
  QString other_log_;                   // empty: own_log_
  QString my_call_;
  QString my_grid_;
  QString save_directory_;
  QSet<QDate> days_;
  QVector<EmeCabrillo::Record> records_;
  QString load_error_;
  QHash<QString, QString> fixes_;       // record key -> grid typed in
  QMap<QString, QCheckBox *> band_boxes_;
  QSet<QString> bands_off_;
  QStringList grid_keys_;               // rows shown in the grid box
  QHash<QString, QLineEdit *> grid_edits_;
  QHash<QString, QLabel *> grid_states_;
  EmeCabrillo::Selection selection_;
  QString text_;

  QLabel * log_label_;
  QToolButton * days_button_;
  QLabel * days_label_;
  QHBoxLayout * bands_layout_;
  QGroupBox * grids_box_;
  QGridLayout * grids_layout_;
  QRadioButton * single_op_;
  QRadioButton * multi_op_;
  QLineEdit * operators_;
  QLineEdit * location_;
  QLineEdit * name_;
  QLineEdit * address_;
  QLineEdit * city_;
  QLineEdit * state_;
  QLineEdit * postcode_;
  QLineEdit * country_;
  QLineEdit * email_;
  QLabel * summary_;
  QPlainTextEdit * preview_;
  QPushButton * save_button_;
};

#endif
