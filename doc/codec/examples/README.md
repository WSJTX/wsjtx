# Example clients

Each example is a client of `wsjt-codec` with no user interface, audio device or rig control. Every one implements the same command line and prints the same lines, so one check runs them all. Between them, `decode` and `render` use every mode the codec decodes and encodes ([Modes](#modes)). [interface.md](../interface.md) specifies the interface they use.

## Command line

```text
codec_client decode --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--low HZ] [--high HZ] [--mycall CALL] WAV
codec_client live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
codec_client render --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--out WAV] [TEXT...]
```

| Command | What it does |
|---|---|
| `decode` | Sends a WAV file of 12000 Hz mono 16-bit PCM (format 1, or WAVE_FORMAT_EXTENSIBLE with the PCM subformat) through the codec as fast as the codec takes it, then `halt`, and prints each decode or measurement as it comes. It takes every mode the codec's `ready` lists in `modes`. |
| `live` | Replays the file as a receiver's audio: it starts on the next UTC period boundary, sends each frame when its last sample is due, and uses the codec's `busy` lines to tell a slow decode from a hung codec. It takes FT8 and FT4, whose allowances it keeps; [interface.md](../interface.md#telling-a-hang-from-a-slow-decode) gives the other modes'. |
| `render` | Renders TEXT in any mode the codec's `ready` lists in `encodes`, as 12000 Hz audio, then decodes that audio back in the same session ([The round trip](#the-round-trip)). |

| Option | Meaning |
|---|---|
| `--codec PATH` | The `wsjt-codec` program. |
| `--data DIR` | The codec's data (`-a`), temporary (`-t`) and read-only (`-r`) directory, and its working directory, where it writes files of its own; default the current directory. When a mode needs `ALLCALL7.TXT` or `CALL3.TXT`, they go here; the examples' decodes, JT4 and JT65 among them, need neither. Give each running codec its own. |
| `--mode` | The mode, upper case, as `ready` names it; default `FT8`. `live` takes `FT8` and `FT4`. |
| `--submode X` | JT4, JT9, JT65 and Q65: the submode, one letter from A to H; default A. |
| `--period S` | The period, in seconds, a finite number that holds a sample at least; default the mode's ([Modes](#modes)). TUNE's is the length of its tone. |
| `--fast` | JT9: fast JT9. Give `--submode` E to H and `--period` 5, 10, 15 or 30 with it: the defaults, A and 60 s, are slow JT9's, which the codec refuses with `fast`. |
| `--depth N` | Decoding depth, 1 to 3; default 3. |
| `--freq HZ` | `decode`: the receive frequency (`rxfreq`), sent only when given. `render`: the lowest tone, in Hz; default 1500, MSK144 1000 and SUPERFOX 750, the only ones they take. |
| `--low HZ`, `--high HZ` | `decode`: the lowest and highest frequency searched (`nfa` and `nfb`); default 200 and 4000, and in FST4, where they are F Low and F High, 600 and 1400. |
| `--mycall CALL` | `decode`: the station's call, which the decoders use for a priori decoding; default none. |
| TEXT | `render`: the message, one; FOX and SUPERFOX take one to five, one a slot; JTTY takes one or more, joined by spaces; ECHO and TUNE take none. |
| `--out WAV` | `render`: also write the rendered audio to this WAV file. The client checks that it can create the file before it starts the codec (exit status 2 if it cannot), and removes a file it created only when the codec does not render: a refusal, an `error` line, a hang, the codec ending before `rendered` or sending `rendered` with nothing encoded before it, and when writing the audio fails. A rendered WAV is kept even when the round trip then fails. |

## Output

Standard output carries only these lines, their fields separated by one space:

| Line | For | Fields |
|---|---|---|
| `decode TIME SNR DT FREQ MESSAGE` | each `decode` line | `TIME` the period's label, six digits; `SNR` in dB and `FREQ` in Hz, integers; `DT` in seconds to one decimal, rounded half to even as WSJT-X shows it, or `nan` when the codec sends `null` (a value that is not finite); `MESSAGE` the rest of the line, as the codec sends it, possibly empty |
| `finished PERIOD_END` | each `decode_finished` line | the period's label |
| `echo TIME LEVEL SNR DFREQ Q` | each `echo` line (Echo) | `LEVEL` and `SNR` in dB and `DFREQ` in Hz, as `DT` prints; `Q` WSJT-X's quality, 0 to 10 |
| `freqcal TIME FREQ ERROR LEVEL SNR` | each `freqcal` line (FreqCal) | the measured frequency and its error in Hz, the level and the SNR in dB, each as `DT` prints, `nan` for `null` |
| `jtty STATE START SNR FREQ TEXT` | each `jtty_update` line that ends a message: `complete`, `expired` or `ended` (JTTY) | `START` in seconds from the stream's first sample and `FREQ` in Hz, as `DT` prints; `SNR` in dB; `TEXT` the rest of the line, as the codec sends it, possibly empty |
| `rendered SAMPLES SENT` | `render`, once the audio is in | the number of samples, and what was sent: the message as sent; FOX's slots as sent, SUPERFOX's as given in upper case, and JTTY's segments, each joined by ` \| `; `ECHO` or `TUNE` |

`decode` labels the first period with the time in a file name that ends `_HHMMSS.wav` or `_HHMM.wav`, as WSJT-X names a saved period `YYMMDD_HHMMSS.wav`, or `YYMMDD_HHMM.wav` in periods of a minute or more, else 000000, and each later period one period on; JTTY has no periods and no labels. `live` labels each period with its UTC start; `render` labels its one period 00:00:00, so it prints 000000; a `freqcal` line's `TIME` is the label plus the measurement's place in the period, so TUNE's first prints 000004.

Standard error carries the codec's diagnostics, passed through, and the client's own messages.

`decode` on `samples/FT8/210703_133430.wav`, its first three lines and its last two ([Running an example](#running-an-example) shows the other modes'):

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
| 0 | The session ended cleanly; for `render`, the round trip succeeded ([The round trip](#the-round-trip)). |
| 1 | The codec did not start, sent no `ready` within 30 s or one that does not list the mode (or, to render, the mode it decodes back with), sent an `error` line, refused the render, ended its output before `rendered` or sent `rendered` with nothing encoded before it, or exited with a status other than 0; `--out` could not be written; or the round trip found no decode of what was sent ([The round trip](#the-round-trip)). |
| 2 | The command line is not usable (TEXT given to ECHO or TUNE, missing for another mode, or more than one outside FOX, SUPERFOX and JTTY, included), the WAV file is not of the form `decode` takes, or `--out` cannot be created. |
| 3 | `live`: the codec made no progress within its allowance. `decode` and `render`: the codec sent no line for 300 s, or four periods when that is longer, while its output was open ([What every client does](#what-every-client-does)). `decode`, `live`, and `render` as it decodes the audio back: the codec did not exit within 10 s of the end of its output. In each case the client stopped it and exited at once, without waiting for the codec's pipes, which a child of the codec can hold open. |

## What every client does

- Sets `OMP_STACKSIZE` to `10M` in the codec's environment unless it is set: the decoders' OpenMP threads need the stack WSJT-X gives them.
- Sends the 8-byte session header, waits up to 30 s for `ready`, and checks that it lists the mode in `modes` (to decode) or `encodes` (to render, and the mode it decodes back with in `modes`); otherwise it stops the codec.
- Writes integer keys as plain JSON integers: a configure with `3.0` for one is declined (`configure_type_error`).
- Sends, with the mode, `trperiod` (but in JTTY), `submode` in JT4, JT9, JT65 and Q65, and `fast` in JT9: a configure that carries the mode sets the mode's default period and `fast` false otherwise.
- Sends `mycall` (empty unless `--mycall` gives one), `his_call` and `his_grid` empty, and `ntol` 1000, `nfa` and `nfb`, with the mode: these examples have no station, and the decoders otherwise use the starting calls (`mycall` K1ABC); a configure that carries the mode sets the mode up as WSJT-X does, which can change `ntol` and the frequencies, so the search keys go with it.
- Reads the codec's standard output and standard error as they come, each on its own, so that neither pipe fills and stops the codec.
- Treats a broken pipe to the codec as the codec having exited: it stops writing and reports what the codec's lines and exit status say.
- Once the codec's output ends after `ready`, gives it 10 s to exit; then it has hung, and the client stops it. When a render fails, the client sends `halt` and gives the codec the same 10 s before it stops it.
- Once the codec has exited, waits at most 2 s for its pipes to close and for its own writes to the codec to finish, then lets go of them: a process the codec started can hold them open. Letting go of standard output ends the codec's output there, after the lines it wrote before it exited, and the client goes on with the codec's own exit status.
- Outside `live`, takes a codec that sends no line for 300 s, or four periods (of up to 1800 s, the codec's longest) when that is longer, while its output is open to have hung: the client stops it and exits with status 3. That is the longest [interface.md](../interface.md#telling-a-hang-from-a-slow-decode) allows a decode pass between progress lines before a pass has completed; a reply to `render` waits for no decode, so its bound is 300 s.
- Sends a `configure` with the period's label (`utc`) before each period's first sample, but in JTTY, which has no periods, and `halt` after the last sample.
- Skips the lines it does not use, lines that are not JSON objects, and `t` values it does not know.

`live` adds `"live_audio":true` and `"busy_events":true` to its configure and keeps this watchdog:

- While `busy` is true, a `busy`, `pass_progress` or `pass_skipped` line, or a `decode` in the mode of the last `busy` line, is progress, due within the allowance.
- Each whole period is due an answer: its `decode_finished`, or a `busy` line naming its label. The client registers the period before it sends the period's first sample, with its scheduled end: when its last sample is due, whether or not the codec has read it. While `busy` is false, the answer is due within the allowance, counted from the later of the scheduled end and the last `busy` false.
- An answer removes its period, even one that comes before all the period's audio is sent, as it can on live audio. An answer for a period that is not registered changes nothing: the client keeps no other record of answers, so a label used again a day later is due its own answer.
- A configure that changes the mode or the period, or a `discontinuity`, can discard a period in flight, which then sends no `decode_finished`; a client that sends one removes the periods it discards from its watchdog. These examples send neither.
- The allowance is 10 s in FT8 and 6 s in FT4 once a pass has completed, at its `decode_finished` or the next `busy` line; before that, the greater of 300 s and four periods. When a deadline passes, the client stops the codec and exits with status 3.
- The client registers `halt` before the first sample: after `halt`, the codec's exit is due as a period's answer is, from the last sample's scheduled time; no line answers it.

## Modes

| Mode | Commands | Period, s (default first) | Its transmission starts | The client also |
|---|---|---|---|---|
| FT8 | `decode`, `live`, `render` | 15 | 0.5 s in | |
| FT4 | `decode`, `live`, `render` | 7.5 | 0.3 s in | |
| JT9 | `decode`, `render` | 60; with `--fast`, 5, 10, 15 or 30, given with `--period` | 1 s in; fast JT9 at the start | sends `submode` (A to H; fast E to H) and `fast`; H round-trips at `--freq` 1000, not 1500, as jt9 decodes it |
| JT65 | `decode`, `render` | 60 | 1 s in | sends `submode` (A to C) |
| JT4 | `decode`, `render` | 60 | 1 s in | sends `submode` (A to G) |
| Q65 | `decode`, `render` | 60, 15, 30, 120 or 300 | 1 s in; 0.5 s at 15 and 30 s | sends `submode` (to render: A to C at 15 s, A to D at 30, A to E at 60 and 300, A to F at 120) |
| FST4 | `decode`, `render` | 60, 15, 30, 120, 300, 900 or 1800 | 1 s in; 0.5 s at 15 s | takes `--low` and `--high`, its F Low and F High, as 600 and 1400 by default, and decodes back with them 200 Hz either side of `--freq`; `render` is refused at 900 and 1800 s, over 600 s of audio |
| FST4W | `decode`, `render` | 120, 300, 900 or 1800 | 1 s in | `render` is refused at 900 and 1800 s, over 600 s of audio |
| WSPR | `decode`, `render` | 120 | 1 s in | |
| MSK144 | `decode`, `render` | 30, 5, 10 or 15 | at the start | renders at 1000 Hz, its tones' lowest; decodes back at its Rx Freq, 1500 Hz |
| ECHO | `decode` (`echo` lines), `render` (WSJT-X's Fixed Tone) | 3 | at the start | decodes back with `live_audio` and `echo_freq`, so that the keys, not the audio's first samples as a saved Echo file carries them, give the echo's parameters |
| JTTY | `decode` (`jtty` lines), `render` | none | 1 s in | sends its `render` without `mode`, and reads its reply's `segment` lines |
| FREQCAL | `decode` (`freqcal` lines) | 15, 5, 10 or 30 | | |
| TUNE | `render`, decoded back as FREQCAL | 15, as `seconds` | at the start | |
| FOX | `render`, decoded back as FT8 | 15 | 0.5 s in | sends the TEXTs as `slots`, 60 Hz apart |
| SUPERFOX | `render`, decoded back as FT8 | 15 | 0.4 s in | sends the TEXTs as `slots`, and decodes back as a SuperFox Hound (`contest_type` hound, `superfox_mode` and `even_sequence`) |

Where WSJT-X starts a transmission is [interface.md](../interface.md#rendered-audio)'s ([Fox and SuperFox](../interface.md#fox-and-superfox) for SuperFox); JTTY's 1 s and TUNE's start are the examples' choices. The codec refuses a period, submode or `--freq` its mode does not take, and the client then exits with status 1, the codec's `error` or `rejected` line on standard error.

### The round trip

`render` decodes its audio back in the same session, in a mode `ready` must list in `modes`: the mode itself, or FREQCAL for TUNE and FT8 for FOX and SUPERFOX. It sends a configure for the mode with `rxfreq` at `--freq` (but in MSK144), and in FST4 F Low and F High 200 Hz either side of it, then one period of noise, or for JTTY the audio's length and 2 s, holding the audio 36 dB down where the table starts the transmission: about 12 dB SNR in 2500 Hz, as decoders expect a noise floor (JT65's decodes nothing without one). The noise is integer arithmetic, so that every example sends the same samples: a 32-bit xorshift (shifts 13, 17 and 5) from 1, each sample the sum of its four bytes less 510, plus the rendered sample shifted right 6 bits.

The round trip succeeds when the codec decodes each message sent (JTTY: a `complete` message with its text), measures an echo above 0 dB SNR (ECHO), or measures the tone within 1 Hz of `--freq` (TUNE).

## Running an example

The commands run from a clone of the source at the release's tag; the release's source tarball leaves out `samples/`. Elsewhere, download the recordings from `https://wsjtx.github.io/wsjtx/samples/`, such as `https://wsjtx.github.io/wsjtx/samples/FT8/210703_133430.wav`, and give their paths.

| Language | Needs | Command |
|---|---|---|
| Python | Python 3.8 or later | `python3 doc/codec/examples/python/codec_client.py decode --codec PATH/wsjt-codec --data codec-data samples/FT8/210703_133430.wav` |
| Node.js | Node.js 18 or later | `node doc/codec/examples/node/codec_client.js decode --codec PATH/wsjt-codec --data codec-data samples/FT8/210703_133430.wav` |
| C++ | A C++17 compiler: GCC, Clang or MinGW-w64 (add `-static` for a standalone `.exe`) | `c++ -std=c++17 -O2 -pthread -o codec_client doc/codec/examples/cpp/codec_client.cpp && ./codec_client decode --codec PATH/wsjt-codec --data codec-data samples/FT8/210703_133430.wav` |
| Rust | Rust 1.79 or later, with Cargo | `cargo run --release --manifest-path doc/codec/examples/rust/Cargo.toml -- decode --codec PATH/wsjt-codec --data codec-data samples/FT8/210703_133430.wav` |
| Java | Java 17 or later and Maven | `mvn -B -q -f doc/codec/examples/java/pom.xml package`, then `java -jar doc/codec/examples/java/target/codec-client.jar decode --codec PATH/wsjt-codec --data codec-data samples/FT8/210703_133430.wav` |

The other modes, with `codec_client` for any of these commands and the recordings in `samples/`, and the first lines the Python example printed:

```text
codec_client decode --codec PATH/wsjt-codec --data codec-data --mode FT4 samples/FT4/000000_000002.wav
decode 000002 -10 -0.2 296 N1TRK N4FKH 569 VA
codec_client decode --codec PATH/wsjt-codec --data codec-data --mode JT9 samples/JT9/130418_1742.wav
decode 174200 -25 0.0 1119 CQ GM7GAX IO75
codec_client decode --codec PATH/wsjt-codec --data codec-data --mode JT65 --submode B --mycall K1ABC samples/JT65/JT65B/000000_0004.wav
decode 000400 -26 2.5 1700 K1ABC G4XYZ IO91
codec_client decode --codec PATH/wsjt-codec --data codec-data --mode Q65 --period 30 samples/Q65/30A_Ionoscatter_6m/201203_024000.wav
decode 024000 -18 0.3 1008 K1JT K9AN R-16
codec_client decode --codec PATH/wsjt-codec --data codec-data --mode FST4 --low 1000 --high 1400 samples/FST4+FST4W/210115_0058.wav
decode 005800 17 0.4 1331 CQ K9KFR EN71
codec_client decode --codec PATH/wsjt-codec --data codec-data --mode FST4W --period 1800 samples/FST4+FST4W/201230_0300.wav
decode 030000 -44 0.3 1433 DL0HOT JO60 30
codec_client decode --codec PATH/wsjt-codec --data codec-data --mode MSK144 --period 15 samples/MSK144/181211_120800.wav
decode 120800 5 4.5 1460 CQ W4IMD EM84
codec_client decode --codec PATH/wsjt-codec --data codec-data --mode WSPR samples/WSPR/150426_0918.wav
decode 091800 -9 1.1 1446 ND6P DM04 30
codec_client decode --codec PATH/wsjt-codec --data codec-data --mode JTTY samples/JTTY/261105_101915_567.wav
jtty complete 8.0 -10 1510.7 OK2PDH PD1ADA +12 OPR GEERT
codec_client render --codec PATH/wsjt-codec --data codec-data --mode JT4 --submode F "CQ K1ABC FN42"
rendered 565029 CQ K1ABC FN42
codec_client render --codec PATH/wsjt-codec --data codec-data --mode ECHO
rendered 24577 ECHO
echo 000000 51.8 4.3 0.0 10
codec_client render --codec PATH/wsjt-codec --data codec-data --mode TUNE --period 5
rendered 60000 TUNE
freqcal 000004 1500.0 0.0 -0.4 50.5
codec_client render --codec PATH/wsjt-codec --data codec-data --mode FOX "K1ABC W9XYZ -05" "K2DEF W9XYZ RR73"
rendered 151681 K1ABC W9XYZ -05 | K2DEF W9XYZ RR73
codec_client render --codec PATH/wsjt-codec --data codec-data --mode SUPERFOX "K1ABC W9XYZ RR73" "K2DEF W9XYZ -12"
rendered 156673 K1ABC W9XYZ RR73 | K2DEF W9XYZ -12
codec_client render --codec PATH/wsjt-codec --data codec-data --mode JTTY "CQ CQ DE K1ABC K1ABC K"
rendered 113280 CQ CQ DE K1ABC K1ABC K
```

The JT65 recording's decode is an a priori one, which needs the station's call: its message is to K1ABC (`K1ABC G4XYZ IO91`), and without `--mycall` it decodes nothing. JT4's recordings are 11025 Hz, which `decode` does not take; `render` shows JT4, and Echo and FreqCal, which have no recordings. On an 18-core arm64 Mac the FST4 decode takes about 20 s at depth 3, and FST4W's at 1800 s about as long.

## The check

[check_client.py](../../../tests/integration/codec_examples/check_client.py) runs a client's commands against a codec and checks what they print and their exit status. `decode` at depth 3 on `samples/FT8/210703_133430.wav` must print every message in [ft8_210703_133430_mtd.expected.txt](../../../test/fixtures/ft8_210703_133430_mtd.expected.txt), matched by text with blanks collapsed (more decodes are allowed), each labeled 133430, and `finished 133430`; and its lines, in any order, must be the Python example's for the same run, which pins the format of every field. The same recording as WAVE_FORMAT_EXTENSIBLE must print the same lines, and with the float subformat exit 2. `decode` must also print, labeled from their names, the messages [test/fixtures](../../../test/fixtures/README.md) lists for the other recordings in `samples/` (FT4, JT9, JT65 with `--submode B --mycall K1ABC`, Q65 at 30 s, MSK144 at 15 s and WSPR), FST4's two strong signals at depth 1 with `--low 1000 --high 1400`, and JTTY's three complete messages, each run with the Python example's lines. `render` must round-trip every mode the codec encodes, JT9 slow and fast ([The round trip](#the-round-trip)), each with the Python example's lines, and write a WAV file holding the samples its `rendered` line counts, the Python example's samples; `--out` in a missing directory must exit 2, and a refused render exit 1 and leave no file.

On Linux and macOS the check also runs the client against a stand-in codec that checks what the client sends (`OMP_STACKSIZE`, `-a`, `-t` and `-r`, the header, an integer `depth`, a whole `trperiod` written as an integer, a label before each period, `halt`). `decode` must skip lines that are not JSON objects, print a `null` `dt` as `nan` and a `dt` of 0.25 as `0.2`, and exit 1 when the stand-in sends an `error` line or exits with status 1; the stand-in writes about four pipe buffers to standard error before `ready`. `render` must exit 1 when the rendered audio decodes to nothing, and keep its `--out` file. When the stand-in ends its standard output and does not exit, `decode` must exit 3 10 s after the output's end: once after `halt`, with lines still coming for 3 s, and once after it stops reading its input, which blocks the client's writer before `halt`. When it refuses the render and does not exit after `halt`, `render` must exit 1 10 s after `halt`. When the stand-in exits 0 and a child of it holds one or two of its pipes, the client must exit at most 2 s after the stand-in's exit (up to 1.5 s late): `decode` with 0 when the child holds standard error, when it holds standard output, after the stand-in's answer, which the client must print, when it holds standard input, which the stand-in stopped reading after 1 s of audio, so the client's writer is blocked, and when it holds standard input and standard output so, the client's reader and writer both waiting on it, which the 2 s covers in all; `render` with 1 when the stand-in refuses the render and the child holds standard output after `halt`. The silence bound outside `live` is checked shortened: each client reads `CODEC_EXAMPLE_CHECK_SILENCE_SECONDS`, which is for this check and not a setting, and a whole number from 1 to 300 there replaces the bound. With it set to 4, when the stand-in sends `ready` and then nothing, neither ending its output nor exiting, `decode` and `render` must exit 3 4 s after `ready` (up to 2 s late) with `no line from the codec in 4 s: it has hung` on standard error, and `render` must leave no `--out` file. In every stand-in run, the stand-in must not be running 2 s after the client has exited. Every configure's `depth` must be an integer. With `--live`, `live` must print every message from the codec in real time. Then, in FT4, it must exit 3 within 2 s after its allowance ends when the stand-in hangs while busy and goes silent (so the client must check its deadlines while no line comes), does so in FT8, hangs while busy sending only lines that are not progress, runs one pass that ends with `busy` false and no `decode_finished` and then sends nothing, answers every period and then hangs after `halt`, answers a period early and then stops reading its input, goes idle and never answers a period, does so after answering that period's label before the period starts (as a period a day earlier would have been answered), answers a period only after the next one is registered and never answers the next, or stops reading its input. It must exit 0 when the stand-in's pass is slow but sends progress lines, when its first pass, before any completes, sends no progress for longer than the allowance, when its pass stays busy, with progress, past a period's scheduled end plus the allowance and the period's answer comes 1 s after `busy` false, or when it answers each period before all its audio is sent; and exit 1 within 32 s when the stand-in sends no `ready`. The stand-in sends `busy` and `pass_progress` lines only after a configure turns on `busy_events`, requires `live_audio` too, and has a child hold its pipes open after it is stopped.

```text
python3 tests/integration/codec_examples/check_client.py --codec PATH/wsjt-codec --sample samples/FT8/210703_133430.wav --expected test/fixtures/ft8_210703_133430_mtd.expected.txt --work-dir check-work [--samples samples] [--fixtures test/fixtures] [--live] -- python3 doc/codec/examples/python/codec_client.py
```

In a build, `ctest -R test_codec_example_` runs the check for each example the build registers, when Python 3.8 or later and `samples/` are present: Python and C++, and Node.js, Rust and Java when the CMake option `WSJT_ENABLE_CODEC_EXAMPLE_LANGUAGE_TESTS` is on and their toolchains are found. The CMake option `WSJT_ENABLE_STREAM_TIMING_TESTS` adds `--live`. An example registers its test in `tests/integration/codec_examples/<language>.cmake`, which calls `wsjt_add_codec_example_test (<language> <command that starts the client>...)` once it finds the language's toolchain; the Python example registers in that directory's `CMakeLists.txt`.
