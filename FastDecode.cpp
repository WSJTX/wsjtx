#include "FastDecode.hpp"
#include "wsjtx_config.h"
#include "DecDataMutex.hpp"

#include <cstring>
#include <functional>
#include <algorithm>
#include <memory>
#include <vector>
#include <QMutexLocker>
#include <QtConcurrent/QtConcurrentRun>

extern "C" void fast_decode_ (short[], int[], double *, char[], char[], char[],
                              fortran_charlen_t, fortran_charlen_t,
                              fortran_charlen_t);

QFuture<FastDecodeResult> startFastDecode (short const samples[], int const arguments[],
                                         double period, char const myCall[],
                                         char const hisCall[])
{
  FastDecodeResult result;
  std::copy_n (arguments, result.arguments.size (), result.arguments.begin ());
  std::array<char, 12> ownCall, dxCall;
  std::copy_n (myCall, ownCall.size (), ownCall.begin ());
  std::copy_n (hisCall, dxCall.size (), dxCall.begin ());
  auto captured = std::make_shared<std::vector<short>> (360000, 0);
  {
    QMutexLocker lock {&dec_data_mutex ()};
    std::copy_n (samples, std::max (0, std::min (360000, arguments[1])),
                 captured->begin ());
  }
  return QtConcurrent::run ([=] () mutable {
    // fast_decode retains Fortran SAVE state between invocations. Independent
    // sample ownership alone does not make concurrent decoder entry safe.
    static QMutex decoderMutex;
    QMutexLocker lock {&decoderMutex};
    fast_decode_ (captured->data (), result.arguments.data (), &period,
        result.messages.data (), ownCall.data (), dxCall.data (),
        fortran_charlen_t (8000), fortran_charlen_t (12), fortran_charlen_t (12));
    return result;
  });
}
