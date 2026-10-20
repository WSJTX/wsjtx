# Golden fixtures

Known-good decoder outputs captured at SHA `3b354122` (a commit with zero decoder changes relative to its parent).

These fixtures are the regression baseline: any change that alters `jt9`'s or `wsprd`'s stdout byte-for-byte fails the harness in CI. The harness protects decoder behavior through the streaming I/O, NDJSON output, control frames, and wsprd streaming paths.

## Current fixtures

| Fixture | Input | Mode | Decodes | Captured from |
|---|---|---|---|---|
| `ft8_210703_133430.expected.txt` | `samples/FT8/210703_133430.wav` | FT8 (`-8`) | 14 signals | `3b354122` |
| `jt9_130418_1742.expected.txt` | `samples/JT9/130418_1742.wav` | JT9 (`-9`) | 6 signals | `3b354122` |
| `q65_30a_201203_024000.expected.txt` | `samples/Q65/30A_Ionoscatter_6m/201203_024000.wav` | Q65-30A (`--q65 -p 30 -f 1000 -d 3`) | 1 signal | K1JT, 2026-08-01 |
| `q65_300a_201210_0505.expected.txt` | `samples/Q65/300A_Optical_Scatter/201210_0505.wav` | Q65-300A (`--q65 -p 300 -f 1000 -d 3`) | 1 signal | K1JT, 2026-08-02 |
| `q65_60d_201212_1838.expected.txt` | `samples/Q65/60D_EME_10GHz/201212_1838.wav` | Q65-60D (`--q65 -b D -p 60 -f 1000 -d 3`) | 1 signal | K1JT, 2026-08-02 |
| `msk144_181211_120800.expected.txt` | `samples/MSK144/181211_120800.wav` | MSK144 (`--msk144 -p 15 -f 1500 -F 50 -d 3`) | 2 signals | K1JT, 2026-08-02 |
| `fst4_210115_0058.expected.txt` | `samples/FST4+FST4W/210115_0058.wav` | FST4 (`--fst4 -p 60 -f 1331 -L 1000 -H 1400 -d 3`) | 3 signals | K1JT, 2026-08-02 |
| `fst4w_201230_0300.expected.txt` | `samples/FST4+FST4W/201230_0300.wav` | FST4W (`--fst4w -p 1800 -f 1433 -F 100 -d 3`) | 1 signal | K1JT, 2026-08-02 |
| `ft4_000000_000002.expected.txt` | `samples/FT4/000000_000002.wav` | FT4 (`--ft4 -d 3`) | 19 signals | K1JT, 2026-08-02 |
| `jt65_000000_0004.expected.txt` | `samples/JT65/JT65B/000000_0004.wav` | JT65 (`--jt65 -b B -f 1700 -d 3`) | 1 signal | K1JT, 2026-08-02 |
| `jt4_OK1KIR_141105_175700.expected.txt` | `samples/JT4/JT4F/OK1KIR_141105_175700.WAV` | JT4-F (`--jt4 -b F -f 1200 -d 3`) | 1 signal | K1JT, 2026-08-02 |
| `wspr_150426_0918.expected.txt` | `samples/WSPR/150426_0918.wav` | WSPR (`wsprd`, no flags) | 9 signals | K1JT, 2026-08-02 |

## Running

Two independent ways to run these:

- **Standalone script** (manual/local use, not itself CI-invoked):
  ```
  cmake --build build --target jt9 wsprd
  test/run-golden-fixtures.sh
  ```
  Exits 0 on pass, 1 on regression.
- **ctest** (this is what CI actually runs): each fixture above is also registered as a `test_jt9_golden_*`/`test_wsprd_golden_*` test via `wsjt_add_jt9_golden_test`/`wsjt_add_wsprd_golden_test` in the top-level `CMakeLists.txt`, using `tests/integration/{jt9,wsprd}/compare_*_output.cmake` for an isolated, byte-exact comparison. These are gated to Linux x86-64 only (`ctest -R test_jt9_golden` / `test_wsprd_golden` to run just these) -- decoder output can drift in small ways across compilers/architectures, so the byte-exact check is pinned to one canonical platform; the other four CI legs still run everything else in `ctest`.

## Adding a fixture

1. Put the input `.wav` under `samples/<MODE>/` (already there for standard modes).
2. Run `./build/jt9 <flags> samples/<MODE>/<file>.wav > test/fixtures/<name>.expected.txt 2>&1` (or `./build/wsprd <flags> <file>.wav > ...` for WSPR).
3. Add a `run_fixture` call in `test/run-golden-fixtures.sh`, **and** a `wsjt_add_jt9_golden_test`/`wsjt_add_wsprd_golden_test` call in the top-level `CMakeLists.txt` -- the shell script alone does not run in CI.
4. Commit the expected file + both script/CMake changes together.

## Coverage

- FT8 + JT9 + Q65-30A + Q65-300A + Q65-60D + MSK144 + FST4 + FST4W + FT4 + JT65 + JT4 + WSPR fixtures above cover basic decoder regression.
- The streaming harnesses extend coverage to FST4 (`-7`), Q65 (`-3`), MSK144 (`-5`), and wsprd streaming.
- Streaming I/O parity: `test/run-stream-wav-parity.sh` validates that `wsjt-codec --stream` decodes **contain** these expected decodes. The check is **containment, not identity**: streaming legitimately finds *more* signals (the early `nzhsym` passes pick up low-SNR signals) and the `snr`/`dt`/`freq` fields drift between the WAV and stream decode paths for the same signal (≈ ±2 dB / ±0.05 s / ±3 Hz — different fine estimates), so the decoded **message text** is the parity identity. FT8/JT9 only (FST4W has no good fixture; EME/JT65 I/Q paths are pass-on-absence).

## GUI FT8 receive scheduling

`test_wsjtx_live_audio_ft8_two_stage` and
`test_wsjtx_live_audio_ft8_three_stage` replay the original FT8 recording at
normal pace through FixtureAudioInput, Detector, MainWindow, and jt9. They select
MTD with Normal depth, four workers, one final cycle, and minimum sensitivity.
Early requests still use the standard decoder. They carry the production
`nft8cycles=2` value, but the standard decoder does not consume that MTD parameter;
it does not mean two executed standard-decoder cycles. Fast depth is unsuitable
because it skips early decoding.

The configured sequences are standard at half-symbol 41 then MTD at 49, or
standard at 41 and 46 then MTD at 50. Each receive DSP point is observed
independently of request submission. An available decoder must receive its
scheduled request; an occupied decoder may skip stage 46 or defer the final.
Stage 41 must start with the backend ready. Skipped early work must not be
published later. Each published invocation must complete exactly once without
overlap, with counts derived from the observed availability at the triggers.
The test logs which path ran; a backpressured run does not establish stage-46
execution coverage.

The final request must be published or captured into the pending slot at its
normal receive DSP point, before the receive consumer accepts the full recording.
Its available PCM must match the original through exactly 169344 or 172800
samples, as appropriate; early requests use 141696 or 158976 samples. The rest of
the allocated 180000-sample window must still be zero. A deferred publication
must retain the captured identity, selected settings, and audio extent even if playback
has finished by then. Request UTC and period must match the intended replay
period; this does not establish that capture timestamps determine request UTC
under delayed delivery. Requests retain input and analysis identity, with
increasing attempts and publication generations. Neither scenario enables the
full-recording hold, injects rollover audio, or drains or reorders events.

`ft8_210703_133430_live.expected.txt` contains three messages from the existing
standard and MTD references: W1FC F5BZB -08, CQ F5RXL IN94, and WM3PEN EA6VQ -09.
Stage 41 must deliver at least one through raw output, GUI processing, and GUI
display. Its live decoder can stop before recovering all three under load.
All three must appear in final MTD raw output and reach the GUI over the period.
Observations belong to each invocation and decode cycle, so another stage
cannot satisfy the stage-41 requirement. Numeric columns identify their original
reference lines; only normalized message text is compared. The full-recording
reference's twenty-message set is not the live scheduling contract: the early
request has only 11.808 seconds of audio, and sensitivity and subtraction history
differ. Additional decodes are allowed.

The later-stage reference is `N1PJT HB9CQK -10`, independently present in both
existing full-recording golden references. Consecutive requests in one GUI-owned
jt9 process established that it is absent from stage-41 raw output and newly
reaches raw output, GUI processing, and display at stage 46 when that stage runs,
or at final MTD when stage 46 is skipped or unconfigured. Each test requires
that absence and those three observations at the individual later invocation.
`KD2UGC F6GCP R-23` was also checked and followed the same pattern; one later-stage
reference is sufficient for this focused contract. Final MTD must emit the
later-stage reference too, but previously displayed messages undergo normal
duplicate suppression and need not reach GUI processing or display again.
Independently truncated WAV runs establish input usability, not what later
consecutive stages emit. The standard early stages retain their own subtraction
state; final MTD starts from fresh audio.

The existing `test_wsjtx_live_audio_ft8` remains the receive-buffer ownership
stress test. It holds the final request for the complete recording and delivers
a synthetic next-period prefix ahead of pending decoder-readiness events, then
checks the outstanding publication and completion survive. Ordinary scheduling
coverage cannot replace that ordering test.

Registration requires tests, GUI smoke tests, and the OpenMP Fortran variant,
on macOS or Linux x86_64. The two scheduling scenarios are excluded when
either `WSJT_ENABLE_ASAN_UBSAN` or `WSJT_ENABLE_TSAN` is enabled; the ownership
test and scheduler/coordinator sanitizer coverage remain available. Normal Linux
x86_64 CI uses its existing Xvfb setup. For a focused local Linux run:

```sh
QT_QPA_PLATFORM=xcb xvfb-run -a -s '-screen 0 1280x1024x24' \
  ctest --test-dir build --output-on-failure \
  -R 'test_wsjtx_live_audio_ft8|test_ft8_mtd_decode_(scheduler|coordinator)'
```

These are scheduling correctness scenarios with a bounded safety timeout, not
requirements to finish decoding before the next audio trigger. Scheduler and
coordinator unit tests cover additional backoff, replacement, and recovery
sequences. These GUI tests do not measure performance regressions or validate a
streaming backend. The deliberately lighter valid settings do not establish
maximum-depth/subpass performance coverage, and the 12 kHz fixture bypasses
soundcard resampling. The selected Normal depth is carried in final requests,
but final MTD uses its own cycle and sensitivity settings rather than that depth
parameter. The feeder releases each chunk at its start time, giving it a
chunk-sized lead; correcting that pacing bias is a separate prerequisite for
precise latency measurements.
