#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A wsjt-codec client: decode a WAV file, replay one as live audio, or render a message.

  codec_client.py decode --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--low HZ] [--high HZ] [--mycall CALL] WAV
  codec_client.py live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
  codec_client.py render --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--out WAV] [TEXT...]

doc/codec/examples/README.md states what each prints and its exit status. Python 3.8 or later, standard library only.
"""

import argparse
import base64
import json
import math
import os
import queue
import re
import struct
import subprocess
import sys
import threading
import time
import wave

RATE = 12000                         # the stream's sample rate, Hz
DECODES = ("FT8", "FT4", "JT9", "JT65", "JT4", "FST4", "FST4W", "Q65", "MSK144", "ECHO", "JTTY", "FREQCAL", "WSPR")  # what decode takes: the modes ready lists in modes
ENCODES = ("FT8", "FT4", "JT4", "JT9", "JT65", "Q65", "FST4", "FST4W", "WSPR", "MSK144", "ECHO", "TUNE", "FOX", "SUPERFOX", "JTTY")  # what render takes: those it lists in encodes
# The period, s, when --period gives none: the mode's own, as a configure that carries the mode sets it; FreqCal has none, so the examples take 15 s; TUNE's is the length of its tone. JTTY has no periods.
PERIOD = {"FT8": 15.0, "FT4": 7.5, "JT9": 60.0, "JT65": 60.0, "JT4": 60.0, "FST4": 60.0, "FST4W": 120.0, "Q65": 60.0, "MSK144": 30.0, "ECHO": 3.0, "FREQCAL": 15.0, "WSPR": 120.0, "TUNE": 15.0, "FOX": 15.0, "SUPERFOX": 15.0}
SUBMODES = ("JT4", "JT9", "JT65", "Q65")   # the modes with a submode: A unless --submode gives one
PERIODS = ("Q65", "FST4", "FST4W", "MSK144")  # the slot modes whose render takes trperiod; fast JT9's does too
DECODED_AS = {"TUNE": "FREQCAL", "FOX": "FT8", "SUPERFOX": "FT8"}  # what render decodes these back with; every other mode, itself
LIVE_ALLOWANCE = {"FT8": 10.0, "FT4": 6.0}  # a live pass's time between progress lines, once one has completed
READY_SECONDS = 30                   # how long the client waits for ready
EXIT_SECONDS = 10                    # how long the codec has to exit once its output ends, or after halt on a failure
RELEASE_SECONDS = 2                  # how long, once the codec has exited, the client waits for the end of its output and for its writes to its input: a process the codec started can hold the pipes
SILENCE_SECONDS = 300                # outside live, how long the codec may send no line: the longest interface.md allows a decode pass between progress lines before one has completed, or four periods when that is longer (silence_bound)
# For the examples' check only (README.md, "The check"), not a setting: CODEC_EXAMPLE_CHECK_SILENCE_SECONDS, a whole number from 1 to 300, replaces the bound, so that a run that checks it need not last 300 s.
_CHECK_SILENCE = os.environ.get("CODEC_EXAMPLE_CHECK_SILENCE_SECONDS", "")
CHECK_SILENCE = int(_CHECK_SILENCE) if re.fullmatch("[0-9]{1,3}", _CHECK_SILENCE) and 1 <= int(_CHECK_SILENCE) <= 300 else None
CHUNK = 1200                         # samples in an audio frame: 0.1 s
# Sent with the mode: these examples have no station, so no calls (the decoders use the starting calls otherwise) but --mycall's, and a configure that carries the mode sets the mode up as WSJT-X does, which can change the search keys and the frequencies, so the search keys go with it.
DECODER = {"his_call": "", "his_grid": "", "ntol": 1000}
SEARCH = {"FST4": (600, 1400)}  # nfa and nfb when --low and --high give none: in FST4 they are its F Low and F High, WSJT-X's 600 and 1400 Hz; elsewhere the waterfall's range

AUDIO, CONTROL = 0x01, 0x02          # frame types
# Session header: magic, sample format 0 (int16), 1 channel, rate in kHz (uint16).
HEADER = b"WSJT" + struct.pack("<BBH", 0, 1, RATE // 1000)
# WAVE_FORMAT_EXTENSIBLE's subformat GUID for PCM, as a WAV file stores it.
PCM_SUBFORMAT = bytes.fromhex("0100000000001000800000aa00389b71")

OK, FAILED, USAGE, HUNG = 0, 1, 2, 3  # exit status


def frame(kind, body):
    """A frame: type (1 byte), body length (uint32 little-endian), body."""
    return struct.pack("<BI", kind, len(body)) + body


def number(value):
    """A JSON number as the examples print it: one decimal, rounded half to even as WSJT-X shows DT, or nan for null (a value that is not finite)."""
    return "nan" if value is None else "%.1f" % value


def tx_start(mode, period, fast):
    """Where WSJT-X starts a transmission in its period, s."""
    if mode in ("MSK144", "ECHO", "TUNE") or fast:
        return 0.0
    if mode == "SUPERFOX":
        return 0.4
    if mode == "FT4":
        return 0.3
    if mode in ("FT8", "FOX") or (mode == "FST4" and period == 15) or (mode == "Q65" and period in (15, 30)):
        return 0.5
    return 1.0


def over_noise(audio, seconds):
    """The rendered audio, 36 dB down (an arithmetic shift right by 6), over noise, in a buffer of the given seconds: about 12 dB SNR in 2500 Hz, as decoders expect a noise floor (JT65's decodes nothing without one).

    The noise is integer arithmetic, so that every example sends the same samples: a 32-bit xorshift (13, 17, 5) from 1, and each sample the sum of its four bytes less 510.
    """
    total = int(seconds * RATE)
    signal = struct.unpack("<%dh" % (len(audio) // 2), audio)
    x, out = 1, []
    for i in range(total):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        noise = (x & 255) + (x >> 8 & 255) + (x >> 16 & 255) + (x >> 24) - 510
        out.append((signal[i] >> 6 if i < len(signal) else 0) + noise)
    return struct.pack("<%dh" % total, *out)


def silence_bound(seconds):
    """Outside live, how long the codec may send no line before the client takes it to have hung: 300 s, or four periods of the given seconds, up to the codec's 1800 s, when that is longer; the check's CHECK_SILENCE replaces it."""
    if CHECK_SILENCE is not None:
        return CHECK_SILENCE
    return max(SILENCE_SECONDS, 4 * min(seconds, 1800)) if seconds else SILENCE_SECONDS


def utc(seconds):
    """A time of day as the configure key utc takes it: "HH:MM:SS"."""
    s = int(seconds) % 86400
    return "%02d:%02d:%02d" % (s // 3600, s // 60 % 60, s % 60)


class Codec:
    """One wsjt-codec process: frames go to its stdin; each stdout line comes back as a dict."""

    def __init__(self, path, data):
        data = os.path.abspath(data)
        os.makedirs(data, exist_ok=True)
        env = dict(os.environ)
        # The decoders' OpenMP threads need the stack WSJT-X gives them.
        env.setdefault("OMP_STACKSIZE", "10M")
        # One directory for the codec's data (-a), temporary (-t) and read-only (-r) files, and its working directory, where it writes files of its own.
        self.proc = subprocess.Popen(
            [os.path.abspath(path), "-a", data, "-t", data, "-r", data], cwd=data, env=env,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))  # Windows: no console window
        self.lines = queue.Queue()
        self.input_open = True
        self.exited_at = None  # when the client saw the codec's exit (time.monotonic())
        # Read stdout and stderr on their own threads: a pipe left full stops the codec.
        threading.Thread(target=self._read_stdout, daemon=True).start()
        threading.Thread(target=self._read_stderr, daemon=True).start()
        self.write(HEADER)

    def _read_stdout(self):
        for raw in self.proc.stdout:
            text = raw.decode("utf-8", "replace").rstrip("\r\n")
            try:
                line = json.loads(text)
            except ValueError:
                line = None
            if isinstance(line, dict):
                self.lines.put(line)
            else:
                print("codec_client: not a JSON object: " + text, file=sys.stderr)
        self.lines.put(None)  # end of output

    def _read_stderr(self):
        # Diagnostics for people, not for parsing. os.write, not sys.stderr: this daemon thread may still be writing when the client exits, and one caught holding sys.stderr's lock then aborts Python.
        stderr = sys.stderr.fileno()
        for raw in self.proc.stderr:
            while raw:
                raw = raw[os.write(stderr, raw):]

    def next(self, timeout):
        """The next line; None once the codec's output has ended, or RELEASE_SECONDS after the codec has exited, as a process it started can hold the pipe; queue.Empty when none comes within timeout s."""
        deadline = time.monotonic() + timeout
        while True:
            try:
                return self.lines.get(timeout=max(0.0, min(0.05, deadline - time.monotonic())))
            except queue.Empty:
                pass
            if self.proc.poll() is not None:
                self._saw_exit()
            if time.monotonic() >= self.release_by():
                return None  # what the codec wrote before it exited has been read: let go of the pipe
            if time.monotonic() >= deadline:
                raise queue.Empty

    def write(self, data):
        if not self.input_open:
            return
        try:
            self.proc.stdin.write(data)
            self.proc.stdin.flush()
        except OSError:  # a broken pipe: the codec has exited, and its output and exit status say why
            self.input_open = False

    def send(self, message):
        # Integers stay plain JSON integers: a configure declines 3.0 for an integer key (configure_type_error).
        self.write(frame(CONTROL, json.dumps(message, separators=(",", ":")).encode("utf-8")))

    def halt(self):
        """Decode what is in, end the session and close the codec's input."""
        self.send({"t": "halt"})
        if self.input_open:
            self.input_open = False
            try:
                self.proc.stdin.close()
            except OSError:
                pass

    def stop(self):
        self.proc.kill()
        self.proc.wait()

    def _saw_exit(self):
        """The codec has exited: what uses its pipes has RELEASE_SECONDS more, from the first time the client sees the exit."""
        if self.exited_at is None:
            self.exited_at = time.monotonic()

    def release_by(self):
        """When the client stops waiting for what uses the codec's pipes (time.monotonic()): RELEASE_SECONDS after it saw the codec's exit; never before it has seen it."""
        return math.inf if self.exited_at is None else self.exited_at + RELEASE_SECONDS

    def exit_status(self):
        """The codec's exit status once it exits, within EXIT_SECONDS; None, with the codec stopped, when it does not."""
        try:
            status = self.proc.wait(timeout=EXIT_SECONDS)
            self._saw_exit()
            return status
        except subprocess.TimeoutExpired:
            print("codec_client: wsjt-codec did not exit within %d s: it has hung" % EXIT_SECONDS, file=sys.stderr)
            self.stop()
            return None

    def quit(self):
        """Quit a session that failed: halt, and stop the codec if it does not exit."""
        self.halt()
        self.exit_status()


class Watchdog:
    """Tells a slow decode from a hung codec by the busy lines (configure's busy_events)."""

    def __init__(self, mode):
        self.mode = mode
        self.lock = threading.Lock()
        self.busy = False
        self.busy_mode = None
        self.completed = False      # a pass has completed
        self.last_progress = time.monotonic()
        self.idle_since = time.monotonic()
        self.waiting = {}           # periods due an answer: label -> when the period's last sample is due

    def allowance(self):
        if self.completed:
            return LIVE_ALLOWANCE[self.mode]
        return max(300.0, 4 * PERIOD[self.mode])

    def period_starts(self, label, end):
        """A whole period starts, before its first sample is sent; its last sample is due at end (time.monotonic()), whether or not the codec reads it."""
        with self.lock:
            self.waiting[label] = end

    def _answer(self, label):
        self.waiting.pop(label, None)  # an answer for a period not waiting changes nothing

    def saw(self, line):
        now, kind = time.monotonic(), line.get("t")
        with self.lock:
            if kind == "busy" and line.get("busy"):
                if self.busy_mode is not None:
                    self.completed = True  # a busy line ends, and completes, the pass before it
                self.busy, self.busy_mode, self.last_progress = True, line.get("mode"), now
                self._answer(line.get("period_end"))
            elif kind == "busy":
                if self.busy_mode is not None:
                    self.completed = True  # busy false ends, and completes, the pass before it too
                self.busy, self.idle_since = False, now
            elif kind in ("pass_progress", "pass_skipped") or (kind == "decode" and line.get("mode") == self.busy_mode):
                self.last_progress = now
            elif kind == "decode_finished":
                self.completed, self.last_progress = True, now
                self._answer(line.get("period_end"))

    def hung(self):
        now = time.monotonic()
        with self.lock:
            if self.busy:  # a pass runs: progress lines are due
                return now - self.last_progress > self.allowance()
            if self.waiting:  # nothing runs: a period whose last sample is due is due its answer
                return now - max(self.idle_since, min(self.waiting.values())) > self.allowance()
            return False


def send_audio(codec, audio, seconds, label, live, watchdog):
    """Send audio a period of the given seconds at a time, each period's label in a configure before its first sample, then halt; with no period (JTTY), all of it with no label.

    Live, start on the next UTC period boundary, label each period with its UTC start and send each frame when its last sample is due, as a receiver's audio arrives.
    """
    samples = len(audio) // 2
    period = int(seconds * RATE) if seconds else max(samples, 1)
    if live:
        boundary = (math.floor(time.time() / seconds) + 1) * seconds
        time.sleep(max(0.0, boundary - time.time()))
        label = boundary
    start = time.monotonic()
    if watchdog:  # after halt the codec's exit is due as a period's answer is, from the last sample's scheduled time; no line answers it, and it is registered before the first sample, as a codec that stops reading blocks the writer
        watchdog.period_starts("halt", start + samples / RATE)
    for k, first in enumerate(range(0, samples, period)):
        end = min(first + period, samples)
        if seconds:
            period_label = label + k * seconds
            if watchdog and end - first == period:
                watchdog.period_starts(utc(period_label).replace(":", ""), start + end / RATE)
            codec.send({"t": "configure", "utc": utc(period_label)})
        for i in range(first, end, CHUNK):
            j = min(i + CHUNK, end)
            if live:
                time.sleep(max(0.0, start + j / RATE - time.monotonic()))
            if not codec.input_open:
                return
            codec.write(frame(AUDIO, audio[2 * i:2 * j]))
    codec.halt()


def run(codec, audio, mode, seconds, label, live):
    """Stream the audio, print what the codec decodes and measures, and return the exit status, the messages decoded (JTTY's complete ones among them) and the echo and freqcal lines."""
    watchdog = Watchdog(mode) if live else None
    writer = threading.Thread(target=send_audio, args=(codec, audio, seconds, label, live, watchdog), daemon=True)
    writer.start()
    messages, measured, errors, heard = [], [], 0, time.monotonic()
    while True:
        try:
            line = codec.next(0.5)
            heard = time.monotonic()
        except queue.Empty:
            line = {}
        if line is None:
            break
        if watchdog:
            watchdog.saw(line)
            if watchdog.hung():
                print("codec_client: no progress from the codec in %.0f s: it has hung" % watchdog.allowance(),
                      file=sys.stderr)
                codec.stop()
                return HUNG, messages, measured  # without waiting for the threads, which may be blocked on the codec's pipes
        elif time.monotonic() - heard > silence_bound(seconds):
            print("codec_client: no line from the codec in %d s: it has hung" % silence_bound(seconds), file=sys.stderr)
            codec.stop()
            return HUNG, messages, measured  # without waiting for the threads, which may be blocked on the codec's pipes
        kind = line.get("t")  # other lines, and kinds this client does not know, are skipped
        if kind == "decode":
            messages.append(line["message"])
            print("decode %s %d %s %d %s" % (line["time"], line["snr"], number(line["dt"]), line["freq"], line["message"]), flush=True)
        elif kind == "decode_finished":
            print("finished " + line["period_end"], flush=True)
        elif kind == "echo":  # Echo: one a period
            measured.append(line)
            print("echo %s %s %s %s %d" % (line["time"], number(line["level"]), number(line["snr"]), number(line["dfreq"]), line["quality"]), flush=True)
        elif kind == "freqcal":  # FreqCal: one a measurement
            measured.append(line)
            print("freqcal %s %s %s %s %s" % (line["time"], number(line["freq"]), number(line["error"]), number(line["level"]), number(line["snr"])), flush=True)
        elif kind == "jtty_update" and line["state"] != "growing":  # JTTY: a message's last line; growing ones are its text so far
            if line["state"] == "complete":
                messages.append(line["text"])
            print("jtty %s %s %d %s %s" % (line["state"], number(line["start"]), line["snr"], number(line["freq"]), line["text"]), flush=True)
        elif kind == "error":
            errors += 1
            print("codec_client: codec error: " + json.dumps(line), file=sys.stderr)
    status = codec.exit_status()  # its output has ended, so its exit is due: the watchdog no longer runs to catch a hang
    if status is None:
        return HUNG, messages, measured  # without waiting for the writer, which may be blocked on the codec's pipe
    writer.join(max(0.0, codec.release_by() - time.monotonic()))  # what is left of the codec's RELEASE_SECONDS, then without it: it may be blocked on the codec's input, which a process the codec started holds
    if status != 0:
        print("codec_client: wsjt-codec exited with status %d" % status, file=sys.stderr)
        return FAILED, messages, measured
    return (FAILED if errors else OK), messages, measured


def start(args, kinds, decodes=None):
    """Start the codec and wait for its ready line, which lists the modes it decodes and encodes; decodes, a mode it must decode too."""
    try:
        codec = Codec(args.codec, args.data)
    except OSError as error:
        print("codec_client: cannot start %s: %s" % (args.codec, error), file=sys.stderr)
        return None
    try:
        line = codec.next(READY_SECONDS)
    except queue.Empty:
        line = "none within %d s" % READY_SECONDS
    if (not isinstance(line, dict) or line.get("t") != "ready" or args.mode not in line.get(kinds, [])
            or (decodes is not None and decodes not in line.get("modes", []))):
        print("codec_client: no ready line naming %s in %s: %s" % (args.mode, kinds, json.dumps(line)), file=sys.stderr)
        codec.stop()
        return None
    return codec


def read_wav(path):
    """The samples of a 12000 Hz mono 16-bit PCM WAV file, format 1 or WAVE_FORMAT_EXTENSIBLE with the PCM subformat."""
    with open(path, "rb") as f:
        wav = f.read()
    if wav[:4] != b"RIFF" or wav[8:12] != b"WAVE":
        raise ValueError("%s is not a WAV file" % path)
    pos, fmt = 12, None
    while pos + 8 <= len(wav):
        chunk, size = wav[pos:pos + 4], struct.unpack_from("<I", wav, pos + 4)[0]
        body = wav[pos + 8:pos + 8 + size]
        if chunk == b"fmt ":
            fmt = body
        elif chunk == b"data":
            if fmt is not None and len(fmt) >= 16:
                tag, channels, rate, _, _, bits = struct.unpack_from("<HHIIHH", fmt)
                pcm = tag == 1 or (tag == 0xFFFE and fmt[24:40] == PCM_SUBFORMAT)
                if pcm and (channels, rate, bits) == (1, RATE, 16):
                    return body  # 16-bit little-endian samples, the stream's format
            break
        pos += 8 + size + (size & 1)  # a chunk is padded to an even length
    raise ValueError("%s is not a 12000 Hz mono 16-bit PCM WAV file" % path)


def file_label(path):
    """WSJT-X names a saved period YYMMDD_HHMMSS.wav, or YYMMDD_HHMM.wav in periods of a minute or more: its start, in seconds of the day, or 0."""
    match = re.search(r"_(\d\d)(\d\d)(\d\d)?\.wav$", path, re.IGNORECASE)
    if not match:
        return 0
    h, m, s = (int(x or 0) for x in match.groups())
    return h * 3600 + m * 60 + s if h < 24 and m < 60 and s < 60 else 0


def configure(args, mode, rxfreq):
    """The configure that sets the mode up to decode, with the search keys and the calls."""
    nfa, nfb = SEARCH.get(mode, (200, 4000))
    message = {"t": "configure", "mode": mode}
    if mode != "JTTY":
        message["trperiod"] = args.period if args.period % 1 else int(args.period)  # with the mode, as FreqCal and fast JT9 need it
    if mode in SUBMODES:
        message["submode"] = args.submode
    if mode == "JT9":
        message["fast"] = args.fast  # with every JT9 configure that carries the mode
    message.update(depth=args.depth, mycall=getattr(args, "mycall", ""), **DECODER,
                   nfa=nfa if args.low is None else args.low, nfb=nfb if args.high is None else args.high)
    if rxfreq is not None:
        message["rxfreq"] = rxfreq
    return message


def decode(args):
    try:
        audio = read_wav(args.wav)
    except (OSError, ValueError) as error:
        print("codec_client: " + str(error), file=sys.stderr)
        return USAGE
    codec = start(args, "modes")
    if codec is None:
        return FAILED
    message = configure(args, args.mode, args.freq)
    if args.command == "live":
        message.update(live_audio=True, busy_events=True)
    codec.send(message)
    seconds = None if args.mode == "JTTY" else args.period
    status, _, _ = run(codec, audio, args.mode, seconds, file_label(args.wav), args.command == "live")
    return status


def render_request(args):
    """The render request for the mode."""
    request = {"t": "render", "id": 1}
    if args.mode == "JTTY":  # JTTY's requests carry no mode
        request.update(text=" ".join(args.text), freq=args.freq, rate=RATE)
        return request
    request["mode"] = args.mode
    if args.mode in ("FOX", "SUPERFOX"):
        request["slots"] = args.text  # one message a slot
    elif args.mode == "TUNE":
        request["seconds"] = int(args.period)  # a steady tone for the period FreqCal measures it in
    elif args.mode == "ECHO":
        request["kind"] = "tone"  # WSJT-X's Fixed Tone
    else:
        request["text"] = " ".join(args.text)
    if args.mode in SUBMODES:
        request["submode"] = args.submode
    if args.mode in PERIODS or args.fast:
        request["trperiod"] = int(args.period)
    if args.fast:
        request["fast"] = True
    request.update(freq=args.freq, rate=RATE)
    return request


def request_render(codec, args):
    """Send the render request and return what it sent, its sample count and its audio, or the exit status when it fails."""
    codec.send(render_request(args))
    sent, samples, pcm = [], 0, []
    while True:  # the reply: encoded (JTTY: a segment line a segment), the pcm lines, then rendered; or rejected
        try:
            line = codec.next(silence_bound(None))  # a reply waits for no decode
        except queue.Empty:
            print("codec_client: no line from the codec in %d s: it has hung" % silence_bound(None), file=sys.stderr)
            codec.stop()
            return HUNG
        if line is None or line.get("t") in ("rejected", "error") or (line.get("t") == "rendered" and not sent):
            print("codec_client: render failed: " + json.dumps(line), file=sys.stderr)
            codec.quit()
            return FAILED
        if line.get("t") == "encoded":
            samples = line["samples"]
            if args.mode == "FOX":
                sent = [slot["msgsent"] for slot in line["slots"]]
            elif args.mode == "SUPERFOX":
                sent = [slot.upper() for slot in args.text]  # SuperFox's encoded line has no msgsent: WSJT-X shows the slots as written
            else:
                sent = [line["msgsent"]]
        elif line.get("t") == "segment":  # JTTY: each segment's text and samples
            sent.append(line["text"])
            samples += line["samples"]
        elif line.get("t") == "pcm":
            pcm.append(base64.b64decode(line["data"]))  # int16 little-endian samples at the requested rate
        elif line.get("t") == "rendered":
            return sent, samples, b"".join(pcm)


def render(args):
    created = False
    if args.out:
        created = not os.path.exists(args.out)
        try:
            open(args.out, "ab").close()  # fail before starting the codec if the file cannot be written; "ab" keeps an existing one
        except OSError as error:
            print("codec_client: " + str(error), file=sys.stderr)
            return USAGE
    codec = start(args, "encodes", DECODED_AS.get(args.mode, args.mode))
    reply = request_render(codec, args) if codec else FAILED
    if not isinstance(reply, tuple):
        if created:
            os.remove(args.out)
        return reply
    sent, samples, audio = reply
    print("rendered %d %s" % (samples, " | ".join(sent)), flush=True)
    if args.out:
        try:
            with wave.open(args.out, "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(RATE)
                w.writeframes(audio)
        except OSError as error:
            print("codec_client: " + str(error), file=sys.stderr)
            if created:
                os.remove(args.out)
            codec.quit()
            return FAILED
    # Decode it back, over noise: the audio where WSJT-X starts it in its period; JTTY's after a second, with a second after it.
    mode = DECODED_AS.get(args.mode, args.mode)
    if args.mode == "JTTY":
        seconds, signal = None, over_noise(bytes(2 * RATE) + audio, len(audio) / 2 / RATE + 2)
    else:
        seconds = args.period
        signal = over_noise(bytes(2 * int(tx_start(args.mode, seconds, args.fast) * RATE)) + audio, seconds)
    if mode == "FST4":
        args.low, args.high = args.freq - 200, args.freq + 200  # its F Low and F High around the render
    message = configure(args, mode, None if mode == "MSK144" else args.freq)  # MSK144's Rx Freq is its tones' center
    if args.mode == "SUPERFOX":
        message.update(contest_type="hound", superfox_mode=True, even_sequence=True)  # a SuperFox Hound decodes it
    if args.mode == "ECHO":
        message.update(live_audio=True, echo_freq=args.freq)  # the echo's parameters from the keys, not from the audio's first samples as a saved file carries them
    codec.send(message)
    status, messages, measured = run(codec, signal, mode, seconds, 0, False)
    if status != OK:
        return status
    if args.mode == "TUNE":
        missing = [] if any(m["t"] == "freqcal" and m["error"] is not None and abs(m["error"]) < 1 for m in measured) else ["a measurement within 1 Hz of %d Hz" % args.freq]
    elif args.mode == "ECHO":
        missing = [] if any(m["t"] == "echo" and m["snr"] is not None and m["snr"] > 0 for m in measured) else ["an echo above the noise"]
    else:
        missing = [text for text in sent if text.split() not in [m.split() for m in messages]]
    if missing:
        print("codec_client: round trip failed: no decode of " + " | ".join(missing), file=sys.stderr)
        return FAILED
    return OK


def main():
    parser = argparse.ArgumentParser(description="A wsjt-codec client.", allow_abbrev=False)
    commands = parser.add_subparsers(dest="command", required=True)
    for name, text, modes in (("decode", "decode a WAV file", DECODES), ("live", "replay a WAV file as live audio", ("FT8", "FT4")),
                              ("render", "render a message and decode it back", ENCODES)):
        command = commands.add_parser(name, help=text, allow_abbrev=False)
        command.add_argument("--codec", required=True, help="the wsjt-codec program")
        command.add_argument("--data", default=".", help="the codec's data and working directory (default: .)")
        command.add_argument("--mode", default="FT8", choices=modes)
        command.add_argument("--depth", type=int, default=3, choices=(1, 2, 3), help="decoding depth (default: 3)")
        if name != "live":
            command.add_argument("--submode", default="A", choices=list("ABCDEFGH"), help="JT4, JT9, JT65 and Q65: the submode (default: A)")
            command.add_argument("--period", type=float, help="the period, s (default: the mode's)")
            command.add_argument("--fast", action="store_true", help="JT9: fast JT9")
        if name == "render":
            command.add_argument("--freq", type=int, help="the lowest tone, Hz (default: 1500; MSK144 1000, SUPERFOX 750)")
            command.add_argument("--out", help="also write the rendered audio to this WAV file")
            command.add_argument("text", nargs="*", help="the message; FOX and SUPERFOX, one a slot; TUNE and ECHO, none")
        else:
            if name == "decode":
                command.add_argument("--freq", type=int, help="the receive frequency, Hz (default: the mode's)")
                command.add_argument("--low", type=int, help="the lowest frequency searched, Hz (default: 200; FST4 600)")
                command.add_argument("--high", type=int, help="the highest frequency searched, Hz (default: 4000; FST4 1400)")
                command.add_argument("--mycall", default="", help="the station's call, for the decoders' a priori decoding (default: none)")
            command.add_argument("wav", help="12000 Hz mono 16-bit PCM WAV file")
    args = parser.parse_args()
    if args.command == "live":
        args.period, args.submode, args.fast, args.freq, args.low, args.high = PERIOD[args.mode], "A", False, None, None, None
    elif args.period is None:
        args.period = PERIOD.get(args.mode)
    elif not (math.isfinite(args.period) and args.period * RATE >= 1):
        parser.error("--period must be a number of seconds that holds a sample at least")
    if args.command == "render":
        args.low = args.high = None
        if args.freq is None:
            args.freq = {"MSK144": 1000, "SUPERFOX": 750}.get(args.mode, 1500)  # MSK144's tones are at 1000 and 2000 Hz, and SuperFox goes at 750
        wants = 0 if args.mode in ("TUNE", "ECHO") else None if args.mode in ("FOX", "SUPERFOX", "JTTY") else 1
        if (wants is not None and len(args.text) != wants) or (wants is None and not args.text):
            parser.error("render --mode %s takes %s" % (args.mode, {0: "no TEXT", 1: "one TEXT"}.get(wants, "one TEXT or more")))
    return render(args) if args.command == "render" else decode(args)


if __name__ == "__main__":
    sys.exit(main())
