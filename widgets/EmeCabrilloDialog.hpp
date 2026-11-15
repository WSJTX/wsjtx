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

//! A Cabrillo log for the ARRL International EME Contest, made from one or
//! more ADIF logs, merged (WSJT-X's own and/or other files, e.g. one from
//! each PC of a multi-operator station): the QSOs on the chosen days, on the
//! bands and in the modes ticked, one per call, band and grid; QSOs on a band
//! whose group is not contested that weekend are flagged and can be left
//! out. QSOs logged without a grid are listed so their grids can be typed in
//! (kept between sessions); every ADIF file is only read.
class EmeCabrilloDialog final
  : public QDialog
{
  Q_OBJECT

public:
  //! days: used only the first time - after that the dialog keeps its own
  //! contest days in the settings, apart from Active Stations' "on days".
  EmeCabrilloDialog (QSettings *, QString const& adif_path, QString const& my_call,
                     QString const& my_grid, QSet<QDate> const& days,
                     QString const& save_directory, QWidget * parent = nullptr);
  ~EmeCabrilloDialog ();

  QSet<QDate> days () const {return days_;}
  //! The logs merged: paths, own_log () for WSJT-X's own.
  QStringList logs () const {return logs_;}
  static QString own_log () {return QStringLiteral ("<own>");}
  void add_log (QString const& path);
  void remove_log (int index);
  //! The Cabrillo text as it would be saved now.
  QString cabrillo_text () const {return text_;}
  EmeCabrillo::Selection const& selection () const {return selection_;}
  //! Write the Cabrillo file; false (and a message) if it cannot.
  bool save_to (QString const& path);

  //! The chosen contest days changed (the dialog's own days).
  Q_SIGNAL void days_changed (QSet<QDate> const&);

private:
  void read_settings ();
  void write_settings ();
  void choose_file ();
  void load ();
  void refresh ();
  void update_text ();
  void count_logs ();
  void show_logs ();
  void show_bands ();
  void show_modes ();
  void show_grid_rows ();
  void grid_typed (QString const& key);
  void save ();
  EmeCabrillo::Header header () const;

  QSettings * settings_;
  QString own_log_path_;
  QStringList logs_;                    // paths, own_log () = own_log_path_
  QString my_call_;
  QString my_grid_;
  QString save_directory_;
  QSet<QDate> days_;
  QVector<QVector<EmeCabrillo::Record>> log_records_;
  QVector<int> log_state_;              // per log: 0 read, 1 own log not there yet, 2 unreadable
  QStringList log_info_;                // per log: what it holds, or the error
  QVector<QVector<QDate>> log_dates_;   // the date of each of their records
  EmeCabrillo::Log log_;                // merged and made ready for select ()
  QVector<EmeCabrillo::Record> copies_; // the same QSO in a later log
  QStringList load_errors_;
  bool operators_filled_ {false};
  QHash<QString, QString> fixes_;       // record key -> grid typed in
  QMap<QString, QCheckBox *> band_boxes_;
  QSet<QString> bands_off_;
  QMap<QString, int> modes_shown_;      // mode name -> QSOs, as the boxes show
  QSet<QString> modes_off_;
  QStringList grid_keys_;               // rows shown in the grid box
  QHash<QString, QLineEdit *> grid_edits_;
  QHash<QString, QLabel *> grid_states_;
  EmeCabrillo::Selection selection_;
  QString text_;

  QGridLayout * logs_layout_;
  QPushButton * add_own_button_;
  QCheckBox * off_weekend_box_;
  QToolButton * days_button_;
  QLabel * days_label_;
  QHBoxLayout * bands_layout_;
  QHBoxLayout * modes_layout_;
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
