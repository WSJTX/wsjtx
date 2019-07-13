#ifndef ABSTRACT_LOG_WINDOW_HPP_
#define ABSTRACT_LOG_WINDOW_HPP_

#include <QWidget>
#include "pimpl_h.hpp"

class QString;
class QSettings;
class Configuration;
class QTableView;
class QFont;

//
// AbstractLogWindow - Base class for log view windows
//
// QWidget that manages the common functionality shared by windows
// that include a QSO log view.
//
class AbstractLogWindow
  : public QWidget
{
  Q_OBJECT

public:
  AbstractLogWindow (QString const& settings_key, QSettings * settings
                     , Configuration const * configuration
                     , QWidget * parent = nullptr);
  virtual ~AbstractLogWindow () = 0;

  // set the QTableView that shows the log records, must have its
  // model set before calling this
  void set_log_view (QTableView *);

  void set_log_view_font (QFont const&);

private:
  virtual void log_model_changed (int row = -1) = 0;

  class impl;
  pimpl<impl> m_;
};

#endif
