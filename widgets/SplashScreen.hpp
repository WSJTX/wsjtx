#ifndef SPLASH_SCREEN_HPP___
#define SPLASH_SCREEN_HPP___

#include <QSplashScreen>

#include "pimpl_h.hpp"

class QEvent;

class SplashScreen final
  : public QSplashScreen
{
  Q_OBJECT

public:
  SplashScreen ();
  ~SplashScreen ();

  Q_SIGNAL void disabled ();

protected:
  bool eventFilter (QObject *, QEvent *) override;

private:
  class impl;
  pimpl<impl> m_;
};

#endif
