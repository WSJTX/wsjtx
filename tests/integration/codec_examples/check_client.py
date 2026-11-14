#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a codec example client against the command line every example implements (doc/codec/examples/README.md).

  check_client.py --codec PATH --sample WAV --expected FILE --work-dir DIR [--live] [--timeout S] -- COMMAND...

COMMAND... starts the client; the check adds the subcommand and its options, and runs:
  decode: the sample at depth 3: exit status 0, every message of the expected file (a jt9 decode list), each decode labeled with the time in the sample's name, and finished for it; and the same lines, in any order, as the Python example prints for the same run, which pins every field's format. The sample rewritten as WAVE_FORMAT_EXTENSIBLE with the PCM subformat must print the same lines; with the float subformat, exit status 2.
  render: an FT8 and an FT4 message: exit status 0, the rendered line with the message as sent, a decode of it, and --out's WAV holding the samples the rendered line counts. --out in a missing directory: exit status 2. A refused render: exit status 1, and no --out file left behind.
  On POSIX, against a stand-in codec (fake_codec below), which checks what the client sends (OMP_STACKSIZE, -a, -t and -r, the header, an integer depth in every configure, a label before each period, halt), writes about four pipe buffers to stderr before ready, and logs to fake_codec.log in the client's --data directory:
  decode: lines that are not JSON objects are skipped, and a decode whose dt is null prints DT nan; exit status 1 after an error line (decode-error) and after the stand-in's exit status 1 (decode-status).
  render: exit status 1 when the rendered audio, a second of silence, decodes to nothing, with --out's WAV kept (render).
  NO_EXIT, all at once, each exit due 10 s after the logged event (from 0.5 s before to 2 s after): decode exits 3 when the stand-in, after halt, sends lines for 3 s more, then ends its standard output and does not exit (decode-noexit, from the end of its output); when it stops reading its input after 1 s of audio, so the client's writer blocks before halt, sends lines for 3 s, then ends its standard output and does not exit (decode-stopread); render exits 1 when the stand-in refuses the render and does not exit after halt (render-noexit, from halt).
  live (--live), in FT4, whose allowance is 6 s once a pass has completed: the real codec in real time, with exit status 0 and every expected message; then, at once, the LIVE_FAKES stand-ins. Exit status 3 within the allowance plus 2 s of the due time when the stand-in: hangs while busy and goes silent (hung), so the client must check its deadlines while no line comes, and does so in FT8, whose allowance is 10 s (hung8); hangs while busy, sending only lines that are not progress (noisy); runs one pass that ends with busy false and no decode_finished, then sends nothing (onepass; a busy false completes a pass, so due at the second period's scheduled end plus the allowance); answers every period, then hangs after halt (afterhalt; due the allowance after halt); answers the first period after 1 s of its audio, then stops reading, so the client's writer blocks before it sends halt (answerstop; due at the last sample's scheduled time plus the allowance); answers the first period and never the second (idle; due at the second's scheduled end plus the allowance); does so after answering the second period's label before the period starts, as a period a day earlier would have been (reused); stops reading its input during the second period (stopread); answers the first period only after the second is registered, and never the second (crossed). Exit status 0 when it: is slow but sends progress lines (slow); runs its first pass, before any completes, with no progress for longer than the allowance (slowstart); keeps a pass busy, with progress, until after the second period's scheduled end plus the allowance, then answers that period 1 s after its busy false (latebusy); answers each period before all its audio is sent (early). Exit status 1 within 32 s when it never sends ready (noready). The stand-in sends busy and pass_progress lines only after a configure turns on busy_events, and logs a problem unless live_audio is on too. In the HANGS a child of the stand-in holds its output pipes open after the client stops it, so a client that waits for them misses its due time.
Every stdout line must be a decode, finished or rendered line, and the stand-in must not be running 2 s after the client has exited. Messages match by text with blanks collapsed; decodes beyond the expected messages are allowed. After each run, and when the check is interrupted, what the client started is stopped: on POSIX its process group, on Windows its process tree while the client runs. Exit status 0 when every run passes.
"""
import argparse
import base64
import concurrent.futures
import json
import os
import re
import signal
import struct
import subprocess
import sys
import threading
import time
import wave

DECODE = re.compile(r"decode (\d{6}) (-?\d+) (-?\d+\.\d|nan) (-?\d+) (\S.*)")
FINISHED = re.compile(r"finished (\d{6})")
RENDERED = re.compile(r"rendered (\d+) (\S.*)")
RENDERS = (("FT8", "CQ K1ABC FN42"), ("FT4", "K1ABC W9XYZ R-07"))
REFUSED = "THIS FREE TEXT IS FAR TOO LONG FOR ANY FT8 MESSAGE"  # more than the 37 bytes a message takes
PCM, FLOAT = (bytes.fromhex("%02x00000000001000800000aa00389b71" % tag) for tag in (1, 3))  # WAVE_FORMAT_EXTENSIBLE subformats
HERE = os.path.dirname(os.path.abspath(__file__))
REFERENCE = os.path.join(HERE, "..", "..", "..", "doc", "codec", "examples", "python", "codec_client.py")

# The stand-in's FT4 session.
FT4_PERIOD, FT4_SAMPLES, FT4_ALLOWANCE = 7.5, 90000, 6.0
HEADER = b"WSJT" + struct.pack("<BBH", 0, 1, 12)
EARLY, LATE = 0.5, 2.0  # how far a timed exit may come before and after its due time, s
# Live stand-in scenarios: the exit status expected, and when the exit is due: the logged event and the seconds after it.
LIVE_FAKES = {
    "hung": (3, "progress", FT4_ALLOWANCE),
    "hung8": (3, "progress", 10.0),
    "noisy": (3, "progress", FT4_ALLOWANCE),
    "onepass": (3, "period2", FT4_PERIOD + FT4_ALLOWANCE),
    "afterhalt": (3, "halt", FT4_ALLOWANCE),
    "answerstop": (3, "period1", 2 * FT4_PERIOD + FT4_ALLOWANCE),
    "slow": (0, None, None),
    "slowstart": (0, None, None),
    "idle": (3, "period2", FT4_PERIOD + FT4_ALLOWANCE),
    "stopread": (3, "period2", FT4_PERIOD + FT4_ALLOWANCE),
    "reused": (3, "period2", FT4_PERIOD + FT4_ALLOWANCE),
    "crossed": (3, "period2", FT4_PERIOD + FT4_ALLOWANCE),
    "latebusy": (0, None, None),
    "early": (0, None, None),
    "noready": (1, "started", 30.0),
}
HANGS = ("hung", "hung8", "noisy", "onepass", "afterhalt", "answerstop", "idle", "stopread", "reused", "crossed", "noready")
# Stand-in scenarios without --live: the exit status expected.
FAKES = {"decode": 0, "decode-error": 1, "decode-status": 1}
# Stand-in scenarios without --live that do not exit: the exit status expected, and when the client's exit is due: the logged event and the seconds after it.
EXIT_SECONDS = 10.0  # how long a client gives the codec to exit once its output ends, or after halt on a failure
NO_EXIT = {"decode-noexit": (3, "closed", EXIT_SECONDS), "decode-stopread": (3, "closed", EXIT_SECONDS), "render-noexit": (1, "halt", EXIT_SECONDS)}
TAIL = 3.0  # how long decode-noexit and decode-stopread send lines after their input ends or they stop reading it, s
RUNNING = set()  # the clients running now: an interrupted check stops them, and all they started


def words(text):
    return " ".join(text.split())


def expected_messages(path):
    """The messages of a jt9 decode list: time, snr, dt, freq, a sync marker, then the message."""
    with open(path) as lines:
        return [words(line.split(None, 5)[5]) for line in lines
                if line.strip() and not line.startswith("#") and not line.lstrip().startswith("<")]


class Run:
    """One client run: its exit status (None when it timed out), stdout lines and failures."""

    def __init__(self, name, command, work, data, timeout, env=None):
        self.name, self.data, self.failures, self.timing = name, data, [], ""
        log = os.path.join(data, "fake_codec.log")
        if os.path.exists(log):
            os.remove(log)
        started = time.monotonic()
        # On POSIX the client runs in a process group of its own, which the codec or stand-in it starts joins: stopping the group stops them all.
        client = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env,
                                  start_new_session=os.name == "posix")
        RUNNING.add(client)
        try:
            out, err = client.communicate(timeout=timeout)
            self.status = client.returncode
        except subprocess.TimeoutExpired:
            stop_tree(client)
            try:
                out, err = client.communicate(timeout=5)
            except subprocess.TimeoutExpired as expired:  # a process outside the group holds the client's pipes
                out, err = expired.stdout or b"", expired.stderr or b""
            self.status = None
            self.fail("no exit within %d s" % timeout)
        self.ended, self.seconds = time.time(), time.monotonic() - started
        self.left_running = self.status is not None and stand_in_running(data)
        stop_tree(client)  # what the client left running, such as a hung codec or a stand-in's child, is stopped before the next run
        RUNNING.discard(client)
        out, err = out.decode("utf-8", "replace"), err.decode("utf-8", "replace")
        for suffix, text in (("out", out), ("err", err)):
            with open(os.path.join(work, "%s.%s" % (name, suffix)), "w") as text_log:
                text_log.write(text)
        self.err = err
        self.lines = out.splitlines()
        self.decodes = [m for m in map(DECODE.fullmatch, self.lines) if m]
        self.finished = [m.group(1) for m in map(FINISHED.fullmatch, self.lines) if m]
        self.rendered = [m for m in map(RENDERED.fullmatch, self.lines) if m]
        for line in self.lines:
            if not (DECODE.fullmatch(line) or FINISHED.fullmatch(line) or RENDERED.fullmatch(line)):
                self.fail("stdout line in no stated form: %r" % line)

    def fail(self, text):
        self.failures.append(text)

    def expect_status(self, status):
        if self.status is not None and self.status != status:
            self.fail("exit status %d, not %d" % (self.status, status))

    def expect_messages(self, messages):
        found = {words(m.group(5)) for m in self.decodes}
        missing = [m for m in messages if m not in found]
        if missing:
            self.fail("%d of %d messages missing: %s" % (len(missing), len(messages), "; ".join(missing)))

    def expect_lines_of(self, reference):
        if sorted(self.lines) != sorted(reference.lines):
            differ = sorted(set(self.lines) ^ set(reference.lines))
            self.fail("lines differ from the Python example's: %s" % "; ".join(differ[:6]))

    def stand_in(self, event=None, delay=None):
        """Fail on the problems the stand-in logged, and, given an event it logged, unless the client exited delay s after it."""
        entries = []
        try:
            with open(os.path.join(self.data, "fake_codec.log")) as log:
                entries = [json.loads(line) for line in log]
        except OSError:
            self.fail("the stand-in left no fake_codec.log in --data, its working directory")
        for entry in entries:
            if "problem" in entry:
                self.fail("stand-in: " + entry["problem"])
        if self.left_running:
            self.fail("the client exited and left the stand-in running")
        times = {entry["event"]: entry["time"] for entry in entries if "event" in entry}
        if event and event in times and self.status is not None:
            late = self.ended - times[event] - delay
            self.timing = "exited %+.1f s from its due time" % late
            if not -EARLY <= late <= LATE:
                self.fail("exited %.1f s after %s, not %.0f s (from -%.1f to +%.1f)" % (late + delay, event, delay, EARLY, LATE))
        elif event and entries and event not in times:
            self.fail("the stand-in logged no %s" % event)

    def report(self):
        verdict = "FAIL" if self.failures else "PASS"
        print("%s %s (%.1f s, exit status %s, %d decodes%s)" % (verdict, self.name, self.seconds, self.status,
                                                                 len(self.decodes), self.timing and ", " + self.timing))
        for failure in self.failures:
            print("    " + failure)
        err = [line for line in self.err.splitlines() if line != "stand-in: diagnostics"]
        if self.failures and err:
            print("    stderr ends:\n" + "\n".join("      " + line for line in err[-15:]))
        return not self.failures


def stop_tree(process):
    """Stop a client and every process it started: on POSIX its process group, on Windows its process tree while the client runs."""
    if os.name == "posix":
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass  # the group has no process left
    elif process.poll() is None:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if process.poll() is None:
        process.kill()


def stand_in_running(data):
    """Whether the stand-in that logged to data, if any, still runs, 2 s after the client exited: a zombie, ended but not yet reaped, does not run."""
    try:
        with open(os.path.join(data, "fake_codec.log")) as log:
            pid = next(json.loads(line)["pid"] for line in log if '"pid"' in line)
    except (OSError, StopIteration, ValueError, KeyError):
        return False  # no stand-in ran
    deadline = time.monotonic() + 2
    while True:
        if os.path.isdir("/proc"):
            try:
                with open("/proc/%d/stat" % pid) as stat:
                    state = stat.read().rsplit(")", 1)[1].split()[0]
            except (OSError, IndexError):
                state = ""
        else:
            try:
                state = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, universal_newlines=True).stdout.strip()
            except OSError:
                return False  # this system cannot tell
        if not state or state.startswith("Z"):
            return False
        if time.monotonic() > deadline:
            return True
        time.sleep(0.05)


def interrupted(signum, frame):
    """Stop the clients running, and all they started, then end the check."""
    for client in list(RUNNING):
        stop_tree(client)
    sys.exit(128 + signum)


def label_plus(label, seconds):
    """An HHMMSS label the given seconds later in the day."""
    t = (int(label[:2]) * 3600 + int(label[2:4]) * 60 + int(label[4:]) + seconds) % 86400
    return "%02d%02d%02d" % (t // 3600, t // 60 % 60, t % 60)


def fake_codec():
    """A stand-in for wsjt-codec: CODEC_EXAMPLE_FAKE names the scenario, one of FAKES, render, NO_EXIT or LIVE_FAKES, in FT8 for hung8 and FT4 otherwise.

    It checks what the client sends and logs each problem, and the times the check measures from, to fake_codec.log in its working directory, and writes about four pipe buffers to stderr before ready. It sends busy and pass_progress lines only after a configure turns on busy_events. hung, hung8, onepass, afterhalt and answerstop go silent; noisy, idle, stopread, reused and crossed keep sending lines that are not progress or an answer until the client stops them. In the HANGS a child of the stand-in holds its output pipes open after the stand-in is stopped. The NO_EXIT scenarios end their standard output, or refuse a render, and then do not exit; they keep standard error open.
    """
    scenario = os.environ.get("CODEC_EXAMPLE_FAKE", "")
    mode = "FT8" if scenario == "hung8" else "FT4"
    period_samples = 2 * FT4_SAMPLES if mode == "FT8" else FT4_SAMPLES
    lock = threading.Lock()
    state = {"label": None, "first_label": None, "period2": None, "labels": 0, "in_period": None, "busy_events": False, "live_audio": False, "halt": False, "render": None, "answered": False}
    audio, ended, rendering, stopped = threading.Event(), threading.Event(), threading.Event(), threading.Event()

    def log(**entry):
        with lock, open("fake_codec.log", "a") as f:
            f.write(json.dumps(entry) + "\n")

    def emit(text):
        with lock:
            try:
                print(text if isinstance(text, str) else json.dumps(dict(v=1, **text)), flush=True)
            except OSError:
                pass  # the client has gone: run on, as a hung codec would, until the client or the check stops it

    def busy(label):
        if state["busy_events"]:
            emit({"t": "busy", "busy": True, "mode": mode, "period_end": label, "nzhsym": 21})

    def idle():
        if state["busy_events"]:
            emit({"t": "busy", "busy": False})

    def finished(label):
        emit({"t": "decode_finished", "period_end": label, "synced": 0, "decoded": 0, "average": 0})

    def progress(label):
        if state["busy_events"]:
            emit({"t": "pass_progress", "mode": mode, "period_end": label, "nzhsym": 21})

    def answer(busy_after, label=None):
        label = label or state["label"]
        busy(label)
        finished(label)
        busy(label) if busy_after else idle()

    def not_an_answer():
        while True:  # a decode in another mode, every second, until the client stops the stand-in
            time.sleep(1)
            emit({"t": "decode", "mode": "FT8", "time": state["label"], "snr": -7, "dt": 0.1, "freq": 1234, "message": "NOT AN ANSWER", "ap": 0})
            if scenario == "noisy":
                emit({"t": "novel_line"})

    def read_input():
        stdin = sys.stdin.buffer
        if stdin.read(8) != HEADER:
            log(problem="the session header is not WSJT, int16, mono, 12 kHz")
        first = True
        while True:
            if scenario == "stopread" and state["labels"] == 2 and state["in_period"] >= FT4_SAMPLES // 3 or state["answered"]:
                return  # stops reading its input
            if scenario == "decode-stopread" and (state["in_period"] or 0) >= 12000:
                stopped.set()
                return  # stops reading its input after 1 s of audio
            head = stdin.read(5)
            if len(head) < 5:
                break
            kind, size = struct.unpack("<BI", head)
            body = stdin.read(size)
            if kind == 2:
                message = json.loads(body)
                if message.get("t") == "halt":
                    state["halt"] = True
                elif message.get("t") == "render":
                    state["render"] = message
                    rendering.set()
                elif message.get("t") == "configure":
                    if first and (message.get("mode") != mode or type(message.get("depth")) is not int):
                        log(problem="the first configure does not set mode %s and an integer depth: %s" % (mode, body.decode()))
                    if "depth" in message and type(message["depth"]) is not int:
                        log(problem="a configure's depth is not an integer: %s" % body.decode())
                    first = False
                    state["busy_events"] = state["busy_events"] or message.get("busy_events") is True
                    state["live_audio"] = state["live_audio"] or message.get("live_audio") is True
                    if "utc" in message:
                        if state["in_period"] not in (None, period_samples):
                            log(problem="a label inside a period")
                        state["label"], state["in_period"] = message["utc"].replace(":", ""), 0
                        state["labels"] += 1
                        state["first_label"] = state["first_label"] or state["label"]
                        if state["labels"] == 1:
                            log(event="period1", time=time.time())
                        if state["labels"] == 2:
                            state["period2"] = time.time()
                            log(event="period2", time=state["period2"])
            elif kind == 1:
                if state["in_period"] is None or state["in_period"] + size // 2 > period_samples:
                    log(problem="audio with no label for its period, or across a period's end")
                state["in_period"] = (state["in_period"] or 0) + size // 2
                audio.set()
        ended.set()

    log(event="started", time=time.time(), pid=os.getpid())
    if not os.environ.get("OMP_STACKSIZE"):
        log(problem="OMP_STACKSIZE is not set")
    args, here = sys.argv[1:], os.path.realpath(os.getcwd())
    for option in ("-a", "-t", "-r"):
        if option not in args[:-1] or os.path.realpath(args[args.index(option) + 1]) != here:
            log(problem="%s is not the working directory" % option)
    threading.Thread(target=read_input, daemon=True).start()
    if scenario in HANGS:
        # A process that holds the stand-in's output pipes open for 40 s after the client stops it, as a codec's own child could: the client must not wait for them.
        subprocess.Popen([sys.executable, "-c", "import time; time.sleep(40)"], stdin=subprocess.DEVNULL)
    if scenario == "noready":
        ended.wait(60)
        time.sleep(70)  # runs on after its input ends, as a hung codec would, until the client stops it
        return 0
    sys.stderr.write("stand-in: diagnostics\n" * 12000)  # about four pipe buffers: a client that does not read stderr as it comes never sees ready
    sys.stderr.flush()
    emit({"t": "ready", "modes": [mode], "encodes": [mode], "protocol": 1})
    if scenario == "render-noexit":  # refuses the render, then, after halt, neither exits nor ends its output
        if not rendering.wait(30):
            log(problem="no render request")
            return 0
        emit({"t": "rejected", "id": state["render"].get("id"), "reason": "bad_request", "detail": "stand-in"})
        while not state["halt"] and not ended.wait(0.05):
            pass
        if not state["halt"]:  # the input's reader sees halt before the end of input
            log(problem="no halt before the end of input")
        log(event="halt", time=time.time())
        time.sleep(70)
        return 0
    if scenario == "render":  # renders a second of silence, which decodes to nothing
        if not rendering.wait(30):
            log(problem="no render request")
            return 0
        request = state["render"]
        emit({"t": "encoded", "id": request.get("id"), "mode": mode, "msgsent": request.get("text"), "samples": 12000})
        emit({"t": "pcm", "id": request.get("id"), "seg": 0, "seq": 0, "data": base64.b64encode(bytes(24000)).decode()})
        emit({"t": "rendered", "id": request.get("id")})
    audio.wait()
    first = state["first_label"]
    live = scenario in LIVE_FAKES
    if live and not (state["busy_events"] and state["live_audio"]):
        log(problem="no configure before the audio turns on both live_audio and busy_events")
    if not live and state["live_audio"]:
        log(problem="decode turns on live_audio")

    def close_and_stay():
        """Send lines the client skips for TAIL s, end standard output, and do not exit."""
        for _ in range(int(TAIL / 0.5)):
            time.sleep(0.5)
            emit({"t": "novel_line"})
        sys.stdout.flush()
        log(event="closed", time=time.time())
        os.close(sys.stdout.fileno())
        time.sleep(70)
        os._exit(0)  # without flushing the closed stdout

    if scenario == "decode-stopread":
        stopped.wait()
        close_and_stay()

    def period2(after):
        """Wait until the second period's label came after seconds ago."""
        while state["period2"] is None or time.time() < state["period2"] + after:
            time.sleep(0.05)

    if scenario in FAKES or scenario == "decode-noexit":
        for text in ("[1,2,3]", "not JSON", '{"v":1,"t":"novel_line"}'):
            emit(text)
        for dt, message in ((None, "NULL DT"), (0.25, "DT TIE")):
            emit({"t": "decode", "mode": "FT4", "time": first, "snr": -7, "dt": dt, "freq": 1234, "message": message, "ap": 0})
        if scenario == "decode-error":
            emit({"t": "error", "code": "configure_range_error", "key": "depth", "mode": "FT4", "detail": "stand-in"})
        emit({"t": "decode_finished", "period_end": first, "synced": 0, "decoded": 2, "average": 0})
    elif scenario == "render":
        finished(first)
    elif scenario in ("hung", "hung8", "noisy", "slow"):
        answer(busy_after=True)  # a pass completes, and the next starts
        log(event="progress", time=time.time())
        if scenario in ("hung", "hung8"):
            time.sleep(70)  # silent
            return 0
        if scenario == "noisy":
            not_an_answer()
        for _ in range(4):  # 16 s of a slow pass, a progress line every 4 s
            time.sleep(4)
            progress(state["label"])
        finished(state["label"])
        idle()
    elif scenario == "slowstart":
        busy(first)  # the first pass, with no progress for longer than the live allowance, which applies only after a pass completes
        time.sleep(FT4_ALLOWANCE + 3)
        finished(first)
        idle()
        period2(0.5)
        answer(busy_after=False)
    elif scenario == "latebusy":
        answer(busy_after=True)  # a pass completes, and the next runs, with progress, past the second period's scheduled end plus the allowance
        while state["period2"] is None or time.time() < state["period2"] + FT4_PERIOD + FT4_ALLOWANCE + 1:
            time.sleep(3)
            progress(first)
        idle()
        time.sleep(1)
        finished(state["label"])  # the second period's answer, within the allowance of the busy false
    elif scenario in ("idle", "stopread", "reused", "crossed"):
        if scenario == "crossed":
            answer(busy_after=False, label=label_plus(first, -3600))  # a pass completes, for no period the client sent
            period2(0.5)
            finished(first)  # the first period's answer, once the second is registered; the second gets none
        else:
            time.sleep(0.2)
            answer(busy_after=False)  # the first period, and then nothing for the second
        if scenario == "reused":
            # Answers for the second period's label before that period starts, as a period with the same label a day earlier would have had: they must not count for it. Its label is the first's plus 7 or 8 s.
            for later in (7, 8):
                finished(label_plus(first, later))
        not_an_answer()
    elif scenario == "onepass":
        busy(first)  # one pass, which answers the first period and ends with busy false but no decode_finished; the second period gets none
        progress(first)
        idle()
        time.sleep(70)
        return 0
    elif scenario == "answerstop":
        while state["in_period"] < 12000:  # the first period, answered after 1 s of its audio; then no input is read
            time.sleep(0.05)
        busy(first)
        progress(first)
        idle()
        state["answered"] = True
        time.sleep(70)
        return 0
    elif scenario in ("early", "afterhalt"):
        answered = set()
        while not (ended.is_set() or scenario == "afterhalt" and state["halt"]):  # each period answered after 1 s of its audio
            if state["label"] not in answered and state["in_period"] >= 12000:
                answered.add(state["label"])
                answer(busy_after=False)
            time.sleep(0.05)
        if scenario == "afterhalt":
            log(event="halt", time=time.time())
            time.sleep(70)  # every period answered, then a hang after halt
            return 0
    ended.wait()
    if not state["halt"]:
        log(problem="no halt before the end of input")
    if scenario == "decode-noexit":  # after halt, sends lines for TAIL s, ends its output, and then does not exit
        close_and_stay()
    return 1 if scenario == "decode-status" else 0


def fake_codec_program(work):
    """A program that runs fake_codec with this Python."""
    path = os.path.join(work, "fake_codec")
    with open(path, "w") as program:
        program.write("#!%s\nimport sys\nsys.dont_write_bytecode = True\nsys.path.insert(0, %r)\n" % (sys.executable, HERE))
        program.write("import check_client\nsys.exit(check_client.fake_codec())\n")
    os.chmod(path, 0o755)
    return path


def write_extensible(path, samples, subformat):
    """A 12000 Hz mono 16-bit WAVE_FORMAT_EXTENSIBLE file."""
    fmt = struct.pack("<HHIIHHHHI", 0xFFFE, 1, 12000, 24000, 2, 16, 22, 16, 4) + subformat
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt) + 8 + len(samples)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(samples)) + samples)


def main():
    for signum in (signal.SIGINT, signal.SIGTERM):  # a client runs in its own process group, which a terminal's Ctrl-C does not reach
        if signal.getsignal(signum) is not signal.SIG_IGN:  # a signal ignored when the check starts stays ignored
            signal.signal(signum, interrupted)
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--codec", required=True)
    parser.add_argument("--sample", required=True, help="an FT8 period saved as YYMMDD_HHMMSS.wav")
    parser.add_argument("--expected", required=True, help="the messages the sample must decode, as jt9 lists them")
    parser.add_argument("--work-dir", required=True)
    parser.add_argument("--live", action="store_true", help="also run live, in real time")
    parser.add_argument("--timeout", type=int, default=120, help="seconds each client run may take (default: 120)")
    parser.add_argument("client", nargs="+", help="the command that starts the client")
    args = parser.parse_args()

    work = os.path.abspath(args.work_dir)
    os.makedirs(work, exist_ok=True)
    codec, sample = os.path.abspath(args.codec), os.path.abspath(args.sample)
    messages = expected_messages(args.expected)
    label = re.search(r"_(\d{6})\.wav$", sample).group(1)
    runs = []

    def start(name, *options, codec=codec, env=None, timeout=args.timeout, client=args.client):
        data = os.path.join(work, name)
        command = client + [options[0], "--codec", codec, "--data", data] + list(options[1:])
        run = Run(name, command, work, data, timeout, env)
        runs.append(run)
        return run

    reference = start("reference", "decode", "--depth", "3", sample, client=[sys.executable, REFERENCE])
    reference.expect_status(0)
    run = start("decode", "decode", "--depth", "3", sample)
    run.expect_status(0)
    run.expect_messages(messages)
    if any(m.group(1) != label for m in run.decodes) or label not in run.finished:
        run.fail("decodes and finished not all labeled %s" % label)
    run.expect_lines_of(reference)

    with wave.open(sample) as w:
        samples = w.readframes(w.getnframes())
    for name, subformat, status in (("decode-extensible", PCM, 0), ("decode-extensible-float", FLOAT, 2)):
        wav = os.path.join(work, name + "_" + label + ".wav")
        write_extensible(wav, samples, subformat)
        run = start(name, "decode", "--depth", "3", wav)
        run.expect_status(status)
        if status == 0:
            run.expect_lines_of(reference)
        elif run.lines:
            run.fail("output from a file it cannot decode")

    for mode, text in RENDERS:
        name = "render-" + mode.lower()
        out = os.path.join(work, name + ".wav")
        if os.path.exists(out):
            os.remove(out)
        run = start(name, "render", "--mode", mode, "--out", out, text)
        run.expect_status(0)
        run.expect_messages([text])
        if [m.group(2) for m in run.rendered] != [text]:
            run.fail("not one rendered line with the message as sent")
        else:
            try:
                with wave.open(out) as w:
                    shape = (w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes())
            except (OSError, EOFError, wave.Error) as error:
                shape = str(error)
            if shape != (1, 2, 12000, int(run.rendered[0].group(1))):
                run.fail("--out WAV is %s, not 12000 Hz mono 16-bit with the rendered samples" % (shape,))
    run = start("render-out-missing-directory", "render", "--out", os.path.join(work, "missing", "x.wav"), RENDERS[0][1])
    run.expect_status(2)
    if run.lines:
        run.fail("output when --out cannot be written")
    out = os.path.join(work, "render-refused.wav")
    if os.path.exists(out):
        os.remove(out)
    run = start("render-refused", "render", "--out", out, REFUSED)
    run.expect_status(1)
    if os.path.exists(out):
        run.fail("a refused render left %s behind" % out)

    # The stand-in is a script with a #! line, which a native Windows client cannot start.
    fake = fake_codec_program(work) if os.name == "posix" and sys.platform not in ("cygwin", "msys") else None
    if not fake:
        print("SKIP the stand-in runs: a native Windows client cannot start them")
    fake_env = {k: v for k, v in os.environ.items() if k != "OMP_STACKSIZE"}  # the client must set it
    if fake:
        run = start("fake-decode", "decode", "--mode", "FT4", sample, codec=fake, env=dict(fake_env, CODEC_EXAMPLE_FAKE="decode"))
        run.expect_status(0)
        for line in ("decode %s -7 nan 1234 NULL DT" % label, "decode %s -7 0.2 1234 DT TIE" % label, "finished " + label):
            if line not in run.lines:
                run.fail("no line %r" % line)
        run.stand_in()
        for scenario in ("decode-error", "decode-status"):  # an error line, then the codec's exit status 1
            run = start("fake-" + scenario, "decode", "--mode", "FT4", sample, codec=fake, env=dict(fake_env, CODEC_EXAMPLE_FAKE=scenario))
            run.expect_status(FAKES[scenario])
            run.stand_in()
        out = os.path.join(work, "fake-render.wav")
        if os.path.exists(out):
            os.remove(out)
        text = RENDERS[1][1]
        run = start("fake-render", "render", "--mode", "FT4", "--out", out, text, codec=fake, env=dict(fake_env, CODEC_EXAMPLE_FAKE="render"))
        run.expect_status(1)  # the rendered audio decodes to nothing
        if [m.group(2) for m in run.rendered] != [text]:
            run.fail("not one rendered line with the message as sent")
        try:
            with wave.open(out) as w:
                shape = (w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes())
        except (OSError, EOFError, wave.Error) as error:
            shape = str(error)
        if shape != (1, 2, 12000, 12000):
            run.fail("--out WAV after a failed round trip is %s, not the 12000 samples rendered" % (shape,))
        run.stand_in()

        def no_exit(scenario):
            status, event, delay = NO_EXIT[scenario]
            env = dict(fake_env, CODEC_EXAMPLE_FAKE=scenario)
            data = os.path.join(work, "fake-" + scenario)
            command = args.client + (["decode", "--codec", fake, "--data", data, "--mode", "FT4", sample] if scenario.startswith("decode")
                                     else ["render", "--codec", fake, "--data", data, "--mode", "FT4", REFUSED])
            run = Run("fake-" + scenario, command, work, data, 30, env)
            run.expect_status(status)
            run.stand_in(event, delay)
            return run
        with concurrent.futures.ThreadPoolExecutor(len(NO_EXIT)) as pool:
            runs.extend(pool.map(no_exit, NO_EXIT))

    if args.live:
        run = start("live", "live", "--depth", "3", sample)
        run.expect_status(0)
        run.expect_messages(messages)
        if not run.finished or any(m.group(1) not in run.finished for m in run.decodes):
            run.fail("a decode labeled with no finished period")
    if args.live and fake:
        def live_fake(scenario):
            status, event, delay = LIVE_FAKES[scenario]
            env = dict(fake_env, CODEC_EXAMPLE_FAKE=scenario)
            data = os.path.join(work, "live-" + scenario)
            command = args.client + ["live", "--codec", fake, "--data", data, "--mode", "FT8" if scenario == "hung8" else "FT4", sample]
            run = Run("live-" + scenario, command, work, data, 60, env)
            run.expect_status(status)
            run.stand_in(event, delay)
            return run
        with concurrent.futures.ThreadPoolExecutor(len(LIVE_FAKES)) as pool:
            runs.extend(pool.map(live_fake, LIVE_FAKES))

    passed = [run.report() for run in runs]
    return 0 if all(passed) else 1


if __name__ == "__main__":
    sys.exit(main())
