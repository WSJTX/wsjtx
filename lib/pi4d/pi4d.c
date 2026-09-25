/*
 This file is part of program pi4d, a decoder for PI4, the 4-FSK
 propagation-beacon mode used by the IARU Region 1 "Next Generation
 Beacon" network. See pi4_spec.h for a fuller description and license.

 File name: pi4d.c

 Description: CLI entry point. Reads a mono 12 kHz PCM .wav file (the
 format widgets/mainwindow.cpp's MainWindow::save_wave_file always
 writes) and prints zero or more decoded PI4 transmissions to stdout,
 one line each:

   <SNR> <DT> <AudioFreqHz> <Variant> <Message>

 followed by a "<DecodeFinished>" sentinel line - mirroring wsprd's own
 QProcess protocol (see MainWindow::startP1/p1ReadFromStdout) so the
 same subprocess-handling pattern transfers directly to a pi4ReadFromStdout.
 The caller already knows the UTC time and the dial frequency (it is
 the one that recorded the file), so unlike wsprd's own output this
 line carries neither - the audio-frequency-only convention already
 used for jt9-pipeline decodes in MainWindow::postDecode.
*/

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "pi4_decode.h"

/* WSJT-X always saves 16-bit mono PCM WAV at 12 kHz with a standard
   44-byte header (see MainWindow::save_wave_file). */
static float *read_wav_mono (const char *path, long *n_samples_out)
{
  FILE *fp;
  long fsize, n_samples, i;
  int16_t *raw;
  float *out;

  fp = fopen (path, "rb");
  if (!fp) {
    fprintf (stderr, "pi4d: cannot open '%s'\n", path);
    return NULL;
  }
  if (fseek (fp, 0, SEEK_END) != 0 || (fsize = ftell (fp)) <= 44) {
    fclose (fp);
    fprintf (stderr, "pi4d: '%s' is not a readable .wav file\n", path);
    return NULL;
  }
  n_samples = (fsize - 44) / 2;
  fseek (fp, 44, SEEK_SET);

  raw = (int16_t *) malloc (sizeof (int16_t) * (size_t) n_samples);
  if (fread (raw, sizeof (int16_t), (size_t) n_samples, fp) != (size_t) n_samples) {
    fprintf (stderr, "pi4d: warning: short read on '%s'\n", path);
  }
  fclose (fp);

  out = (float *) malloc (sizeof (float) * (size_t) n_samples);
  for (i = 0; i < n_samples; i++) out[i] = (float) raw[i] / 32768.0f;
  free (raw);

  *n_samples_out = n_samples;
  return out;
}

int main (int argc, char **argv)
{
  float *audio;
  long n_samples;
  pi4_decode_t decodes[PI4_MAX_COARSE_KEEP];
  int n, i;

  if (argc != 2) {
    fprintf (stderr, "Usage: pi4d infile.wav\n");
    return 1;
  }

  audio = read_wav_mono (argv[1], &n_samples);
  if (!audio) return 1;

  pi4_decode_init ();
  /* boundary_sample=0: WSJT-X saves one period per file, aligned to the
     period start, matching how wsprd is invoked once per saved .wav. */
  n = pi4_decode_window (audio, n_samples, 0, decodes);
  for (i = 0; i < n; i++) {
    /* Report the transmission's midpoint tone (tone0 + 1.5*spacing)
       rather than tone0 itself - a more representative "where is this
       signal" frequency for display. */
    float centre_hz = decodes[i].tone0_hz + 1.5f * pi4_variant_tone_spacing_hz (decodes[i].variant);
    printf ("%4.0f %5.2f %5ld %-8s %s\n",
            decodes[i].snr_db, decodes[i].dt_sec, lroundf (centre_hz),
            pi4_variant_label (decodes[i].variant), decodes[i].text);
  }
  printf ("<DecodeFinished>\n");
  fflush (stdout);

  pi4_decode_cleanup ();
  free (audio);
  return 0;
}
