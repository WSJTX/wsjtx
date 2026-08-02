# Golden fixtures

Known-good decoder outputs captured at SHA `3b354122` (a commit with zero decoder changes relative to its parent).

These fixtures are the regression baseline: any change that alters `jt9`'s stdout byte-for-byte fails the harness in CI. The harness protects decoder behavior through the streaming I/O, NDJSON output, control frames, and wsprd streaming paths.

## Current fixtures

| Fixture | Input | Mode | Decodes | Captured from |
|---|---|---|---|---|
| `ft8_210703_133430.expected.txt` | `samples/FT8/210703_133430.wav` | FT8 (`-8`) | 14 signals | `3b354122` |
| `jt9_130418_1742.expected.txt` | `samples/JT9/130418_1742.wav` | JT9 (`-9`) | 6 signals | `3b354122` |
| `q65_30a_201203_024000.expected.txt` | `samples/Q65/30A_Ionoscatter_6m/201203_024000.wav` | Q65-30A (`--q65 -p 30 -f 1000 -d 3`) | 1 signal | K1JT, 2026-08-01 |
| `q65_300a_201210_0505.expected.txt` | `samples/Q65/300A_Optical_Scatter/201210_0505.wav` | Q65-300A (`--q65 -p 300 -f 1000 -d 3`) | 1 signal | K1JT, 2026-08-02 |
| `q65_60d_201212_1838.expected.txt` | `samples/Q65/60D_EME_10GHz/201212_1838.wav` | Q65-60D (`--q65 -b D -p 60 -f 1000 -d 3`) | 1 signal | K1JT, 2026-08-02 |

## Running

```
cmake --build build --target jt9
test/run-golden-fixtures.sh
```

Exits 0 on pass, 1 on regression. CI runs this on every push/PR to `main` after the build job.

## Adding a fixture

1. Put the input `.wav` under `samples/<MODE>/` (already there for standard modes).
2. Run `./build/jt9 <flags> samples/<MODE>/<file>.wav > test/fixtures/<name>.expected.txt 2>&1`
3. Add a `run_fixture` call in `test/run-golden-fixtures.sh`.
4. Commit the expected file + script change together.

## Coverage

- FT8 + JT9 + Q65-30A + Q65-300A + Q65-60D fixtures above cover basic decoder regression.
- The streaming harnesses extend coverage to FST4 (`-7`), Q65 (`-3`), MSK144 (`-5`), and wsprd streaming.
- Streaming I/O parity: `test/run-stream-wav-parity.sh` validates that `jt9 --stream` decodes **contain** these expected decodes. The check is **containment, not identity**: streaming legitimately finds *more* signals (the early `nzhsym` passes pick up low-SNR signals) and the `snr`/`dt`/`freq` fields drift between the WAV and stream decode paths for the same signal (≈ ±2 dB / ±0.05 s / ±3 Hz — different fine estimates), so the decoded **message text** is the parity identity. FT8/JT9 only (FST4W has no good fixture; EME/JT65 I/Q paths are pass-on-absence).
