// SPDX-License-Identifier: GPL-3.0-or-later
//! A wsjt-codec client: decode a WAV file, replay one as live audio, or render a message.
//!
//!   codec_client decode --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
//!   codec_client live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
//!   codec_client render --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] [--freq HZ] [--out WAV] TEXT
//!
//! doc/codec/examples/README.md states what each prints and its exit status. Its one dependency is serde_json.

use serde_json::{json, Value};
use std::collections::HashMap;
use std::env;
use std::fs::{self, File, OpenOptions};
use std::io::{self, BufRead, BufReader, Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, Command, ExitCode, ExitStatus, Stdio};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

const RATE: u32 = 12000; // the stream's sample rate, Hz
const CHUNK: usize = 1200; // samples in an audio frame: 0.1 s
const READY_WAIT: Duration = Duration::from_secs(30); // how long the codec may take to send ready
const EXIT_WAIT: Duration = Duration::from_secs(10); // how long it has to exit once its output ends, or after halt on a failure

const AUDIO: u8 = 0x01; // frame types
const CONTROL: u8 = 0x02;
// Session header: magic, sample format 0 (int16), 1 channel, rate in kHz (uint16 little-endian).
const HEADER: [u8; 8] = [b'W', b'S', b'J', b'T', 0, 1, (RATE / 1000) as u8, 0];

// Exit status.
const OK: u8 = 0;
const FAILED: u8 = 1;
const USAGE: u8 = 2;
const HUNG: u8 = 3;

const USAGE_TEXT: &str = "usage:
  codec_client decode --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
  codec_client live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
  codec_client render --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] [--freq HZ] [--out WAV] TEXT";

#[derive(Clone, Copy)]
enum Mode {
    Ft8,
    Ft4,
}

impl Mode {
    fn name(self) -> &'static str {
        match self {
            Mode::Ft8 => "FT8",
            Mode::Ft4 => "FT4",
        }
    }

    /// The period, s.
    fn period(self) -> f64 {
        match self {
            Mode::Ft8 => 15.0,
            Mode::Ft4 => 7.5,
        }
    }

    /// Where WSJT-X starts its transmission in the period, s.
    fn tx_start(self) -> f64 {
        match self {
            Mode::Ft8 => 0.5,
            Mode::Ft4 => 0.3,
        }
    }

    /// A live pass's time between progress lines, s, once one has completed.
    fn live_allowance(self) -> f64 {
        match self {
            Mode::Ft8 => 10.0,
            Mode::Ft4 => 6.0,
        }
    }
}

/// The codec's standard input, shared by the thread that streams the audio and the one that reads the codec's lines. None once it is closed.
struct Input(Mutex<Option<ChildStdin>>);

impl Input {
    fn is_open(&self) -> bool {
        self.0.lock().unwrap().is_some()
    }

    fn write(&self, data: &[u8]) {
        let mut stdin = self.0.lock().unwrap();
        if let Some(pipe) = stdin.as_mut() {
            // Rust ignores SIGPIPE, so a write to a codec that has exited fails (BrokenPipe) instead of ending this program: stop writing, and let the codec's lines and exit status say why.
            if pipe.write_all(data).and_then(|_| pipe.flush()).is_err() {
                *stdin = None;
            }
        }
    }

    /// A frame: type (1 byte), body length (uint32 little-endian), body.
    fn frame(&self, kind: u8, body: &[u8]) {
        let mut data = Vec::with_capacity(5 + body.len());
        data.push(kind);
        data.extend_from_slice(&(body.len() as u32).to_le_bytes());
        data.extend_from_slice(body);
        self.write(&data);
    }

    fn send(&self, message: &Value) {
        // Integer keys stay in integer types (depth is a u8): serde_json writes those as plain JSON integers, and an f64 as 3.0, which a configure ignores or misreads without an error.
        self.frame(CONTROL, message.to_string().as_bytes());
    }

    /// Decode what is in, end the session and close the codec's input.
    fn halt(&self) {
        self.send(&json!({"t": "halt"}));
        self.0.lock().unwrap().take();
    }
}

/// One wsjt-codec process: frames go to its stdin; each stdout line comes back as a JSON value, and the channel closes at the end of its output.
struct Codec {
    child: Child,
    input: Arc<Input>,
    lines: Receiver<Value>,
}

impl Codec {
    fn start(path: &Path, data: &Path) -> io::Result<Codec> {
        // Absolute paths: once the working directory is set, a relative program path resolves differently by platform.
        let path = std::path::absolute(path)?;
        let data = std::path::absolute(data)?;
        fs::create_dir_all(&data)?;
        let mut command = Command::new(&path);
        // One directory for the codec's data (-a), temporary (-t) and read-only (-r) files, and its working directory, where it writes files of its own.
        command
            .arg("-a")
            .arg(&data)
            .arg("-t")
            .arg(&data)
            .arg("-r")
            .arg(&data);
        command.current_dir(&data);
        command
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        if env::var_os("OMP_STACKSIZE").is_none() {
            // The decoders' OpenMP threads need the stack WSJT-X gives them.
            command.env("OMP_STACKSIZE", "10M");
        }
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            const CREATE_NO_WINDOW: u32 = 0x0800_0000;
            command.creation_flags(CREATE_NO_WINDOW); // no console window
        }
        let mut child = command.spawn()?;
        let stdin = child.stdin.take().expect("piped stdin");
        let stdout = child.stdout.take().expect("piped stdout");
        let stderr = child.stderr.take().expect("piped stderr");

        // Read stdout and stderr on their own threads: a pipe left full stops the codec.
        let (sender, lines) = mpsc::channel();
        thread::spawn(move || {
            for raw in raw_lines(stdout) {
                let text = String::from_utf8_lossy(&raw);
                let text = text.trim_end_matches(['\r', '\n']);
                match serde_json::from_str::<Value>(text) {
                    Ok(line) => {
                        let _ = sender.send(line); // read on when no one takes the lines: the pipe must not fill
                    }
                    Err(_) => eprintln!("codec_client: not JSON: {text}"),
                }
            }
        });
        thread::spawn(move || {
            // Diagnostics for people, not for parsing.
            for raw in raw_lines(stderr) {
                let _ = io::stderr().lock().write_all(&raw);
            }
        });

        let codec = Codec {
            child,
            input: Arc::new(Input(Mutex::new(Some(stdin)))),
            lines,
        };
        codec.input.write(&HEADER);
        Ok(codec)
    }

    /// The codec's exit status once it exits, within EXIT_WAIT; None, with the codec stopped, when it does not.
    fn exit_status(&mut self) -> io::Result<Option<ExitStatus>> {
        let deadline = Instant::now() + EXIT_WAIT;
        loop {
            if let Some(status) = self.child.try_wait()? {
                return Ok(Some(status));
            }
            if Instant::now() >= deadline {
                eprintln!(
                    "codec_client: wsjt-codec did not exit within {} s: it has hung",
                    EXIT_WAIT.as_secs()
                );
                let _ = self.child.kill();
                let _ = self.child.wait();
                return Ok(None);
            }
            thread::sleep(Duration::from_millis(50));
        }
    }

    /// Quit a session that failed: halt, and stop the codec if it does not exit.
    fn quit(&mut self) {
        self.input.halt();
        let _ = self.exit_status();
    }
}

/// A pipe's lines, as bytes, each with its line ending.
fn raw_lines(pipe: impl Read) -> impl Iterator<Item = Vec<u8>> {
    let mut reader = BufReader::new(pipe);
    std::iter::from_fn(move || {
        let mut raw = Vec::new();
        match reader.read_until(b'\n', &mut raw) {
            Ok(0) | Err(_) => None,
            Ok(_) => Some(raw),
        }
    })
}

/// Tells a slow decode from a hung codec by the busy lines (configure's busy_events).
struct Watchdog(Mutex<Watch>);

struct Watch {
    mode: Mode,
    busy: bool,
    busy_mode: Option<String>,
    completed: bool, // a pass has completed
    last_progress: Instant,
    idle_since: Instant,
    waiting: HashMap<String, Instant>, // periods sent whole and not answered yet: label -> scheduled end
}

impl Watch {
    fn allowance(&self) -> Duration {
        Duration::from_secs_f64(if self.completed {
            self.mode.live_allowance()
        } else {
            f64::max(300.0, 4.0 * self.mode.period())
        })
    }

    fn answer(&mut self, label: Option<&str>) {
        if let Some(label) = label {
            self.waiting.remove(label);
        }
    }
}

impl Watchdog {
    fn new(mode: Mode) -> Watchdog {
        let now = Instant::now();
        Watchdog(Mutex::new(Watch {
            mode,
            busy: false,
            busy_mode: None,
            completed: false,
            last_progress: now,
            idle_since: now,
            waiting: HashMap::new(),
        }))
    }

    fn allowance(&self) -> Duration {
        self.0.lock().unwrap().allowance()
    }

    /// A period that will be sent whole, registered before its first sample: its answer cannot come first, and nothing outlives the answer. It is due from its scheduled end on the replay clock, which runs on while a write to a codec that has stopped reading blocks.
    fn period_due(&self, label: String, end: Instant) {
        self.0.lock().unwrap().waiting.insert(label, end);
    }

    fn saw(&self, line: &Value) {
        let mut w = self.0.lock().unwrap();
        let now = Instant::now();
        match line["t"].as_str() {
            Some("busy") if line["busy"].as_bool() == Some(true) => {
                if w.busy_mode.is_some() {
                    w.completed = true; // a busy line ends, and completes, the pass before it
                }
                w.busy = true;
                w.busy_mode = line["mode"].as_str().map(String::from);
                w.last_progress = now;
                w.answer(line["period_end"].as_str());
            }
            Some("busy") => {
                if w.busy_mode.is_some() {
                    w.completed = true; // busy false ends, and completes, the pass before it too
                }
                w.busy = false;
                w.idle_since = now;
            }
            Some("pass_progress" | "pass_skipped") => w.last_progress = now,
            Some("decode") if line["mode"].as_str() == w.busy_mode.as_deref() => {
                w.last_progress = now
            }
            Some("decode_finished") => {
                w.completed = true;
                w.last_progress = now;
                w.answer(line["period_end"].as_str());
            }
            _ => {}
        }
    }

    fn hung(&self) -> bool {
        let w = self.0.lock().unwrap();
        let now = Instant::now();
        if w.busy {
            // A pass runs: progress lines are due.
            return now.saturating_duration_since(w.last_progress) > w.allowance();
        }
        match w.waiting.values().min() {
            // Nothing runs: a period sent whole is due its line.
            Some(&end) => now.saturating_duration_since(w.idle_since.max(end)) > w.allowance(),
            None => false,
        }
    }
}

/// A time of day as the configure key utc takes it: "HH:MM:SS".
fn utc(seconds: f64) -> String {
    let s = (seconds as u64) % 86400;
    format!("{:02}:{:02}:{:02}", s / 3600, s / 60 % 60, s % 60)
}

fn unix_time() -> f64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0.0, |d| d.as_secs_f64())
}

/// Send audio a period at a time, each period's label in a configure before its first sample, then halt.
///
/// Live, start on the next UTC period boundary, label each period with its UTC start and send each frame when its last sample is due, as a receiver's audio arrives.
fn send_audio(
    input: &Input,
    audio: &[u8],
    mode: Mode,
    mut label: f64,
    live: bool,
    watchdog: Option<&Watchdog>,
) {
    let period = (mode.period() * RATE as f64) as usize;
    let samples = audio.len() / 2;
    if live {
        let boundary = ((unix_time() / mode.period()).floor() + 1.0) * mode.period();
        thread::sleep(Duration::from_secs_f64((boundary - unix_time()).max(0.0)));
        label = boundary;
    }
    let start = Instant::now();
    // After halt the codec's exit is due as a period's answer is, from the last sample's scheduled time; no line answers it, and it is registered before the first sample, as a codec that stops reading blocks the writer.
    if let Some(w) = watchdog {
        w.period_due(
            "halt".into(),
            start + Duration::from_secs_f64(samples as f64 / RATE as f64),
        );
    }
    for (k, first) in (0..samples).step_by(period).enumerate() {
        let period_label = label + k as f64 * mode.period();
        let end = (first + period).min(samples);
        if let Some(watchdog) = watchdog {
            if end - first == period {
                let due = start + Duration::from_secs_f64(end as f64 / RATE as f64);
                watchdog.period_due(utc(period_label).replace(':', ""), due);
            }
        }
        input.send(&json!({"t": "configure", "utc": utc(period_label)}));
        for i in (first..end).step_by(CHUNK) {
            let j = (i + CHUNK).min(end);
            if live {
                let due = start + Duration::from_secs_f64(j as f64 / RATE as f64);
                thread::sleep(due.saturating_duration_since(Instant::now()));
            }
            if !input.is_open() {
                return;
            }
            input.frame(AUDIO, &audio[2 * i..2 * j]);
        }
    }
    input.halt();
}

/// A decode line as this client prints it: DT to one decimal, rounded half to even as WSJT-X shows it, which Rust's {:.1} does on the parsed value; a dt that is not finite arrives as null and prints as nan.
fn decode_text(line: &Value) -> Option<String> {
    let time = line["time"].as_str()?;
    let snr = line["snr"].as_i64()?;
    let dt = match line.get("dt")? {
        Value::Null => "nan".to_string(),
        dt => format!("{:.1}", dt.as_f64()?),
    };
    let freq = line["freq"].as_i64()?;
    let message = line["message"].as_str()?;
    Some(format!("decode {time} {snr} {dt} {freq} {message}"))
}

/// Stream the audio, print the decodes, and return the exit status and the messages decoded.
fn run(codec: &mut Codec, audio: Vec<u8>, mode: Mode, label: f64, live: bool) -> (u8, Vec<String>) {
    let watchdog = live.then(|| Arc::new(Watchdog::new(mode)));
    let writer = {
        let input = Arc::clone(&codec.input);
        let watchdog = watchdog.clone();
        thread::spawn(move || send_audio(&input, &audio, mode, label, live, watchdog.as_deref()))
    };
    let mut messages = Vec::new();
    let mut errors = 0;
    loop {
        let line = match codec.lines.recv_timeout(Duration::from_millis(500)) {
            Ok(line) => line,
            Err(RecvTimeoutError::Timeout) => Value::Null,
            Err(RecvTimeoutError::Disconnected) => break, // the end of the codec's output
        };
        if let Some(watchdog) = &watchdog {
            watchdog.saw(&line);
            if watchdog.hung() {
                eprintln!(
                    "codec_client: no progress from the codec in {:.0} s: it has hung",
                    watchdog.allowance().as_secs_f64()
                );
                let _ = codec.child.kill();
                let _ = codec.child.wait();
                return (HUNG, messages);
            }
        }
        // Other lines, and kinds this client does not know, are skipped.
        match line["t"].as_str() {
            Some("decode") => match decode_text(&line) {
                Some(text) => {
                    println!("{text}");
                    messages.push(line["message"].as_str().unwrap_or_default().to_string());
                }
                None => eprintln!("codec_client: decode line without its fields: {line}"),
            },
            Some("decode_finished") => println!(
                "finished {}",
                line["period_end"].as_str().unwrap_or_default()
            ),
            Some("error") => {
                errors += 1;
                eprintln!("codec_client: codec error: {line}");
            }
            _ => {}
        }
    }
    // Its output has ended, so its exit is due: the watchdog no longer runs to catch a hang.
    let status = match codec.exit_status() {
        Ok(Some(status)) => status,
        Ok(None) => return (HUNG, messages), // without joining the writer, which may be blocked on the codec's pipe
        Err(error) => {
            eprintln!("codec_client: cannot wait for wsjt-codec: {error}");
            return (FAILED, messages);
        }
    };
    let _ = writer.join();
    if status.success() {
        (if errors > 0 { FAILED } else { OK }, messages)
    } else {
        eprintln!("codec_client: wsjt-codec ended with {status}");
        (FAILED, messages)
    }
}

struct Args {
    command: String,
    codec: PathBuf,
    data: PathBuf,
    mode: Mode,
    depth: u8,
    freq: i64,
    out: Option<PathBuf>,
    operand: String, // the WAV file, or render's TEXT
}

fn parse_args(mut words: impl Iterator<Item = String>) -> Result<Args, String> {
    let command = words.next().ok_or("no command")?;
    if !["decode", "live", "render"].contains(&command.as_str()) {
        return Err(format!("unknown command {command}"));
    }
    let mut args = Args {
        command,
        codec: PathBuf::new(), // required: set below
        data: PathBuf::from("."),
        mode: Mode::Ft8,
        depth: 3,
        freq: 1500,
        out: None,
        operand: String::new(),
    };
    let (mut codec, mut operands, mut options_end) = (None, Vec::new(), false);
    while let Some(word) = words.next() {
        if options_end || !word.starts_with("--") {
            operands.push(word);
            continue;
        }
        if word == "--" {
            options_end = true;
            continue;
        }
        let (name, inline) = match word.split_once('=') {
            Some((name, value)) => (name.to_string(), Some(value.to_string())),
            None => (word, None),
        };
        let render_option = args.command == "render" && (name == "--freq" || name == "--out");
        if !(["--codec", "--data", "--mode", "--depth"].contains(&name.as_str()) || render_option) {
            return Err(format!("unknown option {name}"));
        }
        let value = inline
            .or_else(|| words.next())
            .ok_or(format!("{name} needs a value"))?;
        match name.as_str() {
            "--codec" => codec = Some(PathBuf::from(value)),
            "--data" => args.data = PathBuf::from(value),
            "--mode" => {
                args.mode = match value.as_str() {
                    "FT8" => Mode::Ft8,
                    "FT4" => Mode::Ft4,
                    _ => return Err(format!("--mode {value}: FT8 or FT4")),
                }
            }
            "--depth" => {
                args.depth = value
                    .parse()
                    .ok()
                    .filter(|d| (1..=3).contains(d))
                    .ok_or(format!("--depth {value}: 1 to 3"))?
            }
            "--freq" => {
                args.freq = value
                    .parse()
                    .map_err(|_| format!("--freq {value}: an integer"))?
            }
            _ => args.out = Some(PathBuf::from(value)),
        }
    }
    args.codec = codec.ok_or("--codec is required")?;
    if operands.len() != 1 {
        let operand = if args.command == "render" {
            "TEXT"
        } else {
            "WAV file"
        };
        return Err(format!("{} takes one {operand}", args.command));
    }
    args.operand = operands.remove(0);
    Ok(args)
}

/// Start the codec and wait for its ready line, which lists the modes it decodes and encodes.
fn start(args: &Args, kinds: &str) -> Option<Codec> {
    let mut codec = match Codec::start(&args.codec, &args.data) {
        Ok(codec) => codec,
        Err(error) => {
            eprintln!(
                "codec_client: cannot start {}: {error}",
                args.codec.display()
            );
            return None;
        }
    };
    let line = match codec.lines.recv_timeout(READY_WAIT) {
        Ok(line) => line,
        Err(RecvTimeoutError::Timeout) => {
            eprintln!(
                "codec_client: no ready line within {} s",
                READY_WAIT.as_secs()
            );
            let _ = codec.child.kill();
            let _ = codec.child.wait();
            return None;
        }
        Err(RecvTimeoutError::Disconnected) => Value::Null, // the codec ended before ready
    };
    let listed = line[kinds]
        .as_array()
        .is_some_and(|modes| modes.iter().any(|m| m.as_str() == Some(args.mode.name())));
    if line["t"].as_str() != Some("ready") || !listed {
        eprintln!(
            "codec_client: no ready line naming {} in {kinds}: {line}",
            args.mode.name()
        );
        codec.quit();
        return None;
    }
    Some(codec)
}

fn le16(bytes: &[u8]) -> u16 {
    u16::from_le_bytes([bytes[0], bytes[1]])
}

fn le32(bytes: &[u8]) -> u32 {
    u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]])
}

/// WAVE_FORMAT_EXTENSIBLE's subformat GUID for PCM, as the file holds it.
const PCM_SUBFORMAT: [u8; 16] = [
    1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71,
];

/// The samples of a 12000 Hz mono 16-bit PCM WAV file, plain or WAVE_FORMAT_EXTENSIBLE: 16-bit little-endian, the stream's format.
fn read_wav(path: &Path) -> Result<Vec<u8>, String> {
    let bytes = fs::read(path).map_err(|error| format!("{}: {error}", path.display()))?;
    if bytes.len() < 12 || &bytes[0..4] != b"RIFF" || &bytes[8..12] != b"WAVE" {
        return Err(format!("{} is not a WAV file", path.display()));
    }
    let mut format = None;
    let mut at = 12;
    while at + 8 <= bytes.len() {
        let size = le32(&bytes[at + 4..]) as usize;
        let body = &bytes[at + 8..(at + 8).saturating_add(size).min(bytes.len())];
        match &bytes[at..at + 4] {
            b"fmt " if body.len() >= 16 => {
                let extensible_pcm = body.len() >= 40 && body[24..40] == PCM_SUBFORMAT;
                let tag = match le16(body) {
                    0xFFFE if extensible_pcm => 1,
                    tag => tag,
                };
                format = Some((tag, le16(&body[2..]), le32(&body[4..]), le16(&body[14..])))
            }
            b"data" if format == Some((1, 1, RATE, 16)) => {
                return Ok(body[..body.len() / 2 * 2].to_vec())
            }
            b"data" => break,
            _ => {}
        }
        at = at.saturating_add(size).saturating_add(8 + size % 2); // a chunk is padded to an even length
    }
    Err(format!(
        "{} is not 12000 Hz mono 16-bit PCM",
        path.display()
    ))
}

/// render's --out file, opened before the codec starts. A file this client created is removed again unless the audio is written to it.
struct OutFile {
    path: PathBuf,
    file: File,
    created: bool,
    written: bool,
}

impl OutFile {
    fn open(path: &Path) -> io::Result<OutFile> {
        let (file, created) = match OpenOptions::new().write(true).create_new(true).open(path) {
            Ok(file) => (file, true),
            Err(error) if error.kind() == io::ErrorKind::AlreadyExists => {
                (OpenOptions::new().write(true).open(path)?, false)
            }
            Err(error) => return Err(error),
        };
        let path = path.to_path_buf();
        Ok(OutFile {
            path,
            file,
            created,
            written: false,
        })
    }

    fn write(&mut self, audio: &[u8]) -> io::Result<()> {
        self.file.set_len(0)?;
        self.file.write_all(&wav(audio))?;
        self.written = true;
        Ok(())
    }
}

impl Drop for OutFile {
    fn drop(&mut self) {
        if self.created && !self.written {
            let _ = fs::remove_file(&self.path);
        }
    }
}

/// A 12000 Hz mono 16-bit PCM WAV file holding the samples.
fn wav(audio: &[u8]) -> Vec<u8> {
    let size = audio.len() as u32;
    let mut wav = Vec::with_capacity(44 + audio.len());
    wav.extend_from_slice(b"RIFF");
    wav.extend_from_slice(&(36 + size).to_le_bytes());
    wav.extend_from_slice(b"WAVEfmt ");
    wav.extend_from_slice(&16u32.to_le_bytes());
    wav.extend_from_slice(&1u16.to_le_bytes()); // PCM
    wav.extend_from_slice(&1u16.to_le_bytes()); // mono
    wav.extend_from_slice(&RATE.to_le_bytes());
    wav.extend_from_slice(&(2 * RATE).to_le_bytes()); // bytes a second
    wav.extend_from_slice(&2u16.to_le_bytes()); // bytes a sample
    wav.extend_from_slice(&16u16.to_le_bytes()); // bits a sample
    wav.extend_from_slice(b"data");
    wav.extend_from_slice(&size.to_le_bytes());
    wav.extend_from_slice(audio);
    wav
}

/// WSJT-X names a saved period YYMMDD_HHMMSS.wav: its start, in seconds of the day, or 0.
fn file_label(path: &str) -> f64 {
    let name = path.as_bytes();
    if name.len() < 11 || !name[name.len() - 4..].eq_ignore_ascii_case(b".wav") {
        return 0.0;
    }
    let tail = &name[name.len() - 11..name.len() - 4];
    if tail[0] != b'_' || !tail[1..].iter().all(u8::is_ascii_digit) {
        return 0.0;
    }
    let two = |k: usize| u32::from(tail[k] - b'0') * 10 + u32::from(tail[k + 1] - b'0');
    let (h, m, s) = (two(1), two(3), two(5));
    if h < 24 && m < 60 && s < 60 {
        f64::from(h * 3600 + m * 60 + s)
    } else {
        0.0
    }
}

/// Standard base64, as the pcm lines carry the audio.
fn base64_decode(text: &str) -> Option<Vec<u8>> {
    let mut out = Vec::with_capacity(text.len() / 4 * 3);
    let (mut bits, mut count) = (0u32, 0);
    for c in text.bytes().take_while(|&c| c != b'=') {
        let value = match c {
            b'A'..=b'Z' => c - b'A',
            b'a'..=b'z' => c - b'a' + 26,
            b'0'..=b'9' => c - b'0' + 52,
            b'+' => 62,
            b'/' => 63,
            _ => return None,
        };
        bits = (bits << 6 | u32::from(value)) & 0xFFFF;
        count += 6;
        if count >= 8 {
            count -= 8;
            out.push((bits >> count) as u8);
        }
    }
    Some(out)
}

/// The configure that sets the mode. These examples have no station, so no calls (the decoders use the starting calls otherwise), and a configure that carries the mode resets nfa, nfb and ntol.
fn mode_configure(args: &Args) -> Value {
    json!({"t": "configure", "mode": args.mode.name(), "depth": args.depth,
           "mycall": "", "his_call": "", "his_grid": "", "nfa": 200, "nfb": 4000, "ntol": 1000})
}

fn decode(args: &Args) -> u8 {
    let audio = match read_wav(Path::new(&args.operand)) {
        Ok(audio) => audio,
        Err(error) => {
            eprintln!("codec_client: {error}");
            return USAGE;
        }
    };
    let Some(mut codec) = start(args, "modes") else {
        return FAILED;
    };
    let live = args.command == "live";
    let mut configure = mode_configure(args);
    if live {
        configure["live_audio"] = json!(true);
        configure["busy_events"] = json!(true);
    }
    codec.input.send(&configure);
    run(
        &mut codec,
        audio,
        args.mode,
        file_label(&args.operand),
        live,
    )
    .0
}

fn render(args: &Args) -> u8 {
    // Open --out before the codec starts: a path that cannot be written is a usage error, and an OutFile dropped unwritten removes the file it created.
    let mut out = None;
    if let Some(path) = &args.out {
        match OutFile::open(path) {
            Ok(file) => out = Some(file),
            Err(error) => {
                eprintln!("codec_client: {}: {error}", path.display());
                return USAGE;
            }
        }
    }
    let Some(mut codec) = start(args, "encodes") else {
        return FAILED;
    };
    let fail = |codec: &mut Codec, why: String| {
        eprintln!("codec_client: render failed: {why}");
        codec.quit();
        FAILED
    };
    codec.input.send(&json!({"t": "render", "id": 1, "mode": args.mode.name(), "text": args.operand, "freq": args.freq, "rate": RATE}));
    let (mut encoded, mut audio) = (Value::Null, Vec::new());
    loop {
        // The reply: encoded, its pcm lines, then rendered; or rejected.
        let Ok(line) = codec.lines.recv() else {
            return fail(&mut codec, "the codec's output ended".to_string());
        };
        match line["t"].as_str() {
            Some("rejected" | "error") => return fail(&mut codec, line.to_string()),
            Some("encoded") => encoded = line,
            Some("pcm") => match line["data"].as_str().and_then(base64_decode) {
                Some(samples) => audio.extend(samples), // int16 little-endian samples at the requested rate
                None => return fail(&mut codec, line.to_string()),
            },
            Some("rendered") => break,
            _ => {}
        }
    }
    let (Some(samples), Some(msgsent)) = (encoded["samples"].as_u64(), encoded["msgsent"].as_str())
    else {
        return fail(
            &mut codec,
            format!("no encoded line with samples and msgsent: {encoded}"),
        );
    };
    println!("rendered {samples} {msgsent}");
    if let Some(out) = &mut out {
        if let Err(error) = out.write(&audio) {
            eprintln!("codec_client: {}: {error}", out.path.display());
            codec.quit();
            return FAILED;
        }
    }
    // Decode it back: the audio where WSJT-X starts it, in a period of silence.
    let period = (args.mode.period() * RATE as f64) as usize;
    let offset = (args.mode.tx_start() * RATE as f64) as usize;
    let mut signal = vec![0u8; 2 * offset];
    signal.extend_from_slice(&audio);
    signal.resize(2 * period, 0);
    codec.input.send(&mode_configure(args));
    let (status, messages) = run(&mut codec, signal, args.mode, 0.0, false);
    let words = |text: &str| text.split_whitespace().collect::<Vec<_>>().join(" ");
    if status == OK && !messages.iter().any(|m| words(m) == words(msgsent)) {
        eprintln!("codec_client: round trip failed: no decode of {msgsent}");
        return FAILED;
    }
    status
}

fn main() -> ExitCode {
    if env::args()
        .skip(1)
        .take_while(|word| word != "--")
        .any(|word| word == "-h" || word == "--help")
    {
        println!("{USAGE_TEXT}");
        return ExitCode::from(OK);
    }
    let status = match parse_args(env::args().skip(1)) {
        Ok(args) if args.command == "render" => render(&args),
        Ok(args) => decode(&args),
        Err(error) => {
            eprintln!("{USAGE_TEXT}\ncodec_client: {error}");
            USAGE
        }
    };
    ExitCode::from(status)
}
