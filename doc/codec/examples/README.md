# Example clients

Each example is a client of `wsjt-codec` with no user interface, audio device or rig control. Every one implements the same command line and prints the same lines, so one check runs them all. [interface.md](../interface.md) specifies the interface they use.

## Command line

```text
codec_client decode --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
codec_client live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
codec_client render --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] [--freq HZ] [--out WAV] TEXT
```

| Command | What it does |
|---|---|
| `decode` | Sends a WAV file of 12000 Hz mono 16-bit PCM (format 1, or WAVE_FORMAT_EXTENSIBLE with the PCM subformat) through the codec as fast as the codec takes it, then `halt`, and prints each decode as it comes. |
| `live` | Replays the file as a receiver's audio: it starts on the next UTC period boundary, sends each frame when its last sample is due, and uses the codec's `busy` lines to tell a slow decode from a hung codec. |
| `render` | Renders TEXT as 12000 Hz audio, then decodes that audio back in the same session, placed where WSJT-X starts a transmission in its period (FT8 0.5 s, FT4 0.3 s). |

| Option | Meaning |
|---|---|
| `--codec PATH` | The `wsjt-codec` program. |
| `--data DIR` | The codec's data (`-a`), temporary (`-t`) and read-only (`-r`) directory, and its working directory, where it writes files of its own; default the current directory. When a mode needs `ALLCALL7.TXT` or `CALL3.TXT`, they go here; the examples' FT8 and FT4 decodes need neither. Give each running codec its own. |
| `--mode` | `FT8` (default) or `FT4`. |
| `--depth N` | Decoding depth, 1 to 3; default 3. |
| `--freq HZ` | `render`: the lowest tone, in Hz; default 1500. |
| `--out WAV` | `render`: also write the rendered audio to this WAV file. The client checks that it can create the file before it starts the codec (exit status 2 if it cannot), and removes a file it created only when the codec does not render: a refusal, an `error` line, or the codec ending before `rendered`, and when writing the audio fails. A rendered WAV is kept even when the round trip then fails. |

## Output

Standard output carries only these lines, their fields separated by one space:

| Line | For | Fields |
|---|---|---|
| `decode TIME SNR DT FREQ MESSAGE` | each `decode` line | `TIME` the period's label, six digits; `SNR` in dB and `FREQ` in Hz, integers; `DT` in seconds to one decimal, rounded half to even as WSJT-X shows it, or `nan` when the codec sends `null` (a value that is not finite); `MESSAGE` the rest of the line, as the codec sends it |
| `finished PERIOD_END` | each `decode_finished` line | the period's label |
| `rendered SAMPLES MSGSENT` | `render`, once the audio is in | the number of samples, and the message as sent |

`decode` labels the first period with the time in a file name that ends `_HHMMSS.wav`, as WSJT-X names a saved period `YYMMDD_HHMMSS.wav`, else 000000, and each later period one period on. `live` labels each period with its UTC start; `render` labels its one period 00:00:00, so it prints 000000.

Standard error carries the codec's diagnostics, passed through, and the client's own messages.

`decode` on `samples/FT8/210703_133430.wav`, its first three lines and its last two:

```text
decode 133430 16 0.3 2571 W1FC F5BZB -08
decode 133430 12 -0.1 2157 WM3PEN EA6VQ -09
decode 133430 -2 -0.8 1197 CQ F5RXL IN94
decode 133430 -6 0.4 472 KD2UGC F6GCP R-23
finished 133430
```

## Exit status

| Status | Meaning |
|---|---|
| 0 | The session ended cleanly; for `render`, the audio decoded back to the message as sent. |
| 1 | The codec did not start, sent no `ready` within 30 s or one that does not list the mode, sent an `error` line, refused the render, or exited with a status other than 0; `--out` could not be written; or the round trip found no decode of the message. |
| 2 | The command line is not usable, the WAV file is not of the form `decode` takes, or `--out` cannot be created. |
| 3 | `live`: the codec made no progress within its allowance. `decode`, `live`, and `render` as it decodes the audio back: the codec did not exit within 10 s of the end of its output. Either way the client stopped it and exited at once, without waiting for the codec's pipes, which a child of the codec can hold open. |

## What every client does

- Sets `OMP_STACKSIZE` to `10M` in the codec's environment unless it is set: the decoders' OpenMP threads need the stack WSJT-X gives them.
- Sends the 8-byte session header, waits up to 30 s for `ready`, and checks that it lists the mode in `modes` (to decode) or `encodes` (to render); otherwise it stops the codec.
- Writes integer keys as plain JSON integers: a configure ignores or misreads `3.0`, without an error.
- Sends `mycall`, `his_call` and `his_grid` empty, and `nfa`, `nfb` and `ntol`, with the mode: these examples have no station, the decoders otherwise use the starting calls, and a configure that carries the mode resets the other three.
- Reads the codec's standard output and standard error as they come, each on its own, so that neither pipe fills and stops the codec.
- Treats a broken pipe to the codec as the codec having exited: it stops writing and reports what the codec's lines and exit status say.
- Once the codec's output ends after `ready`, gives it 10 s to exit; then it has hung, and the client stops it. When a render fails, the client sends `halt` and gives the codec the same 10 s before it stops it.
- Sends a `configure` with the period's label (`utc`) before each period's first sample, and `halt` after the last sample.
- Skips the lines it does not use, lines that are not JSON objects, and `t` values it does not know.

`live` adds `"live_audio":true` and `"busy_events":true` to its configure and keeps this watchdog:

- While `busy` is true, a `busy`, `pass_progress` or `pass_skipped` line, or a `decode` in the mode of the last `busy` line, is progress, due within the allowance.
- Each whole period is due an answer: its `decode_finished`, or a `busy` line naming its label. The client registers the period before it sends the period's first sample, with its scheduled end: when its last sample is due, whether or not the codec has read it. While `busy` is false, the answer is due within the allowance, counted from the later of the scheduled end and the last `busy` false.
- An answer removes its period, even one that comes before all the period's audio is sent, as it can on live audio. An answer for a period that is not registered changes nothing: the client keeps no other record of answers, so a label used again a day later is due its own answer.
- A configure that changes the mode or the period, or a `discontinuity`, can discard a period in flight, which then sends no `decode_finished`; a client that sends one removes the periods it discards from its watchdog. These examples send neither.
- The allowance is 10 s in FT8 and 6 s in FT4 once a pass has completed, at its `decode_finished` or the next `busy` line; before that, the greater of 300 s and four periods. When a deadline passes, the client stops the codec and exits with status 3.
- The client registers `halt` before the first sample: after `halt`, the codec's exit is due as a period's answer is, from the last sample's scheduled time; no line answers it.

## Running an example

The commands run from a clone of the source at the release's tag; the release's source tarball leaves out `samples/`. Elsewhere, download the recording from `https://wsjtx.github.io/wsjtx/samples/FT8/210703_133430.wav` and give its path.

| Language | Needs | Command |
|---|---|---|
| Python | Python 3.8 or later | `python3 doc/codec/examples/python/codec_client.py decode --codec PATH/wsjt-codec --data codec-data samples/FT8/210703_133430.wav` |
| Node.js | Node.js 18 or later | `node doc/codec/examples/node/codec_client.js decode --codec PATH/wsjt-codec --data codec-data samples/FT8/210703_133430.wav` |
| C++ | A C++17 compiler: GCC, Clang or MinGW-w64 (add `-static` for a standalone `.exe`) | `c++ -std=c++17 -O2 -pthread -o codec_client doc/codec/examples/cpp/codec_client.cpp && ./codec_client decode --codec PATH/wsjt-codec --data codec-data samples/FT8/210703_133430.wav` |
| Rust | Rust 1.79 or later, with Cargo | `cargo run --release --manifest-path doc/codec/examples/rust/Cargo.toml -- decode --codec PATH/wsjt-codec --data codec-data samples/FT8/210703_133430.wav` |
| Java | Java 17 or later and Maven | `mvn -B -q -f doc/codec/examples/java/pom.xml package`, then `java -jar doc/codec/examples/java/target/codec-client.jar decode --codec PATH/wsjt-codec --data codec-data samples/FT8/210703_133430.wav` |

## The check

[check_client.py](../../../tests/integration/codec_examples/check_client.py) runs a client's commands against a codec and checks what they print and their exit status. `decode` at depth 3 on `samples/FT8/210703_133430.wav` must print every message in [ft8_210703_133430_mtd.expected.txt](../../../test/fixtures/ft8_210703_133430_mtd.expected.txt), matched by text with blanks collapsed (more decodes are allowed), each labeled 133430, and `finished 133430`; and its lines, in any order, must be the Python example's for the same run, which pins the format of every field (`DT` 0.25 prints `0.2`). The same recording as WAVE_FORMAT_EXTENSIBLE must print the same lines, and with the float subformat exit 2. `render` must round-trip an FT8 and an FT4 message and write a WAV file holding the samples its `rendered` line counts; `--out` in a missing directory must exit 2, and a refused render exit 1 and leave no file.

On Linux and macOS the check also runs the client against a stand-in codec that checks what the client sends (`OMP_STACKSIZE`, `-a`, `-t` and `-r`, the header, an integer `depth`, a label before each period, `halt`). `decode` must skip lines that are not JSON objects and print a `null` `dt` as `nan`, and exit 1 when the stand-in sends an `error` line or exits with status 1; the stand-in writes about four pipe buffers to standard error before `ready`. `render` must exit 1 when the rendered audio decodes to nothing, and keep its `--out` file. When the stand-in ends its standard output and does not exit, `decode` must exit 3 10 s after the output's end: once after `halt`, with lines still coming for 3 s, and once after it stops reading its input, which blocks the client's writer before `halt`. When it refuses the render and does not exit after `halt`, `render` must exit 1 10 s after `halt`. In every stand-in run, the stand-in must not be running 2 s after the client has exited. Every configure's `depth` must be an integer. With `--live`, `live` must print every message from the codec in real time. Then, in FT4, it must exit 3 within 2 s after its allowance ends when the stand-in hangs while busy and goes silent (so the client must check its deadlines while no line comes), does so in FT8, hangs while busy sending only lines that are not progress, runs one pass that ends with `busy` false and no `decode_finished` and then sends nothing, answers every period and then hangs after `halt`, answers a period early and then stops reading its input, goes idle and never answers a period, does so after answering that period's label before the period starts (as a period a day earlier would have been answered), answers a period only after the next one is registered and never answers the next, or stops reading its input. It must exit 0 when the stand-in's pass is slow but sends progress lines, when its first pass, before any completes, sends no progress for longer than the allowance, when its pass stays busy, with progress, past a period's scheduled end plus the allowance and the period's answer comes 1 s after `busy` false, or when it answers each period before all its audio is sent; and exit 1 within 32 s when the stand-in sends no `ready`. The stand-in sends `busy` and `pass_progress` lines only after a configure turns on `busy_events`, requires `live_audio` too, and has a child hold its pipes open after it is stopped.

```text
python3 tests/integration/codec_examples/check_client.py --codec PATH/wsjt-codec --sample samples/FT8/210703_133430.wav --expected test/fixtures/ft8_210703_133430_mtd.expected.txt --work-dir check-work [--live] -- python3 doc/codec/examples/python/codec_client.py
```

In a build, `ctest -R test_codec_example_` runs the check for each example the build registers, when Python 3.8 or later and `samples/` are present: Python and C++, and Node.js, Rust and Java when the CMake option `WSJT_ENABLE_CODEC_EXAMPLE_LANGUAGE_TESTS` is on and their toolchains are found. The CMake option `WSJT_ENABLE_STREAM_TIMING_TESTS` adds `--live`. An example registers its test in `tests/integration/codec_examples/<language>.cmake`, which calls `wsjt_add_codec_example_test (<language> <command that starts the client>...)` once it finds the language's toolchain; the Python example registers in that directory's `CMakeLists.txt`.
