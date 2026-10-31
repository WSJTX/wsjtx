#ifndef WSJT_WAVEFORM_KERNELS_H
#define WSJT_WAVEFORM_KERNELS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Advance phase_step radians per source tick; emit the first tick of each positive sample_stride.
 * Write count samples and retain phase after count * sample_stride ticks, including skipped ticks.
 * Count must be nonnegative; wave must hold count samples. */
void wsjt_render_tone(double *phase, double phase_step, int count, int sample_stride, float *wave);

#ifdef __cplusplus
}
#endif

#endif
