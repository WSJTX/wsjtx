#include <fftw3.h>
#ifdef QT5
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

#include "revision_utils.hpp"
#include "mainwindow.h"
#include "runtime_paths.h"

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
  QString appDir = QFileInfo {argc > 0 ? QString::fromLocal8Bit (argv[0]) : QString {}}.absoluteDir ().absolutePath ();

  // Read optional file to disable highDPI scaling
  QFile f(QDir {appDir}.absoluteFilePath ("DisableHighDpiScaling"));
  if (!f.exists()) QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

  QApplication a {argc, argv};

  // Override programs executable basename as application name.
  a.setApplicationName ("QMAP");
  a.setApplicationVersion ("0.7");

  QString dataDir = qmapDataDir();
  QFileInfo dataDirInfo {dataDir};
  if (!dataDirInfo.exists() || !dataDirInfo.isDir() || !dataDirInfo.isWritable()
      || !QDir::setCurrent(dataDir)) {
    QString message {"Unable to use QMAP working directory: " + dataDir};
    qWarning() << message;
    QMessageBox::critical(nullptr, QObject::tr("QMAP Startup Error"), message);
    return 1;
  }

  // QMAP C++ and Fortran code still use relative opens for runtime files.
  // Start from the writable data directory before Fortran initializes them.
  _gfortran_set_args(argc, argv);
  _gfortran_set_convert(0);
  ftninit_();
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
