//
// MessageAggregator - an example application that utilizes the WSJT-X
//                     messaging facility
//
// This  application is  only  provided as  a  simple GUI  application
// example to demonstrate the WSJT-X messaging facility. It allows the
// user to set  the server details either as a  unicast UDP server or,
// if a  multicast group address  is provided, as a  multicast server.
// The benefit of the multicast server is that multiple servers can be
// active at  once each  receiving all  WSJT-X broadcast  messages and
// each able to  respond to individual WSJT_X clients.  To utilize the
// multicast  group features  each  WSJT-X client  must  set the  same
// multicast  group address  as  the UDP  server  address for  example
// 239.255.0.0 for a site local multicast group.
//
// The  UI is  a small  panel  to input  the service  port number  and
// optionally  the  multicast  group  address.   Below  that  a  table
// representing  the  log  entries   where  any  QSO  logged  messages
// broadcast  from WSJT-X  clients are  displayed. The  bottom of  the
// application main  window is a  dock area  where a dock  window will
// appear for each WSJT-X client, this  window contains a table of the
// current decode  messages broadcast  from that  WSJT-X client  and a
// status line showing  the status update messages  broadcast from the
// WSJT-X client. The dock windows may  be arranged in a tab bar, side
// by side,  below each  other or, completely  detached from  the dock
// area as floating windows. Double clicking the dock window title bar
// or  dragging and  dropping with  the mouse  allows these  different
// arrangements.
//
// The application  also provides a  simple menu bar including  a view
// menu that allows each dock window to be hidden or revealed.
//

#include <locale>
#include <iostream>
#include <exception>

#include <QFile>
#include <QApplication>
#include <QMessageBox>
#include <QObject>

#include "MessageAggregatorMainWindow.hpp"

// deduce the size of an array
template<class T, size_t N>
inline
size_t size (T (&)[N]) {return N;}

int main (int argc, char * argv[])
{
  QApplication app {argc, argv};
  try
    {
      // ensure number forms are in consistent format, do this after
      // instantiating QApplication so that GUI has correct l18n
      std::locale::global (std::locale::classic ());

      app.setApplicationName ("WSJT-X Reference UDP Message Aggregator Server");
      app.setApplicationVersion ("1.0");

      QObject::connect (&app, SIGNAL (lastWindowClosed ()), &app, SLOT (quit ()));

      {
        QString ss;
        auto sf = qApp->styleSheet ();
        if (sf.size ())
          {
            sf.remove ("file:///");
            QFile file {sf};
            if (!file.open (QFile::ReadOnly))
              {
                throw std::runtime_error {
                  QString {"failed to open \"" + file.fileName () + "\": " + file.errorString ()}.toStdString ()};
              }
            ss += file.readAll ();
          }
        {
          QFile file {":/qss/default.qss"};
          if (!file.open (QFile::ReadOnly))
            {
              throw std::runtime_error {
                QString {"failed to open \"" + file.fileName () + "\": " + file.errorString ()}.toStdString ()};
            }
          ss += file.readAll ();
        }
        app.setStyleSheet (ss);
      }

      MessageAggregatorMainWindow window;
      return app.exec ();
    }
  catch (std::exception const & e)
    {
      QMessageBox::critical (nullptr, app.applicationName (), e.what ());
      std::cerr << "Error: " << e.what () << '\n';
    }
  catch (...)
    {
      QMessageBox::critical (nullptr, app.applicationName (), QObject::tr ("Unexpected error"));
      std::cerr << "Unexpected error\n";
    }
  return -1;
}
