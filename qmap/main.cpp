#include <fftw3.h>
#ifdef QT5
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include <QApplication>

#include "revision_utils.hpp"
#include "mainwindow.h"

extern "C" {
  // Fortran procedures we need
  void four2a_ (_Complex float *, int * nfft, int * ndim, int * isign, int * iform, int len);

  void _gfortran_set_args(int argc, char *argv[]);
  void _gfortran_set_convert(int conv);
  void ftninit_(void);
  void fftbig_(float dd[], int* nfft);
}

int main(int argc, char *argv[])
{
  QApplication a {argc, argv};

// Initialize libgfortran:
  _gfortran_set_args(argc, argv);
  _gfortran_set_convert(0);
  ftninit_();

  // Override programs executable basename as application name.
  a.setApplicationName ("QMAP");
  a.setApplicationVersion ("0.4");
  // switch off as we share an Info.plist file with WSJT-X
  a.setAttribute (Qt::AA_DontUseNativeMenuBar);
  MainWindow w;
  w.show ();
  QObject::connect (&a, &QApplication::lastWindowClosed, &a, &QApplication::quit);
  auto result = a.exec ();

  // clean up lazily initialized FFTW3 resources
  {
    int nfft {-1};
    int ndim {1};
    int isign {1};
    int iform {1};
    // free FFT plan resources
    four2a_ (nullptr, &nfft, &ndim, &isign, &iform, 0);
    fftbig_(nullptr, &nfft);
  }
  fftwf_forget_wisdom ();
  fftwf_cleanup ();
  return result;
}
