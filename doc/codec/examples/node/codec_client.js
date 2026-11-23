#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A wsjt-codec client: decode a WAV file, replay one as live audio, or render a message.
//
//   codec_client.js decode --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--low HZ] [--high HZ] [--mycall CALL] WAV
//   codec_client.js live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
//   codec_client.js render --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--out WAV] [TEXT...]
//
// doc/codec/examples/README.md states what each prints and its exit status. Node.js 18 or later, built-in modules only.

'use strict';

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const readline = require('node:readline');
const { setTimeout: sleep } = require('node:timers/promises');

const RATE = 12000;                          // the stream's sample rate, Hz
const DECODES = ['FT8', 'FT4', 'JT9', 'JT65', 'JT4', 'FST4', 'FST4W', 'Q65', 'MSK144', 'ECHO', 'JTTY', 'FREQCAL', 'WSPR'];  // what decode takes: the modes ready lists in modes
const ENCODES = ['FT8', 'FT4', 'JT4', 'JT9', 'JT65', 'Q65', 'FST4', 'FST4W', 'WSPR', 'MSK144', 'ECHO', 'TUNE', 'FOX', 'SUPERFOX', 'JTTY'];  // what render takes: those it lists in encodes
// The period, s, when --period gives none: the mode's own, as a configure that carries the mode sets it; FreqCal has none, so the examples take 15 s; TUNE's is the length of its tone. JTTY has no periods.
const PERIOD = { FT8: 15, FT4: 7.5, JT9: 60, JT65: 60, JT4: 60, FST4: 60, FST4W: 120, Q65: 60, MSK144: 30, ECHO: 3, FREQCAL: 15, WSPR: 120, TUNE: 15, FOX: 15, SUPERFOX: 15 };
const SUBMODES = ['JT4', 'JT9', 'JT65', 'Q65'];  // the modes with a submode: A unless --submode gives one
const PERIODS = ['Q65', 'FST4', 'FST4W', 'MSK144'];  // the slot modes whose render takes trperiod; fast JT9's does too
const DECODED_AS = { TUNE: 'FREQCAL', FOX: 'FT8', SUPERFOX: 'FT8' };  // what render decodes these back with; every other mode, itself
const LIVE_ALLOWANCE = { FT8: 10, FT4: 6 };  // a live pass's time between progress lines, once one has completed, s
const READY_WITHIN = 30;                     // s: a codec that has sent no ready line by then did not start
const EXIT_WITHIN = 10;                      // s: how long the codec has to exit once its output ends, or after halt on a failure
const SILENCE_WITHIN = 300;                  // s: outside live, how long the codec may send no line: the longest interface.md allows a decode pass between progress lines before one has completed, or four periods when that is longer (silenceBound)
// For the examples' check only (README.md, "The check"), not a setting: CODEC_EXAMPLE_CHECK_SILENCE_SECONDS, a whole number from 1 to 300, replaces the bound, so that a run that checks it need not last 300 s.
const CHECK_SILENCE_TEXT = process.env.CODEC_EXAMPLE_CHECK_SILENCE_SECONDS || '';
const CHECK_SILENCE = /^[0-9]{1,3}$/.test(CHECK_SILENCE_TEXT) && Number(CHECK_SILENCE_TEXT) >= 1 && Number(CHECK_SILENCE_TEXT) <= 300 ? Number(CHECK_SILENCE_TEXT) : null;
const CHUNK = 1200;                          // samples in an audio frame: 0.1 s
// Sent with the mode: these examples have no station, so no calls (the decoders use the starting calls otherwise) but --mycall's, and a configure that carries the mode sets the mode up as WSJT-X does, which can change the search keys and the frequencies, so the search keys go with it.
const DECODER = { his_call: '', his_grid: '', ntol: 1000 };
const SEARCH = { FST4: [600, 1400] };  // nfa and nfb when --low and --high give none: in FST4 they are its F Low and F High, WSJT-X's 600 and 1400 Hz; elsewhere the waterfall's range

const AUDIO = 0x01;                          // frame types
const CONTROL = 0x02;

// The subformat GUID of a WAVE_FORMAT_EXTENSIBLE file holding PCM samples.
const PCM_SUBFORMAT = Buffer.from('0100000000001000800000aa00389b71', 'hex');

const OK = 0;                                // exit status
const FAILED = 1;
const USAGE = 2;
const HUNG = 3;

const USAGE_TEXT = `usage:
  codec_client.js decode --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--low HZ] [--high HZ] [--mycall CALL] WAV
  codec_client.js live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
  codec_client.js render --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--out WAV] [TEXT...]
decode's MODE: ${DECODES.join(', ')}
render's MODE: ${ENCODES.join(', ')} (FOX and SUPERFOX: a TEXT a slot; TUNE and ECHO: no TEXT)`;

// Session header: magic, sample format 0 (int16), 1 channel, rate in kHz (uint16 little-endian).
function sessionHeader() {
  const header = Buffer.alloc(8);
  header.write('WSJT', 0, 'latin1');
  header.writeUInt8(0, 4);
  header.writeUInt8(1, 5);
  header.writeUInt16LE(RATE / 1000, 6);
  return header;
}

// A frame: type (1 byte), body length (uint32 little-endian), body.
function frame(kind, body) {
  const head = Buffer.alloc(5);
  head.writeUInt8(kind, 0);
  head.writeUInt32LE(body.length, 1);
  return Buffer.concat([head, body]);
}

// A time of day as the configure key utc takes it: "HH:MM:SS".
function utc(seconds) {
  const s = Math.trunc(seconds) % 86400;
  return [Math.trunc(s / 3600), Math.trunc(s / 60) % 60, s % 60].map((n) => String(n).padStart(2, '0')).join(':');
}

// A JSON number as the examples print it: one decimal, rounded half to even on the value as WSJT-X shows DT (Fortran f5.1), or nan for null. toFixed rounds the exact value too, but rounds a tie away from zero: 0.25 would print 0.3 where WSJT-X shows 0.2.
function formatNumber(value) {
  if (value === null) return 'nan';  // the codec writes a value that is not finite as null
  const sign = value < 0 || Object.is(value, -0) ? '-' : '';
  const x = Math.abs(value);
  if ((x * 4) % 2 === 1) {  // a tie: an odd multiple of 0.25, which x * 4 gives exactly
    const n = Math.floor(x * 10);
    return sign + ((n % 2 === 0 ? n : n + 1) / 10).toFixed(1);
  }
  return sign + x.toFixed(1);
}

// Where WSJT-X starts a transmission in its period, s.
function txStart(mode, period, fast) {
  if (['MSK144', 'ECHO', 'TUNE'].includes(mode) || fast) return 0;
  if (mode === 'SUPERFOX') return 0.4;
  if (mode === 'FT4') return 0.3;
  if (mode === 'FT8' || mode === 'FOX' || (mode === 'FST4' && period === 15) || (mode === 'Q65' && (period === 15 || period === 30))) return 0.5;
  return 1;
}

// The rendered audio, 36 dB down (an arithmetic shift right by 6), over noise, in a buffer of the given seconds: about 12 dB SNR in 2500 Hz, as decoders expect a noise floor (JT65's decodes nothing without one).
// The noise is integer arithmetic, so that every example sends the same samples: a 32-bit xorshift (13, 17, 5) from 1, and each sample the sum of its four bytes less 510. >>> 0 keeps x unsigned, and >>> shifts it right logically.
function overNoise(audio, seconds) {
  const total = Math.trunc(seconds * RATE);
  const signal = Math.floor(audio.length / 2);
  const out = Buffer.alloc(2 * total);
  let x = 1;
  for (let i = 0; i < total; i += 1) {
    x = (x ^ (x << 13)) >>> 0;
    x = (x ^ (x >>> 17)) >>> 0;
    x = (x ^ (x << 5)) >>> 0;
    const noise = (x & 255) + ((x >>> 8) & 255) + ((x >>> 16) & 255) + (x >>> 24) - 510;
    out.writeInt16LE((i < signal ? audio.readInt16LE(2 * i) >> 6 : 0) + noise, 2 * i);
  }
  return out;
}

function seconds() {
  return performance.now() / 1000;  // monotonic
}

// Outside live, how long the codec may send no line before the client takes it to have hung, s: SILENCE_WITHIN, or four periods of periodSeconds, up to the codec's 1800 s, when that is longer; the check's CHECK_SILENCE replaces it.
function silenceBound(periodSeconds) {
  if (CHECK_SILENCE !== null) return CHECK_SILENCE;
  return periodSeconds ? Math.max(SILENCE_WITHIN, 4 * Math.min(periodSeconds, 1800)) : SILENCE_WITHIN;
}

// One wsjt-codec process: frames go to its stdin; each stdout line comes back as an object.
class Codec {
  constructor(program, data) {
    data = path.resolve(data);
    fs.mkdirSync(data, { recursive: true });
    const env = { ...process.env };
    // The decoders' OpenMP threads need the stack WSJT-X gives them.
    env.OMP_STACKSIZE ??= '10M';
    // One directory for the codec's data (-a), temporary (-t) and read-only (-r) files, and its working directory, where it writes files of its own.
    this.child = spawn(path.resolve(program), ['-a', data, '-t', data, '-r', data], {
      cwd: data,
      env,
      stdio: ['pipe', 'pipe', 'pipe'],
      windowsHide: true,  // Windows: no console window
    });
    this.inputOpen = true;
    this.queue = [];
    this.reader = null;
    this.ended = false;
    // exited: the codec has exited and its pipes have closed; gone: it has exited ('close' also comes when it never started).
    this.exited = new Promise((resolve) => this.child.on('close', (code, signal) => resolve({ code, signal })));
    this.gone = Promise.race([this.exited, new Promise((resolve) => this.child.on('exit', (code, signal) => resolve({ code, signal })))]);
    this.child.on('error', (error) => { this.error = error; });  // it did not start, or could not be stopped
    // Its pipes close as it exits, unless a process it started holds them: 2 s after the exit, with what it wrote by then read, let go of them, which ends its output.
    this.child.once('exit', () => {
      const timer = setTimeout(() => this.release(), 2000);
      this.child.once('close', () => clearTimeout(timer));
    });
    // A write to a codec that has exited fails with EPIPE: stop writing; its lines and exit status say why.
    this.child.stdin.on('error', () => { this.inputOpen = false; });
    this.child.stdin.on('close', () => { this.inputOpen = false; });
    // Read stdout and stderr as they come: a pipe left full stops the codec. readline takes CRLF line ends (Windows) too.
    readline.createInterface({ input: this.child.stdout, crlfDelay: Infinity })
      .on('line', (text) => this.received(text))
      .on('close', () => this.finish());
    this.child.stdout.on('close', () => this.finish());  // also when stop() lets go of the pipe
    this.child.stderr.on('data', (chunk) => process.stderr.write(chunk));  // diagnostics for people, not for parsing
    this.write(sessionHeader());
  }

  finish() {
    if (this.ended) return;
    this.ended = true;
    this.deliver(null);
  }

  received(text) {
    let line;
    try {
      line = JSON.parse(text);
    } catch {
      line = null;
    }
    if (line === null || typeof line !== 'object' || Array.isArray(line)) {  // an array is valid JSON but not an object
      console.error(`codec_client: not a JSON object: ${text}`);
      return;
    }
    this.deliver(line);
  }

  deliver(line) {
    if (this.reader) {
      const reader = this.reader;
      this.reader = null;
      reader(line);
    } else {
      this.queue.push(line);
    }
  }

  // The next line, or null once the codec's output has ended; given seconds, 'silent' when no line comes within them.
  next(seconds) {
    if (this.queue.length) return Promise.resolve(this.queue.shift());
    if (this.ended) return Promise.resolve(null);
    return new Promise((resolve) => {
      const timer = seconds === undefined ? null : setTimeout(() => {
        this.reader = null;
        resolve('silent');
      }, seconds * 1000);
      this.reader = (line) => {
        clearTimeout(timer);
        resolve(line);
      };
    });
  }

  // Resolves when the pipe can take more: decoding sets the pace.
  write(data) {
    const stdin = this.child.stdin;
    if (!this.inputOpen || stdin.destroyed || stdin.write(data)) return Promise.resolve();
    return new Promise((resolve) => {
      const done = () => {
        stdin.off('drain', done);
        stdin.off('close', done);
        resolve();
      };
      stdin.on('drain', done);
      stdin.on('close', done);
    });
  }

  send(message) {
    // JSON.stringify writes an integer as 3, never 3.0: a configure declines 3.0 for an integer key (configure_type_error).
    return this.write(frame(CONTROL, Buffer.from(JSON.stringify(message), 'utf8')));
  }

  // Decode what is in, end the session and close the codec's input.
  halt() {
    this.send({ t: 'halt' });
    if (this.inputOpen) {
      this.inputOpen = false;
      this.child.stdin.end();
    }
  }

  // Let go of the codec's pipes: a process it started can hold them open after it exits.
  release() {
    this.child.stdin.destroy();
    this.child.stdout.destroy();
    this.child.stderr.destroy();
  }

  // Kill the codec, and let go of its pipes once it has exited, or after 2 s; at once when it has exited already, or never started, as no exit is still to come. Resolves when the codec is gone.
  stop() {
    if (this.child.exitCode !== null || this.child.signalCode !== null) {
      this.release();
      return this.exited;
    }
    this.child.kill('SIGKILL');
    const timer = setTimeout(() => this.release(), 2000);
    this.child.once('exit', () => {
      clearTimeout(timer);
      this.release();
    });
    return this.exited;
  }

  // The codec's exit, { code, signal }, once it exits, within EXIT_WITHIN s; null, with the codec stopped, when it does not.
  async exitStatus() {
    let timer;
    const late = new Promise((resolve) => { timer = setTimeout(resolve, EXIT_WITHIN * 1000, null); });
    const exit = await Promise.race([this.gone, late]);
    clearTimeout(timer);
    if (exit === null) {
      console.error(`codec_client: wsjt-codec did not exit within ${EXIT_WITHIN} s: it has hung`);
      await this.stop();
      return null;
    }
    await this.exited;  // its pipes have closed, or the client has let go of them
    return exit;
  }

  // Quit a session that failed: halt, and stop the codec if it does not exit.
  async quit() {
    this.halt();
    await this.exitStatus();
  }
}

// Tells a slow decode from a hung codec by the busy lines (configure's busy_events).
class Watchdog {
  constructor(mode) {
    this.mode = mode;
    this.busy = false;
    this.busyMode = null;
    this.completed = false;          // a pass has completed
    this.lastProgress = seconds();
    this.idleSince = seconds();
    this.waiting = new Map();        // whole periods not answered yet: label -> the period's scheduled end
  }

  allowance() {
    return this.completed ? LIVE_ALLOWANCE[this.mode] : Math.max(300, 4 * PERIOD[this.mode]);
  }

  // A whole period starts: its decode_finished, or a busy line naming it, is due from its scheduled end on the replay clock.
  periodDue(label, end) {
    this.waiting.set(label, end);
  }

  // A period's first answer. Its later ones, and those for a period not sent whole, find nothing waiting: a period waits from before its first sample, so no answer comes before it.
  answer(label) {
    this.waiting.delete(label);
  }

  saw(line) {
    const now = seconds();
    if (line.t === 'busy' && line.busy) {
      if (this.busyMode !== null) this.completed = true;  // a busy line ends, and completes, the pass before it
      this.busy = true;
      this.busyMode = line.mode;
      this.lastProgress = now;
      this.answer(line.period_end);
    } else if (line.t === 'busy') {
      if (this.busyMode !== null) this.completed = true;  // busy false ends, and completes, the pass before it too
      this.busy = false;
      this.idleSince = now;
    } else if (line.t === 'pass_progress' || line.t === 'pass_skipped' || (line.t === 'decode' && line.mode === this.busyMode)) {
      this.lastProgress = now;
    } else if (line.t === 'decode_finished') {
      this.completed = true;
      this.lastProgress = now;
      this.answer(line.period_end);
    }
  }

  hung() {
    const now = seconds();
    if (this.busy) return now - this.lastProgress > this.allowance();  // a pass runs: progress lines are due
    if (this.waiting.size) {  // nothing runs: a whole period is due its line from its scheduled end
      return now - Math.max(this.idleSince, Math.min(...this.waiting.values())) > this.allowance();
    }
    return false;
  }
}

// Send audio a period of periodSeconds at a time, each period's label in a configure before its first sample, then halt; with no period (JTTY), all of it with no label.
// Live, start on the next UTC period boundary, label each period with its UTC start and send each frame when its last sample is due, as a receiver's audio arrives.
async function sendAudio(codec, audio, periodSeconds, label, live, watchdog) {
  const samples = Math.floor(audio.length / 2);
  const period = periodSeconds ? Math.trunc(periodSeconds * RATE) : Math.max(samples, 1);
  if (live) {
    const boundary = (Math.floor(Date.now() / 1000 / periodSeconds) + 1) * periodSeconds;
    await sleep(Math.max(0, boundary * 1000 - Date.now()));
    label = boundary;
  }
  const start = seconds();
  // After halt the codec's exit is due as a period's answer is, from the last sample's scheduled time; no line answers it, and it is registered before the first sample, as a codec that stops reading blocks the writer.
  if (watchdog) watchdog.periodDue('halt', start + samples / RATE);
  for (let k = 0, first = 0; first < samples; k += 1, first += period) {
    const end = Math.min(first + period, samples);
    if (periodSeconds) {
      const periodLabel = utc(label + k * periodSeconds);
      // Due from the period's scheduled end, not from its last write, which a codec that stops reading holds up.
      if (watchdog && end - first === period) watchdog.periodDue(periodLabel.replace(/:/g, ''), start + end / RATE);
      await codec.send({ t: 'configure', utc: periodLabel });
    }
    for (let i = first; i < end; i += CHUNK) {
      const j = Math.min(i + CHUNK, end);
      if (live) await sleep(Math.max(0, (start + j / RATE - seconds()) * 1000));
      if (!codec.inputOpen) return;
      await codec.write(frame(AUDIO, audio.subarray(2 * i, 2 * j)));
    }
  }
  codec.halt();
}

// Stream the audio, print what the codec decodes and measures, and return the exit status, the messages decoded (JTTY's complete ones among them) and the echo and freqcal lines.
async function run(codec, audio, mode, periodSeconds, label, live) {
  const watchdog = live ? new Watchdog(mode) : null;
  let hung = false;
  const timer = live ? setInterval(() => {
    if (!hung && watchdog.hung()) {
      hung = true;
      console.error(`codec_client: no progress from the codec in ${watchdog.allowance()} s: it has hung`);
      codec.stop();
    }
  }, 500) : null;
  const writer = sendAudio(codec, audio, periodSeconds, label, live, watchdog);
  const messages = [];
  const measured = [];
  let errors = 0;
  for (;;) {
    const line = await codec.next(live ? undefined : silenceBound(periodSeconds));
    if (line === null || hung) break;
    if (line === 'silent') {
      hung = true;
      console.error(`codec_client: no line from the codec in ${Math.trunc(silenceBound(periodSeconds))} s: it has hung`);
      codec.stop();
      break;
    }
    if (watchdog) watchdog.saw(line);
    if (line.t === 'decode') {  // other lines, and kinds this client does not know, are skipped
      messages.push(line.message);
      console.log(`decode ${line.time} ${line.snr} ${formatNumber(line.dt)} ${line.freq} ${line.message}`);
    } else if (line.t === 'decode_finished') {
      console.log(`finished ${line.period_end}`);
    } else if (line.t === 'echo') {  // Echo: one a period
      measured.push(line);
      console.log(`echo ${line.time} ${formatNumber(line.level)} ${formatNumber(line.snr)} ${formatNumber(line.dfreq)} ${line.quality}`);
    } else if (line.t === 'freqcal') {  // FreqCal: one a measurement
      measured.push(line);
      console.log(`freqcal ${line.time} ${formatNumber(line.freq)} ${formatNumber(line.error)} ${formatNumber(line.level)} ${formatNumber(line.snr)}`);
    } else if (line.t === 'jtty_update' && line.state !== 'growing') {  // JTTY: a message's last line; growing ones are its text so far
      if (line.state === 'complete') messages.push(line.text);
      console.log(`jtty ${line.state} ${formatNumber(line.start)} ${line.snr} ${formatNumber(line.freq)} ${line.text}`);
    } else if (line.t === 'error') {
      errors += 1;
      console.error(`codec_client: codec error: ${JSON.stringify(line)}`);
    }
  }
  clearInterval(timer);
  // Its output has ended, so its exit is due: the watchdog no longer runs to catch a hang.
  const exit = hung ? await codec.exited : await codec.exitStatus();
  await writer;
  if (hung || exit === null) return { status: HUNG, messages, measured };
  const { code, signal } = exit;
  if (code !== 0) {
    console.error(`codec_client: wsjt-codec exited with ${signal ? `signal ${signal}` : `status ${code}`}`);
    return { status: FAILED, messages, measured };
  }
  return { status: errors ? FAILED : OK, messages, measured };
}

// Start the codec and wait for its ready line, which lists the modes it decodes and encodes; decodes, a mode it must decode too.
async function start(options, kinds, decodes = null) {
  let codec;
  try {
    codec = new Codec(options.codec, options.data);
  } catch (error) {
    console.error(`codec_client: cannot start ${options.codec}: ${error.message}`);
    return null;
  }
  let timer;
  const timeout = new Promise((resolve) => { timer = setTimeout(resolve, READY_WITHIN * 1000, 'timeout'); });
  const line = await Promise.race([codec.next(), timeout]);
  clearTimeout(timer);
  if (line === 'timeout') {
    console.error(`codec_client: no ready line from ${options.codec} in ${READY_WITHIN} s`);
    await codec.stop();
    return null;
  }
  const lists = (key, mode) => Array.isArray(line[key]) && line[key].includes(mode);
  if (line === null || line.t !== 'ready' || !lists(kinds, options.mode) || (decodes !== null && !lists('modes', decodes))) {
    if (line !== null) console.error(`codec_client: no ready line naming ${options.mode} in ${kinds}: ${JSON.stringify(line)}`);
    await codec.stop();  // at once, as on the timeout: this run cannot use the codec
    // The output ended before ready: the codec did not start, and the error event, once it has exited, says why when it could not be started.
    if (line === null) console.error(codec.error ? `codec_client: cannot start ${options.codec}: ${codec.error.message}` : 'codec_client: no ready line: the output ended');
    return null;
  }
  return codec;
}

// The samples of a 12000 Hz mono 16-bit PCM WAV file, plain or WAVE_FORMAT_EXTENSIBLE: 16-bit little-endian, the stream's format.
function readWav(file) {
  const data = fs.readFileSync(file);
  if (data.length < 12 || data.toString('latin1', 0, 4) !== 'RIFF' || data.toString('latin1', 8, 12) !== 'WAVE') {
    throw new Error(`${file} is not a WAV file`);
  }
  let format = null;
  for (let at = 12; at + 8 <= data.length;) {
    const id = data.toString('latin1', at, at + 4);
    const size = data.readUInt32LE(at + 4);
    const body = data.subarray(at + 8, at + 8 + size);
    if (id === 'fmt ' && body.length >= 16) {
      const tag = body.readUInt16LE(0);  // 1 is PCM; 0xFFFE, WAVE_FORMAT_EXTENSIBLE, names its format by the GUID at offset 24
      const pcm = tag === 1 || (tag === 0xfffe && body.length >= 40 && body.subarray(24, 40).equals(PCM_SUBFORMAT));
      format = [pcm, body.readUInt16LE(2), body.readUInt32LE(4), body.readUInt16LE(14)];
    } else if (id === 'data') {
      if (!format || format.join() !== [true, 1, RATE, 16].join()) throw new Error(`${file} is not 12000 Hz mono 16-bit PCM`);
      return body.subarray(0, body.length - (body.length % 2));
    }
    at += 8 + size + (size % 2);  // chunks are padded to an even length
  }
  throw new Error(`${file} has no data chunk`);
}

function writeWav(file, samples) {
  const head = Buffer.alloc(44);
  head.write('RIFF', 0, 'latin1');
  head.writeUInt32LE(36 + samples.length, 4);
  head.write('WAVEfmt ', 8, 'latin1');
  head.writeUInt32LE(16, 16);        // format chunk size
  head.writeUInt16LE(1, 20);         // PCM
  head.writeUInt16LE(1, 22);         // mono
  head.writeUInt32LE(RATE, 24);
  head.writeUInt32LE(2 * RATE, 28);  // bytes a second
  head.writeUInt16LE(2, 32);         // bytes a sample
  head.writeUInt16LE(16, 34);        // bits a sample
  head.write('data', 36, 'latin1');
  head.writeUInt32LE(samples.length, 40);
  fs.writeFileSync(file, Buffer.concat([head, samples]));
}

// WSJT-X names a saved period YYMMDD_HHMMSS.wav, or YYMMDD_HHMM.wav in periods of a minute or more: its start, in seconds of the day, or 0.
function fileLabel(file) {
  const match = /_(\d\d)(\d\d)(\d\d)?\.wav$/i.exec(file);
  if (!match) return 0;
  const [h, m, s] = match.slice(1).map((x) => Number(x ?? 0));
  return h < 24 && m < 60 && s < 60 ? h * 3600 + m * 60 + s : 0;
}

// The configure that sets the mode up to decode, with the search keys and the calls.
function configure(options, mode, rxfreq) {
  const [nfa, nfb] = SEARCH[mode] ?? [200, 4000];
  const message = { t: 'configure', mode, depth: options.depth, mycall: options.mycall, nfa: options.low ?? nfa, nfb: options.high ?? nfb, ...DECODER };
  if (mode !== 'JTTY') message.trperiod = options.period;  // with the mode, as FreqCal and fast JT9 need it
  if (SUBMODES.includes(mode)) message.submode = options.submode;
  if (mode === 'JT9') message.fast = options.fast;  // with every JT9 configure that carries the mode
  if (rxfreq !== null) message.rxfreq = rxfreq;
  return message;
}

async function decode(options) {
  let audio;
  try {
    audio = readWav(options.wav);
  } catch (error) {
    console.error(`codec_client: ${error.message}`);
    return USAGE;
  }
  const codec = await start(options, 'modes');
  if (!codec) return FAILED;
  const live = options.command === 'live';
  const message = configure(options, options.mode, options.freq);
  if (live) Object.assign(message, { live_audio: true, busy_events: true });
  await codec.send(message);
  const periodSeconds = options.mode === 'JTTY' ? null : options.period;
  return (await run(codec, audio, options.mode, periodSeconds, fileLabel(options.wav), live)).status;
}

function words(text) {
  return text.split(/\s+/).filter(Boolean).join(' ');
}

// The render request for the mode.
function renderRequest(options) {
  const request = { t: 'render', id: 1, freq: options.freq, rate: RATE };
  if (options.mode === 'JTTY') {  // JTTY's requests carry no mode
    request.text = options.text.join(' ');
    return request;
  }
  request.mode = options.mode;
  if (options.mode === 'FOX' || options.mode === 'SUPERFOX') request.slots = options.text;  // one message a slot
  else if (options.mode === 'TUNE') request.seconds = Math.trunc(options.period);  // a steady tone for the period FreqCal measures it in
  else if (options.mode === 'ECHO') request.kind = 'tone';  // WSJT-X's Fixed Tone
  else request.text = options.text.join(' ');
  if (SUBMODES.includes(options.mode)) request.submode = options.submode;
  if (PERIODS.includes(options.mode) || options.fast) request.trperiod = Math.trunc(options.period);
  if (options.fast) request.fast = true;
  return request;
}

async function render(options) {
  // Check --out before the codec starts. Opening it to append creates it and changes nothing in a file that is there; a render that fails removes a file this run created.
  let created = false;
  if (options.out) {
    try {
      created = !fs.existsSync(options.out);
      fs.closeSync(fs.openSync(options.out, 'a'));
    } catch (error) {
      console.error(`codec_client: cannot write --out: ${error.message}`);
      return USAGE;
    }
  }
  const failed = (status = FAILED) => {
    if (created) fs.rmSync(options.out, { force: true });
    return status;
  };
  const codec = await start(options, 'encodes', DECODED_AS[options.mode] ?? options.mode);
  if (!codec) return failed();
  await codec.send(renderRequest(options));
  let sent = [];
  let samples = 0;
  const pcm = [];
  for (;;) {  // the reply: encoded (JTTY: a segment line a segment), the pcm lines, then rendered; or rejected
    const line = await codec.next(silenceBound(null));  // a reply waits for no decode
    if (line === 'silent') {
      console.error(`codec_client: no line from the codec in ${Math.trunc(silenceBound(null))} s: it has hung`);
      await codec.stop();
      return failed(HUNG);
    }
    // A rendered line before any encoded or segment line ends a reply that rendered nothing.
    if (line === null || line.t === 'rejected' || line.t === 'error' || (line.t === 'rendered' && !sent.length)) {
      console.error(`codec_client: render failed: ${JSON.stringify(line)}`);
      await codec.quit();
      return failed();
    }
    if (line.t === 'encoded') {
      samples = line.samples;
      if (options.mode === 'FOX') sent = line.slots.map((slot) => slot.msgsent);
      else if (options.mode === 'SUPERFOX') sent = options.text.map((slot) => slot.toUpperCase());  // SuperFox's encoded line has no msgsent: WSJT-X shows the slots as written
      else sent = [line.msgsent];
    } else if (line.t === 'segment') {  // JTTY: each segment's text and samples
      sent.push(line.text);
      samples += line.samples;
    } else if (line.t === 'pcm') {
      pcm.push(Buffer.from(line.data, 'base64'));  // int16 little-endian samples at the requested rate
    } else if (line.t === 'rendered') {
      break;
    }
  }
  const audio = Buffer.concat(pcm);
  console.log(`rendered ${samples} ${sent.join(' | ')}`);
  if (options.out) {
    try {
      writeWav(options.out, audio);
    } catch (error) {
      console.error(`codec_client: ${error.message}`);
      await codec.quit();
      return failed();
    }
  }
  // Decode it back, over noise: the audio where WSJT-X starts it in its period; JTTY's after a second, with a second after it.
  const mode = DECODED_AS[options.mode] ?? options.mode;
  let periodSeconds = null;
  let signal;
  if (options.mode === 'JTTY') {
    signal = overNoise(Buffer.concat([Buffer.alloc(2 * RATE), audio]), audio.length / 2 / RATE + 2);
  } else {
    periodSeconds = options.period;
    signal = overNoise(Buffer.concat([Buffer.alloc(2 * Math.trunc(txStart(options.mode, periodSeconds, options.fast) * RATE)), audio]), periodSeconds);
  }
  if (mode === 'FST4') {  // its F Low and F High around the render
    options.low = options.freq - 200;
    options.high = options.freq + 200;
  }
  const message = configure(options, mode, mode === 'MSK144' ? null : options.freq);  // MSK144's Rx Freq is its tones' center
  if (options.mode === 'SUPERFOX') Object.assign(message, { contest_type: 'hound', superfox_mode: true, even_sequence: true });  // a SuperFox Hound decodes it
  if (options.mode === 'ECHO') Object.assign(message, { live_audio: true, echo_freq: options.freq });  // the echo's parameters from the keys, not from the audio's first samples as a saved file carries them
  await codec.send(message);
  const { status, messages, measured } = await run(codec, signal, mode, periodSeconds, 0, false);
  if (status !== OK) return status;
  let missing;
  if (options.mode === 'TUNE') {
    missing = measured.some((m) => m.t === 'freqcal' && m.error !== null && Math.abs(m.error) < 1) ? [] : [`a measurement within 1 Hz of ${options.freq} Hz`];
  } else if (options.mode === 'ECHO') {
    missing = measured.some((m) => m.t === 'echo' && m.snr > 0) ? [] : ['an echo above the noise'];  // a null snr is not above 0
  } else {
    missing = sent.filter((text) => !messages.some((m) => words(m) === words(text)));
  }
  if (missing.length) {
    console.error(`codec_client: round trip failed: no decode of ${missing.join(' | ')}`);
    return FAILED;
  }
  return OK;
}

class UsageError extends Error {}

function integer(name, text) {
  if (!/^[+-]?\d+$/.test(text)) throw new UsageError(`--${name} takes an integer, not ${text}`);
  return Number(text);
}

function decimal(name, text) {
  if (!/^[+-]?(\d+\.?\d*|\.\d+)(e[+-]?\d+)?$/i.test(text)) throw new UsageError(`--${name} takes a number, not ${text}`);
  return Number(text);
}

// Each command's options that take a value, its flags, and the modes its --mode takes.
const COMMANDS = {
  decode: { values: ['codec', 'data', 'mode', 'depth', 'submode', 'period', 'freq', 'low', 'high', 'mycall'], flags: ['fast'], modes: DECODES },
  live: { values: ['codec', 'data', 'mode', 'depth'], flags: [], modes: ['FT8', 'FT4'] },
  render: { values: ['codec', 'data', 'mode', 'depth', 'submode', 'period', 'freq', 'out'], flags: ['fast'], modes: ENCODES },
};

function parseCommandLine(argv) {
  const [command, ...rest] = argv;
  if (command === '-h' || command === '--help') return { help: true };
  if (!['decode', 'live', 'render'].includes(command)) throw new UsageError('the command is decode, live or render');
  const { values, flags, modes } = COMMANDS[command];
  const given = {};
  const positional = [];
  for (let i = 0; i < rest.length; i += 1) {
    const match = /^--([a-z]+)(?:=(.*))?$/s.exec(rest[i]);
    if (rest[i] === '--') {
      positional.push(...rest.slice(i + 1));
      break;
    } else if (rest[i] === '-h' || rest[i] === '--help') {
      return { help: true };
    } else if (!match) {
      positional.push(rest[i]);
    } else if (flags.includes(match[1])) {
      if (match[2] !== undefined) throw new UsageError(`--${match[1]} takes no value`);
      given[match[1]] = true;
    } else if (!values.includes(match[1])) {
      throw new UsageError(`${command} takes no --${match[1]}`);
    } else if (match[2] !== undefined) {
      given[match[1]] = match[2];
    } else if (i + 1 < rest.length) {
      given[match[1]] = rest[++i];
    } else {
      throw new UsageError(`--${match[1]} needs a value`);
    }
  }
  if (given.codec === undefined) throw new UsageError('--codec is required');
  const options = {
    command,
    codec: given.codec,
    data: given.data ?? '.',
    mode: given.mode ?? 'FT8',
    depth: given.depth === undefined ? 3 : integer('depth', given.depth),
    submode: given.submode ?? 'A',
    period: given.period === undefined ? null : decimal('period', given.period),
    fast: given.fast === true,
    freq: given.freq === undefined ? null : integer('freq', given.freq),
    low: given.low === undefined ? null : integer('low', given.low),
    high: given.high === undefined ? null : integer('high', given.high),
    mycall: given.mycall ?? '',
    out: given.out,
  };
  if (!modes.includes(options.mode)) throw new UsageError(`${command}'s --mode is one of ${modes.join(', ')}`);
  if (![1, 2, 3].includes(options.depth)) throw new UsageError('--depth is 1, 2 or 3');
  if (!/^[A-H]$/.test(options.submode)) throw new UsageError('--submode is a letter from A to H');
  if (options.period === null) {
    options.period = PERIOD[options.mode] ?? null;  // the mode's: live takes no --period, and JTTY has none
  } else if (!(Number.isFinite(options.period) && options.period * RATE >= 1)) {  // a period is sent a whole number of samples at a time
    throw new UsageError('--period must be a number of seconds that holds a sample at least');
  }
  if (command === 'render') {
    if (options.freq === null) options.freq = { MSK144: 1000, SUPERFOX: 750 }[options.mode] ?? 1500;  // MSK144's tones are at 1000 and 2000 Hz, and SuperFox goes at 750
    // TUNE and ECHO take no TEXT; FOX and SUPERFOX one a slot, and JTTY one or more; every other mode one.
    const wants = ['TUNE', 'ECHO'].includes(options.mode) ? 0 : ['FOX', 'SUPERFOX', 'JTTY'].includes(options.mode) ? null : 1;
    if ((wants !== null && positional.length !== wants) || (wants === null && !positional.length)) {
      throw new UsageError(`render --mode ${options.mode} takes ${wants === 0 ? 'no TEXT' : wants === 1 ? 'one TEXT' : 'one TEXT or more'}`);
    }
    options.text = positional;
  } else {
    if (positional.length !== 1) throw new UsageError(`${command} takes one WAV`);
    options.wav = positional[0];
  }
  return options;
}

async function main(argv) {
  let options;
  try {
    options = parseCommandLine(argv);
  } catch (error) {
    if (!(error instanceof UsageError)) throw error;
    console.error(`${USAGE_TEXT}\ncodec_client: ${error.message}`);
    return USAGE;
  }
  if (options.help) {
    console.log(USAGE_TEXT);
    return OK;
  }
  return options.command === 'render' ? render(options) : decode(options);
}

main(process.argv.slice(2)).then((status) => { process.exitCode = status; });
