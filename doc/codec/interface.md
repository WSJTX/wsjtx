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

## Environment

Set `OMP_STACKSIZE=10M` in the codec's environment, as WSJT-X sets it for its decoder. Without it the decoders' OpenMP threads get the default stack, and on Linux each thread's copy of the decoders' thread-local arrays, about 2.7 MB, comes out of that stack, leaving about 5.5 MiB of the usual 8 MiB.

## Directories and files

- `-a`: the codec reads its FFTW wisdom, `jt9_wisdom.dat`, from here at the start and saves it here; it writes `timer.out` here in every session; MSK144 training writes its `.pcoeff` files here; JT4 and JT65 read `CALL3.TXT` from here; Q65 keeps its caller history, `tsil.3q`, here in the `na_vhf`, `ww_digi`, `arrl_digi` and `q65_pileup` activities and can append to `q65_decodes.txt`; FST4W reads and rewrites `fst4w_calls.txt` here. Keep it across runs.
- `-t`: the decoders' temporary files, such as `decoded.txt` and `avemsg.txt`.
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
- `trperiod` must be a finite number above 0 and at most 1800 (`invalid_trperiod`). JT9 decodes only at a `trperiod` of 60 (`configure_range_error`, key `trperiod`).
- An accepted configure has no reply, and neither do `flush`, `discontinuity` and `halt`. The lines that follow them are those of the decoding they cause.
- Keys the codec does not know are ignored, so a misspelled key is not reported. `pack` and `render` refuse keys they do not know.
- Write integer keys as plain JSON integers: `1500`, not `1500.0` or `1.5e3`. A number in another form is not reported, and the key is ignored or misread; `nutc` is the exception, and a number in another form there declines the frame with `configure_type_error`. Other numbers are plain decimals too, except the elements of `phase_eq`, which may carry exponents.
- A configure that carries `mode` or `trperiod`, even with the values in force, resets each of `nfa`, `nfb` and `ntol` that it does not carry. `nfa` and `nfb` return to their values at the start of the session (`-L` and `-H`, 200 and 4000 Hz by default); `ntol` returns to the mode's default: 100 Hz in FST4W, 20 in JT65+JT9 and JTTY, 10 in Q65 and 1000 in the other modes. An `ntol` carried in the same frame is capped at 100 in FST4W and at 1000 in the other modes but JTTY, and Q65 and JT65+JT9 take 10 and 20 whatever it says. Send `nfa`, `nfb` and `ntol` with every configure that carries `mode` or `trperiod`.
- A configure that carries `mode` without `trperiod` also sets the mode's default period: 15 s for FT8, 7.5 s for FT4, 30 s for MSK144 and 60 s for the other modes.
- Two groups of keys each carry one setting, and a frame that carries any key of a group replaces the whole setting, so send each group whole: `depth_level`, `q65_maxiters_level`, `use_averaging`, `deep_ap_search` and `q65_auto_clear_average`; and `contest_type`, `single_decode`, `vhf_features`, `noise_blanker_level` and `q65_pileup`. The keys of the group that the frame leaves out become 0, false or none (`noise_blanker_level` -3). A `contest_type` of `q65_pileup` sets `q65_pileup` too unless the frame carries it.
- `depth` and the first group are two forms of one setting: a frame that carries both is declined with `configure_type_error`, key `depth`. `depth_level` and `q65_maxiters_level` in one frame must agree in their low two bits (`configure_type_error`, key `q65_maxiters_level`).
- A configure that carries `mycall` without `my_call_standard`, or `his_call` without `his_call_standard`, sets the flag from the call: true for a call FT8 sends unchanged in every standard message, CQ included (upper case, as WSJT-X sends it, with no leading Q; a trailing /R or /P allowed, /M or /MM not), false for an empty or nonstandard one.
- Before any configure, the session runs on the command line's values: among them depth 1 unless `-d` sets it, and `live_audio` and `busy_events` false. The codec checks the command line's values once, after `ready`; one the decoder would refuse sends a `configure_range_error`, and the session goes on.

### Periods and labels

- A period is `trperiod` times 12,000 samples, but never shorter than the samples the decoder takes: an FT4 period is at least 72,576 samples (6.048 s). A period starts at the session's first audio sample, and at the first sample after a configure that changes the mode or the period, a `discontinuity` or a `flush`. A configure that changes only the label does not start one. Send every sample.
- A configure that changes the mode or the period discards a period in flight that has not reached its stop (from a file, the whole period, or FT4's and multithreaded FT8's stop): it sends no `decode_finished`, and the decodes its early FT8 passes or MSK144 blocks sent stand. A period that has reached its stop decodes and sends its `decode_finished` as usual. Any other configure applies to the period in flight, which decodes at the stop point the new settings give.
- Label each period with `utc` (`"HH:MM:SS"`) or `nutc` (HHMMSS) in a configure sent after the previous period's last sample and before this period's first decode pass. Split an audio frame at the period boundary to put the configure there. In MSK144 the label belongs before the period's first 7,168 samples.
- The label in force when a pass runs is the `time` of its `decode` lines, and the `period_end` of the period's `decode_finished`, always six digits.
- The label is decoder input as well. Changing it within a period starts a new decoder input: FT8's next pass sends the period's earlier decodes again, and JT65 averages odd and even minutes apart by it.
- Prefer `utc`. In periods of a minute or more an `nutc` up to 2359 reads as HHMM, so 00:04:00 sent as `400` reads as 04:00. A frame that carries both uses `utc`. A label that is not a time of day declines the frame (`configure_range_error`, key `nutc` or `utc`).
- Each decoder gets the label in the form WSJT-X gives it, whichever form is sent. In periods of a minute or more, JT9, JT65 and JT65+JT9 decode with HHMM and JT4 with HHMM00, and their `time` and `period_end` are HHMM00.
- A label stays as it was sent, and a later mode reads it again, except that in JT4, Q65, FST4 and FST4W periods of a minute or more an HHMM `nutc` is multiplied by 100 on arrival. Send a label with every period, and after every mode change.
- MSK144 decodes each 3,584-sample block as it arrives, only in the period's first 30 s, and never a partial block.

### Live audio and files

- From a file (`live_audio` false, as at the start), each period decodes on its whole audio once the period is in; FT4 and multithreaded FT8 decode at their stop points. Single-threaded FT8's early passes (41 and 47) still run; outside SuperFox Hound decoding, multithreaded FT8 runs its early passes (41 and 46) only with `ft8_decoder_start` 0 or 1, and that key starts at 3. No pass is skipped for a busy decoder, only for want of memory (see [Memory](#memory)), and nothing reads the clock. A `flush` decodes the file's last, short period.
- From live audio, send `live_audio` true. Each period then decodes at WSJT-X's stop points for the mode, and a pass that WSJT-X's busy decoder would not run is skipped (`pass_skipped`). FT8's passes stop on the wall clock as WSJT-X's live passes do: pass 41 when the UTC 15 s period is 13.4 s in, and pass 47's subtraction step at 14.3 s; the final pass is not cut short.
- So a live client streams in real time, starts each period on its UTC boundary (a `discontinuity` there re-anchors the period), and labels each period. Audio out of step with UTC loses FT8 decodes.
- Only the multithreaded FT8 decoder defers a final pass that the busy decoder cannot run, as WSJT-X's does. The deferred final runs when the decoder comes free, unless a later final replaces it (`pass_skipped`, reason `replaced`). Every other mode but MSK144 skips such a final (reason `busy`).
- A configure that changes the mode, the period, `live_audio`, or a decoder or station setting that WSJT-X cancels on drops a waiting final (reason `cancelled`).
- A live producer that does not send `live_audio` gets every pass, however far behind. The codec buffers up to 16 MiB of input and queues up to 64 MiB for the decoder, audio and frames alike, and one pass more, which can be a 43.2 MB FST4W-1800 period; past that it stops reading, and the client's writes block as on a full pipe.

### Discontinuity, flush and halt

- `discontinuity`: the next audio sample does not follow the last one. In a period mode it re-anchors the period. Every pass whose audio is in has run, and a deferred final still runs. A period in flight that has not reached its stop (from a file, the whole period, or FT4's and multithreaded FT8's stop) ends with no decode and no `decode_finished`; the decodes its early FT8 passes or MSK144 blocks sent stand. The next audio sample starts a new period. At a period's start a `discontinuity` only re-anchors the audio clock: the next sample's arrival sets again when later audio falls due, which `pass_skipped`'s reason `busy` and `busy` false go by. In JTTY it ends the JTTY session.
- `flush`: decode what has come in, and go on. A deferred final runs, the period in flight decodes once, now, if it has samples, and the next audio sample starts a new period. Use it for a file's last, short period. The period's remaining passes run at their usual half-symbols on the audio that is in, so a decode's `pass` names the half-symbol, not how much audio there was. In JTTY it ends the JTTY session, and the stream goes on.
- `halt`: drain the period in flight, decoding it once, after a deferred final, if it has samples; then exit. Closing stdin does the same.

### Telling a hang from a slow decode

Send `busy_events` true, and keep the deadlines WSJT-X's watchdog keeps:

- `busy` true comes at the start of every decode pass, with the pass's `mode`, `period_end` and `nzhsym`, and `again` true on a decode again; and when a final is deferred, with `deferred` true. `busy` false comes once no pass runs, no final waits and everything received before the last decode ended has been decided, and at `halt` or the end of input. With the decode thread, `busy` stays true across back-to-back passes; without one (a build without OpenMP, or none could be started, which stderr notes), it turns false between them too. It always returns to false. MSK144 sends no `busy` lines.
- While `busy` is true, progress is a `busy`, `pass_progress` or `pass_skipped` line, or a `decode` whose mode is the last `busy` line's (JT65+JT9's decodes say JT9 or JT65). MSK144's lines and replies are not progress.
- Allow a pass this long between progress lines: on live audio, outside a decode again and after the first completion, 10 s in FT8 and 6 s in FT4; otherwise the greater of 60 s and four periods, or, before the first completion, of 300 s and four periods. A pass ends, and completes, at its `decode_finished` or at the next `busy` line; early FT8 passes send no `decode_finished`.
- While `busy` is false no decode runs. Once a period's scheduled end has passed, whether or not the codec has read all its audio, its `decode_finished`, or a `busy` line naming its `period_end`, is due within the same time, except for the periods that send no `decode_finished` (see below). That deadline runs only while `busy` is false: a `busy` true suspends it, and it starts again from zero at the next `busy` false. On live audio either line can come before the period's last sample has been sent, since passes run at their stop points: count a period answered when its line arrives.
- No deadline runs from a reply's first line to its last, nor while `planning` is true. Afterwards each deadline resumes where it stopped, so replies sent one after another cannot hide a hung decode.
- The stream's own thread (frames, replies, MSK144 and JTTY) needs no deadline. A deadline kept on it must allow a block interval plus the longest render sent and the waits of encode requests, and must stop while `planning` is true.
- `pass_progress` is advisory: at most one a second.
- `decode_finished` comes once per period, and once more per decode again. A period sends none when a mode or period change discards it, when a `discontinuity` drops it short of its stop, or when it is a SuperFox even-sequence period whose stop is below 50. A period whose final pass is skipped still sends one.

### Ordering

- Decode lines, `decode_finished`, `pass_skipped` and `busy` lines, and the errors for frames sent in a period mode come out in the order of the input that caused them: an error waits behind the decode work queued before it.
- Periods' lines can interleave. A period's `decode` lines carry its label as `time` and its other lines carry it as `period_end`; group them by it. A configure that enters MSK144 or JTTY starts that mode at once, so the previous mode's remaining decode lines can follow the new mode's.
- A reply to `pack` or `render` waits for no decode. It can come before the decode lines for audio sent before it, before a waiting final that starts just before it, and before the errors for configure, control or odd-length audio frames sent before it. Match an error that has no `id` by its `code`, not by the request in flight.

### Exit status

| Status | When |
|---|---|
| 0 | After `halt` or the end of input, once the last period's lines are out; also after `-v` and `--help` |
| 1 | A bad session header (one `error` line with `msg` instead of `code`), a frame longer than 43,200,000 bytes (`frame_too_large`), no memory to buffer or queue the input (`out_of_memory`), no memory for the decoder's buffer (an `error` line with `msg`, before `ready`), or a fatal JTTY error |
| 2 | An invalid command-line value (stderr only), or `-9` with a `-p` other than 60 (`configure_range_error` in place of `ready`) |

- After an `out_of_memory` raised while queuing input for the decoder, and after JTTY's fatal errors, the status is not guaranteed to be 1.
- Treat the end of stdout before `ready` as a failed start, whatever the status: an unrecognized option prints the help on stdout and exits 0.

### Memory

- Besides the input it buffers and queues (up to 16 MiB and 64 MiB, above), a codec process holds two buffers of 48 MB, the stream's period buffer and the decoder's; up to three spare copies of a pass's audio and a copy of the last decoded period's audio, each up to 43.2 MB (an FST4W-1800 period); and the decode thread's own copy of the decoders' thread-local arrays, about 2.7 MB on Linux, as each OpenMP thread has.
- Once a session has decoded both MSK144 and a period mode that runs OpenMP threads, it holds a second pool of those threads, each with its copy.
- When the codec cannot buffer or queue its input it sends `out_of_memory` and exits. When it cannot copy a pass's audio for the decoder it skips the pass (`pass_skipped`, reason `memory`), on file audio too.
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

### Keys that keep WSJT-X's behavior

These configure keys carry WSJT-X's settings and state. Send the values WSJT-X would use; a key never sent keeps its starting value, apart from the resets, groups and call flags under [Configure](#configure).

- Live audio and timing: `live_audio` is WSJT-X's distinction between live audio and a file. `decode_after_eme_delay` is its "Decode after EME delay": a configure that carries it, `mode` or `trperiod` sets the EME delay to 2.5 s while `decode_after_eme_delay` is true and to 0 while it is false, and `eme_delay_seconds`, when sent, wins.
- Decoder progress: `busy_events` turns on the `busy`, `pass_progress` and `planning` lines, which tell a slow decode from a hung one as WSJT-X's watchdog does.
- Decoding again: `nagain_flag` true asks once to decode the last period again. On live audio it is refused while the decoder is busy (`decode_again_unavailable`), as WSJT-X's Decode button is; send it again after a `decode_finished`. MSK144 and JTTY keep no period to decode again.
- Averages: `clear_average` is WSJT-X's Clear Avg, for JT4, JT65, JT65+JT9 and Q65, and acts only on the next decode that runs.
- MSK144: `short_messages` and `swl` are WSJT-X's "Sh" and "SWL" check boxes; `train` is its "Measure phase response", reported by `msk144_trained`, after which `train` is false; `phase_eq` holds the five coefficients its Equalization Tools apply (all zeros, the default, is no equalization). Each is in force in every mode until another configure sets it.
- JT4 and JT65: `min_width` is JT4's; `min_sync` is JT4's, JT65's and JT65+JT9's; `n_2pass` and `aggressive` (WSJT-X's Aggressive, 0 to 10) are JT65's and JT65+JT9's; `jt65_ap_on` is JT65's; `n_trials` is the trial count of JT65's soft-decision decoder.
- JT65+JT9 only: `robust_mode`, `tx_mode` and `npts_c0_array`.
- AP decoding: `ft8_ap_on` and `ap_width_hz` reach both FT8 decoders (in the multithreaded one, `ft8_ap_on` gates only its a8 pass); `ap_cq_only` reaches the single-threaded FT8 decoder, FT4, FST4 and Q65; `qso_progress_state` (0 to 5) reaches both FT8 decoders, FT4, JT65, JT65+JT9, FST4 and Q65.
- The multithreaded FT8 decoder: `multithreaded_ft8` selects it, and only it reads `ft8_cycles`, `ft8_rxf_sensitivity`, `ft8_threads`, `ft8_low_threshold`, `ft8_subpass`, `ap_my_call`, `hide_ft8_duplicates`, `enable_dxc_search`, `wide_dxc_search`, `hound_mode`, `multi_instance`, `skip_tx1`, `nagain_filter`, `stop_hint`, `candthin_threshold` and `dt_center_seconds`, and the station state `last_tx_seconds_ago`, `currently_txing`, `mode_changed`, `sec_band_changed` and `delay_units`. `ft8_decoder_start` sets its pass schedule. An `ft8_threads` outside 0 to 24 declines a configure in FT8 (`configure_range_error`), single-threaded too.
- SuperFox: `superfox_mode` and `even_sequence` drive SuperFox decoding, and `date` reaches only the SuperFox decoder.
- Q65: `max_drift_hz` sets Q65's drift search; `q65_pileup` (or a `contest_type` of `q65_pileup`) is WSJT-X's Q65 Pileup activity.

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
{"v":1,"t":"ready","modes":["FT8","FT4","JT9","JT65","JT65+JT9","JT4","FST4","FST4W","Q65","MSK144","JTTY"],"encodes":["FT8","FT4","JT4","JT9","JT65","Q65","FST4","FST4W","WSPR","MSK144","ECHO","TUNE","FOX","SUPERFOX","JTTY"],"protocol":1}
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
- A reply's lines are consecutive, each carries the request's `id`, and the last is `packed`, `rendered` or `rejected`. A refused request sends only its `rejected` line.

| Request | Reply |
| --- | --- |
| A slot mode's `pack` | `encoded`, then `packed` |
| A slot mode's `render` | `encoded`, the audio's `pcm` lines, then `rendered` |
| JTTY's `pack` | One `segment` line a segment, then `packed` |
| JTTY's `render` | Each `segment` line followed by its `pcm` lines, then `rendered` |

- A request is refused by its `rejected` line, which carries its `id`; no `error` line carries one. The `error` lines a request can cause instead are `invalid_request_id`; `control_frame_too_large`, for a request frame over 262,144 bytes; `configure_parse_error`, for one without a readable `t`; and a fatal error, after which the codec exits.
- The codec answers requests in the order sent, each at its place in the input, and a reply waits for no decode ([Ordering](#ordering)). A request holds up decoding instead: the codec reads no audio while it computes a render, so on live audio a pass whose audio arrives meanwhile runs late, and FT8's wall-clock stops can shorten it; and a reply's lines hold up the next decode line until the client reads them.
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
{"v":1,"t":"ready","modes":["FT8","FT4","JT9","JT65","JT65+JT9","JT4","FST4","FST4W","Q65","MSK144","JTTY"],"encodes":["FT8","FT4","JT4","JT9","JT65","Q65","FST4","FST4W","WSPR","MSK144","ECHO","TUNE","FOX","SUPERFOX","JTTY"],"protocol":1}
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
- A session ends at a `discontinuity`, a `flush`, an odd-length audio frame, a configure that leaves JTTY, `halt` and the end of input. The next JTTY audio sample begins a new one.
- After each decoder step the codec sends one `jtty_update` line for each pending update, as WSJT-X takes them. Its `id` numbers messages, not requests, so route lines by `t`. Each message's last line is its `complete`, `expired` or `ended` one ([JTTY update states](#jtty-update-states)).
- The decoder keeps a message's first 80 characters: its later frames move `latest` but add no text. Typed dots never make a `gaps` entry. A `freq`, `start` or `latest` that is not finite is null.
- Between a session's `session` line and its last update come only `error` lines, replies to requests, `planning` lines and, when the session began with a mode change, the previous mode's remaining decode lines. An `odd_audio_frame` error comes before the `ended` lines it causes.
- Ending a session also searches the audio window that extends past its last sample, unlike WSJT-X, so a last frame that ends shortly before the session does still decodes.
- A fatal error ends the codec at once: a message in progress gets no last line.
- The decoder searches `rxfreq` ± `ntol`, and two 300 Hz channels centered on 1350 and 1650 Hz, each center moved into `nfa` to `nfb` and the channel clipped to it: 1200 to 1800 Hz with the default `nfa` and `nfb`. Nothing whose lowest tone is above about 2950 Hz decodes, as in WSJT-X. A configure that would leave `rxfreq`, `ntol`, `nfa` or `nfb` outside 0 to 6000 Hz is declined (`configure_range_error`, with no `mode`); that is the range the keys take, not the band searched.
- A decoder step that needs FFT plans for which `-a` holds no FFTW wisdom measures them: up to tens of seconds on ARM Linux, while the codec reads no input and a period decode that needs a new plan waits. The plans follow from `rxfreq`, `ntol`, `nfa` and `nfb`, and the first step over audio is the first to measure. With `busy_events`, `planning` lines bracket the step. The codec saves the wisdom to `-a` after each step that measured and at `halt` or the end of input, so keep `-a` across runs ([Directories and files](#directories-and-files)).

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

## WSPR

In this release wsjt-codec encodes WSPR but does not decode it: `ready` lists WSPR in `encodes`, not in `modes`. `wsprd -0` decodes it. wsprd is not in the codec's tarball: each release ships it in a tarball of its own, `wsjtx-<release>-<target>-wsprd.tar.gz`.

- `wsprd -0` reads the same session header and frames on its standard input and writes one JSON object a line on its standard output; its names are under [wsprd control messages](#wsprd-control-messages) and the tables that follow. Its diagnostics go to standard error.
- A process decodes one period. Send the header, a `configure` with `date`, `time` and `dialfreq` (and `wspr_type` 15 for WSPR-15), the period's audio from its start, then `halt`; closing standard input does the same as `halt`.
- wsprd decodes once its input ends, on the first 114 s of audio it received (1,368,000 samples), or 912 s for WSPR-15; less is `insufficient_audio`.
- `ready` comes before wsprd reads the header. A header it cannot take, or input that ends inside a frame's 5-byte header, ends it with exit status 1 and a message on standard error only. An unknown option, or an `-a` it cannot read and write, prints its usage text on standard output in place of `ready`, and exits 1. Rely on the exit status, not on an `error` line.
- Its errors end it as [wsprd error codes](#wsprd-error-codes) states. A decoded period sends its `decode` lines, in order of frequency, then `decode_finished`, and wsprd exits with status 0.
- A `decode` line's `freq` is `dialfreq` plus the signal's audio frequency, in MHz, and its `time` is the configure's `time`.
- With `-0`, the configure's `dialfreq` and `wspr_type` stand in for `-f` and `-m`. wsprd keeps its files in `-a`, the working directory by default, and stops if it cannot write there: it appends its decodes to `ALL_WSPR.TXT`, reads and rewrites `hashtable.txt`, the calls and grids it has decoded, for resolving hashed calls, and writes `wspr_spots.txt`, `wspr_timer.out` and `wspr_wisdom.dat`. Keep `-a` across runs; two wsprd processes in one `-a` write the same files.
- WSJT-X runs wsprd with `-a` set to its data directory and the options of its decode depth: `-qB` at Fast, `-C 500 -o 4` at Normal, and `-C 500 -o 4 -d` at Deep. They apply with `-0` too.

### A wsprd session

This session ran `wsprd -0` on [`samples/WSPR/150426_0918.wav`](https://wsjtx.github.io/wsjtx/samples/WSPR/150426_0918.wav), a 120 s WSPR recording, from an empty working directory. It sent the session header, then:

1. a control frame: `{"t":"configure","date":"150426","time":"0918","dialfreq":14.0956,"wspr_type":2}`;
2. 120 audio frames of 12,000 samples, the file's 1,440,000 samples;
3. a control frame: `{"t":"halt"}`.

It received:

```text
{"v":1,"t":"ready","modes":["WSPR"],"protocol":1}
{"v":1,"t":"decode","mode":"WSPR","date":"150426","time":"0918","snr":-9,"dt":1.1,"freq":14.097046,"drift":0,"message":"ND6P DM04 30"}
{"v":1,"t":"decode","mode":"WSPR","date":"150426","time":"0918","snr":-15,"dt":0.1,"freq":14.097060,"drift":0,"message":"W5BIT EL09 17"}
[6 lines elided]
{"v":1,"t":"decode","mode":"WSPR","date":"150426","time":"0918","snr":-25,"dt":0.7,"freq":14.097194,"drift":0,"message":"W3BI FN20 30"}
{"v":1,"t":"decode_finished","date":"150426","time":"0918"}
```

wsprd exited with status 0, and left `ALL_WSPR.TXT`, `hashtable.txt`, `wspr_spots.txt`, `wspr_timer.out` and `wspr_wisdom.dat` in its working directory.

- wsprd decoded the first 114 s of the 120 s it received, after `halt`.
- `ND6P DM04 30` at 14.097046 MHz is about 1446 Hz of audio above the 14.0956 MHz dial.

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

### Configure keys

`configure` sets only the keys it has. The keys most clients send come first, marked; the rest follow in alphabetical order. Many keys carry a WSJT-X setting to the decoder that reads it, so that the codec decodes as WSJT-X does; the keys marked "no effect" change nothing. Start is the value before any configure sets the key, and the option that sets it; send the values WSJT-X would. A first configure for live FT8:

```json
{"t":"configure","mode":"FT8","trperiod":15,"depth":3,"nfa":200,"nfb":4000,"ntol":1000,"mycall":"K1ABC","mygrid":"FN42","live_audio":true,"utc":"12:00:00"}
```

| Key | Type | Use | Start | Meaning |
| --- | --- | --- | --- | --- |
| `mode` | string or integer | most clients | FT8 | The mode to decode: one of the names `ready` lists in `modes`, case aside, or its integer code: FT8 8, FT4 5, JT9 9, JT65 65, JT65+JT9 74, JT4 4, FST4 240, FST4W 241 (242 is FST4W with the command line's `-Y`: its decodes show an unresolved hashed call as its hash22 value, `<NNNNNNN>`), Q65 66, MSK144 144. JTTY has none. |
| `trperiod` | number | most clients | 15 (`-p`) | The period, seconds: a finite number above 0 and at most 1800. It starts at 15 s with no mode option (see Command line). |
| `submode` | string or integer | most clients | A (`-b`) | The submode, where the mode has one: an upper-case letter from A (only its first character is read), or 0 for A and so on. |
| `depth` | integer | most clients | 1 (`-d`) | Decoding depth, 1 to 3. |
| `nfa` | integer | most clients | 200 (`-L`) | Lowest audio frequency searched, Hz. |
| `nfb` | integer | most clients | 4000 (`-H`) | Highest audio frequency searched, Hz. |
| `rxfreq` | integer | most clients | 1500 (`-f`) | Receive audio frequency, Hz; it sets the transmit frequency too, unless `tx_audio_offset_hz` does. |
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
| `clear_average` | boolean |  | false | Clears the averages for the next decode, as WSJT-X's Clear Avg (JT4, JT65, JT65+JT9 and Q65). |
| `common_ft8b` | boolean | no effect | true | No decoder reads it. |
| `currently_txing` | boolean |  | false | Read only by the multithreaded FT8 decoder. |
| `date` | string |  | none | The date, "YYYY-MM-DD"; only the SuperFox decoder reads it. |
| `decode_after_eme_delay` | boolean |  | false | WSJT-X's Decode after EME delay: later stop points and a 2.5 s EME delay. |
| `deep_ap_search` | boolean |  | false | Deep AP search (JT4, JT65 and JT65+JT9); one of the `depth_level` group. |
| `delay_units` | integer |  | 0 | Read only by the multithreaded FT8 decoder. |
| `depth_level` | integer |  | 1 (`-d`) | The decoding depth, as `depth`; a frame that has it, `q65_maxiters_level`, `use_averaging`, `deep_ap_search` or `q65_auto_clear_average` sets all five, and takes no `depth`. |
| `dt_center_seconds` | number |  | 0 | The multithreaded FT8 decoder's DT center, seconds, applied in hundredths. |
| `dt_tolerance_seconds` | number | no effect | 3 | No decoder reads it. |
| `eme_delay_seconds` | number |  | 0 | The EME delay, seconds; the codec sets it as WSJT-X does (2.5 with `decode_after_eme_delay`, else 0) unless a frame gives it. |
| `enable_dxc_search` | boolean |  | true | The multithreaded FT8 decoder's DX call search. |
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
| `last_tx_seconds_ago` | integer |  | 0 | Read only by the multithreaded FT8 decoder. |
| `max_drift_hz` | integer |  | 0 | WSJT-X's Max Drift for Q65, in symbol rates per transmission (not Hz), 0 to 50. |
| `min_sync` | integer |  | 0 | The minimum sync (JT4, JT65 and JT65+JT9). |
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
| `q65_auto_clear_average` | boolean |  | false | WSJT-X's automatic clearing of averages after an averaged decode (JT65, JT65+JT9 and Q65); one of the `depth_level` group. |
| `q65_maxiters_level` | integer |  | 1 (`-d`) | Q65's iterations level; it agrees with `depth_level` when a frame has both. |
| `q65_pileup` | boolean |  | false | WSJT-X's Q65 pileup decoding; one of the `contest_type` group. |
| `qso_progress_state` | integer |  | 0 (`-Q`) | QSO progress, 0 to 5, for AP decoding (FT8, FT4, JT65, JT65+JT9, FST4 and Q65). |
| `robust_mode` | boolean |  | false | JT65+JT9's robust mode; only JT65+JT9 reads it. |
| `sec_band_changed` | integer |  | 0 | Read only by the multithreaded FT8 decoder. |
| `short_messages` | boolean |  | false | MSK144's Sh: short messages. |
| `single_decode` | boolean |  | false | WSJT-X's single decode; one of the `contest_type` group. |
| `skip_tx1` | boolean |  | false | WSJT-X's skip Tx1 (multithreaded FT8). |
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

### Request keys

The keys of `pack` and `render`. A request without `mode`, or with `"mode":"JTTY"`, is JTTY's; any other mode's is a slot mode's. A key the request's kind does not take is refused (`rejected`, `bad_request`).

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

### Output lines

| Line | Meaning |
| --- | --- |
| `ready` | Sent once the session header is read: the modes `configure` decodes, the modes `pack` and `render` encode, and `protocol`. |
| `decode` | One decoded message: mode, period label, SNR, DT, frequency, message and the mode's own fields. |
| `decode_finished` | A period's decoding is done, with the decoder's counts for its last pass. |
| `pass_skipped` | A decode pass that WSJT-X would not run, and why. |
| `msk144_trained` | MSK144 training completed: the call, the phase-response file and its coefficients. |
| `busy` | With `busy_events`: a decode pass starts (true), or no decode runs or waits (false). |
| `pass_progress` | With `busy_events`: a decode pass is still running. |
| `planning` | With `busy_events`: JTTY is measuring an FFT plan (true) or has done so (false); the codec reads no input meanwhile. |
| `error` | An error, named by its `code`; three carry `msg` instead ([the `error` fields](#error)). |
| `session` | A JTTY session began: its number and the stream index of its first sample. |
| `jtty_update` | A JTTY message, growing, complete, expired or ended, with its text. |
| `encoded` | A slot mode's `pack` or `render`: the message as sent and its tones. |
| `segment` | A JTTY `pack` or `render`: one segment of the message and its frames. |
| `pcm` | A `render`'s audio: int16 little-endian mono samples in base64, 16384 a line but the last. |
| `packed` | The last line of a `pack` reply. |
| `rendered` | The last line of a `render` reply. |
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
| `snr` | all | integer | SNR, dB. |
| `dt` | all | number | DT, seconds: the decoder's single-precision value to 9 significant digits. |
| `freq` | all | integer | Audio frequency, Hz. |
| `message` | all | string | The message as jt9 prints it, `?` and `OOO` included. |
| `ap` | FT8, FT4, FST4, FST4W | integer | The N of jt9's aN; 0 for none. |
| `sync` | JT65, JT4 | string | JT65: `#`, `#*` or `##`; JT4: `*` or `#`. Empty for a candidate jt9 prints without it, whose message is empty too. |
| `flags` | JT65, JT4 | string | jt9's flags, blanks trimmed: empty, `f`, `d5`, `a13` and so on. |
| `q` | Q65 | integer | The qN code. |
| `avg` | Q65 | integer | The periods averaged. |
| `copied` | Q65 | boolean | jt9's #: the sender copied our last message. |
| `period` | FST4, FST4W, Q65 | integer | The TR period, seconds. |
| `spread` | FST4, FST4W | number | The spread, Hz, when the decoder measured it. |
| `superfox` | SuperFox | boolean | true; a SuperFox decode has none of the fields from `ap` to `spread`. |
| `text_width` | SuperFox | integer | The columns jt9's line gives the message; SuperFox pads some texts with blanks. |
| `training` | MSK144 | boolean | true while configure's `train` is still true after its window, where WSJT-X marks the decode ^. |
| `pass` | all but MSK144 | integer | The half-symbol stop of the pass that decoded it: 41, 46 or 47 for an early FT8 pass. |

#### `decode_finished`

| Field | Type | Meaning |
| --- | --- | --- |
| `period_end` | string | The period's label. |
| `synced` | integer | jt9's synced count for the pass that ended the period. |
| `decoded` | integer | jt9's decoded count for that pass, not a count of the `decode` lines sent. |
| `average` | integer | jt9's average count; Q65's is 1000 times the even periods averaged plus the odd. All three are 0 for a final not run and for MSK144. |

#### `pass_skipped`

| Field | Type | Meaning |
| --- | --- | --- |
| `mode` | string | The pass's mode. |
| `period_end` | string | The label of the period's `decode_finished`. |
| `nzhsym` | integer | The pass's half-symbol: 41, 46 or 47 for an early FT8 pass, the mode's stop point for a final. |
| `stage` | string | early or final ([Pass skipped stages](#pass-skipped-stages)). |
| `reason` | string | Why it was skipped ([Pass skipped reasons](#pass-skipped-reasons)). |

#### `msk144_trained`

| Field | Type | Meaning |
| --- | --- | --- |
| `call` | string | The DX call it trained on, configure's `his_call`. |
| `file` | string or null | The phase-response file it wrote, in `-a`; null when the file could not be created. |
| `phase_eq` | array of five numbers | The coefficients, which a configure's `phase_eq` applies; null for a value JSON cannot hold. |

#### `busy`

| Field | Type | Meaning |
| --- | --- | --- |
| `busy` | boolean | true at the start of a decode pass or when a final is deferred; false once no pass runs or waits. |
| `mode` | string | With true: the pass's mode. |
| `period_end` | string | With true: the pass's period, as its `pass_skipped` would give it. |
| `nzhsym` | integer | With true: the pass's half-symbol. |
| `again` | boolean | true on a decode again. |
| `deferred` | boolean | true when a final is deferred. |

#### `pass_progress`

| Field | Type | Meaning |
| --- | --- | --- |
| `mode` | string | The running pass's mode, as its `busy` line gives it. |
| `period_end` | string | The running pass's period. |
| `nzhsym` | integer | The running pass's half-symbol. |

#### `planning`

| Field | Type | Meaning |
| --- | --- | --- |
| `planning` | boolean | true before a JTTY step measures an FFT plan; false when it is done. |

#### `error`

| Field | Type | Meaning |
| --- | --- | --- |
| `code` | string | The error ([Error codes](#error-codes)); three errors have `msg` instead. |
| `msg` | string | In place of `code`, free text, for three errors that end the session: a session header the codec cannot take, and no memory for the decoder's buffer, each before `ready` with exit status 1; and a JTTY fatal error, mid-session, whose exit status is not guaranteed to be 1. |
| `detail` | string | Free text. |
| `key` | string | `configure_type_error` and `configure_range_error`: the key. |
| `expected` | string | `configure_type_error`: the JSON type the key takes (`int`, `real`, `string`, `bool`, `string\|int`, `array of numbers`), or the rule its value broke (`HH:MM:SS`, `year<2100`, `int 10^N\|3*10^N`, `depth alone`, `same depth_level`). |
| `got` | string or integer | `configure_type_error`: the JSON type sent (`string`, `number`, `bool`, `null`; an array or object reads as `number`, and as `array` or `object` only in `phase_eq`), or `conflict` for the depth rules; `unknown_schema_version`: the version sent. |
| `mode` | string | `configure_range_error`: the mode the frame would leave (none in JTTY); `decode_rejected`: the mode. |
| `operation` | string | `decode_rejected`: the decoder's operation. |
| `status` | string | `decode_rejected`: the decoder's status. |
| `field` | string | `decode_rejected`: the decoder option at fault, in the decoder's terms, when it has one. |

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
| `odd_audio_frame` | An audio frame with an odd byte count; it is skipped. |
| `control_frame_too_large` | A control frame longer than 262,144 bytes; it is skipped. |
| `frame_too_large` | A frame longer than 43,200,000 bytes; the session ends, exit status 1. |
| `invalid_request_id` | A `pack` or `render` without a valid `id`. |
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

### wsprd control messages

`wsprd -0` decodes one WSPR period a process from the same session header and frames; it takes 12 kHz mono int16 audio only.

| Message | Meaning |
| --- | --- |
| `configure` | Sets the date, time, dial frequency and WSPR type; send it before the audio. |
| `halt` | Decodes the audio received and ends the session. |

### wsprd keys

The keys of wsprd's `configure`.

| Key | Type | Use | Meaning |
| --- | --- | --- | --- |
| `t` | string |  | `configure` or `halt`. |
| `date` | string |  | The date, "YYMMDD". Required. |
| `time` | string |  | The period's start, "HHMM". Required. |
| `dialfreq` | number |  | The dial frequency, MHz. Required. |
| `wspr_type` | integer |  | 2 for WSPR, 15 for WSPR-15; 2 without it. |
| `mycall` | string | no effect | Read and not used. |
| `mygrid` | string | no effect | Read and not used. |

### wsprd output lines

| Line | Meaning |
| --- | --- |
| `ready` | Sent at start, before the session header is read: `modes` is ["WSPR"]. |
| `decode` | One decoded message: date, time, SNR, DT, frequency in MHz, drift and message. |
| `decode_finished` | The period's decoding is done. |
| `error` | An error, named by its `code`. |

### wsprd output fields

Each line begins with `"v":1` and its `t`.

#### `ready`

| Field | Type | Meaning |
| --- | --- | --- |
| `modes` | array of strings | ["WSPR"]. |
| `protocol` | integer | 1. |

#### `decode`

| Field | Type | Meaning |
| --- | --- | --- |
| `mode` | string | WSPR. |
| `date` | string | configure's `date`, YYMMDD. |
| `time` | string | configure's `time`, HHMM. |
| `snr` | integer | SNR, dB. |
| `dt` | number | DT, seconds, to one decimal. |
| `freq` | number | The frequency, MHz, to six decimals. |
| `drift` | integer | The drift. |
| `message` | string | The message. |

#### `decode_finished`

| Field | Type | Meaning |
| --- | --- | --- |
| `date` | string | configure's `date`. |
| `time` | string | configure's `time`. |

#### `error`

| Field | Type | Meaning |
| --- | --- | --- |
| `code` | string | The error ([wsprd error codes](#wsprd-error-codes)). |
| `detail` | string | Free text. |
| `msg` | string | Free text in place of `code` and `detail`; none of wsprd's errors uses it. |

### wsprd error codes

Each of these but `control_parse_error` ends wsprd with exit status 1.

| Code | Meaning |
| --- | --- |
| `frame_too_large` | A control frame longer than 1024 bytes, or an audio frame longer than WSPR-15's 10,944,000 samples. |
| `frame_read_error` | The input ended inside a frame's body. |
| `allocation_failed` | Memory or an FFT plan could not be allocated. |
| `control_parse_error` | A control frame without a string `t`; the frame is skipped. |
| `odd_audio_frame` | An audio frame with an odd byte count. |
| `invalid_wspr_type` | A `wspr_type` other than 2 or 15. |
| `missing_configure` | No configure before `halt` or the end of input. |
| `incomplete_configure` | The configure lacks `date`, `time` or `dialfreq`. |
| `insufficient_audio` | Less audio than one period of the configured WSPR type. |
