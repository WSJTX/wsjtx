#ifndef EXCEPTION_CATCHING_APPLICATION_HPP__
#define EXCEPTION_CATCHING_APPLICATION_HPP__

#include <QApplication>

#include <boost/log/core.hpp>
#include "Logger.hpp"

class QObject;
class QEvent;

//
// We  can't  use the  GUI  after  QApplication::exit() is  called  so
// uncaught exceptions can get lost  on Windows systems where there is
// no console terminal, so here we override QApplication::notify() and
// wrap the  base class  call with a  try block to  catch and  log any
// uncaught exceptions.
//
class ExceptionCatchingApplication
  : public QApplication
{
public:
  explicit ExceptionCatchingApplication (int& argc, char * * argv)
    : QApplication {argc, argv}
  {
  }
  bool notify (QObject * receiver, QEvent * e) override
  {
    try
      {
        return QApplication::notify (receiver, e);
      }
    catch (std::exception const& e)
      {
        LOG_FATAL ("Unexpected exception caught in event loop: " << e.what ());
      }
    catch (...)
      {
        LOG_FATAL ("Unexpected unknown exception caught in event loop");
      }
    // There's nowhere to go from here as Qt will not pass exceptions
    // through the event loop, so we must abort.
    boost::log::core::get ()->flush ();
    qFatal ("Aborting");
    return false;
  }
};

#endif
