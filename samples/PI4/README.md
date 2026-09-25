# PI4 test recordings

`CONTRIBUTING.md` asks that a decoding change be tested against known `.wav`
files. No real over-the-air "Next Generation Beacon" recording was available
when this decoder was written, so these three files are **synthetic** —
generated directly from the same PI4 encoder the decoder itself uses
(`lib/pi4d/pi4_spec.c`'s `pi4_pack_message`/`pi4_encode_symbols_for`), not
real captures. Unlike every other mode's folder under `samples/`, they are
**not** listed in `samples/CMakeLists.txt`'s `SAMPLE_FILES` and so are not
part of the official downloadable sample database — that list is for genuine
off-air recordings, and these should stay out of it (or be replaced by real
recordings, if the project later obtains some) rather than be presented to
end users as if they were one.

All three are 28 s, 12 kHz mono 16-bit PCM, matching what
`MainWindow::save_wave_file` produces and what `pi4d` expects as input.

| File | Message | Variant | Signal | Noise (σ) | Offset | Expected `pi4d` output |
|---|---|---|---|---|---|---|
| `synthetic_OZ7IGY_PI4.wav` | OZ7IGY | PI4 | 0.30 | none | 1.20 s | `55 1.20 1034 PI4 OZ7IGY` |
| `synthetic_SR3LES_weak_PI4.wav` | SR3LES | PI4 | 0.05 | 0.08 | 1.50 s | `21 1.49 1034 PI4 SR3LES` |
| `synthetic_PE1ITR_PI4-80.wav` | PE1ITR | PI4-80 | 0.25 | 0.02 | 0.80 s | `48 0.80 1269 PI4-80 PE1ITR` |

`OZ7IGY` is the protocol page's own worked example
(rudius.net/oz2m/ngnb/pi4_.htm); `SR3LES` and `PE1ITR` match the callsigns
used in the sdroxide PI4 decoder's own test suite this was ported from. The
weak/noisy case exercises the same sensitivity margin as
`pi4d_selftest.c`'s `test_clean_signal_decodes`/noise tests; the PI4-80 file
exercises variant discrimination, which the coarse search must get right
before a Fano attempt is ever made.

Verified against the actual `pi4d` binary (not just the unit self-test) when
these files were generated - see the SNR/DT/message columns above, which are
`pi4d`'s real stdout for each file, run right before committing them.

## Regenerating or adding more

Built from a small scratch generator (not checked in - it is a thin wrapper
around the same `pi4_pack_message`/`pi4_encode_symbols_for`/`pi4_variant_*`
functions `pi4d` itself links against):

```sh
gcc -std=gnu99 -O2 -Ilib/pi4d -Ilib/wsprd \
    gen_test_wav.c lib/pi4d/pi4_spec.c lib/pi4d/pi4_demod.c \
    lib/wsprd/fano.c lib/wsprd/tab.c -lfftw3f -lm -o gen_test_wav
./gen_test_wav <outfile.wav> <TEXT> <variant 0-3> <amp> <noise_sigma> <offset_s> <period_s>
# variant: 0=PI4  1=PI4-80  2=PI4-96  3=PI4-120
```
