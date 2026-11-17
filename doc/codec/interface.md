# wsjt-codec interface

wsjt-codec decodes and encodes in a child process. The client writes a session header, then audio and control frames, to its standard input, and reads one JSON object per line from its standard output. This reference describes the codec built from the same source; `wsjt-codec -v` prints its release.

## Command line

```
wsjt-codec [OPTIONS]
```

With no file argument, wsjt-codec runs a session on its standard input and output. With standard input a terminal, it prints its help instead. `wsjt-codec -h` lists every option.

| Option | Meaning |
| --- | --- |
| `-a PATH`, `--data-path PATH` | Writable data files (default `.`) |
| `-t PATH`, `--temp-path PATH` | Temporary files (default `.`) |
| `-r PATH`, `--share-path PATH` | Read-only data files, `ALLCALL7.TXT` (default `.`) |
| `-w PATIENCE`, `--patience PATIENCE` | FFTW planning patience, 0 to 4 (default 1) |
| `-m THREADS`, `--fft-threads THREADS` | Threads for large FFTs (default 1) |
| `-v`, `--version` | Print the release and exit |

With no `-p`, or `-p 60`, a session starts with 15 s periods with no mode option, 7.5 s with `-5` and 30 s with `-k`; otherwise it keeps `-p`'s period, 60 s by default, so `-8` starts FT8 with 60 s periods. Pass no mode option. The mode options and the decoder options set starting values only; `-M`, `-U` and `-E` do not apply to a session. The codec checks the starting values once, after `ready`. Send the mode, the period and the decoder settings in `configure`.

### WSPR files

```
wsjt-codec -2 [-d DEPTH] [-O MHZ] [-a PATH] FILE ...
```

`-2` (`--wspr`) decodes each file as `wsprd` decodes it, with the same code: it prints `wsprd`'s lines and `<DecodeFinished>`, appends to `ALL_WSPR.TXT`, rewrites `wspr_spots.txt`, and reads and rewrites `hashtable.txt` in `-a`, byte for byte as `wsprd` with the options below, starting from a fresh `-a`. (The codec reads its FFTW wisdom, `jt9_wisdom.dat`, from `-a` first; other modes' runs add to it.)

- A file follows `wsprd`'s rule. A name containing `.wav` is a 12 kHz, 16-bit mono WAV with a 44-byte header, decoded as WSPR-2; otherwise a name containing `.c2` is a `.c2` file as WSJT-X writes it, which carries its own dial frequency and WSPR type. The lines take their date and time from the `YYMMDD_HHMM` before the suffix.
- `-d` maps to `wsprd`'s options as WSJT-X maps its decoding depth, by the low three bits: 1 (the default) `-qB`, 2 `-C 500 -o 4`, 3 `-C 500 -o 4 -d`, and 0 or 4 to 7 none.
- `-O` (`--dial`) is the dial frequency in MHz, `wsprd`'s `-f`, default 0, which gives the audio frequency in MHz, as when WSJT-X decodes a file it opens. A `.c2` file's own dial frequency wins.
- The period is 120 s; a `-p` other than 120 is refused.
- `-q` leaves out `<DecodeFinished>`.
- The codec writes `timer.out` and saves `jt9_wisdom.dat` in `-a`, and not `wsprd`'s `wspr_timer.out` or `wspr_wisdom.dat`.
- A file `wsprd` cannot read stops the codec there, with `wsprd`'s message and status 1 (and gfortran's `STOP 1` on standard error).
- With no file, `-2` starts a session in [WSPR](#wspr) with 120 s periods, as `-8` starts one in FT8; send the mode in `configure`.

## Environment

Set `OMP_STACKSIZE=10M` in the codec's environment, as WSJT-X sets it for its decoder. Without it the decoders' OpenMP threads get the default stack, and on Linux each thread's copy of the decoders' thread-local arrays, about 2.7 MB, comes out of that stack, leaving about 5.5 MiB of the usual 8 MiB.

## Directories and files

- `-a`: the codec reads its FFTW wisdom, `jt9_wisdom.dat`, from here at the start and saves it here; it writes `timer.out` here in every session; MSK144 training writes its `.pcoeff` files here; the reference-spectrum equalization reads and writes `refspec.dat` here, WSJT-X's file and format; JT4 and JT65 read `CALL3.TXT` from here; Q65 keeps its caller history, `tsil.3q`, here in the `na_vhf`, `ww_digi`, `arrl_digi` and `q65_pileup` activities and can append to `q65_decodes.txt`; FST4W reads and rewrites `fst4w_calls.txt` here; WSPR loads its hash table from `hashtable.txt` here before the session's first WSPR decode and rewrites it after every WSPR period, wsprd's file and format ([WSPR](#wspr)). Keep it across runs.
- `-t`: the decoders' temporary files, such as `decoded.txt`, and the side files `houndcallers.txt`, `avemsg.txt` and `red.dat`, which also come as lines ([Side outputs and labels](#side-outputs-and-labels)).
- `-r`: `ALLCALL7.TXT`, the call list the multithreaded FT8 decoder loads. Without it the codec writes `ALLCALL7.TXT is too short or broken?` to standard error.
- The working directory: JT4 can write `fort.77` there, and a file named `rx_messages.txt` there is a test hook: the FT8 decoder returns its lines as decodes, and JT65+JT9 decodes nothing while it is there. Start each codec in a working directory of its own.

`ALLCALL7.TXT` and `CALL3.TXT` are in the WSJT-X source tree, and WSJT-X installs them with its data files. WSJT-X keeps its own `CALL3.TXT` in its writable data directory and can download a newer one; a client supplies the codec's in `-a`.

## Session header

Send the 8-byte header first: the magic `WSJT`, the format 0 (int16 PCM), 1 channel, and the rate in kHz, 12, as a 16-bit little-endian integer.

```
57 53 4A 54 00 01 0C 00
```

The codec reads the header, then sends `ready`. A header it cannot take gets an `error` line with `msg`, and the codec exits with status 1.

## Frames

After the header, standard input carries frames: a type byte, the body's length as a 32-bit little-endian integer, then the body.

| Type | Body |
| --- | --- |
| `0x01` | Audio: int16 little-endian samples, 12 kHz mono. A frame with an odd byte count is skipped (`odd_audio_frame`). |
| `0x02` | Control: one JSON object in UTF-8, without a trailing newline, at most 262,144 bytes; its keys may come in any order. Write it compact: the codec takes only spaces between tokens, ignores a key that follows a tab or a line break, and declines a frame whose `t` does (`configure_parse_error`). A longer frame is skipped (`control_frame_too_large`). |
| Other | Skipped. |

A frame of any type longer than 43,200,000 bytes, the sample buffer (1800 s at 12 kHz), ends the session: `frame_too_large`, exit status 1.

Standard output carries only the JSON lines; each has `"v":1` and a `t` that names it. Standard error carries diagnostics for people; read it too, so that its pipe never fills.

## Receive

This section follows a decode session from its first configure to its exit status.

### Configure

- Send a configure before the first audio. A configure applies where it arrives in the stream, and audio is decoded under the most recent one.
- A configure changes only the keys it carries, with the exceptions below; every other setting keeps its value.
- A configure is applied whole or not at all. A declined one sends one `error` line and changes nothing, its label included.
- A configure is checked against the mode it would leave the stream in, including values carried from earlier frames. `configure_range_error` names the first refused key, and its `mode` is the mode the frame would leave; the stream stays in the mode it had.
- `trperiod` must be a finite number above 0 and at most 1800 (`invalid_trperiod`). JT9, JT4, JT65 and JT65+JT9 decode only at a `trperiod` of 60, fast JT9 (`fast` true) and FreqCal only at 5, 10, 15 or 30, fast JT9 in submodes E to H, ECHO only at 3, and WSPR only at 120 (`configure_range_error`). The keys checked, in order: `fast` in another mode than JT9, `trperiod`, then fast JT9's `submode`.
- An accepted configure has no reply, and neither do `flush`, `discontinuity` and `halt`. The lines that follow them are those of the decoding they cause.
- Keys the codec does not know are ignored, so a misspelled key is not reported. `pack` and `render` refuse keys they do not know.
- Write integer keys as plain JSON integers: `1500`, not `1500.0` or `1.5e3`. A number in another form is not reported, and the key is ignored or misread; `nutc` is the exception, and a number in another form there declines the frame with `configure_type_error`. Other numbers are plain decimals too, except the elements of `phase_eq`, which may carry exponents.
- A configure that carries `mode` or `trperiod`, even with the values in force, resets each of `nfa`, `nfb` and `ntol` that it does not carry. `nfa` and `nfb` return to their values at the start of the session (`-L` and `-H`, 200 and 4000 Hz by default); `ntol` returns to the mode's default: 100 Hz in FST4W, 20 in JT65+JT9 and JTTY, 10 in Q65, 50 in FreqCal and fast JT9 and 1000 in the other modes. An `ntol` carried in the same frame is capped at 100 in FST4W and at 1000 in the other modes but JTTY, and Q65 and JT65+JT9 take 10 and 20 whatever it says. Send `nfa`, `nfb` and `ntol` with every configure that carries `mode` or `trperiod`.
- A configure that carries `mode` without `trperiod` also sets the mode's default period: 15 s for FT8, 7.5 s for FT4, 30 s for MSK144, 3 s for ECHO, 120 s for WSPR and 60 s for the other modes, which FreqCal and fast JT9 refuse, so send `trperiod` with `"mode":"FREQCAL"` and with fast JT9's `mode`. A configure that carries `mode` without `fast` sets `fast` false, so send `fast` with every fast JT9 configure that carries `mode`. A configure that enters fast JT9, one that carries `"mode":"JT9"` with `fast` true (as choosing JT9 with Fast on) or turns `fast` on (as WSJT-X's Fast check box), sets the receive and the transmit frequency to 700, as WSJT-X sets Rx Freq and Tx Freq, unless it carries `rxfreq` (`tx_audio_offset_hz` for the transmit one). A `fast` true in fast JT9 without `mode` enters nothing and resets neither frequency nor `ntol`, as WSJT-X keeps Rx Freq and F Tol on a submode change; one that carries only `trperiod` keeps the frequencies and resets `ntol` to 50 by the rule above. WSJT-X's Fast check box keeps F Tol, where the codec's frame that turns `fast` on carries `trperiod` and so resets `ntol` to 50: send `ntol`.
- Two groups of keys each carry one setting, and a frame that carries any key of a group replaces the whole setting, so send each group whole: `depth_level`, `q65_maxiters_level`, `use_averaging`, `deep_ap_search` and `q65_auto_clear_average`; and `contest_type`, `single_decode`, `vhf_features`, `noise_blanker_level` and `q65_pileup`. The keys of the group that the frame leaves out become 0, false or none (`noise_blanker_level` -3). A `contest_type` of `q65_pileup` sets `q65_pileup` too unless the frame carries it.
- `depth` and the first group are two forms of one setting: a frame that carries both is declined with `configure_type_error`, key `depth`. `depth_level` and `q65_maxiters_level` in one frame must agree in their low two bits (`configure_type_error`, key `q65_maxiters_level`).
- A configure that carries `mycall` without `my_call_standard`, or `his_call` without `his_call_standard`, sets the flag from the call: true for a call FT8 sends unchanged in every standard message, CQ included (upper case, as WSJT-X sends it, with no leading Q; a trailing /R or /P allowed, /M or /MM not), false for an empty or nonstandard one.
- Before any configure, the session runs on the command line's values: among them depth 1 unless `-d` sets it, and `live_audio` and `busy_events` false. The codec checks the command line's values once, after `ready`; one the decoder would refuse sends a `configure_range_error`, and the session goes on.

### Periods and labels

- A period is `trperiod` times 12,000 samples, but never shorter than the samples the decoder takes: an FT4 period is at least 72,576 samples (6.048 s). A period starts at the session's first audio sample, and at the first sample after a configure that changes the mode or the period, a `discontinuity` or a `flush`. A configure that changes only the label does not start one. Send every sample.
- A configure that changes the mode or the period discards a period in flight that has not reached its stop (from a file, the whole period, or FT4's and multithreaded FT8's stop; in fast JT9 the try that decodes the period, its last at most): it sends no `decode_finished`, and the decodes its early FT8 passes or MSK144 blocks sent stand. A period that has reached its stop decodes and sends its `decode_finished` as usual. Any other configure applies to the period in flight, which decodes at the stop point the new settings give.
- Label each period with `utc` (`"HH:MM:SS"`) or `nutc` (HHMMSS) in a configure sent after the previous period's last sample and before this period's first decode pass. Split an audio frame at the period boundary to put the configure there. In MSK144 the label belongs before the period's first 7,168 samples.
- The label in force when a pass runs is the `time` of its `decode` lines, and the `period_end` of the period's `decode_finished`, always six digits.
- The label is decoder input as well. Changing it within a period starts a new decoder input: FT8's next pass sends the period's earlier decodes again, and JT65 averages odd and even minutes apart by it.
- Prefer `utc`. In periods of a minute or more an `nutc` up to 2359 reads as HHMM, so 00:04:00 sent as `400` reads as 04:00. A frame that carries both uses `utc`. A label that is not a time of day declines the frame (`configure_range_error`, key `nutc` or `utc`).
- Each decoder gets the label in the form WSJT-X gives it, whichever form is sent. In periods of a minute or more, JT9, JT65 and JT65+JT9 decode with HHMM and JT4 with HHMM00, and their `time` and `period_end` are HHMM00.
- A label stays as it was sent, and a later mode reads it again, except that in JT4, Q65, FST4 and FST4W periods of a minute or more an HHMM `nutc` is multiplied by 100 on arrival. Send a label with every period, and after every mode change.
- MSK144 decodes each 3,584-sample block as it arrives, only in the period's first 30 s, and never a partial block.
- Fast JT9 decodes once a period, as WSJT-X does, on the period's audio up to a 3,456-sample block end. From live audio that is the first block end with less than 0.35 s of the period left: 58,752 samples at 5 s, 117,504 at 10 s, 176,256 at 15 s and 355,968 at 30 s. While the decoder is busy it tries again at each later whole block, and a period whose last try finds it busy is skipped (`pass_skipped`, reason `busy`); at 5 s and 10 s the first try is the last. From a file it decodes at the first block end at or past the period's samples less 3,584: 179,712 in a 15 s period. A try that gets no copy of its audio waits for the next, as a busy one does. A period cut short (a `flush`, `halt` or the end of input) before its last try decodes as WSJT-X decodes a file of the samples it has, at the first block end at or past them less 3,584; with fewer than one block, which WSJT-X never decodes, the codec decodes all of them.

### Live audio and files

- From a file (`live_audio` false, as at the start), each period decodes on its whole audio once the period is in; FT4 and multithreaded FT8 decode at their stop points, fast JT9 at its file point (see [Periods and labels](#periods-and-labels)) and Echo at its stop ([Echo periods](#echo-periods)). Single-threaded FT8's early passes (41 and 47) still run; outside SuperFox Hound decoding, multithreaded FT8 runs its early passes (41 and 46) only with `ft8_decoder_start` 0 or 1, and that key starts at 3. No pass is skipped for a busy decoder, only for want of memory (see [Memory](#memory)), and nothing reads the clock. A `flush` decodes the file's last, short period.
- From live audio, send `live_audio` true. Each period then decodes at WSJT-X's stop points for the mode, and a pass that WSJT-X's busy decoder would not run is skipped (`pass_skipped`). FT8's passes stop on the wall clock as WSJT-X's live passes do: pass 41 when the UTC 15 s period is 13.4 s in, and pass 47's subtraction step at 14.3 s; the final pass is not cut short.
- So a live client streams in real time, starts each period on its UTC boundary (a `discontinuity` there re-anchors the period), and labels each period. Audio out of step with UTC loses FT8 decodes.
- Only the multithreaded FT8 decoder defers a final pass that the busy decoder cannot run, as WSJT-X's does. The deferred final runs when the decoder comes free, unless a later final replaces it (`pass_skipped`, reason `replaced`). Fast JT9 tries again at its later blocks (see [Periods and labels](#periods-and-labels)). Every other mode but MSK144, Echo and FreqCal, which never wait for the decoder, skips such a final (reason `busy`).
- A configure that changes the mode, the period, `live_audio`, or a decoder or station setting that WSJT-X cancels on drops a waiting final (reason `cancelled`).
- A live producer that does not send `live_audio` gets every pass, however far behind. The codec buffers up to 16 MiB of input and queues up to 64 MiB for the decoder, audio and frames alike, and one pass more, which can be a 43.2 MB FST4W-1800 period; past that it stops reading, and the client's writes block as on a full pipe.

### Discontinuity, flush and halt

- `discontinuity`: the next audio sample does not follow the last one. In a period mode it re-anchors the period. Every pass whose audio is in has run, and a deferred final still runs. A period in flight that has not reached its stop (from a file, the whole period, or FT4's and multithreaded FT8's stop; in fast JT9 the try that decodes the period, its last at most) ends with no decode and no `decode_finished`; the decodes its early FT8 passes or MSK144 blocks sent stand. The next audio sample starts a new period. At a period's start a `discontinuity` only re-anchors the audio clock: the next sample's arrival sets again when later audio falls due, which `pass_skipped`'s reason `busy` and `busy` false go by. In JTTY it ends the JTTY session.
- `flush`: decode what has come in, and go on. A deferred final runs, the period in flight decodes once, now, if it has samples, and the next audio sample starts a new period. Use it for a file's last, short period. The period's remaining passes run at their usual half-symbols on the audio that is in, so a decode's `pass` names the half-symbol, not how much audio there was. In JTTY it ends the JTTY session, and the stream goes on.
- `halt`: drain the period in flight, decoding it once, after a deferred final, if it has samples; then exit. Closing stdin does the same.

### Telling a hang from a slow decode

Send `busy_events` true, and keep the deadlines WSJT-X's watchdog keeps:

- `busy` true comes at the start of every decode pass, with the pass's `mode`, `period_end` and `nzhsym`, and `again` true on a decode again; and when a final is deferred, with `deferred` true. `busy` false comes once no pass runs, no final waits and everything received before the last decode ended has been decided, and at `halt` or the end of input. With the decode thread, `busy` stays true across back-to-back passes; without one (a build without OpenMP, or none could be started, which stderr notes), it turns false between them too. It always returns to false. MSK144, Echo and FreqCal send no `busy` lines.
- While `busy` is true, progress is a `busy`, `pass_progress` or `pass_skipped` line, or a `decode` whose mode is the last `busy` line's (JT65+JT9's decodes say JT9 or JT65). MSK144's, Echo's and FreqCal's lines, the rows under [Spectrum rows and equalization](#spectrum-rows-and-equalization) and replies are not progress.
- Allow a pass this long between progress lines: on live audio, outside a decode again and after the first completion, 10 s in FT8 and 6 s in FT4; otherwise the greater of 60 s and four periods, or, before the first completion, of 300 s and four periods. A pass ends, and completes, at its `decode_finished` or at the next `busy` line; early FT8 passes send no `decode_finished`.
- While `busy` is false no decode runs. Once a period's scheduled end has passed, whether or not the codec has read all its audio, its `decode_finished`, or a `busy` line naming its `period_end`, is due within the same time, except for the periods that send no `decode_finished` (see below). That deadline runs only while `busy` is false: a `busy` true suspends it, and it starts again from zero at the next `busy` false. On live audio either line can come before the period's last sample has been sent, since passes run at their stop points: count a period answered when its line arrives.
- No deadline runs from a reply's first line to its last, nor while `planning` is true. Afterwards each deadline resumes where it stopped, so replies sent one after another cannot hide a hung decode.
- A JTTY review's `busy` lines carry `review` and come from the stream's own thread ([JTTY review](#jtty-review)). A client is busy while the last `busy` line without `review` says true or a review is in progress, as WSJT-X's decoder counts its JTTY review as busy; the deadlines above follow only the lines without `review`.
- The stream's own thread (frames, replies, MSK144, Echo, JTTY, JTTY reviews and FreqCal) needs no deadline. A deadline kept on it must allow a block interval plus the longest render sent and the waits of encode requests, and must stop while `planning` is true.
- `pass_progress` is advisory: at most one a second.
- `decode_finished` comes once per period, and once more per decode again. A period sends none when a mode or period change discards it, when a `discontinuity` drops it short of its stop (in fast JT9, of the try that decodes it, its last at most), or when it is a SuperFox even-sequence period whose stop is below 50 or a FreqCal period. A period whose final pass is skipped still sends one.

### Ordering

- Decode lines, `decode_finished`, `pass_skipped` and `busy` lines, and the errors for frames sent in a period mode other than Echo and FreqCal come out in the order of the input that caused them: an error waits behind the decode work queued before it. Echo and FreqCal write their lines and those errors themselves, in input order ([Echo periods](#echo-periods)).
- Periods' lines can interleave. A period's `decode` lines carry its label as `time` and its other lines carry it as `period_end`; group them by it. A configure that enters MSK144, ECHO, JTTY or FreqCal starts that mode at once, so the previous mode's remaining decode lines can follow the new mode's.
- A reply to `pack` or `render` waits for no decode. It can come before the decode lines for audio sent before it, before a waiting final that starts just before it, and before the errors for configure, control or odd-length audio frames sent before it. Match an error that has no `id` by its `code`, not by the request in flight.
- With `spectrum`, `ref_spectrum` or `prepared_audio` on, other lines go out ahead of the live rows queued before them, and live rows can be discarded ([Spectrum rows and equalization](#spectrum-rows-and-equalization)).

### Exit status

| Status | When |
|---|---|
| 0 | After `halt` or the end of input, once the last period's lines are out; also after `-v` and `--help` |
| 1 | A bad session header (one `error` line with `msg` instead of `code`), a frame longer than 43,200,000 bytes (`frame_too_large`), no memory to buffer or queue the input (`out_of_memory`), no memory for the decoder's buffer (an `error` line with `msg`, before `ready`), or a fatal JTTY error; with `-2` and files, an `-a` that is not a usable directory or a file `wsprd` cannot read (`wsprd`'s message on stderr) |
| 2 | An invalid command-line value (stderr only), or `-9`, `-4`, `-6`, or `-6` with `-9`, and a `-p` other than 60 (`configure_range_error` in place of `ready`); `-2` with a `-p` other than 120 (stderr only) |

- After an `out_of_memory` raised while queuing input for the decoder, and after JTTY's fatal errors, the status is not guaranteed to be 1.
- Once the output queue runs ([Spectrum rows and equalization](#spectrum-rows-and-equalization)), an exit by a runtime error, or by a stop in code the codec shares with WSJT-X, loses the lines still queued, decodes included.
- Treat the end of stdout before `ready` as a failed start, whatever the status: an unrecognized option prints the help on stdout and exits 0.

### Memory

- Besides the input it buffers and queues (up to 16 MiB and 64 MiB, above), a codec process holds two buffers of 48 MB, the stream's period buffer and the decoder's; up to three spare copies of a pass's audio and a copy of the last decoded period's audio, each up to 43.2 MB (an FST4W-1800 period); and the decode thread's own copy of the decoders' thread-local arrays, about 2.7 MB on Linux, as each OpenMP thread has.
- Once `spectrum`, `ref_spectrum` or `prepared_audio` has been on, the output queue holds up to 20 rows (about 0.7 MB) and up to 16 MiB of other lines ([Spectrum rows and equalization](#spectrum-rows-and-equalization)), and its writer thread has a 256 KiB stack and, on Linux, its own copy of the thread-local arrays.
- Once a JTTY session has decoded audio, the codec keeps the last 180 s of it for [JTTY review](#jtty-review) in a buffer of 4,320,000 bytes, allocated whole at its first sample, and a review in progress holds a copy of the audio it reviews, up to as much again.
- Once a session has decoded both MSK144 and a period mode that runs OpenMP threads, it holds a second pool of those threads, each with its copy.
- Once a session has decoded WSPR, it holds about 4.2 MB for the rest of it: the hash table (0.6 MB), the period's I/Q and what subtraction leaves of it (1 MB), the subtraction's buffers (1.1 MB), the decoder's spectra (0.7 MB) and its transform's buffers (0.7 MB). Each WSPR decode also allocates about 12 MB of FFT buffers while it downsamples the period, freed before the decode itself runs.
- When the codec cannot buffer or queue its input it sends `out_of_memory` and exits. When it cannot copy a pass's audio for the decoder it skips the pass (`pass_skipped`, reason `memory`), on file audio too; live, fast JT9 tries again at its next block and skips only at its last, and from a file, where its first try is its last, it skips at once.
- On an 18-core arm64 Mac, the session under [A first session](#a-first-session) peaked at 121 MB resident in a fresh process, and at 268 MB with `multithreaded_ft8` true.

### Versions and unknown names

- Every line carries `"v":1`, and `ready` carries `"protocol":1`. Both have stayed 1 while lines and fields were added, so they do not tell interfaces apart: the release does. `wsjt-codec -v` prints it, for example `wsjt-codec 3.3.0-devel (revision 337cc8)`.
- A configure may carry `version`. 1 is the only value accepted; another declines the frame with `unknown_schema_version`.
- Ignore a line whose `t` you do not know, and a field you do not know; a `decode` line's fields differ by mode family. The codec skips frame types it does not know, and sends nothing for a control frame whose `t` it does not know.

### Displaying dt and spread

- `dt` and `spread` are the decoder's single-precision values written to 9 significant digits, which read back as those values.
- WSJT-X shows `dt` with one decimal (Fortran `f5.1`), and `spread` with three decimals below 0.95 Hz and two from there (`f6.3`, `f6.2`), each rounded half to even on the value.
- C's `printf("%5.1f", dt)` on glibc and macOS rounds this way, and so does Python's `format(dt, "5.1f")`. JavaScript's `toFixed` and Qt's `QString::number` round ties away from zero, and differ from WSJT-X on about one FT8 decode in 50.
- For example, `N1PJT HB9CQK -10` in the session below has `"dt":0.25`. WSJT-X shows 0.2; `(0.25).toFixed(1)` gives 0.3.
- To show the whole line as WSJT-X does, follow the per-family layouts in [`lib/decoder_callbacks.f90`](../../lib/decoder_callbacks.f90), with the time as HHMM in periods of a minute or more.

### Spectrum rows and equalization

- The codec computes the data a client draws its spectrum, waterfall, signal meter and Fast Graph from, and draws nothing. A row carries numbers: the power and peak in dB of a block of samples, and the bins of a power spectrum, or in MSK144 and fast JT9 Fast Graph columns. The pixels are the client's: averaging rows, summing bins into pixels, the bins' dB, Flatten, gain, zero and the palette. WSJT-X's waterfall does this in `WideGraph::dataSink2` ([`widgets/widegraph.cpp`](../../widgets/widegraph.cpp)) and `CPlotter::draw` ([`widgets/plotter.cpp`](../../widgets/plotter.cpp)).
- The Cumulative and Linear Avg curves are the client's too. In a period WSJT-X starts both from the mean of `data`/1000 over the period's rows so far; in JTTY from the running mean of `data`/1000 over a session's rows up to the 625th (180 s), after which each row moves it by 1/625 of its difference from the row. Linear Avg is that curve flattened and smoothed by the client's settings.
- The `spectrum` and `fast_spectrum` rows are optional. `spectrum` turns them on; at its start value, false, the codec computes none and sends none. With `spectrum` false, `ref_spectrum` `off` and `prepared_audio` false, the start values, no writer thread starts either.
- Every row is computed from the audio as equalized (`ref_spectrum`), the signal the decoders hear, and every `spectrum` row in every mode the same way: the same samples from the same start give the same row.
- `spectrum`, in every mode but MSK144, fast JT9 and JTTY: one row per 3,456-sample block of the period, live every block, from a file those up to the stop (JT65+JT9 every block). `k` is the block's end in samples from the period's start and `hsym` its index from 1. `power` and `peak` are WSJT-X's signal meter. `data` holds `bins` values, 6,827 from 0 to 5000 Hz: the linear power spectrum of the last 16,384 samples, zeros before the period's start, as WSJT-X's waterfall takes it (`symspec`'s `s` at gain 0, [`lib/symspec.f90`](../../lib/symspec.f90)); value m, from 0, is at m·`df` Hz. A period has rows only if `spectrum` is on when its first block completes; turned on later, rows start at the next period. Each period's rows start again at `hsym` 1.
- `spectrum`, in JTTY: one row per 3,456 samples of a session from its first sample, or from the first after `spectrum` comes on (while equalizing, from the start of the block then held). `reception` is its session, the number the `session` line carries as `session`; `begin` and `end` are its first and after-last stream samples; the rest is as above, counted from the rows' first sample. The codec has no wall clock for a sample: time a row by the client's clock at `end`.
- `fast_spectrum`, in MSK144 and fast JT9 in place of `spectrum`: the Fast Graph, one row per block of a period's first 30 s, MSK144's of 3,584 samples and fast JT9's of 3,456. The graph has up to 703 columns, one per 512 samples, or per 256 when `trperiod` is below 30; past the 703rd, each new column rewrites the last. A row carries the columns it writes: `first`, the 0-based index of the first, and `columns`, how many. `green` is each column's power in dB, and `data` each column's 64 bins of linear power, as `hspec`'s `s` at gain 0 ([`lib/hspec.f90`](../../lib/hspec.f90)): bin b, from 0, sums the 512-point FFT's bins 2b+1 and 2b+2, 23.4375 Hz each, up to 3000 Hz. `power` and `peak`, the signal meter, are the last column's with signal. Turned on after a period's first block, rows start with the block then completing, its `first` above 0; the earlier columns are not sent.
- `low_sidelobes` is WSJT-X's "Waterfall spectra" setting: true is "Low sidelobes", a Nuttall window on each `spectrum` row's FFT, and false is "Most sensitive"; WSJT-X and the codec start at Low sidelobes. In the codec it also windows the symbol spectra the JT9 decoders read, JT65+JT9's and slow JT9's, so it can change those decodes. Slow JT9 uses the setting in force when its decode is requested; JT65+JT9 takes it block by block. `fast_spectrum` has no window.
- Equalization is WSJT-X's reference-spectrum equalization, on live audio only, ahead of every decoder and every row. `ref_spectrum` `measure` builds the reference from the audio and rewrites `refspec.dat` in `-a` every 13,824 samples, passing the audio on unfiltered until a configure ends it: WSJT-X measures for 7 s, so send `off` or `apply` when done. `apply` is WSJT-X's Ref Spec: it loads `refspec.dat` and filters the audio by it.
- `ref_spectrum_clear` true is WSJT-X's "Erase reference spectrum", except that it also ends a measurement: it zeros the measurement, deletes `refspec.dat` and sets `ref_spectrum` to `off` before the frame's own `ref_spectrum` applies, so `measure` in the same configure measures again from scratch. No `ref_spectrum` line follows a clear: clear the Reference curve shown.
- A `ref_spectrum` line carries the reference curve in dB, at each rewrite while measuring and when `apply` loads the file. A file that cannot be written, read or deleted gives one `ref_spectrum_file` error until `ref_spectrum` changes or a clear, and the session goes on; `apply` without a readable file leaves the audio as it is and tries the file again every 3,456 samples or fewer.
- In JTTY, while equalizing, audio waits for its 3,456-sample block, up to 288 ms, before it decodes. A block the client stops sending partway waits until the session closes: a `discontinuity`, `flush`, `halt`, the end of input, an odd-length audio frame or a mode change.
- `prepared_audio` sends back the audio after the receive chain, the signal the decoders hear, from live audio only: equalized while `ref_spectrum` is `apply`, otherwise as received. From a file nothing is sent. A client that saves audio saves these lines, so that its files hold the audio as equalized, as WSJT-X's Save Decoded and Save All files do.
- In a period mode and in MSK144, one `prepared_audio` line per 3,456-sample block (MSK144's 3,584) from the period's start, past the stop, through the period's last whole block; in JT65+JT9, which has no stop, through its last sample. `k` is the end of the line's samples in samples from the period's start and `samples` their number: `k` rises by `samples` within a period and starts again with the next. The lines carry no period label: a period starts where `k` starts again. A period has lines only if `prepared_audio` is on, on live audio, when its first block completes; turned on later, they start at the next period, and once a block goes without one, the period sends no more.
- A part block: before the stop in a period mode, a `flush`, `halt` or the end of input sends the block's samples so far, and so does a configure that moves the stop to audio already in, after which the period goes on and the rest of the block goes with its end. Otherwise a part block in progress is dropped: at a `discontinuity`, a change of mode or period, and, in MSK144 or past the stop, at a `flush`, `halt` or the end of input.
- In JTTY, one `prepared_audio` line for each piece of audio handed to the session while `prepared_audio` is on: while equalizing, blocks of 3,456 samples counted from the session's first sample (shorter where equalizing starts or stops or the session closes), otherwise each audio frame as it comes, in pieces of up to 4,096 samples. `reception`, `begin` and `end` are as a JTTY `spectrum` row has them.
- A `prepared_audio` line's `data` holds int16 little-endian samples in base64, as `pcm` lines carry audio. A block's line comes after any `ref_spectrum` line the block brings and before its `spectrum` or `fast_spectrum` row. The lines come at about 32 KB/s (more in JTTY with short audio frames, one line each). They are never dropped, unlike live rows: if the client stops reading standard output, they pile up, and after about 9 minutes (16 MiB) the codec pauses, decodes included, until the client reads again. Each line is under 64 KiB.
- Once a configure first turns `spectrum`, `ref_spectrum` or `prepared_audio` on, one writer thread writes every standard output line through a queue for the rest of the session; if it cannot start, the codec writes each line at once, as without it. On live audio a `spectrum` or `fast_spectrum` row waits there behind at most 19 others; when another arrives the oldest is discarded, so live rows never make a decode wait, and a `rows_dropped` line comes before the next such row written. Every other line, `ref_spectrum` and `prepared_audio` lines and rows from a file included, goes out in order, ahead of the live rows queued before it, and is never discarded: past 16 MiB of them the codec waits for the reader, and decodes wait with it. A file session with `spectrum` on reaches that after about 9 unread FT8 periods. A configure from live audio to a file waits until the live rows queued are written.
- Live, rows come at about 3.5 lines a second (MSK144 3.35, over a period's first 30 s), and 0.87 more while measuring. Arrays are base64 (RFC 4648) of IEEE float32 little-endian values, and every row is under 64 KiB.

### Side outputs and labels

- The decoders write the side files WSJT-X's GUI reads, as WSJT-X's decoder does: `houndcallers.txt`, `avemsg.txt` and `red.dat` in `-t`, and `tsil.3q` in `-a`. Each also comes as a line, so that a client that shares no directory with the codec gets the same lists; the files stay as they are.
- `fox_callers`, after each FT8 decode pass in Fox mode (`contest_type` `fox`): the callers the pass writes to `houndcallers.txt`, in its order. A caller is a decode whose first word is `mycall` (or a compound form of it) and whose third word is a grid or absent, at 1000 Hz or above unless `superfox_mode`; the list keeps each such decode while its age, in units of 30 s by the period labels, is 4 or less, so a station decoded in two periods is listed twice. WSJT-X shows the list in its band activity pane after dropping the stations queued, worked or not answering a directed CQ, adding each continent and sorting; that is the client's.
- One difference from the file: a pass that ages out every caller writes nothing to `houndcallers.txt`, which then keeps the last pass's lines (the decoder rewrites it without truncating it), and its `fox_callers` has no callers. The next pass empties the file. Single-threaded FT8 has later passes in the same period (47 and 50), and so has the multithreaded decoder with `ft8_decoder_start` 0 or 1 (after 41 and 46, its final); with a later start the multithreaded decoder runs one pass a period, and the file keeps the stale lines until the next period's pass. WSJT-X shows the file's lines, so for that period its list differs from the line's.
- WSJT-X's Fox reset, on its Reset button and on a band change, deletes `houndcallers.txt`, and the decoder then starts a new list. `fox_callers_clear` true does the same, once, at the configure's place in the stream: after the decode passes before it, so the next FT8 pass in Fox mode starts a new list. Send it where WSJT-X resets. No line answers it: clear the list shown, as the next `fox_callers` will.
- The `fox_callers` line holds about 77 bytes a caller, so at the decoder's cap of 1000 callers it would be about 77 KB (an estimate from the line's form, not measured); a client that limits a line to 64 KiB can skip it, as any line it does not read.
- `averages`, after each JT4 and JT65 decode pass: `avemsg.txt`'s entries, the periods the decoder holds for averaging, up to 64. JT65+JT9 and Q65 write no `avemsg.txt` and send none. `clear_average` clears the entries for the next pass. WSJT-X's Message Averaging window shows each entry as `avemsg.txt`'s line, Fortran `(a1,i5.4,f6.1,f6.2,i6,1x,a1)`: `$` when the last average used it, else `.`; the label as HHMM; sync and dt, rounded as under [Displaying dt and spread](#displaying-dt-and-spread); frequency; and the polarity as JT4's `*` (1) or `$` (-1), or JT65's `*` (1), `#` (-1) or a blank (0).
- `q65_sync`, after each Q65 decode pass that has sync curves: `red.dat`'s values, from which WSJT-X draws Q65's red (averaged) and orange (current) sync curves and its "DT =" text. A pass without curves writes no `red.dat` and sends none: keep the last. WSJT-X draws the red curve only when `averaged_min` or `averaged_max` is not 0, which takes two periods averaged. Bin `first_bin`+i, from i = 0, is at (`first_bin`+i)·`df` Hz, the product in single precision, within `nfa` to `nfb`. The line grows with the period: about 12 KB at 30 s, 24 KB at 60 s, 54 KB at 120 s and 140 KB at 300 s (with `nfa` 200 and `nfb` 4000), so at 300 s it is over 64 KiB. A client that limits a line to 64 KiB can skip the line, as it skips any line it does not read ([Versions and unknown names](#versions-and-unknown-names)); the decodes do not depend on it.
- `q65_callers`, after each Q65 decode pass in a contest activity (`contest_type` `na_vhf`, `ww_digi`, `arrl_digi` or `q65_pileup`) that loaded the list: the pileup list the pass leaves, in its order, up to 50, which it saves to `tsil.3q`; the line comes whether or not the save succeeds, and a pass that cannot save it loads the old file next time. WSJT-X's Active Stations list shows it after dropping the callers not decoded in 24 h, adding the Moon's elevation at each grid (and, from live audio, dropping those where it is below -5 degrees) and sorting by frequency; that is the client's. `last_seen` comes from the codec's clock.
- The pileup list lives in `tsil.3q`: each contest pass loads it, adds the callers it decodes, and saves it. Two configure keys edit it as WSJT-X does between decodes, once each, at the configure's place in the stream: after the decode passes before it and before the next, so no pass saves over the edit (WSJT-X's own edit can be lost to a decode in progress). `q65_callers_clear` true deletes the file, as WSJT-X's "Erase list of Q65 callers". `q65_callers_remove`, an array of calls, removes for each the first entry with its call, as WSJT-X's Ctrl-click and logging do; the list keeps a call's first 6 characters, and so does the comparison. A frame with both clears first. No line answers an edit: apply it to the list shown, as the next `q65_callers` will.
- Each side line comes after its pass's `decode` lines and before its period's `decode_finished`, from the thread that runs the pass (the decode thread, or the frame loop's thread without one), with the pass's period label as `time` and its `pass`, as the pass's `decode` lines have them; the edits run on that thread too. None is progress under [Telling a hang from a slow decode](#telling-a-hang-from-a-slow-decode).
- Labels: a `decode` line carries `submode`, the letter the pass decoded with, in JT4, JT9 (fast JT9 too), JT65 and Q65: configure's `submode` as it stood for that pass, so a session that changes it between periods labels each period's decodes with its own. In JT65+JT9 its JT65 lines carry it, as its JT65 decoder reads it, and its JT9 lines carry none, as its JT9 decoder reads no submode. MSK144's lines carry `period`, `trperiod`'s whole seconds, as WSJT-X passes it to its MSK144 decoder; FST4's, FST4W's, Q65's and fast JT9's carry it too. Other modes' periods are their fixed ones, or 60 s, as their labels' HHMM form shows.

### Keys that keep WSJT-X's behavior

These configure keys carry WSJT-X's settings and state. Send the values WSJT-X would use; a key never sent keeps its starting value, apart from the resets, groups and call flags under [Configure](#configure).

- Live audio and timing: `live_audio` is WSJT-X's distinction between live audio and a file. `decode_after_eme_delay` is its "Decode after EME delay": a configure that carries it, `mode` or `trperiod` sets the EME delay to 2.5 s while `decode_after_eme_delay` is true and to 0 while it is false, and `eme_delay_seconds`, when sent, wins.
- Decoder progress: `busy_events` turns on the `busy`, `pass_progress` and `planning` lines, which tell a slow decode from a hung one as WSJT-X's watchdog does.
- Decoding again: `nagain_flag` true asks once to decode the last period again. On live audio it is refused while the decoder is busy (`decode_again_unavailable`), as WSJT-X's Decode button is; send it again after a `decode_finished`. MSK144, Echo, JTTY, FreqCal and WSPR keep no period to decode again; in JTTY, WSJT-X's Decode button is a [JTTY review](#jtty-review): send `jtty_review`.
- Averages: `clear_average` is WSJT-X's Clear Avg, for JT4, JT65, JT65+JT9 and Q65, and acts only on the next decode that runs; in ECHO it clears Echo's averages instead, at its next measurement ([Echo keys](#echo-keys)).
- MSK144: `short_messages` and `swl` are WSJT-X's "Sh" and "SWL" check boxes; `train` is its "Measure phase response", reported by `msk144_trained`, after which `train` is false; `phase_eq` holds the five coefficients its Equalization Tools apply (all zeros, the default, is no equalization). Each is in force in every mode until another configure sets it.
- JT4 and JT65: `min_width` is JT4's; `min_sync` is JT4's, JT65's and JT65+JT9's, and in fast JT9 only its sign counts: below 0 a decode returns up to 50 lines, otherwise 2, as in WSJT-X; `n_2pass` and `aggressive` (WSJT-X's Aggressive, 0 to 10) are JT65's and JT65+JT9's; `jt65_ap_on` is JT65's; `n_trials` is the trial count of JT65's soft-decision decoder.
- JT65+JT9 only: `robust_mode`, `tx_mode` and `npts_c0_array`.
- AP decoding: `ft8_ap_on` and `ap_width_hz` reach both FT8 decoders (in the multithreaded one, `ft8_ap_on` gates only its a8 pass); `ap_cq_only` reaches the single-threaded FT8 decoder, FT4, FST4 and Q65; `qso_progress_state` (0 to 5) reaches both FT8 decoders, FT4, JT65, JT65+JT9, FST4 and Q65.
- The multithreaded FT8 decoder: `multithreaded_ft8` selects it, and only it reads `ft8_cycles`, `ft8_rxf_sensitivity`, `ft8_threads`, `ft8_low_threshold`, `ft8_subpass`, `ap_my_call`, `hide_ft8_duplicates`, `enable_dxc_search`, `wide_dxc_search`, `hound_mode`, `multi_instance`, `skip_tx1`, `nagain_filter`, `stop_hint`, `candthin_threshold` and `dt_center_seconds`, and the station state `last_tx_seconds_ago`, `currently_txing`, `mode_changed`, `sec_band_changed` and `delay_units`. `ft8_decoder_start` sets its pass schedule. An `ft8_threads` outside 0 to 24 declines a configure in FT8 (`configure_range_error`), single-threaded too.
- SuperFox: `superfox_mode` and `even_sequence` drive SuperFox decoding, and `date` reaches only the SuperFox decoder.
- Fox: `fox_callers_clear` is WSJT-X's Fox reset of the caller list ([Side outputs and labels](#side-outputs-and-labels)).
- WSPR: `wspr_hash_clear` is WSJT-X's Erase WSPR hashtable ([WSPR](#wspr)).
- Displays and equalization: `spectrum`, `low_sidelobes` (which also windows slow JT9's and JT65+JT9's symbol spectra), `ref_spectrum` and `ref_spectrum_clear` ([Spectrum rows and equalization](#spectrum-rows-and-equalization)).
- Saved audio: `prepared_audio` sends back the audio after the receive chain, for the files WSJT-X's Save Decoded and Save All write ([Spectrum rows and equalization](#spectrum-rows-and-equalization)).
- Q65: `max_drift_hz` sets Q65's drift search; `q65_pileup` (or a `contest_type` of `q65_pileup`) is WSJT-X's Q65 Pileup activity; `q65_callers_clear` and `q65_callers_remove` edit the pileup list as WSJT-X's Erase and Ctrl-click do ([Side outputs and labels](#side-outputs-and-labels)).

### A first session

This session ran [`test/wav_to_stream_harness.py`](../../test/wav_to_stream_harness.py) on [`samples/FT8/210703_133430.wav`](https://wsjtx.github.io/wsjtx/samples/FT8/210703_133430.wav), a 15 s FT8 recording, with `OMP_STACKSIZE=10M` and `--utc 13:34:30`. The harness writes all its input, then reads the codec's output.

It sent, in order:

1. the session header, `57 53 4a 54 00 01 0c 00`: `WSJT`, int16, one channel, 12 kHz;
2. a control frame (type 0x02) of 124 bytes: `{"t":"configure","mode":"FT8","depth":3,"mycall":"KJ5HST","mygrid":"EM18","ntol":1000,"nfa":200,"nfb":3000,"utc":"13:34:30"}`;
3. fifteen audio frames (type 0x01) of 12,000 samples (24,000 bytes) each, the file's 180,000 samples, one period;
4. a control frame (type 0x02) of 12 bytes: `{"t":"halt"}`.

It received this; lines that begin `wsjt-codec:` came from stderr, which the harness merges into its output:

```
wsjt-codec: header ok (fmt=0 ch=1 rate=12 kHz)
{"v":1,"t":"ready","modes":["FT8","FT4","JT9","JT65","JT65+JT9","JT4","FST4","FST4W","Q65","MSK144","ECHO","JTTY","FREQCAL","WSPR"],"encodes":["FT8","FT4","JT4","JT9","JT65","Q65","FST4","FST4W","WSPR","MSK144","ECHO","TUNE","FOX","SUPERFOX","JTTY"],"protocol":1}
wsjt-codec: pass nzhsym=41 samples=141696 depth=3 cycles=3 multithreaded=F live=F again=F emedelay=0.0
{"v":1,"t":"decode","mode":"FT8","time":"133430","snr":17,"dt":0.254999995,"freq":2571,"message":"W1FC F5BZB -08","ap":0,"pass":41}
{"v":1,"t":"decode","mode":"FT8","time":"133430","snr":15,"dt":-0.0649999976,"freq":2157,"message":"WM3PEN EA6VQ -09","ap":0,"pass":41}
[13 lines elided]
wsjt-codec: pass nzhsym=47 samples=162432 depth=3 cycles=3 multithreaded=F live=F again=F emedelay=0.0
wsjt-codec: pass nzhsym=50 samples=180000 depth=3 cycles=3 multithreaded=F live=F again=F emedelay=0.0
{"v":1,"t":"decode","mode":"FT8","time":"133430","snr":-2,"dt":0.25,"freq":466,"message":"N1PJT HB9CQK -10","ap":0,"pass":50}
[5 lines elided]
{"v":1,"t":"decode_finished","period_end":"133430","synced":0,"decoded":21,"average":0}
```

The codec exited with status 0.

- `ready` follows the header; no line answers the configure.
- The early pass at half-symbol 41 sent 15 decodes, and the final pass at 50 sent 6 more; no decode came from pass 47.
- Every decode carries the period's label, `133430`, from the configure's `utc`.
- `decoded` in `decode_finished` is the final pass's count, not a count of the `decode` lines sent: that pass sent 6 lines and counts 21.
- `halt` has no line of its own: the period's lines came out, and the codec exited.

## Transmit

`pack` and `render` encode a message as WSJT-X encodes it for a transmission: `pack` returns its channel symbols, `render` its audio. Send them in any mode, at any point of a session, with or without audio; a client that only encodes sends the session header, then its requests.

### Requests and replies

- `mode` picks the encoder, as [Request keys](#request-keys) states: JTTY's requests are under [JTTY text and templates](#jtty-text-and-templates), the others under [Slot modes](#slot-modes).
- A request without a valid `id` (`1.0` and `"5"` are not) gets an `error` line, `invalid_request_id`, with no `id`, and nothing else.
- A key the request does not take, a misspelled or mis-cased one included, refuses it (`bad_request`, with `key` as written); so do `freq`, `rate`, `seconds` and `cwid` on a `pack`. A key whose value is an array or an object, other than `slots`, refuses the request with no `key`.
- A control frame whose `t` the codec does not know, `PACK` among them, gets no reply, even with an `id`.
- A `pack` or `render` reply's lines are consecutive, each carries the request's `id`, and the last is `packed`, `rendered` or `rejected`. A refused request sends only its `rejected` line. A [JTTY review](#jtty-review)'s lines are not consecutive and end with `reviewed`.

| Request | Reply |
| --- | --- |
| A slot mode's `pack` | `encoded`, then `packed` |
| A slot mode's `render` | `encoded`, the audio's `pcm` lines, then `rendered` |
| JTTY's `pack` | One `segment` line a segment, then `packed` |
| JTTY's `render` | Each `segment` line followed by its `pcm` lines, then `rendered` |

- A request is refused by its `rejected` line, which carries its `id`; no `error` line carries one. The `error` lines a request can cause instead are `invalid_request_id`; `control_frame_too_large`, for a request frame over 262,144 bytes; `configure_parse_error`, for one without a readable `t`; and a fatal error, after which the codec exits.
- The codec answers requests in the order sent, each at its place in the input, and a reply waits for no decode ([Ordering](#ordering)). A request holds up decoding instead: the codec reads no audio while it computes a render, so on live audio a pass whose audio arrives meanwhile runs late, and FT8's wall-clock stops can shorten it; and a reply's lines hold up the next decode line until the client reads them; once the output queue runs ([Spectrum rows and equalization](#spectrum-rows-and-equalization)), only until they are queued, unless 16 MiB of lines wait.
- A render takes from a fraction of a second (FT8) to seconds (JT65, WSPR or FST4 at 48000 Hz).

### Slot modes

| `mode` | Needs | Also takes |
| --- | --- | --- |
| `FT8` | `text` | `spacing` |
| `FT4` | `text` |  |
| `JT4` | `text`; `submode` A to G | `cwid` |
| `JT9` | `text`; `submode` A to H | `spacing`, `cwid`; `fast` true, which needs `submode` E to H and `trperiod` 5, 10, 15 or 30, and takes no `spacing` |
| `JT65` | `text`; `submode` A to C | `cwid` |
| `Q65` | `text`; `trperiod` 15, 30, 60, 120 or 300; `submode` A to C at 15 s, A to D at 30, A to E at 60 and 300, A to F at 120 | `copied`, `cwid` |
| `FST4` | `text`; `trperiod` 15, 30, 60, 120, 300, 900 or 1800 | `spacing`, `cwid` |
| `FST4W` | `text`; `trperiod` 120, 300, 900 or 1800 | `spacing`, `cwid` |
| `WSPR` | `text` | `spacing`, `cwid` |
| `MSK144` | `text`; `trperiod` 5, 10, 15 or 30 | `cwid` |
| `ECHO` | `kind` `tone`; or `kind` `message`, `text` and `spacing`; or `kind` `cw` and `text` |  |
| `TUNE` | `seconds`, on a `render` |  |
| `FOX`, `SUPERFOX` | `slots` | [Fox and SuperFox](#fox-and-superfox) |

- Every request has `t` and `id`, and a `render` adds `freq` and `rate` ([Rendered audio](#rendered-audio)); `cwid` is a render's only. A key the table does not give the mode refuses the request (`bad_request`, naming the key).
- `submode` takes either case. `spacing` defaults to 1; an Echo message's is 10, 15, 20, 25 or 30 Hz. `kind` is WSJT-X's Echo Fixed Tone, Message or CW. `copied` is WSJT-X's flag that the sender copied the station it answers.
- `text` is what WSJT-X's Tx box holds. The codec takes it less its outer spaces, upper-cases a to z, and expands nothing. It takes at most 37 bytes, 22 in JT4, JT9, JT65 and WSPR, and in ECHO six letters and digits; longer is refused `too_long`, and a blank text `empty`.
- A message that does not encode in the mode is refused (`bad_request`, key `text`); WSJT-X shows it as `*** bad message ***` and does not send it.
- WSPR's `text` is one of the messages WSJT-X builds from the station's call, grid and power: `CALL GRID4 DBM`, a call of three to six characters; `PFX/CALL DBM` or `CALL/SFX DBM`, a prefix of one to three characters, a suffix of one character or two digits; or `<CALL> GRID6 DBM`. DBM is one of WSJT-X's powers, 0, 3, 7, 10 and so on to 60. FST4W takes the same, but `<CALL> GRID6` without the power. A message that would decode as another is refused: `K1ABC FN42 38` would decode as `K1ABC FN42 37`. The one exception is a call that begins 3DA0, which WSPR sends as 3D0: `msgsent` shows it as written, as WSJT-X does, and wsprd decodes 3D0.
- In JT4, JT9, JT65 and Q65 a `text` that begins with `@` is WSJT-X's single tone, at the frequency in the next four characters: `@1500` is 1500 Hz, `@12345` is 1234 Hz, and anything there that does not read as an integer, such as `@` alone, `@ABCD` or `@A150`, is 1000 Hz. A frequency below 100 Hz, or a `/` or `,` before any number, is refused, and so is MSK144's `@`. A single tone's `encoded` line carries `freq`, its frequency; its render takes no `freq`, and `copied` and `cwid` only false.
- Compare `msgsent` with `text`, for it can differ with no flag: JT4, JT9 and JT65 drop what they cannot send and cut free text to 13 characters (`THIS IS FREE TEXT` goes as `THIS IS FREE`). ECHO's `msgsent` is `ECHO`, TUNE's `TUNE`. A single tone's is JT4's `text`, Q65's frequency right-justified in five characters followed by a space and Hz (`" 1500 Hz"`), and empty in JT9 and JT65.
- With `fallback` true, the message went as free text, its first 13 characters: `HELLO THERE FRIEND` in FT8 goes as `HELLO THERE F`.
- `type`, in JT4, JT9, JT65 and MSK144, is WSJT-X's message type: 1 standard, 2 to 5 with a prefix or suffix, 6 free text, 0 a single tone, and in JT65 7 a shorthand (`RO`, `RRR` or `73` alone). MSK144's is 1, or 7 for a short message, `<CALL1 CALL2> RPT` with RPT one of -03, +00, +03, +06, +10, +13 and +16, each with or without R, or RRR or 73; a short message has 40 tones.
- `tones` is empty for a single tone and ECHO's `cw`, `[0]` for TUNE and ECHO's `tone`, and six symbols for an Echo message.

Each mode's symbols, samples a symbol at 12000 Hz, tone spacing, and top tone above the lowest:

| Mode | Symbols | Samples a symbol | Tone spacing, Hz | Top tone |
| --- | --- | --- | --- | --- |
| FT8 | 79 | 1920 | 6.25, times `spacing` | 7 spacings |
| FT4 | 105: its 103 tones and a ramp symbol either side | 576 | 12000/576 | 3 spacings |
| JT4 | 206 | 2520 × 12000/11025 | 4.375 times 1, 2, 4, 9, 18, 36, 72 for A to G | 3 spacings |
| JT9 | 85 | 6912 | 12000/6912 times 2<sup>submode</sup> and `spacing` | 8 spacings |
| Fast JT9 | 85 | 480, 240, 120, 60 for E to H | 12000 over the samples a symbol | 8 spacings |
| JT65 | 126 | 4096 × 12000/11025 | 11025/4096 times 1, 2, 4 for A to C | 65 spacings |
| Q65 | 85 | 1800, 3600, 7200, 16000, 41472 for 15, 30, 60, 120, 300 s | 12000 over the samples a symbol, times 2<sup>submode</sup> | 64 spacings |
| FST4, FST4W | 160 | 720, 1680, 3888, 8200, 21504, 66560, 134400 for 15, 30, 60, 120, 300, 900, 1800 s | 12000 over the samples a symbol, times `spacing` | 3 spacings |
| WSPR | 162 | 8192 | 12000/8192, times `spacing` | 3 spacings |
| MSK144 | 144, or 40 for a short message | 6 | Tones at 1000 and 2000 Hz | 1000 Hz (2000 Hz absolute) |
| ECHO, a message | 6 | 4096 | `spacing` | 36 spacings |
| ECHO's `tone`, TUNE, a single tone | One tone |  |  | The tone |

2<sup>submode</sup> counts A as 0.

### Rendered audio

- `freq` takes 200 to 5000 Hz, a JSON number. MSK144 takes 1000 only, its tones being 1000 and 2000 Hz; ECHO's `tone` and `message` take 1490 to below 1510, WSJT-X's 1500 Hz and its dither, and its `cw` 1500; a single tone takes none. FOX's and SUPERFOX's are under [Fox and SuperFox](#fox-and-superfox).
- At 12000 Hz the top tone must be below 6000 Hz, or the render is refused with key `freq` (`text` for a single tone): Q65 at 60 s in submode E from 5000 Hz would reach 6707 Hz.
- Decode each `pcm` line's `data` and join them in order: `samples` in `encoded` is the total, at `rate`. The samples are full scale, before WSJT-X's output attenuation, so scale them to the transmit level.
- The audio is the message from its first sample, as many samples as the mode's symbols take at the rate, rounded down, and one more: FT8's is 151,681 samples at 12000 Hz and 606,721 at 48000. MSK144 and fast JT9 repeat their message for the period less 0.5 s, and one sample more: 174,001 samples at 15 s and 12000 Hz. ECHO is 2.048 s and one sample; TUNE is `seconds` of a steady tone at `freq`, from 1 to 600.
- A render of more than 600 s of audio is refused `too_long`, with no `key`: FST4 and FST4W at 900 and 1800 s.
- The codec does not time a transmission; the client does. WSJT-X starts the audio 1 s into its period: 0.5 s in FT8, in FST4 at 15 s and in Q65 at 15 and 30 s, 0.3 s in FT4, and at the period's start in MSK144, fast JT9 and Echo, which it sends every 6 s.

### CW ID

- `cwid` applies in JT4, JT9, JT65, Q65, FST4, FST4W, WSPR and MSK144, and `samples` counts the CW ID too. FT8, FT4, ECHO and TUNE refuse it (`bad_request`, key `cwid`), as WSJT-X sends none there.
- The call is the `mycall` of the last accepted `configure` that set one. The render is refused `not_configured`, key `mycall`, when no configure has set a call (a blank one counts as none), or when the call is longer than 64 characters, does not decode as a JSON string, has a character Morse lacks (letters, digits and `/` are sent) or is longer than 250 Morse elements; `detail` says which.
- The CW ID is the call in Morse at 22.5 WPM, at the message's frequency (in FST4 and FST4W, the center of their four tones), from the sample after the message's last. WSJT-X plays silence to the end of its audio buffer first; a render does not.
- In MSK144 and fast JT9 the CW ID replaces the message from 1 ms after the period less 5 s, at 1500 Hz, keyed faster than 22.5 WPM when the call's Morse would run longer than 4 s; `encoded` still gives the message. Every transmission with a CW ID ends with it.

### Hashed calls

- A message's hashed call (`<CALL>`) is resolved through the codec's transmit hash tables, which it keeps as WSJT-X keeps its own: one set for FT8 and FT4, which FOX and SUPERFOX use too, and one for FST4, FST4W, Q65 and MSK144. Each holds the calls of every message encoded with it since the codec started, by `pack` or `render`, and never a decode's; WSJT-X's second set also takes the calls of the decodes it shows.
- A message whose hashed call has the hash of a different call already in its set does not encode, in WSJT-X or the codec: `bad_request`, and `detail` says so. A message that carries the call in full, encoded first, replaces the other.

### Fox and SuperFox

- `FOX` is WSJT-X's FT8 Fox, which sends up to five FT8 messages at once, one a slot. `SUPERFOX` is its SuperFox option, which packs them into one transmission.
- `slots` holds at most four messages for a SuperFox with `text`. Each slot is taken less its outer spaces, at most 37 bytes. The encoders upper-case it, so a lower-case slot sends the same tones; FOX's `msgsent` comes back in upper case.
- Each FOX slot must encode in FT8; the `encoded` line's `slots` give each one's FT8 encoding. FOX's `freq` is the first slot's lowest tone, a JSON integer from 200 to 5000 Hz, and each later slot is 60 Hz above the one before. Its audio is 79 symbols of 1920 samples at 12000 Hz, and one sample.
- Each SUPERFOX slot must be one of WSJT-X's forms, word for word: `CQ CALL GRID4`, alone; `HOUND CALL RR73`; `HOUND CALL RPT`; or `HOUND1 RR73; HOUND2 <CALL> RPT`. RPT is a sign and two digits from -18 to +12, zero as +00, and each HOUND at most 6 characters. The slots name the Fox call whole in every combined slot, and as its base call in every other Hound slot.
- SuperFox sends the last slot's Fox call for every Hound. For a compound or suffixed Fox call, send the CQ with its grid, and make the last slot one that names the base call; WSJT-X's own SuperFox fails the same way otherwise.
- SuperFox carries at most 5 Hounds with RR73 and 4 with reports, 9 in all, or 4 in all with `text`; a combined slot holds one of each.
- SUPERFOX's `text` is upper-cased, keeps its leading blanks, and is at most 26 bytes less its trailing blanks; it is sent when given, even blank. `more_cqs` defaults to false. `otp` defaults to 0, WSJT-X's code with verification off; WSJT-X makes the code from the operator's seed and the time, and here the client makes it.
- SUPERFOX's `encoded` line carries its 151 channel symbols (0 to 128) in `tones`, and no `msgsent`: WSJT-X shows and logs the slots and the text as written. Its `freq` is 750, a JSON integer. Its audio is 153 symbols of 1024 samples at 12000 Hz, and one sample.
- WSJT-X starts a Fox 0.5 s and a SuperFox 0.4 s into its period.

### A pack session

This session sent the session header, then these control frames, each type 0x02; the JTTY requests that followed them are under [A JTTY pack](#a-jtty-pack):

1. `{"t":"configure","mycall":"K1ABC","mygrid":"FN42"}`
2. `{"t":"pack","id":1,"mode":"FT8","text":"K1ABC W9XYZ -05"}`
3. `{"t":"pack","id":2,"mode":"Q65","submode":"A","trperiod":60,"text":"K1ABC W9XYZ -05"}`
4. `{"t":"pack","id":3,"mode":"Q65","submode":"A","text":"K1ABC W9XYZ -05"}`

It received:

```text
[1 line elided: ready, as under A render]
{"v":1,"t":"encoded","id":1,"mode":"FT8","msgsent":"K1ABC W9XYZ -05","tones":[3,1,4,0,6,5,2,0,3,2,2,4,7,5,2,3,5,0,4,0,6,1,1,4,7,0,1,7,4,6,2,5,3,5,6,0,3,1,4,0,6,5,2,1,0,4,0,4,7,1,3,3,5,5,1,3,2,6,6,7,5,5,2,3,5,6,5,4,7,0,2,1,3,1,4,0,6,5,2],"fallback":false,"spacing":1}
{"v":1,"t":"packed","id":1}
{"v":1,"t":"encoded","id":2,"mode":"Q65","msgsent":"K1ABC W9XYZ -05","tones":[0,3,28,56,36,21,7,6,0,10,56,0,0,2,0,63,44,35,53,34,3,0,0,23,10,0,0,41,42,50,6,7,0,54,0,61,51,0,8,62,46,50,22,4,62,0,43,45,46,0,21,55,9,27,0,44,49,28,45,0,14,0,21,18,21,0,26,58,0,39,61,11,59,0,57,0,18,60,6,5,52,56,3,36,0],"fallback":false,"submode":"A","trperiod":60,"copied":false}
{"v":1,"t":"packed","id":2}
{"v":1,"t":"rejected","id":3,"reason":"bad_request","key":"trperiod","detail":"trperiod must be 15, 30, 60, 120 or 300, a JSON integer of seconds"}
```

- The configure has no reply; it sets the station's call that JTTY's `%M` uses. A slot mode's `pack` needs no configure.
- `encoded` gives what each request applied: FT8's default `spacing`, and Q65's `copied`, false by default.
- The third request leaves out the period Q65 needs, and its reply is its `rejected` line alone.

### A render

This ran the Python example's `render` ([codec_client.py](examples/python/codec_client.py)) for `CQ K1ABC FN42`, with `--codec` a script that started the codec and kept a copy of its standard output, and `OMP_STACKSIZE=10M`. The client asks for FT8 at 1500 Hz and 12000 Hz, then decodes the audio back, 0.5 s into a period of silence.

It sent the session header, then:

1. a control frame: `{"t":"render","id":1,"mode":"FT8","text":"CQ K1ABC FN42","freq":1500,"rate":12000}`;
2. two control frames: `{"t":"configure","mode":"FT8","depth":3,"mycall":"","his_call":"","his_grid":"","nfa":200,"nfb":4000,"ntol":1000}`, then `{"t":"configure","utc":"00:00:00"}`;
3. 150 audio frames of 1,200 samples, one 15 s period holding the rendered audio from 0.5 s;
4. a control frame: `{"t":"halt"}`.

The codec's standard output:

```text
{"v":1,"t":"ready","modes":["FT8","FT4","JT9","JT65","JT65+JT9","JT4","FST4","FST4W","Q65","MSK144","ECHO","JTTY","FREQCAL","WSPR"],"encodes":["FT8","FT4","JT4","JT9","JT65","Q65","FST4","FST4W","WSPR","MSK144","ECHO","TUNE","FOX","SUPERFOX","JTTY"],"protocol":1}
{"v":1,"t":"encoded","id":1,"mode":"FT8","msgsent":"CQ K1ABC FN42","tones":[3,1,4,0,6,5,2,0,0,0,0,0,0,0,0,1,0,0,5,4,7,6,7,0,4,6,0,6,0,2,1,5,3,3,4,3,3,1,4,0,6,5,2,7,3,6,0,1,1,0,4,7,5,1,7,0,0,7,3,3,4,7,4,5,4,5,5,1,3,3,5,4,3,1,4,0,6,5,2],"fallback":false,"spacing":1,"freq":1500,"rate":12000,"samples":151681}
{"v":1,"t":"pcm","id":1,"seg":0,"seq":0,"data":"AAABAAYACQD//+b/zv/T/wcA[43,668 base64 characters elided]"}
[8 lines elided: pcm, seq 1 to 8, 16,384 samples each]
{"v":1,"t":"pcm","id":1,"seg":0,"seq":9,"data":"UIZEyG4sU3UzdoYuS8oGhwSO[11,244 base64 characters elided]"}
{"v":1,"t":"rendered","id":1}
{"v":1,"t":"decode","mode":"FT8","time":"000000","snr":42,"dt":0.0,"freq":1500,"message":"CQ K1ABC FN42","ap":0,"pass":41}
{"v":1,"t":"decode_finished","period_end":"000000","synced":0,"decoded":1,"average":0}
```

The client printed `rendered 151681 CQ K1ABC FN42`, `decode 000000 42 0.0 1500 CQ K1ABC FN42` and `finished 000000`, and exited with status 0.

- `samples`, 151,681, is nine `pcm` lines of 16,384 samples and one of 4,225: 79 symbols of 1920 samples, and one.
- A `render` needs no configure: the client sent its configures after the render, for the decoding.

## JTTY

JTTY sends typed text, keyboard to keyboard, in frames of 1.888 s with no periods. A configure with `"mode":"JTTY"` decodes it; `pack` and `render` without `mode` encode it, in any mode.

### JTTY sessions

- A JTTY session begins with its first audio sample, and its [`session`](#session) line comes first. Odd-length audio frames are skipped and not counted in `origin`. Where a session begins within a transmission changes what decodes, as it does in WSJT-X.
- A session ends at a `discontinuity`, a `flush`, an odd-length audio frame, a configure that leaves JTTY or changes `live_audio` (as WSJT-X starts a new reception when you open a file), `halt` and the end of input. The next JTTY audio sample begins a new one.
- After each decoder step the codec sends one `jtty_update` line for each pending update, as WSJT-X takes them. Its `id` numbers messages, not requests, so route lines by `t`. Each message's last line is its `complete`, `expired` or `ended` one ([JTTY update states](#jtty-update-states)), except a cancelled [JTTY review](#jtty-review)'s messages, which get none.
- The decoder keeps a message's first 80 characters: its later frames move `latest` but add no text. Typed dots never make a `gaps` entry. A `freq`, `start` or `latest` that is not finite is null.
- Between a session's `session` line and its last update come only `error` lines, replies to requests, a [JTTY review](#jtty-review)'s lines, `planning` lines, `spectrum`, `ref_spectrum` and `rows_dropped` lines and, when the session began with a mode change, the previous mode's remaining decode lines. An `odd_audio_frame` error comes before the `ended` lines it causes.
- Ending a session also searches the audio window that extends past its last sample, unlike WSJT-X, so a last frame that ends shortly before the session does still decodes.
- A fatal error ends the codec at once: a message in progress gets no last line.
- The decoder searches `rxfreq` ± `ntol`, and fixed channels that tile `nfa` to `nfb`: (`nfb` − `nfa`)/300 of them, rounded, at least 2 and at most 16, each widened by 10 Hz on either side and clipped to `nfa` to `nfb`, none when `nfa` is above `nfb`. With the default `nfa` and `nfb` that is 13 channels about 292 Hz apart. WSJT-X takes `nfa` and `nfb` from the Wide Graph's start frequency and its top, so a client that wants WSJT-X's decodes sends those, and sends them again when they change. Nothing whose lowest tone is above about 2950 Hz decodes, as in WSJT-X. A configure that would leave `rxfreq`, `ntol`, `nfa` or `nfb` outside 0 to 6000 Hz is declined (`configure_range_error`, with no `mode`); that is the range the keys take, not the band searched.
- A decoder step that needs FFT plans for which `-a` holds no FFTW wisdom measures them: up to tens of seconds on ARM Linux, while the codec reads no input and a period decode that needs a new plan waits. The plans follow from `rxfreq`, `ntol`, `nfa` and `nfb`, and the first step over audio is the first to measure. With `busy_events`, `planning` lines bracket the step. The codec saves the wisdom to `-a` after each step that measured and at `halt` or the end of input, so keep `-a` across runs ([Directories and files](#directories-and-files)).

### JTTY review

A `jtty_review` request decodes kept JTTY audio again, as WSJT-X's review does when you press Decode in JTTY or double-click a waterfall row, on a receive context of its own, at the frequency and band edges it names.

- **Kept audio:** the last 180 s of the audio JTTY sessions decoded, after equalization, every session together, oldest out first, as WSJT-X keeps it. A session can be reviewed while any of its audio is kept, after it ends and after mode changes.
- **The request:** `{"t":"jtty_review","id":I}` with the keys [Request keys](#request-keys) marks `jtty_review`, each optional but `id`. Without `session` it reviews every kept session, oldest first, each over all its kept audio (WSJT-X's Decode button); with `live_audio` false, only the sessions begun since `live_audio` last went false (from the start, all), as WSJT-X's Decode with a file loaded reviews that recording. With `session` and `sample`, a picked time (WSJT-X's double-click) in any kept session: the 40 s from 5 s before `sample`, or from the session's first kept sample when that is later, clipped to the kept audio, ending early at a `complete` message within `ntol` of `rxfreq`. With nothing kept, a review of every session ends at once (`busy` true and false with `busy_events`, then `reviewed`).
- **Refusals:** `rejected` with `bad_request` (the `key` at fault: a key it does not take, a value of the wrong form or out of range, `sample` without `session`) or `unavailable`: `mode` outside JTTY, `session` with no kept audio, or a `sample` outside first kept < `sample` ≤ last kept + 1. A request without a usable `id` gets the error `invalid_request_id`.
- **Its lines:** `jtty_update` lines carrying `review` (the request's `id`), whose `session` is the reviewed session, and whose `start` and `latest` count from the stream's first sample as the session's own lines do; then `reviewed`, the request's last line. A review's messages take ids from the same sequence as the sessions', so ids stay unique; WSJT-X shows review lines apart from live ones.
- **When it runs:** on the stream's own thread, one decoder step at a time at a frame boundary when no input is waiting, so live JTTY decoding never waits for more than one of its steps, as in WSJT-X; a step at another `rxfreq`, `ntol`, `nfa` or `nfb` than the live session's may need new FFT plans and measure them, as a live step does ([JTTY sessions](#jtty-sessions)), which is possible but not measured. Its lines interleave with the live session's. Without a stdin reader thread nothing shows that input waits, so the whole review runs at once when the request is read, and input waits for it. At `halt` and the end of input the codec finishes the review before it ends (WSJT-X cancels it at close), so a `halt` during a review of every session waits for up to 180 s of audio to decode. Its FFT plans are the live session's (the plans are shared), at the codec's patience.
- **What ends it:** its last step (`reviewed` with `cancelled` false); any new `jtty_review` with a usable `id`, which cancels it even when the new one is then refused, as every Decode press in WSJT-X does; a configure that leaves JTTY; and a configure that changes `live_audio`, as WSJT-X's opening a file does (`cancelled` true). A cancelled review sends no more updates, and its open messages get no last line.
- **Busy:** a review in progress counts as the decoder being busy, as in WSJT-X. With `busy_events`, `busy` true with `"mode":"JTTY"` and `review` comes when it starts and `busy` false with `review` just before its `reviewed` line; no deadline follows them ([Telling a hang from a slow decode](#telling-a-hang-from-a-slow-decode)). While it runs the codec reads and answers everything else as usual; `nagain_flag` stays refused in JTTY (`decode_again_unavailable`).
- **Live decoding is unchanged, by construction:** the review keeps its own alias references (the decoder's memory of recent frames, which rejects shifted copies), swapped in around each of its steps, so the live session's state is not touched. In WSJT-X the review and the live decoder share them. No test audio here makes alias references matter, so this is stated from the design, not measured.

### JTTY text and templates

- A JTTY request carries exactly one of `text` and `template`.
- `text` is typed text: its placeholders are expanded, each newline ends a message, and `"final":false` leaves the last message open, its last frame not ending it.
- `template` is a function key: sent natively when it is one of the native forms below, else expanded as text that ends its message.
- Each is at most 32,767 UTF-16 units, and 1,048,576 expanded. The whole request must fit a control frame, 262,144 bytes.
- `profile` is `none` (the default), `field_day` or `rtty`. `his_call` and `exchange` take at most 64 UTF-16 units, no character below U+0020, none from U+007F to U+009F, and no `%`. `serial` and `report` are never defaulted (`missing`).
- The station's call and grid are the whole `mycall` and `mygrid` of the last accepted configure that set them, less any ASCII white space at either end. Each is usable when it is at most 64 characters of printable ASCII, not blank and without `%`. A request that uses one when no configure has set a usable one is refused `not_configured`, naming `mycall` or `mygrid`.

### Placeholders and profiles

Placeholders are upper case only, and are replaced in this order, so a value inserted earlier can complete a later one, as in WSJT-X:

| Placeholder | Becomes |
| --- | --- |
| `%M` | The station's call |
| `%H` | `his_call` |
| `%Q` | `his_call` |
| `%N` | `serial`, at least three digits: `007`, `-01` |
| `%E` | With `profile` `none`, `serial` as `%N`; with `field_day`, `exchange` in upper case, with a space between its count and class and its section (`1DEMA` is `1D EMA`); with `rtty`, `exchange` in upper case, or `serial` as `%N` when the exchange is DX or # |
| `%G` | The first four characters of the station's grid, upper case |
| `%R` | `report`, signed, at least two digits: `-07`, `+05` |

An absent `his_call` or `exchange` expands to nothing, as an empty field does in WSJT-X.

A native form sends structured atoms in place of characters. Its `%E` is an atom: 599 and the serial (0 to 131071) with `none`, and with `rtty` when the exchange is DX or #; the count, class and section with `field_day`; otherwise 599 and the `rtty` exchange, a serial or a state. A template is native when it matches one of these forms after collapsing its spaces and upper-casing it, or, for the three `599 %N` forms, exactly as written:

- `CQ %M CQ`, `%H %E`, `%H 599 %N`, `%H %G`, `%H TU CQ %M CQ`, `%M`, `%H`, `TU NOW %Q %E`, `TU NOW %Q 599 %N`, `TU NOW %Q %G`, `%H AGN?`, `%E`, `599 %N`, `%G` and `599 %G`;
- the control phrases `AGN?`, `CALL?`, `AGN CALL`, `NR?`, `AGN NR`, `EXCH?`, `STATE?`, `SECTION?`, `ZONE?`, `GRID?`, `RPRT?`, `QSL TU`, `TU`, `QRZ?`, `QSO B4`, `WAIT`, `NIL?` and `OK?`.

A native form also needs what it sends to fit an atom: each call in it a standard call of 3 to 6 characters, one or two letters or digits with at least one letter, then a digit and one to three letters, with no `/` and no leading Q; a serial from 0 to 131071; a grid that is a 4-, 6- or 8-character locator; and an exchange its profile can send, with `field_day` a count from 1 to 32, a class from A to F and a 2- or 3-character section, and with `rtty` DX, #, a serial or a 2- or 3-character state or province. Otherwise the request is refused `invalid_runtime`, and `detail` says why; send the same words as `text`, which goes as characters.

### Segments

- The codec cuts each line of the text into segments of at most 80 characters, at a space where it can, and sends one `segment` line a segment. A segment holds up to 16 frames.
- In a segment's `frames`, a five-character text frame keeps its trailing spaces unless it ends the message.
- A line's later segments continue its message, and a receiver keeps a message's first 80 characters.
- A character JTTY lacks becomes `#` for each UTF-16 unit, `##` beyond the BMP; each byte that is not UTF-8 becomes `#`; and NUL and `~` become a space. `substituted` says so, on the segment and on `packed` or `rendered`.
- A JTTY `render` takes `freq` and `rate` as a slot mode's does, and each `segment` line carries `samples` and is followed by its `pcm` lines. A render of more than 600 s of audio is refused `too_long`, with no `key`. A JTTY receiver decodes nothing whose lowest tone is above about 2950 Hz, though `freq` may be up to 5000 Hz.

### A JTTY pack

The session under [A pack session](#a-pack-session) went on with these control frames:

1. `{"t":"pack","id":4,"template":"CQ %M CQ"}`
2. `{"t":"pack","id":5,"template":"%H 599 %N","his_call":"W9XYZ","serial":7}`
3. `{"t":"pack","id":6,"text":"CQ CQ DE %M %M K"}`
4. `{"t":"pack","id":7,"template":"%H 599 %N","his_call":"W9XYZ"}`

It received:

```text
{"v":1,"t":"segment","id":4,"seg":0,"text":"CQ K1ABC CQ","canonical":"CQ K1ABC CQ","final":true,"substituted":false,"frames":[{"text":"CQ K1ABC CQ"}],"seconds":1.888}
{"v":1,"t":"packed","id":4,"segments":1,"substituted":false}
{"v":1,"t":"segment","id":5,"seg":0,"text":"W9XYZ 599 007","canonical":"W9XYZ 599 007","final":true,"substituted":false,"frames":[{"text":"W9XYZ"},{"text":"599 007"}],"seconds":3.776}
{"v":1,"t":"packed","id":5,"segments":1,"substituted":false}
{"v":1,"t":"segment","id":6,"seg":0,"text":"CQ CQ DE K1ABC K1ABC K","canonical":"CQ CQ DE K1ABC K1ABC K","final":true,"substituted":false,"frames":[{"text":"CQ CQ"},{"text":" DE K"},{"text":"1ABC "},{"text":"K1ABC"},{"text":"K"}],"seconds":9.440}
{"v":1,"t":"packed","id":6,"segments":1,"substituted":false}
{"v":1,"t":"rejected","id":7,"reason":"missing","key":"serial","detail":"the request uses serial and does not give it"}
```

The codec exited with status 0 after the session's `halt`.

- `%M` is the configure's `mycall`.
- The native `CQ %M CQ` is one frame. Typed text goes in five-character frames: `CQ CQ DE %M %M K` takes five.
- The native `%H 599 %N` sends two frames, the call and `599 007`.
- Without `serial`, `%N` cannot be filled, and the request is refused `missing`.

## FreqCal

FreqCal measures the frequency of a carrier, as WSJT-X's FreqCal mode does. A configure with `"mode":"FREQCAL"` (case aside; it has no integer code and no command-line option) starts it.

- `trperiod` must be 5, 10, 15 or 30; another, a configure that carries `mode` without `trperiod` included, is declined (`configure_range_error`, key `trperiod`).
- `rxfreq` and `ntol` are WSJT-X's Rx Freq and F Tol, the search window: from `rxfreq` − `ntol` to `rxfreq` + `ntol` Hz, or from 0 to twice `rxfreq` when `ntol` is above `rxfreq`. FreqCal takes `rxfreq` from 200 to 5000 Hz and `ntol` from 1 to 1000 Hz, WSJT-X's ranges; a configure that would leave either outside is declined (`configure_range_error`), except that an `ntol` above 1000 sent with `mode` or `trperiod` is first capped at 1000, as in every mode; sent alone it is declined. At the top of those ranges (`rxfreq` + `ntol` of 6000 Hz) a peak in the window's top bin makes the measurement read one value past its spectrum, as in WSJT-X, which takes the same values. A configure that carries `"mode":"FREQCAL"` sets the receive frequency to 1500 unless it carries `rxfreq`, as choosing FreqCal sets WSJT-X's Rx Freq; the transmit frequency keeps its value, as WSJT-X's Tx Freq does, and one that carries only `trperiod` keeps both. Every configure that carries `mode` or `trperiod` without `ntol` resets `ntol`, in FreqCal to 50, WSJT-X's JT9 F Tol.
- It measures at the end of every 8th 3,456-sample block of a period from the 16th up to WSJT-X's stop, ((int(`trperiod`/0.288))/8)×8 blocks: once at 5 s, 3 times at 10 s, 5 at 15 s and 12 at 30 s, at `k` = 55,296, 82,944, ... samples from the period's start, on the last 55,296 samples (4.608 s). Live and from a file alike; a part block is never measured, so a `flush`, `halt` or the end of input measures nothing more.
- Each measurement is a [`freqcal`](#freqcal) line, WSJT-X's line as numbers: its `time` is the period's label plus `k`/12000 s, never the clock, which WSJT-X's line reads. WSJT-X's line also shows its dial kHz, the dial plus `rxfreq`; the client adds it, as it adds the dial to every frequency. With nothing in the window (silence), `freq`, `level` and `snr` are -99.9 and `error` is -99.9 less `rxfreq`, as in WSJT-X.
- WSJT-X's band stepping through its FreqCal frequencies, its Measure log `fmt.all` and its Solve fit are the client's.
- FreqCal decodes nothing: no `decode`, `decode_finished`, `busy`, `pass_progress` or `pass_skipped` lines, and no period to decode again. With `spectrum` on it sends the `spectrum` rows every period mode sends, live every block and from a file those up to its stop. From a file each block's row comes before its measurement, and the next block's row after it; live, a measurement goes out ahead of the live rows queued before it, as every other line does, but never after a later block's row, and live rows can be discarded ([Spectrum rows and equalization](#spectrum-rows-and-equalization)). With `ref_spectrum` `apply` on live audio it measures the equalized audio, as WSJT-X does.
- It measures on the codec's own thread as each 8th block is read, as WSJT-X measures on its GUI thread, so its lines and the errors for frames sent in FreqCal come out in input order, and a previous mode's remaining decode lines can follow them. Its FFT is planned at FFTW patience 0, as in WSJT-X's process.

```
{"v":1,"t":"freqcal","time":"120004","freq":1500.370,"error":0.370,"level":6.1,"snr":55.4,"low_snr":false}
```

## Echo

Echo measures the echoes of the station's own transmissions, as WSJT-X's Echo mode does. A configure with `"mode":"ECHO"` measures it in periods of 3 s; `pack` and `render` with `"mode":"ECHO"` encode the transmission, in any mode. The transmit and receive cycle, the Doppler and the dither are the client's.

### Echo periods

- At each period's 9th 3,456-sample block, 31,104 samples in, live and from a file, the codec measures the period as WSJT-X measures it at its 9th half-symbol: `avecho` on its first 28,672 samples, with the keys in force there. A configure before that sample applies to the period; one after it, to the next. The period's `echo` line comes, then its `decode_finished`. Echo sends no `decode`, `busy` or `pass_skipped` line, and decodes no JT9, which WSJT-X's dataSink also asks its decoder for in Echo.
- Every period that reaches its 9th block is measured, whatever its samples, as WSJT-X measures each period its input fills that far. One that ends before is not: a `discontinuity` or a mode change drops it with no `decode_finished`, and a `flush`, `halt` or the end of input ends it with its `decode_finished` and no `echo`.
- The codec has no transmit periods. WSJT-X stops its receive audio while it transmits, unless Full Duplex is on, so its transmit periods never reach the 9th block. So a half-duplex client sends no audio while it transmits and sends a `discontinuity` before the first sample of the next period, on its UTC boundary, as every live period starts; a full-duplex client sends every sample, and its transmit periods are measured, as WSJT-X's are with Full Duplex on.
- The echo's `time` is the label in force at the 9th block. WSJT-X labels a live Echo line with the UTC rounded down to 6 s while Enable Tx is on, and to 3 s otherwise, and a file's with the file's time.
- A message's characters are read 4,096 samples each from the period's first sample (`lib/decode_echo.f90`).
- The measurement runs on the codec's main thread, as WSJT-X's runs on its own, and its lines and the errors for frames sent in ECHO go out from there in input order; the previous mode's remaining decode lines can follow them. It takes at least 10 ms there (`avecho` sleeps 10 ms, as in WSJT-X), while the main thread reads no input. Its FFTs are planned at patience 0, as WSJT-X's process plans them.
- The `echo` line is about 44 KB, under 64 KiB.

### Echo keys

- `doppler`, `rit`, `echo_freq`, `spread`, `kind`, `echo_spacing`, `echo_text`, `echo_average` and `echo_auto` carry WSJT-X's values ([Configure keys](#configure-keys) lists their types and starts). Each is in force in every mode until another configure sets it. A value out of range declines the frame with `configure_range_error`, a wrong type with `configure_type_error`.
- `doppler` is WSJT-X's audio Doppler shift, 0 with CFOM, and `rit` the receiver's RIT: JSON numbers, each truncated toward zero as WSJT-X truncates its own shift, and less than 2^31 Hz either way (`configure_range_error`). The codec expects the echo at `echo_freq` plus `doppler` less `rit`.
- `spread` is the spread to measure with: send the self spread while `echo_auto` is true and the DX spread otherwise, as WSJT-X's Astro window gives them. `echo_auto` changes only a file's measurement, which takes the file's spread unless it is true.
- Live, the codec packs the parameters into the first 15 samples of its copy of the period, as WSJT-X does, unless `kind` is tone: cw packs a spacing of 0, and message packs `echo_spacing` and the tones of `echo_text`, and the `echo` line's `call` is the message recovered. With tone nothing is packed and those samples stay audio, as in WSJT-X.
- From a file (`live_audio` false), the codec reads `doppler`, `rit`, `echo_freq`, `spread`, the spacing and the tones back from the period's first 15 samples, where WSJT-X's saved Echo files carry them (`echosim`'s carry all but the spacing and the tones), whatever the keys say, but for the spread: with `echo_auto` true the measurement takes the `spread` key instead. `echo_average`, `echo_auto` and `clear_average` still apply. The `echo` line reports what it read, the spacing as WSJT-X's line shows it, 0 outside 0 to 30.
- `clear_average` true, in a configure that leaves the stream in ECHO, starts the averages again at the next measurement, as WSJT-X's Clear Avg does; false cancels a clear not yet made. It reaches no other mode's decoder. WSJT-X also clears when Enable Tx is turned on in Echo, but keeps the Echo graph's last curves, where Clear Avg zeroes them: send `clear_average` true with `echo_auto` true. The two differ only on a period avecho does not measure (its nominal or expected echo frequency outside 750 to 2250 Hz), whose `blue` and `red` are then zeros after a clear, as after Clear Avg, and the last ones otherwise. A session starts with a clear pending, as WSJT-X does.
- `prepared_audio` lines carry an Echo period's samples as received (or equalized). WSJT-X's saved Echo files carry the parameters in their first 15 samples (`lib/save_echo_params.f90`), which a client that saves Echo audio packs itself.

## WSPR

wsjt-codec decodes WSPR as WSJT-X does, through wsprd's own decoder, and encodes it ([Slot modes](#slot-modes)). A configure with `"mode":"WSPR"` (case aside; it has no integer code) starts it. This is WSPR-2, WSJT-X's WSPR.

### WSPR periods

- `trperiod` must be 120, the default a configure that carries `"mode":"WSPR"` sets; another is declined (`configure_range_error`, key `trperiod`).
- Each period decodes once, on the decode thread as the other period modes decode: from live audio at WSJT-X's stop, 396 half-symbols (1,368,576 samples, 114.05 s into the period), and from a file once the period is in. The decoder reads the period's first 1,368,000 samples, the 114 s wsprd reads from WSJT-X's WAV file, so live and file decode the same audio. Its `decode` lines carry `"pass":396`.
- `depth` is WSJT-X's decode depth, with WSJT-X's wsprd options: 1 (Fast) `-qB`, 2 (Normal) `-C 500 -o 4`, 3 (Deep) `-C 500 -o 4 -d`, read from the depth's low three bits as WSJT-X reads it, so 9 is 1; low bits 0 or 4 to 7, wsprd's defaults.
- The `decode` lines come in wsprd's order, ascending frequency, then the period's `decode_finished`, its `decoded` the number of them. With `busy_events`, `busy` true comes before them and `busy` false after, as for the other period modes.
- A live period the busy decoder would not run is skipped (`pass_skipped`, reason `busy`), as is a period the decoder finds no memory for (reason `memory`), and a period cut short (a `flush`, `halt` or the end of input) decodes what it has, zeros after it, as wsprd decodes a short WAV file; its `pass` is then the half-symbols counted, at most 181, as in the other period modes but FT8.
- WSPR keeps no period to decode again: `nagain_flag` is refused (`decode_again_unavailable`), as WSJT-X's Decode button does nothing in WSPR.
- The hash table, which resolves the calls in type 3 messages (`<K1ABC> FN42AX 37`), lives for the session: the codec loads it from `hashtable.txt` in `-a` before the session's first WSPR decode and writes it back, in wsprd's format, after every WSPR period it decodes, so it carries from period to period and, through `-a`, from session to session. An edit of the file during a session is overwritten at the next WSPR period. `wspr_hash_clear` true empties the table and deletes the file, as WSJT-X's Erase WSPR hashtable, at the configure's place in the stream: after the decodes before it, so the next period's type 3 calls show as `<...>` until their stations' type 1 or 2 messages are decoded again.
- The codec writes no `ALL_WSPR.TXT` or `wspr_spots.txt` and never reads or writes `wspr_wisdom.dat`: the logs are the client's, from the `decode` lines, and wsprd's FFTs see the codec's `jt9_wisdom.dat` ([Directories and files](#directories-and-files)). Its decodes equal wsprd's in a fresh `-a`.
- With `spectrum`, `ref_spectrum` or `prepared_audio`, WSPR sends the rows and lines every period mode sends, and decodes the equalized audio, as WSJT-X equalizes before it saves the WAV file wsprd reads ([Spectrum rows and equalization](#spectrum-rows-and-equalization)).

### Rebuilding wsprd's lines

A WSPR `decode` line carries every number wsprd's lines print, so a client can write WSJT-X's logs byte for byte. With C's printf (rounding half to even on the binary value, as glibc and macOS do; see [Displaying dt and spread](#displaying-dt-and-spread)):

- the dial frequency D, in MHz, is the double wsprd reads from `-f`: WSJT-X passes the dial as `%.6f` MHz, so parse that text, or divide an integer dial in Hz by 1e6;
- F = D + `audio_freq`/1e6, in double: `audio_freq` parses to the exact value wsprd adds;
- date6 is the UTC date of the period's start, YYMMDD, which the client knows and the codec does not; time4 is the first four digits of `time`.

```
ALL_WSPR.TXT    "%6s %4s %3.0f %5.2f %11.7f  %-22s %2d %5.2f %2d %2d %4d %2d %3d %5u %5d\n"
                date6, time4, snr_real, dt, F, message, drift, wspr_sync, wspr_pass,
                wspr_blocksize, wspr_jitter, wspr_osd, wspr_hard_errors, wspr_cycles, wspr_metric
wspr_spots.txt  "%6s %4s %3d %3.0f %4.1f %10.6f  %-22s %2d %5u %4d\n"
                date6, time4, wspr_sync10, snr_real, dt, F, message, drift, wspr_cycles, wspr_jitter
```

wsprd appends to `ALL_WSPR.TXT` and rewrites `wspr_spots.txt` with each period's lines. Its standard output line is `"%4s %3.0f %4.1f %10.6f %2d  %-s \n"` of time4, `snr_real`, `dt`, F, `drift` and `message`. Read `snr_real`, `dt` and `wspr_sync` back into single precision first, as wsprd holds them; their 9 digits read back as those values.

### A WSPR session

This session sent [`samples/WSPR/150426_0918.wav`](https://wsjtx.github.io/wsjtx/samples/WSPR/150426_0918.wav), a 120 s WSPR recording, to the codec in a fresh `-a`, with `OMP_STACKSIZE=10M`: the session header, then a control frame `{"t":"configure","mode":"WSPR","depth":3,"nutc":918}`, 120 audio frames of 12,000 samples, and `{"t":"halt"}`.

It received:

```text
{"v":1,"t":"ready","modes":["FT8","FT4","JT9","JT65","JT65+JT9","JT4","FST4","FST4W","Q65","MSK144","ECHO","JTTY","FREQCAL","WSPR"],"encodes":["FT8","FT4","JT4","JT9","JT65","Q65","FST4","FST4W","WSPR","MSK144","ECHO","TUNE","FOX","SUPERFOX","JTTY"],"protocol":1}
{"v":1,"t":"decode","mode":"WSPR","time":"091800","snr":-9,"dt":1.13333321,"freq":1446,"message":"ND6P DM04 30","audio_freq":1446.283203125,"snr_real":-8.51409531,"drift":0,"wspr_sync":0.683369577,"wspr_sync10":6,"wspr_pass":1,"wspr_blocksize":1,"wspr_jitter":0,"wspr_osd":0,"wspr_hard_errors":0,"wspr_cycles":1,"wspr_metric":612,"pass":396}
[6 lines elided]
{"v":1,"t":"decode","mode":"WSPR","time":"091800","snr":-27,"dt":0.706666708,"freq":1594,"message":"W3BI FN20 30","audio_freq":1594.432373046875,"snr_real":-26.6666451,"drift":0,"wspr_sync":0.143378228,"wspr_sync10":1,"wspr_pass":1,"wspr_blocksize":1,"wspr_jitter":0,"wspr_osd":0,"wspr_hard_errors":30,"wspr_cycles":362,"wspr_metric":-101,"pass":396}
{"v":1,"t":"decode_finished","period_end":"091800","synced":0,"decoded":8,"average":0}
```

The codec exited with status 0 and left `hashtable.txt` in `-a`.

- With a dial of 14.0956 MHz (`-f 14.095600`) and the date 150426, F for the first line is 14.0956 + 1446.283203125/1e6, and the line rebuilds as wsprd's `ALL_WSPR.TXT` line for the same file:

```text
150426 0918  -9  1.13  14.0970463  ND6P DM04 30            0  0.68  1  1    0  0   0     1   612
```

- `W3BI FN20 30`'s `wspr_metric` is -101, as wsprd prints it.

## Names

Every name the codec reads or writes, from the source of this release.

### Control messages

A control frame's `t` names the message.

| Message | Meaning |
| --- | --- |
| `configure` | Sets the mode, the period and the decoder's settings; it changes only the keys it has. |
| `halt` | Decodes the period in flight if it has samples, then ends the session. |
| `flush` | Decodes what the period in flight has, once, and goes on; the next sample starts a new period. |
| `discontinuity` | The next audio sample does not follow the last one: the period starts again with it. |
| `pack` | Encodes a message: its tones, or its JTTY segments and frames. |
| `render` | Encodes a message and returns its audio. |
| `jtty_review` | Decodes kept JTTY audio again ([JTTY review](#jtty-review)). |

### Configure keys

`configure` sets only the keys it has. The keys most clients send come first, marked; the rest follow in alphabetical order. Many keys carry a WSJT-X setting to the decoder that reads it, so that the codec decodes as WSJT-X does; the keys marked "no effect" change nothing. Start is the value before any configure sets the key, and the option that sets it; send the values WSJT-X would. A first configure for live FT8:

```json
{"t":"configure","mode":"FT8","trperiod":15,"depth":3,"nfa":200,"nfb":4000,"ntol":1000,"mycall":"K1ABC","mygrid":"FN42","live_audio":true,"utc":"12:00:00"}
```

| Key | Type | Use | Start | Meaning |
| --- | --- | --- | --- | --- |
| `mode` | string or integer | most clients | FT8 | The mode to decode: one of the names `ready` lists in `modes`, case aside, or its integer code: FT8 8, FT4 5, JT9 9, JT65 65, JT65+JT9 74, JT4 4, FST4 240, FST4W 241 (242 is FST4W with the command line's `-Y`: its decodes show an unresolved hashed call as its hash22 value, `<NNNNNNN>`), Q65 66, MSK144 144. JTTY, ECHO, FREQCAL and WSPR have none. |
| `trperiod` | number | most clients | 15 (`-p`) | The period, seconds: a finite number above 0 and at most 1800. It starts at 15 s with no mode option (see Command line). |
| `submode` | string or integer | most clients | A (`-b`) | The submode, where the mode has one: an upper-case letter from A (only its first character is read), or 0 for A and so on. |
| `depth` | integer | most clients | 1 (`-d`) | Decoding depth, 1 to 3; in WSPR, WSJT-X's wsprd options for it ([WSPR](#wspr)). |
| `nfa` | integer | most clients | 200 (`-L`) | Lowest audio frequency searched, Hz. |
| `nfb` | integer | most clients | 4000 (`-H`) | Highest audio frequency searched, Hz. |
| `rxfreq` | integer | most clients | 1500 (`-f`) | Receive audio frequency, Hz; it sets the transmit frequency too, unless `tx_audio_offset_hz` does. Entering FreqCal moves only the receive frequency to 1500, and entering fast JT9 moves both to 700, unless the frame carries the key ([Configure](#configure)). |
| `ntol` | integer | most clients | 20, Q65 10 (`-F`) | Frequency tolerance around `rxfreq`, Hz. |
| `mycall` | string | most clients | K1ABC (`-c`) | The station's call. FT8, FT4, JT4 (its deep search), JT65, FST4 and Q65 decode with it, the starting value too, so send it; MSK144 and transmit take only a configure's. The decoders read its first 12 characters. |
| `mygrid` | string | most clients | empty (`-G`) | The station's grid locator. The decoders read its first 6 characters. |
| `his_call` | string | most clients | empty (`-x`) | The other station's call. FT8, FT4, JT4 (its deep search), JT65, FST4 and Q65 decode with it, and FT8's a8 pass only with `his_grid` too; send it, empty for none. The decoders read its first 12 characters. |
| `his_grid` | string | most clients | empty (`-g`) | The other station's grid locator. FT8 (its a8 pass), JT4 (its deep search), JT65 and Q65 decode with it; send it, empty for none. The decoders read its first 6 characters. |
| `utc` | string | most clients | 00:00:00 | The period's label, "HH:MM:SS"; `utc` wins when a frame has `nutc` too. |
| `nutc` | integer | most clients | 0 | The period's label as an HHMMSS integer; in periods of a minute or more, a value up to 2359 reads as HHMM. |
| `live_audio` | boolean | most clients | false | True for live audio: each period decodes at WSJT-X's stop points and a pass that falls behind is skipped. |
| `busy_events` | boolean | most clients | false | Turns the `busy`, `pass_progress` and `planning` lines on. |
| `multithreaded_ft8` | boolean | most clients | false | Selects the multithreaded FT8 decoder. |
| `ft8_threads` | integer | most clients | 0 (`-N`) | Threads of the multithreaded FT8 decoder: 0 chooses from the CPU count, at most 12; 1 to 12 runs that many, at most the CPU count. A configure takes 0 to 24, and 13 to 24 run one thread. |
| `contest_type` | string | most clients | none | The special operating activity, in lower case: none, na_vhf, eu_vhf, field_day, rtty, ww_digi, fox, hound, arrl_digi or q65_pileup; another value is ignored, with no error. A frame that has it, `single_decode`, `vhf_features`, `noise_blanker_level` or `q65_pileup` sets all five. |
| `superfox_mode` | boolean | most clients | false | WSJT-X's SuperFox option; a Hound (`contest_type` hound) with it and `even_sequence` decodes SuperFox. |
| `even_sequence` | boolean | most clients | false | True for the periods whose label ends in 0, as WSJT-X sets it; SuperFox decodes in those. |
| `aggressive` | integer |  | 0 | JT65's Aggressive setting (JT65 and JT65+JT9): 0 to 10, as WSJT-X offers it; WSJT-X's own starts at 4. |
| `ap_cq_only` | boolean |  | false | The decoders' AP CQ-only option (single-threaded FT8, FT4, FST4 and Q65). |
| `ap_my_call` | boolean |  | false | WSJT-X's AP state for the station's call, which it clears 120 s after its last FT8 transmission (multithreaded FT8). |
| `ap_width_hz` | integer |  | 75 | The AP decoding bandwidth, Hz (both FT8 decoders). WSJT-X sends 50 in FT8, and to its multithreaded FT8 decoder 5 below 30 MHz, 15 below 100 MHz and 50 above. |
| `candthin_threshold` | number |  | 1 | The multithreaded FT8 decoder's candthin threshold, applied in hundredths. |
| `clear_average` | boolean |  | false | Clears the averages for the next decode, as WSJT-X's Clear Avg (JT4, JT65, JT65+JT9 and Q65); in ECHO, Echo's at its next measurement. |
| `common_ft8b` | boolean | no effect | true | No decoder reads it. |
| `currently_txing` | boolean |  | false | Read only by the multithreaded FT8 decoder. |
| `date` | string |  | none | The date, "YYYY-MM-DD"; only the SuperFox decoder reads it. |
| `decode_after_eme_delay` | boolean |  | false | WSJT-X's Decode after EME delay: later stop points and a 2.5 s EME delay. |
| `deep_ap_search` | boolean |  | false | Deep AP search (JT4, JT65 and JT65+JT9); one of the `depth_level` group. |
| `delay_units` | integer |  | 0 | Read only by the multithreaded FT8 decoder. |
| `depth_level` | integer |  | 1 (`-d`) | The decoding depth, as `depth`; a frame that has it, `q65_maxiters_level`, `use_averaging`, `deep_ap_search` or `q65_auto_clear_average` sets all five, and takes no `depth`. |
| `doppler` | number |  | 0 | Echo: the audio Doppler shift, Hz, WSJT-X's (0 with CFOM), truncated toward zero ([Echo keys](#echo-keys)). |
| `dt_center_seconds` | number |  | 0 | The multithreaded FT8 decoder's DT center, seconds, applied in hundredths. |
| `dt_tolerance_seconds` | number | no effect | 3 | No decoder reads it. |
| `echo_auto` | boolean |  | false | Echo: WSJT-X's Enable Tx; from a file the measurement takes `spread` instead of the file's spread (live it changes nothing). |
| `echo_average` | integer |  | 10 | Echo: the periods averaged, 1, 2, 5, 10, 20, 50 or 100. |
| `echo_freq` | number |  | 1500 | Echo: f1, 1500 Hz plus the dither drawn at transmit, from 1490 to below 1510. |
| `echo_spacing` | integer |  | 10 | Echo: a message's tone spacing, 10, 15, 20, 25 or 30 Hz. |
| `echo_text` | string |  | empty | Echo: the message sent, up to six letters and digits. |
| `eme_delay_seconds` | number |  | 0 | The EME delay, seconds; the codec sets it as WSJT-X does (2.5 with `decode_after_eme_delay`, else 0) unless a frame gives it. |
| `enable_dxc_search` | boolean |  | true | The multithreaded FT8 decoder's DX call search. |
| `fast` | boolean |  | false | Fast JT9, JT9's Fast setting: JT9 only, in submodes E to H at a `trperiod` of 5, 10, 15 or 30. A configure that carries `mode` without it sets it false. Entering fast JT9 sets the receive and transmit frequencies to 700 unless the frame carries `rxfreq` or `tx_audio_offset_hz`; a `fast` true already in fast JT9 without `mode` changes neither; `ntol`'s default in fast JT9 is 50 ([Configure](#configure)). |
| `fox_callers_clear` | boolean |  | false | true deletes `houndcallers.txt` in `-t`, as WSJT-X's Fox reset does, once, at the frame's place in the stream, so the next FT8 pass in Fox mode starts a new caller list ([Side outputs and labels](#side-outputs-and-labels)); false does nothing. |
| `ft8_ap_on` | boolean |  | true | FT8 AP decoding (both FT8 decoders; in the multithreaded one, its a8 pass only). |
| `ft8_cycles` | integer |  | 3 (`-C`) | Cycles of the multithreaded FT8 decoder, 1 to 3. |
| `ft8_decoder_start` | integer |  | 3 (`-D`) | The multithreaded FT8 decoder's pass schedule, 0 to 4. |
| `ft8_low_threshold` | boolean |  | true | The multithreaded FT8 decoder's low threshold; WSJT-X sets it but at its lowest FT8 sensitivity. |
| `ft8_rxf_sensitivity` | integer |  | 3 (`-R`) | The multithreaded FT8 decoder's sensitivity at the receive frequency, 1 to 3. |
| `ft8_subpass` | boolean |  | true | The multithreaded FT8 decoder's subpass; WSJT-X sets it at its highest FT8 sensitivity. |
| `hide_ft8_duplicates` | boolean |  | false | Hides duplicate FT8 decodes (multithreaded FT8). |
| `hint_mode` | boolean | no effect | false | No decoder reads it. |
| `his_b_call` | string |  | empty | The other station's base call (multithreaded FT8). |
| `his_call_standard` | boolean |  | false; true when `-x` gives a standard call | Whether `his_call` is a standard call, as under [Configure](#configure) (multithreaded FT8). |
| `hound_mode` | boolean |  | false | WSJT-X's Hound mode (multithreaded FT8). |
| `jt65_ap_on` | boolean |  | true | JT65's AP decoding. |
| `jt65_jt9_split_hz` | integer |  | 2700 (`--split`) | JT65+JT9's split: the lowest JT9 frequency, Hz. |
| `kin_samples` | integer | no effect | set by the codec | The codec sets it before each decode. |
| `kind` | string |  | tone | Echo: tone, message or cw, case aside, WSJT-X's Fixed Tone, Message and CW. |
| `last_tx_seconds_ago` | integer |  | 0 | Read only by the multithreaded FT8 decoder. |
| `low_sidelobes` | boolean |  | true | WSJT-X's "Low sidelobes" waterfall setting: a Nuttall window on each `spectrum` row's FFT; false is its "Most sensitive". In the codec it also windows the symbol spectra the JT9 decoders read, JT65+JT9's and slow JT9's ([Spectrum rows and equalization](#spectrum-rows-and-equalization)). |
| `max_drift_hz` | integer |  | 0 | WSJT-X's Max Drift for Q65, in symbol rates per transmission (not Hz), 0 to 50. |
| `min_sync` | integer |  | 0 | The minimum sync (JT4, JT65 and JT65+JT9); in fast JT9, below 0, up to 50 lines a decode instead of 2. |
| `min_width` | integer |  | 1, JT4 submode A 0 | JT4's minimum width. |
| `mode_changed` | boolean |  | false | Read only by the multithreaded FT8 decoder. |
| `multi_instance` | boolean |  | false | WSJT-X's several-instances flag (multithreaded FT8). |
| `my_b_call` | string |  | empty | The station's base call (multithreaded FT8). |
| `my_call_standard` | boolean |  | true; false when `-c` gives a nonstandard call or `b` | Whether `mycall` is a standard call, as under [Configure](#configure) (multithreaded FT8). |
| `n_2pass` | integer |  | 2 | 2 for JT65's two-pass decoding, else 1 (JT65 and JT65+JT9). |
| `n_trials` | integer |  | 1000 | Trials of JT65's soft-decision decoder: a power of ten, or three times one. |
| `nagain_filter` | boolean |  | false | Read only by the multithreaded FT8 decoder. |
| `nagain_flag` | boolean |  | false | Asks once to decode the last period again. |
| `noise_blanker_level` | integer |  | -3 (0 when `-7`, `-W` or `-Y` starts the session) | FST4 and FST4W's noise blanker, as WSJT-X's NB box sends it: 0 to 25 percent, or -1 or -2 for its other two settings. -3, which a frame of the group sets when it leaves the key out, selects another blanker mode. Only FST4 and FST4W read it; one of the `contest_type` group. |
| `npts_c0_array` | integer |  | 74736 | JT65+JT9's JT9 sample count, 1 to 81648; only JT65+JT9 reads it. |
| `nzhsym_per_period` | integer | no effect | set by the codec | The codec sets it before each decode. |
| `phase_eq` | array of five numbers |  | all zeros | MSK144's phase-equalization coefficients, as its Equalization Tools apply them; all zeros is none. |
| `prepared_audio` | boolean |  | false | Sends back the audio after the receive chain, from live audio, as `prepared_audio` lines ([Spectrum rows and equalization](#spectrum-rows-and-equalization)). |
| `q65_auto_clear_average` | boolean |  | false | WSJT-X's automatic clearing of averages after an averaged decode (JT65, JT65+JT9 and Q65); one of the `depth_level` group. |
| `q65_callers_clear` | boolean |  | false | true deletes `tsil.3q` in `-a`, the Q65 pileup list, once, at the frame's place in the stream ([Side outputs and labels](#side-outputs-and-labels)); false does nothing. |
| `q65_callers_remove` | array of strings |  | none | Removes each call's first entry from `tsil.3q`, once, at the frame's place in the stream, after a `q65_callers_clear` in the same frame; at most 50 calls (`configure_range_error`), each read to its first 12 characters. |
| `q65_maxiters_level` | integer |  | 1 (`-d`) | Q65's iterations level; it agrees with `depth_level` when a frame has both. |
| `q65_pileup` | boolean |  | false | WSJT-X's Q65 pileup decoding; one of the `contest_type` group. |
| `qso_progress_state` | integer |  | 0 (`-Q`) | QSO progress, 0 to 5, for AP decoding (FT8, FT4, JT65, JT65+JT9, FST4 and Q65). |
| `ref_spectrum` | string |  | off | Reference-spectrum equalization of live audio, ahead of every decoder and every row: `off`, `measure` or `apply` ([Spectrum rows and equalization](#spectrum-rows-and-equalization)). Another value is `configure_range_error`. |
| `ref_spectrum_clear` | boolean |  | false | true erases the reference measurement, deletes `refspec.dat` in `-a` and sets `ref_spectrum` to `off`, before the frame's `ref_spectrum` applies; false does nothing. |
| `rit` | number |  | 0 | Echo: the receiver's RIT, Hz, truncated toward zero. |
| `robust_mode` | boolean |  | false | JT65+JT9's robust mode; only JT65+JT9 reads it. |
| `sec_band_changed` | integer |  | 0 | Read only by the multithreaded FT8 decoder. |
| `short_messages` | boolean |  | false | MSK144's Sh: short messages. |
| `single_decode` | boolean |  | false | WSJT-X's single decode; one of the `contest_type` group. |
| `skip_tx1` | boolean |  | false | WSJT-X's skip Tx1 (multithreaded FT8). |
| `spectrum` | boolean |  | false | The `spectrum` and `fast_spectrum` rows, from which a client draws its spectrum, waterfall, signal meter and Fast Graph ([Spectrum rows and equalization](#spectrum-rows-and-equalization)). |
| `spread` | number |  | 0 | Echo: the spread to measure with, Hz, the self or DX spread WSJT-X's Astro window gives; avecho takes at least 0.04. |
| `stop_hint` | boolean |  | false | Read only by the multithreaded FT8 decoder. |
| `swl` | boolean |  | false | MSK144's SWL. |
| `t` | string |  |  | `configure`. |
| `train` | boolean |  | false | MSK144's Measure phase response; false again once training completes (`msk144_trained`). |
| `tx_audio_offset_hz` | integer |  | 1500 (`-f`) | The transmit audio frequency, Hz, apart from `rxfreq`. |
| `tx_mode` | string or integer |  | JT65, JT9 with `-T` | JT9 or JT65, or 9 or 65; only JT65+JT9 reads it. |
| `use_averaging` | boolean |  | false | Averaging (JT4, JT65, JT65+JT9 and Q65); one of the `depth_level` group. |
| `version` | integer |  | 1 | The frame's schema version; without it, 1. A frame of another is declined (`unknown_schema_version`). |
| `vhf_features` | boolean |  | false | WSJT-X's VHF features; one of the `contest_type` group. |
| `wide_dxc_search` | boolean |  | true, false with `-Z` | The multithreaded FT8 decoder's wideband DX call search. |
| `wspr_hash_clear` | boolean |  | false | true empties WSPR's hash table and deletes `hashtable.txt` in `-a`, as WSJT-X's Erase WSPR hashtable does, once, at the frame's place in the stream ([WSPR](#wspr)); false does nothing. |

### Request keys

The keys of `pack` and `render`, and of `jtty_review` ([JTTY review](#jtty-review)), which takes `t`, `id` and the six marked `jtty_review`. A request without `mode`, or with `"mode":"JTTY"`, is JTTY's; any other mode's is a slot mode's. A key the request's kind does not take is refused (`rejected`, `bad_request`).

| Key | Type | Meaning |
| --- | --- | --- |
| `t` | string | `pack` or `render`. |
| `id` | integer | The request's id, -2147483648 to 2147483647; each line of its reply carries it. |
| `mode` | string | The mode to encode, case aside: one of the names `ready` lists in `encodes`. Without it, JTTY. |
| `text` | string | The message to send; in JTTY, typed text with its placeholders, and in SUPERFOX, the free text. |
| `template` | string | JTTY: a function-key template, sent instead of `text`. |
| `final` | boolean | JTTY: false leaves the last message open. |
| `profile` | string | JTTY: none, field_day or rtty. |
| `his_call` | string | JTTY: the other station's call, for %H and %Q. |
| `exchange` | string | JTTY: the exchange, for %E. |
| `serial` | integer | JTTY: the serial number, for %N and %E. |
| `report` | integer | JTTY: the report, for %R. |
| `freq` | number | `render`: the lowest tone, Hz. |
| `rate` | integer | `render`: the sample rate, 12000 or 48000. |
| `submode` | string or integer | JT4, JT9, JT65 and Q65: the submode, a letter, or 0 for A and so on. |
| `trperiod` | integer | FST4, FST4W, Q65, MSK144 and fast JT9: the period, seconds. |
| `spacing` | integer | The tone spacing, 1, 2 or 4 (FT8, slow JT9, FST4, FST4W and WSPR); an Echo message's spacing, Hz. |
| `copied` | boolean | Q65: sets the message's spare bit, WSJT-X's Q65 pileup flag. |
| `fast` | boolean | JT9: fast JT9, submodes E to H. |
| `kind` | string | ECHO: tone, message or cw. |
| `slots` | array of strings | FOX and SUPERFOX: one to five messages, one a slot. |
| `more_cqs` | boolean | SUPERFOX: WSJT-X's More CQs. |
| `otp` | integer | SUPERFOX: the six-digit verification code, 0 to 999999. |
| `seconds` | integer | TUNE's `render`: the length, 1 to 600 s. |
| `cwid` | boolean | `render`: true sends WSJT-X's CW ID of the configured `mycall` after the message. |
| `session` | integer | `jtty_review`: the JTTY session to review, from 1; without it, every kept session. |
| `sample` | integer | `jtty_review`, with `session`: the picked time, a stream sample (WSJT-X sends the double-clicked row's last sample); the review covers the 40 s from 5 s before it, or from the session's first kept sample when that is later, clipped to the kept audio. |
| `rxfreq` | integer | `jtty_review`: the Rx frequency to search, 0 to 6000 Hz; without it, the configured `rxfreq`. |
| `ntol` | integer | `jtty_review`: the tolerance around `rxfreq`, 0 to 6000 Hz; without it, the configured `ntol`. |
| `nfa` | integer | `jtty_review`: the lower band edge, 0 to 6000 Hz; without it, the configured `nfa`. |
| `nfb` | integer | `jtty_review`: the upper band edge, 0 to 6000 Hz; without it, the configured `nfb`. |

### Output lines

| Line | Meaning |
| --- | --- |
| `ready` | Sent once the session header is read: the modes `configure` decodes, the modes `pack` and `render` encode, and `protocol`. |
| `decode` | One decoded message: mode, period label, SNR, DT, frequency, message and the mode's own fields. |
| `decode_finished` | A period's decoding is done, with the decoder's counts for its last pass. |
| `pass_skipped` | A decode pass that WSJT-X would not run, and why. |
| `msk144_trained` | MSK144 training completed: the call, the phase-response file and its coefficients. |
| `echo` | An Echo period's measurement: level, SNR, Doppler error, width, the averages and the call recovered, with the blue and red spectra. |
| `busy` | With `busy_events`: a decode pass starts (true), or no decode runs or waits (false); with `review`, a JTTY review starts or ends. |
| `pass_progress` | With `busy_events`: a decode pass is still running. |
| `planning` | With `busy_events`: JTTY is measuring an FFT plan (true) or has done so (false); the codec reads no input meanwhile. |
| `spectrum` | With `spectrum`, in every mode but MSK144 and fast JT9: one row for a waterfall, a block's power and peak and the power spectrum of the last 16,384 samples. |
| `fast_spectrum` | With `spectrum`, in MSK144 and fast JT9: Fast Graph columns. |
| `ref_spectrum` | With `ref_spectrum` measuring or applying: the reference curve in dB. |
| `rows_dropped` | Live audio, with `spectrum`: the `spectrum` and `fast_spectrum` lines discarded since the last. |
| `prepared_audio` | Live audio, with `prepared_audio`: the audio after the receive chain, equalized or as received. |
| `freqcal` | In FreqCal: one frequency measurement, its time, frequency, error, level and S/N. |
| `fox_callers` | After each FT8 pass in Fox mode: the Fox caller list, `houndcallers.txt`. |
| `averages` | After each JT4 and JT65 pass: the periods held for averaging, `avemsg.txt`. |
| `q65_sync` | After each Q65 pass with sync curves: the curves, `red.dat`. |
| `q65_callers` | After each Q65 contest pass: the pileup list, `tsil.3q`. |
| `error` | An error, named by its `code`; three carry `msg` instead ([the `error` fields](#error)). |
| `session` | A JTTY session began: its number and the stream index of its first sample. |
| `jtty_update` | A JTTY message, growing, complete, expired or ended, with its text; with `review`, from a JTTY review. |
| `encoded` | A slot mode's `pack` or `render`: the message as sent and its tones. |
| `segment` | A JTTY `pack` or `render`: one segment of the message and its frames. |
| `pcm` | A `render`'s audio: int16 little-endian mono samples in base64, 16384 a line but the last. |
| `packed` | The last line of a `pack` reply. |
| `rendered` | The last line of a `render` reply. |
| `reviewed` | The last line of a `jtty_review`. |
| `rejected` | The request was refused: its `reason`, the `key` at fault and a `detail`; the last line of its reply. |

### Output fields

Every line begins with `"v":1` and its `t`. The fields each line can carry follow, by line; a field given for some modes or requests appears only with them. A number field whose value is not finite, such as a `dt` or `spread`, is `null`.

#### `ready`

| Field | Type | Meaning |
| --- | --- | --- |
| `modes` | array of strings | The modes `configure` decodes. |
| `encodes` | array of strings | The modes `pack` and `render` encode. |
| `protocol` | integer | 1. |

#### `decode`

| Field | Modes | Type | Meaning |
| --- | --- | --- | --- |
| `mode` | all | string | The mode; JT65+JT9's decodes say JT9 or JT65. |
| `time` | all | string | The period's label, always six digits. |
| `snr` | all | integer | SNR, dB; in WSPR what wsprd's `%3.0f` prints of `snr_real`, rounded half to even (wsprd prints `-0` where `snr` is 0 and `snr_real` is negative). |
| `dt` | all | number | DT, seconds: the decoder's single-precision value to 9 significant digits. In fast JT9 it is the message's start in the period, to 0.1 s, as `fast9` prints it, not a time offset. |
| `freq` | all | integer | Audio frequency, Hz; in WSPR `audio_freq` rounded half to even. |
| `message` | all | string | The message as jt9 prints it, `?` and `OOO` included. |
| `ap` | FT8, FT4, FST4, FST4W | integer | The N of jt9's aN; 0 for none. |
| `sync` | JT65, JT4 | string | JT65: `#`, `#*` or `##`; JT4: `*` or `#`. Empty for a candidate jt9 prints without it, whose message is empty too. |
| `flags` | JT65, JT4 | string | jt9's flags, blanks trimmed: empty, `f`, `d5`, `a13` and so on. |
| `q` | Q65 | integer | The qN code. |
| `avg` | Q65 | integer | The periods averaged. |
| `copied` | Q65 | boolean | jt9's #: the sender copied our last message. |
| `submode` | JT4, JT9, JT65, Q65; JT65+JT9's JT65 lines | string | The submode the pass decoded with, a letter from A. |
| `period` | FST4, FST4W, Q65, fast JT9, MSK144 | integer | The TR period, seconds; MSK144's, `trperiod`'s whole seconds. |
| `spread` | FST4, FST4W | number | The spread, Hz, when the decoder measured it. |
| `superfox` | SuperFox | boolean | true; a SuperFox decode has none of the fields from `ap` to `spread`. |
| `text_width` | SuperFox | integer | The columns jt9's line gives the message; SuperFox pads some texts with blanks. |
| `training` | MSK144 | boolean | true while configure's `train` is still true after its window, where WSJT-X marks the decode ^. |
| `audio_freq` | WSPR | number | wsprd's audio frequency, Hz, its single-precision 1500 + f1, written in full, so that a double parse is exact ([Rebuilding wsprd's lines](#rebuilding-wsprds-lines)). |
| `snr_real` | WSPR | number | wsprd's SNR, dB, to 9 significant digits. |
| `drift` | WSPR | integer | wsprd's drift, Hz over the transmission, truncated toward zero as wsprd prints it. |
| `wspr_sync` | WSPR | number | wsprd's sync, to 9 significant digits. |
| `wspr_sync10` | WSPR | integer | `wspr_spots.txt`'s sync column, wsprd's `(int)(10*sync)` in single precision. |
| `wspr_pass` | WSPR | integer | The wsprd pass that decoded it, 1 to 3. |
| `wspr_blocksize` | WSPR | integer | `ALL_WSPR.TXT`'s block size column. |
| `wspr_jitter` | WSPR | integer | The time jitter, in 375 Hz samples, that decoded it. |
| `wspr_osd` | WSPR | integer | 1 when the ordered-statistics decoder decoded it, else 0. |
| `wspr_hard_errors` | WSPR | integer | `ALL_WSPR.TXT`'s hard-error count. |
| `wspr_cycles` | WSPR | integer | The Fano decoder's cycles over 81, as `ALL_WSPR.TXT` and `wspr_spots.txt` print them. |
| `wspr_metric` | WSPR | integer | The Fano metric as `ALL_WSPR.TXT` prints it (`%5d` of an unsigned value, so it can be negative). |
| `pass` | all but MSK144 | integer | The half-symbol stop of the pass that decoded it: 41, 46 or 47 for an early FT8 pass; 396 in WSPR; in fast JT9 the block it decoded at, its end in samples over 3,456, and none when a period cut short below one block decodes (its `busy` line's `nzhsym` is then 0). |

#### `decode_finished`

| Field | Type | Meaning |
| --- | --- | --- |
| `period_end` | string | The period's label. |
| `synced` | integer | jt9's synced count for the pass that ended the period. |
| `decoded` | integer | jt9's decoded count for that pass, not a count of the `decode` lines sent; in WSPR, whose wsprd prints no counts, the number of its `decode` lines. |
| `average` | integer | jt9's average count; Q65's is 1000 times the even periods averaged plus the odd. All three are 0 for a final not run and for MSK144, fast JT9 and Echo, and `synced` and `average` in WSPR. |

#### `pass_skipped`

| Field | Type | Meaning |
| --- | --- | --- |
| `mode` | string | The pass's mode. |
| `period_end` | string | The label of the period's `decode_finished`. |
| `nzhsym` | integer | The pass's half-symbol: 41, 46 or 47 for an early FT8 pass, the mode's stop point for a final; in fast JT9 the block of the period's last try, as `pass`. |
| `stage` | string | early or final ([Pass skipped stages](#pass-skipped-stages)). |
| `reason` | string | Why it was skipped ([Pass skipped reasons](#pass-skipped-reasons)). |

#### `msk144_trained`

| Field | Type | Meaning |
| --- | --- | --- |
| `call` | string | The DX call it trained on, configure's `his_call`. |
| `file` | string or null | The phase-response file it wrote, in `-a`; null when the file could not be created. |
| `phase_eq` | array of five numbers | The coefficients, which a configure's `phase_eq` applies; null for a value JSON cannot hold. |

#### `echo`

| Field | Type | Meaning |
| --- | --- | --- |
| `time` | string | The period's label, six digits. |
| `level` | number | The received level, dB (`avecho`'s xlevel; −99 for silence). |
| `snr` | number | The echo's SNR in 2,500 Hz, dB; −99 when it has no baseline, 0 when avecho measured nothing: the parameters put the nominal or the expected echo frequency outside 750 to 2250 Hz. |
| `db_err` | number | The SNR's uncertainty, dB, divided by the square root of `nsum`. |
| `dfreq` | number | The echo's offset from its expected frequency, Hz (WSJT-X's DF); 0 at 1000 Hz or more. |
| `quality` | integer | WSJT-X's Q, 0 to 10. |
| `width` | number | The spread measured with, Hz, at least 0.04 and at most 700 (WSJT-X's Width). |
| `nsum` | integer | The periods summed since the averages started (WSJT-X's N); the average holds the last `echo_average` of them. |
| `call` | string | Six characters: a message's characters recovered from the echoes summed since the averages started, blanks where none; six blanks when no message is packed (cw, and tone unless its unpacked first samples read as a message's parameters). |
| `doppler_total` | integer | From a file only: the total Doppler the file carries, Hz (WSJT-X's Doppler column). |
| `doppler` | integer | From a file only: the audio Doppler the file carries, Hz. |
| `rit` | integer | From a file only: the RIT the file carries, Hz. |
| `echo_freq` | number | From a file only: the f1 the file carries, Hz. |
| `spread` | number | From a file only: the spread the file carries, Hz. |
| `echo_spacing` | integer | From a file only: the spacing the file carries, Hz, as WSJT-X's line shows it: 0 outside 0 to 30; one other than 10 to 30 is not a message's. |
| `df` | number | The spectra's bin width, 12000/32768 Hz. |
| `bins` | integer | 4096. |
| `blue` | string | WSJT-X's Echo graph's blue curve: `bins` float32 values, little-endian, in base64, centered on 1500 Hz plus the first averaged period's Doppler less the RIT; value m, from 0, is drawn (m − 2048)·`df` Hz from the center (widgets/echoplot.cpp). |
| `red` | string | Its red curve, as `blue`, centered on the expected echo frequency, f1 plus the Doppler less the RIT: the spectrum with each message tone moved there. |

#### `busy`

| Field | Type | Meaning |
| --- | --- | --- |
| `busy` | boolean | true at the start of a decode pass or when a final is deferred; false once no pass runs or waits. With `review`, true when a JTTY review starts and false when it ends. |
| `mode` | string | With true: the pass's mode. |
| `period_end` | string | With true: the pass's period, as its `pass_skipped` would give it. |
| `nzhsym` | integer | With true: the pass's half-symbol; in fast JT9 its block, as `decode`'s `pass`. |
| `again` | boolean | true on a decode again. |
| `deferred` | boolean | true when a final is deferred. |
| `review` | integer | A JTTY review's line: the `jtty_review` request's id. Its true line has `mode` JTTY and no other field. |

#### `pass_progress`

| Field | Type | Meaning |
| --- | --- | --- |
| `mode` | string | The running pass's mode, as its `busy` line gives it. |
| `period_end` | string | The running pass's period. |
| `nzhsym` | integer | The running pass's half-symbol. Fast JT9 sends no `pass_progress`. |

#### `planning`

| Field | Type | Meaning |
| --- | --- | --- |
| `planning` | boolean | true before a JTTY step measures an FFT plan; false when it is done. |

#### `spectrum`

| Field | Type | Meaning |
| --- | --- | --- |
| `k` | integer | Not in JTTY: the block's end in samples from the period's start. |
| `hsym` | integer | Not in JTTY: the block's index in the period, from 1. |
| `reception` | integer | JTTY: the row's session, the `session` line's `session`. |
| `begin` | integer | JTTY: the stream index of the row's first sample. |
| `end` | integer | JTTY: the stream index after its last sample. |
| `power` | number | The power in dB of the block's samples, less the first 10 of a period or of a JTTY session's rows: WSJT-X's signal meter. |
| `peak` | number | The peak in dB of the same samples. |
| `df` | number | The bins' spacing in Hz, 12000/16384. |
| `bins` | integer | The number of bins, 6,827 (0 to 5000 Hz). |
| `data` | string | The bins: the linear power spectrum of the last 16,384 samples, as `symspec`'s `s` at gain 0; value m, from 0, is at m·`df` Hz. IEEE float32 little-endian values, base64. |

#### `fast_spectrum`

| Field | Type | Meaning |
| --- | --- | --- |
| `k` | integer | The block's end in samples from the period's start. |
| `first` | integer | The 0-based index of the first column the row writes: 0 at a period's first block, above 0 in the first row after `spectrum` comes on after a period's first block. |
| `columns` | integer | The number of columns the row writes. |
| `power` | number | The power in dB of the 512 samples of the row's last column with signal, WSJT-X's signal meter. |
| `peak` | number | The peak in dB of the same samples. |
| `green` | string | Each column's power in dB: float32 values, base64. |
| `data` | string | Each column's 64 bins, column by column: linear power as `hspec`'s `s` at gain 0, bin b summing the 512-point FFT's bins 2b+1 and 2b+2: float32 values, base64. |

#### `ref_spectrum`

| Field | Type | Meaning |
| --- | --- | --- |
| `df` | number | The curve's bin spacing in Hz, 12000/6912. |
| `bins` | integer | 3,456. |
| `data` | string | The reference curve in dB, float32 values, base64: value m, from 0, is at (m+1)·`df` Hz, the third column of the (m+1)th line after the header line of `refspec.dat`, which rounds it. |

#### `rows_dropped`

| Field | Type | Meaning |
| --- | --- | --- |
| `lines` | integer | The `spectrum` and `fast_spectrum` lines discarded since the last `rows_dropped`. |

#### `prepared_audio`

| Field | Type | Meaning |
| --- | --- | --- |
| `k` | integer | Not in JTTY: the end of the line's samples in samples from the period's start. |
| `samples` | integer | Not in JTTY: the number of samples the line carries. |
| `reception` | integer | JTTY: the line's session, the `session` line's `session`. |
| `begin` | integer | JTTY: the stream index of the line's first sample. |
| `end` | integer | JTTY: the stream index after its last sample. |
| `data` | string | The samples after the receive chain: int16 little-endian mono, base64. |

#### `fox_callers`

| Field | Type | Meaning |
| --- | --- | --- |
| `time` | string | The line's: the pass's period label, as its `decode` lines' `time`. |
| `callers` | array of objects | The line's: the callers `houndcallers.txt` gets, in its order, each an object of `call`, `grid`, `snr`, `freq`, `distance` and `age`. |
| `pass` | integer | The line's, after `callers`: the pass's half-symbol stop, as its `decode` lines' `pass`. |
| `call` | string | A caller's: its call, as decoded. |
| `grid` | string | A caller's: its grid, four characters, or empty. |
| `snr` | integer | A caller's: the decode's SNR, dB. |
| `freq` | integer | A caller's: the decode's audio frequency, Hz. |
| `distance` | integer | A caller's: from `mygrid` to `grid`, km; 9999 without a grid. |
| `age` | integer | A caller's: units of 30 s since the decode, by the period labels, 0 to 4. |

#### `averages`

| Field | Type | Meaning |
| --- | --- | --- |
| `time` | string | The line's: the pass's period label. An entry's: the label of its period, six digits (`avemsg.txt` shows HHMM). |
| `mode` | string | The line's: JT4 or JT65. |
| `entries` | array of objects | The line's: `avemsg.txt`'s entries, in its order, each an object of `used`, `time`, `sync`, `dt`, `freq` and `polarity`. |
| `pass` | integer | The line's, after `entries`: the pass's half-symbol stop. |
| `used` | boolean | An entry's: the last average used it (`$` in the file). |
| `sync` | number | An entry's: its sync, as the decoder's single-precision value to 9 significant digits. |
| `dt` | number | An entry's: its DT, seconds, likewise. |
| `freq` | integer | An entry's: its audio frequency, Hz. |
| `polarity` | integer | An entry's: its sync's polarity, JT4 1 or -1, JT65 1, -1 or 0. |

#### `q65_sync`

| Field | Type | Meaning |
| --- | --- | --- |
| `time` | string | The pass's period label. |
| `dt` | number | The DT WSJT-X shows as "DT =", seconds. |
| `averaged_min` | number | `red.dat`'s second value: g times the least value of the averaged curve over all its bins, before its baseline is subtracted, where g is the factor `averaged` is scaled by (0.4 once two periods average, else 0). |
| `averaged_max` | number | `red.dat`'s third value: g times the greatest value, likewise. |
| `df` | number | The bins' spacing, Hz. |
| `first_bin` | integer | The first bin's index: `averaged` and `current` start at `first_bin`·`df` Hz. |
| `averaged` | string | The red curve, one value a bin: float32 values, base64. |
| `current` | string | The orange curve, likewise. |
| `pass` | integer | The pass's half-symbol stop. |

#### `q65_callers`

| Field | Type | Meaning |
| --- | --- | --- |
| `time` | string | The line's: the pass's period label. |
| `callers` | array of objects | The line's: the pileup list the pass leaves and saves to `tsil.3q`, in its order, each an object of `call`, `grid`, `last_seen` and `freq`. |
| `pass` | integer | The line's, after `callers`: the pass's half-symbol stop. |
| `call` | string | A caller's: its call, its first 6 characters. |
| `grid` | string | A caller's: its grid, four characters. |
| `last_seen` | integer | A caller's: the Unix time of its last decode, from the codec's clock. |
| `freq` | integer | A caller's: its last decode's audio frequency, Hz. |

#### `error`

| Field | Type | Meaning |
| --- | --- | --- |
| `code` | string | The error ([Error codes](#error-codes)); three errors have `msg` instead. |
| `msg` | string | In place of `code`, free text, for three errors that end the session: a session header the codec cannot take, and no memory for the decoder's buffer, each before `ready` with exit status 1; and a JTTY fatal error, mid-session, whose exit status is not guaranteed to be 1. |
| `detail` | string | Free text. |
| `key` | string | `configure_type_error` and `configure_range_error`: the key. |
| `expected` | string | `configure_type_error`: the JSON type the key takes (`int`, `real`, `string`, `bool`, `string\|int`, `array of numbers`, `array of strings`), or the rule its value broke (`HH:MM:SS`, `year<2100`, `int 10^N\|3*10^N`, `depth alone`, `same depth_level`). |
| `got` | string or integer | `configure_type_error`: the JSON type sent (`string`, `number`, `bool`, `null`; an array or object reads as `number`, and as `array` or `object` only in `phase_eq` and `q65_callers_remove`), or `conflict` for the depth rules; `unknown_schema_version`: the version sent. |
| `mode` | string | `configure_range_error`: the mode the frame would leave (none in JTTY); `decode_rejected`: the mode. |
| `operation` | string | `decode_rejected`: the decoder's operation. |
| `status` | string | `decode_rejected`: the decoder's status. |
| `field` | string | `decode_rejected`: the decoder option at fault, in the decoder's terms, when it has one. |

#### `freqcal`

| Field | Type | Meaning |
| --- | --- | --- |
| `time` | string | The period's label plus the measurement's `k`/12000 s, whole seconds, HHMMSS, past midnight wrapped. |
| `freq` | number or null | The measured frequency, Hz, to 3 decimals (WSJT-X's fMeas). |
| `error` | number or null | `freq` less `rxfreq`, Hz, to 3 decimals (WSJT-X's DF). |
| `level` | number or null | The noise level in the window plus 8 dB, as WSJT-X shows it, to 1 decimal. |
| `snr` | number or null | The peak over the noise (not over `level`), dB, to 1 decimal. |
| `low_snr` | boolean | `snr` below 20, where WSJT-X marks the line with `*`. |

#### `session`

| Field | Type | Meaning |
| --- | --- | --- |
| `session` | integer | The session's number, from 1 in each process. |
| `origin` | integer | The stream index of its first sample, counted from 0 at the first audio sample in any mode. |

#### `jtty_update`

| Field | Type | Meaning |
| --- | --- | --- |
| `session` | integer | The session. |
| `id` | integer | The message's id: unique in the process, the same on each of its updates. |
| `state` | string | growing, complete, expired or ended ([JTTY update states](#jtty-update-states)). |
| `freq` | number or null | The latest frame's lowest tone, Hz, to 3 decimals. |
| `snr` | integer | The first frame's SNR, whole dB; it does not change. |
| `start` | number or null | The sync time of the message's first decoded frame, stream samples / 12000. |
| `latest` | number or null | The sync time of its latest decoded frame. |
| `text` | string | What WSJT-X shows: trimmed, filler removed, at most 80 characters, possibly empty. |
| `gaps` | array of integers | The 0-based offset in `text` of each ... that stands for missed frames. |
| `review` | integer | Only on a JTTY review's updates: the `jtty_review` request's id. `session` is then the reviewed session. |

#### `reviewed`

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | integer | The `jtty_review` request's id. |
| `cancelled` | boolean | true when a later `jtty_review`, or a configure that left JTTY or changed `live_audio`, ended it. |

#### `encoded`

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | integer | The request's id. |
| `mode` | string | The mode, upper case. |
| `msgsent` | string | The message as WSJT-X shows and logs it, which a receiver decodes; it can differ from the text sent. FOX has it in each slot, SUPERFOX none. |
| `tones` | array of integers | The channel symbols WSJT-X keys. |
| `type` | integer | JT4, JT9, JT65 and MSK144: WSJT-X's message type. |
| `fallback` | boolean | FT8, FT4, FST4, FST4W, Q65 and MSK144: the message did not encode as written and its first 13 characters went as free text. |
| `submode` | string | The submode applied, its letter. |
| `trperiod` | integer | The period applied, seconds. |
| `spacing` | integer | The spacing applied. |
| `copied` | boolean | Q65: the copied bit applied. |
| `fast` | boolean | Fast JT9. |
| `kind` | string | ECHO: tone, message or cw. |
| `freq` | number | `render`: the lowest tone, Hz; a single tone's frequency on `pack` and `render`. |
| `rate` | integer | `render`: the sample rate. |
| `seconds` | integer | TUNE's `render`: the length, seconds. |
| `cwid` | string | A `render` with `cwid`: the call its CW ID sends, upper case. |
| `samples` | integer | `render`: the number of samples its `pcm` lines carry. |
| `slots` | array of objects | FOX: each slot's `msgsent`, `tones` and `fallback`. |
| `more_cqs` | boolean | SUPERFOX: the request's `more_cqs`. |
| `otp` | integer | SUPERFOX: the request's `otp`. |

#### `segment`

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | integer | The request's id. |
| `seg` | integer | The segment's index, from 0. |
| `text` | string | The characters it carries: trimmed, case kept, replacements shown. |
| `canonical` | string | What its frames carry. |
| `final` | boolean | Whether its last frame ends the message. |
| `substituted` | boolean | Whether a character in it was replaced. |
| `frames` | array of objects | Each frame as the receiver renders it, as `{"text": ...}`. |
| `seconds` | number | Its seconds on air. |
| `samples` | integer | `render`: the number of samples its `pcm` lines carry. |

#### `pcm`

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | integer | The request's id. |
| `seg` | integer | The segment; 0 for a slot mode. |
| `seq` | integer | The line's index in its segment, from 0. |
| `data` | string | int16 little-endian mono samples, base64, 16384 a line but the last. |

#### `packed`

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | integer | The request's id. |
| `segments` | integer | JTTY: the number of segments. |
| `substituted` | boolean | JTTY: whether a character in the text was replaced. |

#### `rendered`

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | integer | The request's id. |
| `segments` | integer | JTTY: the number of segments. |
| `substituted` | boolean | JTTY: whether a character in the text was replaced. |

#### `rejected`

| Field | Type | Meaning |
| --- | --- | --- |
| `id` | integer | The request's id. |
| `reason` | string | Why ([Rejected reasons](#rejected-reasons)). |
| `key` | string | The key at fault; absent when there is none. |
| `detail` | string | Free text. |

### Pass skipped reasons

| Reason | Meaning |
| --- | --- |
| `busy` | The decoder was busy when the pass's trigger fired, with a decode running or a final waiting. |
| `backoff` | Multithreaded FT8 skips early passes until it catches up. |
| `replaced` | A waiting final gave way to a later one. |
| `cancelled` | A waiting final was dropped as WSJT-X drops it: by a configure that changes the mode, the period, `live_audio` or a setting WSJT-X cancels on, or because its decode range or submode changed while it waited. |
| `memory` | There was no memory to copy the pass's audio for the decoder. |

### Pass skipped stages

| Stage | Meaning |
| --- | --- |
| `early` | An early FT8 pass, at half-symbol 41, 46 or 47. |
| `final` | The pass at the mode's stop point. |

### JTTY update states

| State | Meaning |
| --- | --- |
| `growing` | The message is still arriving. |
| `complete` | Its final frame arrived; the message's last line. |
| `expired` | Its final frame has not arrived and about 8 s of audio followed its latest frame; the message's last line. |
| `ended` | The session ended with the message in progress; the message's last line. |

### Error codes

The `code` of an `error` line.

| Code | Meaning |
| --- | --- |
| `configure_parse_error` | A control frame without a readable `t`, or a `phase_eq` that is not JSON; the frame is declined. |
| `configure_type_error` | A configure key whose value has the wrong JSON type, or a value the parser refuses: an `n_trials` not a power of ten or three times one, a `date` year from 2100, a `utc` not `HH:MM:SS`, a non-integer `nutc`, and `depth` with a key of its group or a `q65_maxiters_level` that disagrees with `depth_level`. It carries `key`, `expected` and `got`; the frame is declined. An array or object given to an integer or number key other than `nutc` is ignored, with no error. |
| `configure_range_error` | A configure the decoder would refuse, naming the first refused `key`; the frame is declined. Also sent once after `ready` for a command-line value. |
| `unknown_schema_version` | A configure whose `version` the codec does not know; the frame is declined. |
| `unknown_mode` | A configure naming a mode the codec does not decode; the frame is declined. |
| `invalid_trperiod` | A configure whose `trperiod` is not a finite number above 0 and at most 1800; the frame is declined. |
| `decode_rejected` | A decode attempt the decoder refused, with the option at fault in `field` and `detail`. |
| `decode_again_unavailable` | A `nagain_flag` decode again that cannot run: there is no decoded period, the decoder is busy on live audio, or the mode or period changed first. |
| `ref_spectrum_file` | `refspec.dat` could not be written, read or deleted; `detail` says which, with the path. Sent once until `ref_spectrum` changes or a clear; the session goes on. |
| `odd_audio_frame` | An audio frame with an odd byte count; it is skipped. |
| `control_frame_too_large` | A control frame longer than 262,144 bytes; it is skipped. |
| `frame_too_large` | A frame longer than 43,200,000 bytes; the session ends, exit status 1. |
| `invalid_request_id` | A `pack`, `render` or `jtty_review` without a valid `id`. |
| `out_of_memory` | The codec could not buffer or queue its input; the session ends. |

### Rejected reasons

The `reason` of a `rejected` line.

| Reason | Meaning |
| --- | --- |
| `bad_request` | A key is wrong or unknown (`key` names it; none when the request is not a flat object), or the message does not encode. |
| `empty` | The text, or a slot, is blank. |
| `too_long` | The text is longer than the mode takes, or the audio would be longer than 600 s. |
| `not_configured` | The request uses the station's call or grid and no configure has set a usable one. |
| `missing` | JTTY: the request uses `serial` or `report` and does not give it. |
| `invalid_runtime` | JTTY: a native form its context cannot fill. |
| `encoding_failed` | The encoder produced no frames or audio. |
| `unavailable` | `jtty_review`: the stream is not in JTTY (`key` `mode`), no audio of the session is kept (`session`), or the picked sample is outside the kept audio (`sample`). |
