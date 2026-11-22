/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <fftw3.h>

fftwf_plan wsjt_fftwf_plan_dft_1d_inplace(int length, fftwf_complex *buffer,
                                        int direction, int flags)
{
    return fftwf_plan_dft_1d(length, buffer, buffer, direction, flags);
}
