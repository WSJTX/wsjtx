#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A wsjt-codec client: decode a WAV file, replay one as live audio, or render a message.

  codec_client.py decode --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
  codec_client.py live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
  codec_client.py render --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] [--freq HZ] [--out WAV] TEXT

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
PERIOD = {"FT8": 15.0, "FT4": 7.5}   # period, s
TX_START = {"FT8": 0.5, "FT4": 0.3}  # where WSJT-X starts its transmission in the period, s
LIVE_ALLOWANCE = {"FT8": 10.0, "FT4": 6.0}  # a live pass's time between progress lines, once one has completed
READY_SECONDS = 30                   # how long the client waits for ready
EXIT_SECONDS = 10                    # how long the codec has to exit once its output ends, or after halt on a failure
CHUNK = 1200                         # samples in an audio frame: 0.1 s
# Sent with the mode: these examples have no station, so no calls (the decoders use the starting calls otherwise), and a configure that carries the mode resets nfa, nfb and ntol.
DECODER = {"mycall": "", "his_call": "", "his_grid": "", "nfa": 200, "nfb": 4000, "ntol": 1000}

AUDIO, CONTROL = 0x01, 0x02          # frame types
# Session header: magic, sample format 0 (int16), 1 channel, rate in kHz (uint16).
HEADER = b"WSJT" + struct.pack("<BBH", 0, 1, RATE // 1000)
# WAVE_FORMAT_EXTENSIBLE's subformat GUID for PCM, as a WAV file stores it.
PCM_SUBFORMAT = bytes.fromhex("0100000000001000800000aa00389b71")

OK, FAILED, USAGE, HUNG = 0, 1, 2, 3  # exit status


def frame(kind, body):
    """A frame: type (1 byte), body length (uint32 little-endian), body."""
    return struct.pack("<BI", kind, len(body)) + body


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

    def write(self, data):
        if not self.input_open:
            return
        try:
            self.proc.stdin.write(data)
            self.proc.stdin.flush()
        except OSError:  # a broken pipe: the codec has exited, and its output and exit status say why
            self.input_open = False

    def send(self, message):
        # Integers stay plain JSON integers: a configure ignores or misreads 3.0, without an error.
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

    def exit_status(self):
        """The codec's exit status once it exits, within EXIT_SECONDS; None, with the codec stopped, when it does not."""
        try:
            return self.proc.wait(timeout=EXIT_SECONDS)
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


def send_audio(codec, audio, mode, label, live, watchdog):
    """Send audio a period at a time, each period's label in a configure before its first sample, then halt.

    Live, start on the next UTC period boundary, label each period with its UTC start and send each frame when its last sample is due, as a receiver's audio arrives.
    """
    period = int(PERIOD[mode] * RATE)
    samples = len(audio) // 2
    if live:
        boundary = (math.floor(time.time() / PERIOD[mode]) + 1) * PERIOD[mode]
        time.sleep(max(0.0, boundary - time.time()))
        label = boundary
    start = time.monotonic()
    if watchdog:  # after halt the codec's exit is due as a period's answer is, from the last sample's scheduled time; no line answers it, and it is registered before the first sample, as a codec that stops reading blocks the writer
        watchdog.period_starts("halt", start + samples / RATE)
    for k, first in enumerate(range(0, samples, period)):
        period_label = label + k * PERIOD[mode]
        end = min(first + period, samples)
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


def run(codec, audio, mode, label, live):
    """Stream the audio, print the decodes, and return the exit status and the messages decoded."""
    watchdog = Watchdog(mode) if live else None
    writer = threading.Thread(target=send_audio, args=(codec, audio, mode, label, live, watchdog), daemon=True)
    writer.start()
    messages, errors = [], 0
    while True:
        try:
            line = codec.lines.get(timeout=0.5)
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
                return HUNG, messages  # without waiting for the threads, which may be blocked on the codec's pipes
        kind = line.get("t")  # other lines, and kinds this client does not know, are skipped
        if kind == "decode":
            messages.append(line["message"])
            dt = "nan" if line["dt"] is None else "%.1f" % line["dt"]  # null: a dt that is not finite
            print("decode %s %d %s %d %s" % (line["time"], line["snr"], dt, line["freq"], line["message"]), flush=True)
        elif kind == "decode_finished":
            print("finished " + line["period_end"], flush=True)
        elif kind == "error":
            errors += 1
            print("codec_client: codec error: " + json.dumps(line), file=sys.stderr)
    status = codec.exit_status()  # its output has ended, so its exit is due: the watchdog no longer runs to catch a hang
    if status is None:
        return HUNG, messages  # without waiting for the writer, which may be blocked on the codec's pipe
    writer.join()
    if status != 0:
        print("codec_client: wsjt-codec exited with status %d" % status, file=sys.stderr)
        return FAILED, messages
    return (FAILED if errors else OK), messages


def start(args, kinds):
    """Start the codec and wait for its ready line, which lists the modes it decodes and encodes."""
    try:
        codec = Codec(args.codec, args.data)
    except OSError as error:
        print("codec_client: cannot start %s: %s" % (args.codec, error), file=sys.stderr)
        return None
    try:
        line = codec.lines.get(timeout=READY_SECONDS)
    except queue.Empty:
        line = "none within %d s" % READY_SECONDS
    if not isinstance(line, dict) or line.get("t") != "ready" or args.mode not in line.get(kinds, []):
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
    """WSJT-X names a saved period YYMMDD_HHMMSS.wav: its start, in seconds of the day, or 0."""
    match = re.search(r"_(\d\d)(\d\d)(\d\d)\.wav$", path, re.IGNORECASE)
    if not match:
        return 0
    h, m, s = (int(x) for x in match.groups())
    return h * 3600 + m * 60 + s if h < 24 and m < 60 and s < 60 else 0


def decode(args):
    try:
        audio = read_wav(args.wav)
    except (OSError, ValueError) as error:
        print("codec_client: " + str(error), file=sys.stderr)
        return USAGE
    codec = start(args, "modes")
    if codec is None:
        return FAILED
    configure = {"t": "configure", "mode": args.mode, "depth": args.depth, **DECODER}
    if args.command == "live":
        configure.update(live_audio=True, busy_events=True)
    codec.send(configure)
    status, _ = run(codec, audio, args.mode, file_label(args.wav), args.command == "live")
    return status


def request_render(codec, args):
    """Send the render request and return its encoded line and audio, or None when it fails."""
    codec.send({"t": "render", "id": 1, "mode": args.mode, "text": args.text, "freq": args.freq, "rate": RATE})
    encoded, pcm = None, []
    while True:  # the reply: encoded, its pcm lines, then rendered; or rejected
        line = codec.lines.get()
        if line is None or line.get("t") in ("rejected", "error"):
            print("codec_client: render failed: " + json.dumps(line), file=sys.stderr)
            codec.quit()
            return None
        if line.get("t") == "encoded":
            encoded = line
        elif line.get("t") == "pcm":
            pcm.append(base64.b64decode(line["data"]))  # int16 little-endian samples at the requested rate
        elif line.get("t") == "rendered":
            return encoded, b"".join(pcm)


def render(args):
    created = False
    if args.out:
        created = not os.path.exists(args.out)
        try:
            open(args.out, "ab").close()  # fail before starting the codec if the file cannot be written; "ab" keeps an existing one
        except OSError as error:
            print("codec_client: " + str(error), file=sys.stderr)
            return USAGE
    codec = start(args, "encodes")
    reply = request_render(codec, args) if codec else None
    if reply is None:
        if created:
            os.remove(args.out)
        return FAILED
    encoded, audio = reply
    print("rendered %d %s" % (encoded["samples"], encoded["msgsent"]), flush=True)
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
    # Decode it back: the audio where WSJT-X starts it, in a period of silence.
    period = int(PERIOD[args.mode] * RATE)
    offset = int(TX_START[args.mode] * RATE)
    signal = (bytes(2 * offset) + audio)[:2 * period]
    codec.send({"t": "configure", "mode": args.mode, "depth": args.depth, **DECODER})
    status, messages = run(codec, signal + bytes(2 * period - len(signal)), args.mode, 0, False)
    if status == OK and encoded["msgsent"].split() not in [m.split() for m in messages]:
        print("codec_client: round trip failed: no decode of " + encoded["msgsent"], file=sys.stderr)
        status = FAILED
    return status


def main():
    parser = argparse.ArgumentParser(description="A wsjt-codec client.", allow_abbrev=False)
    commands = parser.add_subparsers(dest="command", required=True)
    for name, text in (("decode", "decode a WAV file"), ("live", "replay a WAV file as live audio"),
                       ("render", "render a message and decode it back")):
        command = commands.add_parser(name, help=text, allow_abbrev=False)
        command.add_argument("--codec", required=True, help="the wsjt-codec program")
        command.add_argument("--data", default=".", help="the codec's data and working directory (default: .)")
        command.add_argument("--mode", default="FT8", choices=sorted(PERIOD))
        command.add_argument("--depth", type=int, default=3, choices=(1, 2, 3), help="decoding depth (default: 3)")
        if name == "render":
            command.add_argument("--freq", type=int, default=1500, help="the lowest tone, Hz (default: 1500)")
            command.add_argument("--out", help="also write the rendered audio to this WAV file")
            command.add_argument("text", help="the message")
        else:
            command.add_argument("wav", help="12000 Hz mono 16-bit PCM WAV file")
    args = parser.parse_args()
    return render(args) if args.command == "render" else decode(args)


if __name__ == "__main__":
    sys.exit(main())
