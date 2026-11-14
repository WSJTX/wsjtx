#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A wsjt-codec client: decode a WAV file, replay one as live audio, or render a message.
//
//   codec_client.js decode --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
//   codec_client.js live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
//   codec_client.js render --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] [--freq HZ] [--out WAV] TEXT
//
// doc/codec/examples/README.md states what each prints and its exit status. Node.js 18 or later, built-in modules only.

'use strict';

const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const readline = require('node:readline');
const { setTimeout: sleep } = require('node:timers/promises');

const RATE = 12000;                          // the stream's sample rate, Hz
const PERIOD = { FT8: 15, FT4: 7.5 };        // period, s
const TX_START = { FT8: 0.5, FT4: 0.3 };     // where WSJT-X starts its transmission in the period, s
const LIVE_ALLOWANCE = { FT8: 10, FT4: 6 };  // a live pass's time between progress lines, once one has completed, s
const READY_WITHIN = 30;                     // s: a codec that has sent no ready line by then did not start
const EXIT_WITHIN = 10;                      // s: how long the codec has to exit once its output ends, or after halt on a failure
const CHUNK = 1200;                          // samples in an audio frame: 0.1 s
// Sent with the mode: these examples have no station, so no calls (the decoders use the starting calls otherwise), and a configure that carries the mode resets nfa, nfb and ntol.
const DECODER = { mycall: '', his_call: '', his_grid: '', nfa: 200, nfb: 4000, ntol: 1000 };

const AUDIO = 0x01;                          // frame types
const CONTROL = 0x02;

// The subformat GUID of a WAVE_FORMAT_EXTENSIBLE file holding PCM samples.
const PCM_SUBFORMAT = Buffer.from('0100000000001000800000aa00389b71', 'hex');

const OK = 0;                                // exit status
const FAILED = 1;
const USAGE = 2;
const HUNG = 3;

const USAGE_TEXT = `usage:
  codec_client.js decode --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
  codec_client.js live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
  codec_client.js render --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] [--freq HZ] [--out WAV] TEXT`;

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

// dt to one decimal as WSJT-X shows it (Fortran f5.1): rounded half to even on the value. toFixed rounds the exact value too, but rounds a tie away from zero: 0.25 would print 0.3 where WSJT-X shows 0.2.
function formatDt(dt) {
  if (dt === null) return 'nan';  // the codec writes a dt that is not finite as null
  const sign = dt < 0 || Object.is(dt, -0) ? '-' : '';
  const x = Math.abs(dt);
  if ((x * 4) % 2 === 1) {  // a tie: an odd multiple of 0.25, which x * 4 gives exactly
    const n = Math.floor(x * 10);
    return sign + ((n % 2 === 0 ? n : n + 1) / 10).toFixed(1);
  }
  return sign + x.toFixed(1);
}

function seconds() {
  return performance.now() / 1000;  // monotonic
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
    if (line === null || typeof line !== 'object') {
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

  // The next line, or null once the codec's output has ended.
  next() {
    if (this.queue.length) return Promise.resolve(this.queue.shift());
    if (this.ended) return Promise.resolve(null);
    return new Promise((resolve) => { this.reader = resolve; });
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
    // JSON.stringify writes an integer as 3, never 3.0: a configure ignores or misreads 3.0, without an error.
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

  // Kill the codec, and let go of its pipes once it has exited, or after 2 s. Resolves when the codec is gone.
  stop() {
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
    // Its pipes close as it exits, unless a process it started holds them: let go of them after 2 s.
    const release = setTimeout(() => this.release(), 2000);
    await this.exited;
    clearTimeout(release);
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

// Send audio a period at a time, each period's label in a configure before its first sample, then halt.
// Live, start on the next UTC period boundary, label each period with its UTC start and send each frame when its last sample is due, as a receiver's audio arrives.
async function sendAudio(codec, audio, mode, label, live, watchdog) {
  const period = PERIOD[mode] * RATE;
  const samples = Math.floor(audio.length / 2);
  if (live) {
    const boundary = (Math.floor(Date.now() / 1000 / PERIOD[mode]) + 1) * PERIOD[mode];
    await sleep(Math.max(0, boundary * 1000 - Date.now()));
    label = boundary;
  }
  const start = seconds();
  // After halt the codec's exit is due as a period's answer is, from the last sample's scheduled time; no line answers it, and it is registered before the first sample, as a codec that stops reading blocks the writer.
  if (watchdog) watchdog.periodDue('halt', start + samples / RATE);
  for (let k = 0, first = 0; first < samples; k += 1, first += period) {
    const periodLabel = utc(label + k * PERIOD[mode]);
    const end = Math.min(first + period, samples);
    // Due from the period's scheduled end, not from its last write, which a codec that stops reading holds up.
    if (watchdog && end - first === period) watchdog.periodDue(periodLabel.replace(/:/g, ''), start + end / RATE);
    await codec.send({ t: 'configure', utc: periodLabel });
    for (let i = first; i < end; i += CHUNK) {
      const j = Math.min(i + CHUNK, end);
      if (live) await sleep(Math.max(0, (start + j / RATE - seconds()) * 1000));
      if (!codec.inputOpen) return;
      await codec.write(frame(AUDIO, audio.subarray(2 * i, 2 * j)));
    }
  }
  codec.halt();
}

// Stream the audio, print the decodes, and return the exit status and the messages decoded.
async function run(codec, audio, mode, label, live) {
  const watchdog = live ? new Watchdog(mode) : null;
  let hung = false;
  const timer = live ? setInterval(() => {
    if (!hung && watchdog.hung()) {
      hung = true;
      console.error(`codec_client: no progress from the codec in ${watchdog.allowance()} s: it has hung`);
      codec.stop();
    }
  }, 500) : null;
  const writer = sendAudio(codec, audio, mode, label, live, watchdog);
  const messages = [];
  let errors = 0;
  for (;;) {
    const line = await codec.next();
    if (line === null || hung) break;
    if (watchdog) watchdog.saw(line);
    if (line.t === 'decode') {  // other lines, and kinds this client does not know, are skipped
      messages.push(line.message);
      console.log(`decode ${line.time} ${line.snr} ${formatDt(line.dt)} ${line.freq} ${line.message}`);
    } else if (line.t === 'decode_finished') {
      console.log(`finished ${line.period_end}`);
    } else if (line.t === 'error') {
      errors += 1;
      console.error(`codec_client: codec error: ${JSON.stringify(line)}`);
    }
  }
  clearInterval(timer);
  // Its output has ended, so its exit is due: the watchdog no longer runs to catch a hang.
  const exit = hung ? await codec.exited : await codec.exitStatus();
  await writer;
  if (hung || exit === null) return { status: HUNG, messages };
  const { code, signal } = exit;
  if (code !== 0) {
    console.error(`codec_client: wsjt-codec exited with ${signal ? `signal ${signal}` : `status ${code}`}`);
    return { status: FAILED, messages };
  }
  return { status: errors ? FAILED : OK, messages };
}

// Start the codec and wait for its ready line, which lists the modes it decodes and encodes.
async function start(options, kinds) {
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
  if (line === null || line.t !== 'ready' || !Array.isArray(line[kinds]) || !line[kinds].includes(options.mode)) {
    if (line !== null) console.error(`codec_client: no ready line naming ${options.mode} in ${kinds}: ${JSON.stringify(line)}`);
    await codec.quit();
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

// WSJT-X names a saved period YYMMDD_HHMMSS.wav: its start, in seconds of the day, or 0.
function fileLabel(file) {
  const match = /_(\d\d)(\d\d)(\d\d)\.wav$/i.exec(file);
  if (!match) return 0;
  const [h, m, s] = match.slice(1).map(Number);
  return h < 24 && m < 60 && s < 60 ? h * 3600 + m * 60 + s : 0;
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
  const configure = { t: 'configure', mode: options.mode, depth: options.depth, ...DECODER };
  if (live) Object.assign(configure, { live_audio: true, busy_events: true });
  await codec.send(configure);
  return (await run(codec, audio, options.mode, fileLabel(options.wav), live)).status;
}

function words(text) {
  return text.split(/\s+/).filter(Boolean).join(' ');
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
  const failed = () => {
    if (created) fs.rmSync(options.out, { force: true });
    return FAILED;
  };
  const codec = await start(options, 'encodes');
  if (!codec) return failed();
  await codec.send({ t: 'render', id: 1, mode: options.mode, text: options.text, freq: options.freq, rate: RATE });
  let encoded = null;
  const pcm = [];
  for (;;) {  // the reply: encoded, its pcm lines, then rendered; or rejected
    const line = await codec.next();
    if (line === null || line.t === 'rejected' || line.t === 'error') {
      console.error(`codec_client: render failed: ${JSON.stringify(line)}`);
      await codec.quit();
      return failed();
    }
    if (line.t === 'encoded') encoded = line;
    else if (line.t === 'pcm') pcm.push(Buffer.from(line.data, 'base64'));  // int16 little-endian samples at the requested rate
    else if (line.t === 'rendered') break;
  }
  const audio = Buffer.concat(pcm);
  console.log(`rendered ${encoded.samples} ${encoded.msgsent}`);
  if (options.out) {
    try {
      writeWav(options.out, audio);
    } catch (error) {
      console.error(`codec_client: ${error.message}`);
      await codec.quit();
      return failed();
    }
  }
  // Decode it back: the audio where WSJT-X starts it, in a period of silence.
  const signal = Buffer.alloc(2 * PERIOD[options.mode] * RATE);
  audio.copy(signal, 2 * Math.round(TX_START[options.mode] * RATE));  // copies what fits
  await codec.send({ t: 'configure', mode: options.mode, depth: options.depth, ...DECODER });
  const { status, messages } = await run(codec, signal, options.mode, 0, false);
  if (status === OK && !messages.some((message) => words(message) === words(encoded.msgsent))) {
    console.error(`codec_client: round trip failed: no decode of ${encoded.msgsent}`);
    return FAILED;
  }
  return status;
}

class UsageError extends Error {}

function integer(name, text) {
  if (!/^[+-]?\d+$/.test(text)) throw new UsageError(`--${name} takes an integer, not ${text}`);
  return Number(text);
}

function parseCommandLine(argv) {
  const [command, ...rest] = argv;
  if (command === '-h' || command === '--help') return { help: true };
  if (!['decode', 'live', 'render'].includes(command)) throw new UsageError('the command is decode, live or render');
  const names = command === 'render' ? ['codec', 'data', 'mode', 'depth', 'freq', 'out'] : ['codec', 'data', 'mode', 'depth'];
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
    } else if (!names.includes(match[1])) {
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
  if (positional.length !== 1) throw new UsageError(`${command} takes one ${command === 'render' ? 'TEXT' : 'WAV'}`);
  const options = {
    command,
    codec: given.codec,
    data: given.data ?? '.',
    mode: given.mode ?? 'FT8',
    depth: given.depth === undefined ? 3 : integer('depth', given.depth),
    freq: given.freq === undefined ? 1500 : integer('freq', given.freq),
    out: given.out,
    [command === 'render' ? 'text' : 'wav']: positional[0],
  };
  if (!['FT8', 'FT4'].includes(options.mode)) throw new UsageError('--mode is FT8 or FT4');
  if (![1, 2, 3].includes(options.depth)) throw new UsageError('--depth is 1, 2 or 3');
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
