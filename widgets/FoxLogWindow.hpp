#ifndef FOX_LOG_WINDOW_HPP_
#define FOX_LOG_WINDOW_HPP_

#include "AbstractLogWindow.hpp"
#include "pimpl_h.hpp"

class QSettings;
class Configuration;
class QFont;
class FoxLog;

class FoxLogWindow final
  : public AbstractLogWindow
{
  Q_OBJECT

public:
  explicit FoxLogWindow (QSettings *, Configuration const *, FoxLog * fox_log
                         , QWidget * parent = nullptr);
  ~FoxLogWindow ();

  void callers (int);
  void queued (int);
  void rate (int);
  void rate (QString);

  Q_SIGNAL void reset_log_model () const;

private:
  void log_model_changed (int row) override;

  class impl;
  pimpl<impl> m_;
};

#endif
