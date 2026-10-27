#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""wsjt-codec with a terminal on stdin prints its usage and exits 0 rather than
waiting for a stream header; a WAV file named on its command line still decodes,
as jt9 decodes it, and the same with --stream.

Usage: test_wsjt_codec_terminal.py <wsjt-codec> <jt9> <work directory> [<FT8 WAV file>]
"""

import os
import shutil
import subprocess
import sys


def run_on_terminal(program, work, name, arguments, timeout):
    directory = os.path.join(work, name)
    os.makedirs(directory)
    leader, follower = os.openpty()
    try:
        process = subprocess.Popen([program] + arguments, cwd=directory, stdin=follower,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        os.close(follower)
        follower = -1
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            stdout, stderr = process.communicate()
            sys.exit(f"{os.path.basename(program)} {' '.join(arguments)} with a terminal on stdin was "
                     f"still running after {timeout} s:\n"
                     f"{stdout.decode(errors='replace')}{stderr.decode(errors='replace')}")
    finally:
        if follower >= 0:
            os.close(follower)
        os.close(leader)
    return process.returncode, stdout.decode(errors="replace"), stderr.decode(errors="replace"), \
        sorted(os.listdir(directory))


def main():
    codec, jt9, work = sys.argv[1:4]
    sample = sys.argv[4] if len(sys.argv) > 4 else None
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    for name, arguments in (("no-arguments", []), ("stream", ["--stream"])):
        result, stdout, stderr, written = run_on_terminal(codec, work, name, arguments, 10)
        if result != 0 or "Usage: wsjt-codec [OPTIONS]" not in stdout:
            sys.exit(f"wsjt-codec {' '.join(arguments)} with a terminal on stdin did not print its usage "
                     f"(exit {result}):\n{stdout}{stderr}")
        if written:
            sys.exit(f"wsjt-codec {' '.join(arguments)} with a terminal on stdin wrote {written}")

    if sample is None:
        return

    wav = ["-8", "-a", ".", "-t", ".", sample]
    result, wav_stdout, stderr, _ = run_on_terminal(codec, work, "wav", wav, 90)
    jt9_result, jt9_stdout, jt9_stderr, _ = run_on_terminal(jt9, work, "jt9", wav, 90)
    if jt9_result != 0 or result != 0 or wav_stdout != jt9_stdout:
        sys.exit(f"wsjt-codec did not decode {sample} as jt9 does (exit {result}, jt9 exit {jt9_result}):\n"
                 f"wsjt-codec:\n{wav_stdout}{stderr}\njt9:\n{jt9_stdout}{jt9_stderr}")
    if "~  CQ F5RXL IN94" not in wav_stdout or "<DecodeFinished>" not in wav_stdout:
        sys.exit(f"wsjt-codec with a terminal on stdin did not decode {sample} (exit {result}):\n"
                 f"{wav_stdout}{stderr}")

    result, stdout, stderr, _ = run_on_terminal(codec, work, "wav-stream", ["--stream"] + wav, 90)
    if result != 0 or stdout != wav_stdout:
        sys.exit(f"wsjt-codec --stream did not decode {sample} as wsjt-codec does (exit {result}):\n"
                 f"{stdout}{stderr}")


if __name__ == "__main__":
    main()
