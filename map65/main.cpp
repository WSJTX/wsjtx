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
#include "globals.h"
#include "runtime_paths.h"

#include <cstdio>

extern "C" void set_runtime_params_(int rate_hz, int nfft, int nfft_big);

extern "C" {
  // Fortran procedures we need
  void four2a_ (_Complex float *, int * nfft, int * ndim, int * isign, int * iform, int len);
}

extern int g_sampleRate;
extern int active_nfft;
int g_activeNfft = 32768;   // default

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
  a.setApplicationName ("MAP65");
  a.setApplicationVersion ("3.8.2");
  a.setAttribute (Qt::AA_DontUseNativeMenuBar);
  
  QString appDir = QApplication::applicationDirPath();
  QString dataDir = writableMap65DataDir();
  QSettings settings(map65SettingsFile(appDir, dataDir), QSettings::IniFormat);
  settings.beginGroup("Common");
  int srFlag = readFSam96000(settings, 1);
  settings.endGroup();
  if(srFlag <= 1) g_sampleRate = 96000;
  else if(srFlag == 2) g_sampleRate = 192000;
  if (g_sampleRate != 96000 && g_sampleRate != 192000)
      g_sampleRate = 96000;

// ------------------------------------------------------------
// NEW: compute active FFT sizes for MAP65
// ------------------------------------------------------------
int active_rate = g_sampleRate;

// Keep ~3 Hz bin resolution like WSJT-X/QMAP
auto round_pow2 = [](int x) {
    int p = 1;
    while (p < x) p <<= 1;
    return p;
};

// symspec FFT size
active_nfft = round_pow2(
    static_cast<int>(
        static_cast<long long>(BASELINE_NFFT) * active_rate / BASELINE_RATE
    )
);

g_activeNfft = active_nfft;

// big FFT size (56 symbols * sample rate)
int active_nfft_big = 56 * active_rate;

// ------------------------------------------------------------
// Push runtime parameters into Fortran BEFORE MainWindow starts
// ------------------------------------------------------------
set_runtime_params_(active_rate, active_nfft, active_nfft_big);

// ------------------------------------------------------------
// allocate buffers now that sample rate is known
// ------------------------------------------------------------
id.resize(4 * 60 * g_sampleRate);

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
