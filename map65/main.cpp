#include <fftw3.h>
#ifdef QT5
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include <QApplication>
#ifdef _WIN32
#include <windows.h>
#endif


#include "revision_utils.hpp"
#include "mainwindow.h"
#include "fortran_mutex.hpp"

#include <cstdio>

extern "C" {
  // Fortran procedures we need
  void four2a_ (_Complex float *, int * nfft, int * ndim, int * isign, int * iform, int len);
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
#  ifdef MAP_GUI_SUBSYSTEM
    FreeConsole();
#  endif
#endif

#ifdef _WIN32
#  ifdef MAP_GUI_SUBSYSTEM
#    pragma message("MAP_GUI_SUBSYSTEM is defined in C++")
#  else
#    pragma message("MAP_GUI_SUBSYSTEM is NOT defined in C++")
#  endif
#endif
  QApplication a {argc, argv};
  
  // Override programs executable basename as application name.
  a.setApplicationName ("MAP65");
  a.setApplicationVersion ("3.6");
  // switch off as we share an Info.plist file with WSJT-X
  a.setAttribute (Qt::AA_DontUseNativeMenuBar);
  MainWindow w;
  
  w.show ();
  QObject::connect (&a, &QApplication::lastWindowClosed, &a, &QApplication::quit);
  auto result = a.exec ();

  // clean up lazily initialized FFTW3 resources
  {
    std::lock_guard<std::mutex> lock(g_fortran_decode_mutex);
    int nfft {-1};
    int ndim {1};
    int isign {1};
    int iform {1};
    // free FFT plan resources
    four2a_ (nullptr, &nfft, &ndim, &isign, &iform, 0);
  }
  fftwf_forget_wisdom ();
  fftwf_cleanup ();

  return result;
}
