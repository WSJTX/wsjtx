#ifndef SPECTRUM_COLUMNS_H
#define SPECTRUM_COLUMNS_H

#include <algorithm>

namespace qmap_runtime {

// Reduce the averaged spectrum splot[0..nfft) to one value per visible plot column:
// the maximum over nbpp bins, the first bin being i0+1.  A strong-signal tag (lstrong has
// 1024 entries across the sampled bandwidth) makes the column value negative.  Only the
// first `cols` columns are filled (the rest are zeroed), and bins outside the spectrum
// are not read: the span can reach past the sampled band.
inline void reduceToColumns(float const* splot, int nfft, int i0, int nbpp, int cols,
                            unsigned char const* lstrong, float* swide, int nswide)
{
  cols = std::min(cols, nswide);
  int i = i0;
  for (int j = 0; j < cols; ++j) {
    float smax = 0;
    for (int k = 0; k < nbpp; ++k) {
      ++i;
      if (i >= 0 && i < nfft && splot[i] > smax) smax = splot[i];
    }
    swide[j] = smax;
    if (i >= 0 && i < nfft) {
      int const ls = std::min(1 + static_cast<int>(i * 1024LL / nfft), 1023);
      if (lstrong[ls] != 0) swide[j] = -smax;
    }
  }
  for (int j = std::max(cols, 0); j < nswide; ++j) swide[j] = 0;
}

}  // namespace qmap_runtime

#endif // SPECTRUM_COLUMNS_H
