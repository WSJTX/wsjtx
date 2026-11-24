// SPDX-License-Identifier: GPL-3.0-or-later
//! A wsjt-codec client: decode a WAV file, replay one as live audio, or render a message.
//!
//!   codec_client decode --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--low HZ] [--high HZ] [--mycall CALL] WAV
//!   codec_client live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
//!   codec_client render --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--out WAV] [TEXT...]
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
const RELEASE_WAIT: Duration = Duration::from_secs(2); // how long, once it has exited, the client waits for the end of its output and for its writes to its input: a process the codec started can hold the pipes

/// What decode takes: the modes ready lists in modes.
const DECODES: [&str; 13] = [
    "FT8", "FT4", "JT9", "JT65", "JT4", "FST4", "FST4W", "Q65", "MSK144", "ECHO", "JTTY",
    "FREQCAL", "WSPR",
];
/// What render takes: those it lists in encodes.
const ENCODES: [&str; 15] = [
    "FT8", "FT4", "JT4", "JT9", "JT65", "Q65", "FST4", "FST4W", "WSPR", "MSK144", "ECHO", "TUNE",
    "FOX", "SUPERFOX", "JTTY",
];
/// What live takes.
const LIVE: [&str; 2] = ["FT8", "FT4"];
/// The modes with a submode: A unless --submode gives one.
const SUBMODES: [&str; 4] = ["JT4", "JT9", "JT65", "Q65"];
/// The slot modes whose render takes trperiod; fast JT9's does too.
const PERIODS: [&str; 4] = ["Q65", "FST4", "FST4W", "MSK144"];

/// Outside live, how long the codec may send no line before the client takes it to have hung: 300 s, the longest interface.md allows a decode pass between progress lines before one has completed, or four periods of the given seconds, up to the codec's 1800 s, when that is longer.
/// For the examples' check only (README.md, "The check"), not a setting: CODEC_EXAMPLE_CHECK_SILENCE_SECONDS, a whole number from 1 to 300, replaces it, so that a run that checks it need not last 300 s.
fn silence_wait(seconds: Option<f64>) -> Duration {
    let check = env::var("CODEC_EXAMPLE_CHECK_SILENCE_SECONDS").unwrap_or_default();
    let digits = (1..=3).contains(&check.len()) && check.bytes().all(|b| b.is_ascii_digit());
    match check.parse::<u64>() {
        Ok(check) if digits && (1..=300).contains(&check) => Duration::from_secs(check),
        _ => Duration::from_secs_f64(f64::max(300.0, 4.0 * seconds.unwrap_or_default().min(1800.0))),
    }
}

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
  codec_client decode --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--low HZ] [--high HZ] [--mycall CALL] WAV
  codec_client live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
  codec_client render --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--out WAV] [TEXT...]";

/// The period, s, when --period gives none: the mode's own, as a configure that carries the mode sets it; FreqCal has none, so the examples take 15 s; TUNE's is the length of its tone. JTTY has no periods.
fn mode_period(mode: &str) -> Option<f64> {
    match mode {
        "FT8" | "FREQCAL" | "TUNE" | "FOX" | "SUPERFOX" => Some(15.0),
        "FT4" => Some(7.5),
        "JT9" | "JT65" | "JT4" | "FST4" | "Q65" => Some(60.0),
        "FST4W" | "WSPR" => Some(120.0),
        "MSK144" => Some(30.0),
        "ECHO" => Some(3.0),
        _ => None,
    }
}

/// What render decodes the mode back with; every other mode, itself.
fn decoded_as(mode: &'static str) -> &'static str {
    match mode {
        "TUNE" => "FREQCAL",
        "FOX" | "SUPERFOX" => "FT8",
        _ => mode,
    }
}

/// A live pass's time between progress lines, s, once one has completed: FT4's, or FT8's.
fn live_allowance(mode: &str) -> f64 {
    if mode == "FT4" {
        6.0
    } else {
        10.0
    }
}

/// nfa and nfb when --low and --high give none: in FST4 they are its F Low and F High, WSJT-X's 600 and 1400 Hz; elsewhere the waterfall's range.
fn search(mode: &str) -> (i64, i64) {
    if mode == "FST4" {
        (600, 1400)
    } else {
        (200, 4000)
    }
}

/// Where WSJT-X starts a transmission in its period, s.
fn tx_start(mode: &str, period: f64, fast: bool) -> f64 {
    match mode {
        "MSK144" | "ECHO" | "TUNE" => 0.0,
        _ if fast => 0.0,
        "SUPERFOX" => 0.4,
        "FT4" => 0.3,
        "FT8" | "FOX" => 0.5,
        "FST4" if period == 15.0 => 0.5,
        "Q65" if period == 15.0 || period == 30.0 => 0.5,
        _ => 1.0,
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
        // Integer keys stay in integer types (depth is a u8): serde_json writes those as plain JSON integers, and an f64 as 3.0, which a configure declines for an integer key (configure_type_error).
        self.frame(CONTROL, message.to_string().as_bytes());
    }

    /// Decode what is in, end the session and close the codec's input.
    fn halt(&self) {
        self.send(&json!({"t": "halt"}));
        self.0.lock().unwrap().take();
    }
}

/// One wsjt-codec process: frames go to its stdin; each stdout line that is a JSON object comes back as a JSON value, and the channel closes at the end of its output.
struct Codec {
    child: Child,
    input: Arc<Input>,
    lines: Receiver<Value>,
    exited_at: Option<Instant>, // when the client saw the codec's exit
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
                    Ok(line) if line.is_object() => {
                        let _ = sender.send(line); // read on when no one takes the lines: the pipe must not fill
                    }
                    _ => eprintln!("codec_client: not a JSON object: {text}"),
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
            exited_at: None,
        };
        codec.input.write(&HEADER);
        Ok(codec)
    }

    /// The next line within the timeout; Disconnected once the codec's output has ended, or RELEASE_WAIT after the codec has exited, as a process it started can hold the pipe.
    fn next(&mut self, timeout: Duration) -> Result<Value, RecvTimeoutError> {
        let deadline = Instant::now() + timeout;
        loop {
            let slice = deadline
                .saturating_duration_since(Instant::now())
                .min(Duration::from_millis(50));
            match self.lines.recv_timeout(slice) {
                Err(RecvTimeoutError::Timeout) => {}
                got => return got,
            }
            if self.exited_at.is_none() && matches!(self.child.try_wait(), Ok(Some(_))) {
                self.exited_at = Some(Instant::now());
            }
            if self.exited_at.is_some_and(|at| at.elapsed() >= RELEASE_WAIT) {
                return Err(RecvTimeoutError::Disconnected); // what the codec wrote before it exited has been read: let go of the pipe
            }
            if Instant::now() >= deadline {
                return Err(RecvTimeoutError::Timeout);
            }
        }
    }

    /// The codec's exit status once it exits, within EXIT_WAIT; None, with the codec stopped, when it does not.
    fn exit_status(&mut self) -> io::Result<Option<ExitStatus>> {
        let deadline = Instant::now() + EXIT_WAIT;
        loop {
            if let Some(status) = self.child.try_wait()? {
                self.exited_at.get_or_insert_with(Instant::now); // what uses its pipes has RELEASE_WAIT more, from the first time the client sees the exit
                return Ok(Some(status));
            }
            if Instant::now() >= deadline {
                eprintln!(
                    "codec_client: wsjt-codec did not exit within {} s: it has hung",
                    EXIT_WAIT.as_secs()
                );
                self.stop();
                return Ok(None);
            }
            thread::sleep(Duration::from_millis(50));
        }
    }

    /// Stop the codec at once.
    fn stop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
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
    mode: &'static str,
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
            live_allowance(self.mode)
        } else {
            f64::max(300.0, 4.0 * mode_period(self.mode).unwrap_or_default())
        })
    }

    fn answer(&mut self, label: Option<&str>) {
        if let Some(label) = label {
            self.waiting.remove(label);
        }
    }
}

impl Watchdog {
    fn new(mode: &'static str) -> Watchdog {
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

/// Send audio a period of the given seconds at a time, each period's label in a configure before its first sample, then halt; with no period (JTTY), all of it with no label.
///
/// Live, start on the next UTC period boundary, label each period with its UTC start and send each frame when its last sample is due, as a receiver's audio arrives.
fn send_audio(
    input: &Input,
    audio: &[u8],
    seconds: Option<f64>,
    mut label: f64,
    live: bool,
    watchdog: Option<&Watchdog>,
) {
    let samples = audio.len() / 2;
    let period = match seconds {
        Some(seconds) => (seconds * f64::from(RATE)) as usize, // at least 1: parse_args takes no shorter --period
        None => samples.max(1),
    };
    if let (true, Some(seconds)) = (live, seconds) {
        let boundary = ((unix_time() / seconds).floor() + 1.0) * seconds;
        thread::sleep(Duration::from_secs_f64((boundary - unix_time()).max(0.0)));
        label = boundary;
    }
    let start = Instant::now();
    // After halt the codec's exit is due as a period's answer is, from the last sample's scheduled time; no line answers it, and it is registered before the first sample, as a codec that stops reading blocks the writer.
    if let Some(w) = watchdog {
        w.period_due(
            "halt".into(),
            start + Duration::from_secs_f64(samples as f64 / f64::from(RATE)),
        );
    }
    for (k, first) in (0..samples).step_by(period).enumerate() {
        let end = (first + period).min(samples);
        if let Some(seconds) = seconds {
            let period_label = label + k as f64 * seconds;
            if let Some(watchdog) = watchdog {
                if end - first == period {
                    let due = start + Duration::from_secs_f64(end as f64 / f64::from(RATE));
                    watchdog.period_due(utc(period_label).replace(':', ""), due);
                }
            }
            input.send(&json!({"t": "configure", "utc": utc(period_label)}));
        }
        for i in (first..end).step_by(CHUNK) {
            let j = (i + CHUNK).min(end);
            if live {
                let due = start + Duration::from_secs_f64(j as f64 / f64::from(RATE));
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

/// A JSON number as the examples print it: one decimal, rounded half to even as WSJT-X shows DT, which Rust's {:.1} does on the parsed value; null (a value that is not finite) prints as nan. None for a field that is missing or not a number.
fn number(line: &Value, key: &str) -> Option<String> {
    match line.get(key)? {
        Value::Null => Some("nan".to_string()),
        value => Some(format!("{:.1}", value.as_f64()?)),
    }
}

/// A decode line as this client prints it.
fn decode_text(line: &Value) -> Option<String> {
    let time = line["time"].as_str()?;
    let snr = line["snr"].as_i64()?;
    let dt = number(line, "dt")?;
    let freq = line["freq"].as_i64()?;
    let message = line["message"].as_str()?;
    Some(format!("decode {time} {snr} {dt} {freq} {message}"))
}

/// An echo line (Echo: one a period) as this client prints it.
fn echo_text(line: &Value) -> Option<String> {
    let time = line["time"].as_str()?;
    let level = number(line, "level")?;
    let snr = number(line, "snr")?;
    let dfreq = number(line, "dfreq")?;
    let quality = line["quality"].as_i64()?;
    Some(format!("echo {time} {level} {snr} {dfreq} {quality}"))
}

/// A freqcal line (FreqCal: one a measurement) as this client prints it.
fn freqcal_text(line: &Value) -> Option<String> {
    let time = line["time"].as_str()?;
    let freq = number(line, "freq")?;
    let error = number(line, "error")?;
    let level = number(line, "level")?;
    let snr = number(line, "snr")?;
    Some(format!("freqcal {time} {freq} {error} {level} {snr}"))
}

/// A JTTY message's last line (complete, expired or ended) as this client prints it.
fn jtty_text(line: &Value) -> Option<String> {
    let state = line["state"].as_str()?;
    let start = number(line, "start")?;
    let snr = line["snr"].as_i64()?;
    let freq = number(line, "freq")?;
    let text = line["text"].as_str()?;
    Some(format!("jtty {state} {start} {snr} {freq} {text}"))
}

/// Stream the audio, print what the codec decodes and measures, and return the exit status, the messages decoded (JTTY's complete ones among them) and the echo and freqcal lines.
fn run(
    codec: &mut Codec,
    audio: Vec<u8>,
    mode: &'static str,
    seconds: Option<f64>,
    label: f64,
    live: bool,
) -> (u8, Vec<String>, Vec<Value>) {
    let watchdog = live.then(|| Arc::new(Watchdog::new(mode)));
    let writer = {
        let input = Arc::clone(&codec.input);
        let watchdog = watchdog.clone();
        thread::spawn(move || send_audio(&input, &audio, seconds, label, live, watchdog.as_deref()))
    };
    let mut messages = Vec::new();
    let mut measured = Vec::new();
    let mut errors = 0;
    let mut heard = Instant::now();
    let silence = silence_wait(seconds);
    loop {
        let line = match codec.next(Duration::from_millis(500)) {
            Ok(line) => {
                heard = Instant::now();
                line
            }
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
                codec.stop();
                return (HUNG, messages, measured);
            }
        } else if heard.elapsed() > silence {
            eprintln!(
                "codec_client: no line from the codec in {} s: it has hung",
                silence.as_secs()
            );
            codec.stop();
            return (HUNG, messages, measured); // without joining the writer, which may be blocked on the codec's pipe
        }
        // Other lines, and kinds this client does not know, are skipped.
        let kind = line["t"].as_str();
        let text = match kind {
            Some("decode") => decode_text(&line),
            Some("echo") => echo_text(&line),
            Some("freqcal") => freqcal_text(&line),
            // JTTY: a message's last line; growing ones are its text so far.
            Some("jtty_update") if line["state"].as_str() != Some("growing") => jtty_text(&line),
            Some("decode_finished") => {
                println!(
                    "finished {}",
                    line["period_end"].as_str().unwrap_or_default()
                );
                continue;
            }
            Some("error") => {
                errors += 1;
                eprintln!("codec_client: codec error: {line}");
                continue;
            }
            _ => continue,
        };
        let Some(text) = text else {
            eprintln!(
                "codec_client: {} line without its fields: {line}",
                kind.unwrap_or_default()
            );
            continue;
        };
        println!("{text}");
        match kind {
            Some("decode") => {
                messages.push(line["message"].as_str().unwrap_or_default().to_string())
            }
            Some("jtty_update") if line["state"].as_str() == Some("complete") => {
                messages.push(line["text"].as_str().unwrap_or_default().to_string())
            }
            Some("echo" | "freqcal") => measured.push(line),
            _ => {}
        }
    }
    // Its output has ended, so its exit is due: the watchdog no longer runs to catch a hang.
    let status = match codec.exit_status() {
        Ok(Some(status)) => status,
        Ok(None) => return (HUNG, messages, measured), // without joining the writer, which may be blocked on the codec's pipe
        Err(error) => {
            eprintln!("codec_client: cannot wait for wsjt-codec: {error}");
            return (FAILED, messages, measured);
        }
    };
    // What is left of RELEASE_WAIT from the codec's exit, as the client first saw it, then without the writer: it may be blocked on the codec's input, which a process the codec started holds.
    let release = codec.exited_at.unwrap_or_else(Instant::now) + RELEASE_WAIT;
    while !writer.is_finished() && Instant::now() < release {
        thread::sleep(Duration::from_millis(50));
    }
    if writer.is_finished() {
        let _ = writer.join();
    }
    if status.success() {
        (if errors > 0 { FAILED } else { OK }, messages, measured)
    } else {
        eprintln!("codec_client: wsjt-codec ended with {status}");
        (FAILED, messages, measured)
    }
}

struct Args {
    command: String,
    codec: PathBuf,
    data: PathBuf,
    mode: &'static str,
    depth: u8,
    submode: String,
    period: Option<f64>, // the mode's unless --period gives one; JTTY none
    fast: bool,
    freq: Option<i64>, // decode: the receive frequency, sent only when given; render: the lowest tone
    low: Option<i64>,
    high: Option<i64>,
    mycall: String,
    out: Option<PathBuf>,
    operands: Vec<String>, // the WAV file, or render's TEXT
}

fn parse_args(mut words: impl Iterator<Item = String>) -> Result<Args, String> {
    let command = words.next().ok_or("no command")?;
    // The modes each command takes, and its options beyond those every command takes.
    let (modes, options): (&[&'static str], &[&str]) = match command.as_str() {
        "decode" => (
            &DECODES,
            &[
                "--submode",
                "--period",
                "--fast",
                "--freq",
                "--low",
                "--high",
                "--mycall",
            ],
        ),
        "live" => (&LIVE, &[]),
        "render" => (
            &ENCODES,
            &["--submode", "--period", "--fast", "--freq", "--out"],
        ),
        _ => return Err(format!("unknown command {command}")),
    };
    let mut args = Args {
        command,
        codec: PathBuf::new(), // required: set below
        data: PathBuf::from("."),
        mode: "FT8",
        depth: 3,
        submode: "A".to_string(),
        period: None,
        fast: false,
        freq: None,
        low: None,
        high: None,
        mycall: String::new(),
        out: None,
        operands: Vec::new(),
    };
    let (mut codec, mut options_end) = (None, false);
    while let Some(word) = words.next() {
        if options_end || !word.starts_with("--") {
            args.operands.push(word);
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
        if !(["--codec", "--data", "--mode", "--depth"].contains(&name.as_str())
            || options.contains(&name.as_str()))
        {
            return Err(format!("unknown option {name}"));
        }
        if name == "--fast" {
            if inline.is_some() {
                return Err("--fast takes no value".to_string());
            }
            args.fast = true;
            continue;
        }
        let value = inline
            .or_else(|| words.next())
            .ok_or(format!("{name} needs a value"))?;
        match name.as_str() {
            "--codec" => codec = Some(PathBuf::from(value)),
            "--data" => args.data = PathBuf::from(value),
            "--mode" => {
                args.mode = modes
                    .iter()
                    .find(|&&mode| mode == value)
                    .copied()
                    .ok_or(format!("--mode {value}: one of {}", modes.join(", ")))?
            }
            "--depth" => {
                args.depth = value
                    .parse()
                    .ok()
                    .filter(|d| (1..=3).contains(d))
                    .ok_or(format!("--depth {value}: 1 to 3"))?
            }
            "--submode" => {
                if !(value.len() == 1 && ("A"..="H").contains(&value.as_str())) {
                    return Err(format!("--submode {value}: A to H"));
                }
                args.submode = value
            }
            "--period" => {
                // Finite, and at least one sample long, as the audio is sent a period at a time.
                args.period = Some(
                    value
                        .parse()
                        .ok()
                        .filter(|p: &f64| p.is_finite() && p * f64::from(RATE) >= 1.0)
                        .ok_or(format!(
                            "--period {value}: a number of seconds that holds a sample at least"
                        ))?,
                )
            }
            "--freq" | "--low" | "--high" => {
                let hz = value
                    .parse()
                    .map_err(|_| format!("{name} {value}: an integer"))?;
                match name.as_str() {
                    "--freq" => args.freq = Some(hz),
                    "--low" => args.low = Some(hz),
                    _ => args.high = Some(hz),
                }
            }
            "--mycall" => args.mycall = value,
            _ => args.out = Some(PathBuf::from(value)),
        }
    }
    args.codec = codec.ok_or("--codec is required")?;
    if args.period.is_none() {
        args.period = mode_period(args.mode);
    }
    if args.command != "render" {
        if args.operands.len() != 1 {
            return Err(format!("{} takes one WAV file", args.command));
        }
        return Ok(args);
    }
    // MSK144's tones are at 1000 and 2000 Hz, and SuperFox goes at 750.
    args.freq.get_or_insert(match args.mode {
        "MSK144" => 1000,
        "SUPERFOX" => 750,
        _ => 1500,
    });
    let (fits, takes) = match args.mode {
        "TUNE" | "ECHO" => (args.operands.is_empty(), "no TEXT"),
        "FOX" | "SUPERFOX" | "JTTY" => (!args.operands.is_empty(), "one TEXT or more"),
        _ => (args.operands.len() == 1, "one TEXT"),
    };
    if !fits {
        return Err(format!("render --mode {} takes {takes}", args.mode));
    }
    Ok(args)
}

/// Start the codec and wait for its ready line, which lists the modes it decodes and encodes; decodes, a mode it must decode too.
fn start(args: &Args, kinds: &str, decodes: Option<&str>) -> Option<Codec> {
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
    let line = match codec.next(READY_WAIT) {
        Ok(line) => line,
        Err(RecvTimeoutError::Timeout) => json!(format!("none within {} s", READY_WAIT.as_secs())),
        Err(RecvTimeoutError::Disconnected) => Value::Null, // the codec's output ended before ready
    };
    let lists = |key: &str, mode: &str| {
        line[key]
            .as_array()
            .is_some_and(|modes| modes.iter().any(|m| m.as_str() == Some(mode)))
    };
    if line["t"].as_str() != Some("ready")
        || !lists(kinds, args.mode)
        || decodes.is_some_and(|mode| !lists("modes", mode))
    {
        eprintln!(
            "codec_client: no ready line naming {} in {kinds}: {line}",
            args.mode
        );
        codec.stop(); // at once: a codec that sent no ready line may not be reading its input
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

/// WSJT-X names a saved period YYMMDD_HHMMSS.wav, or YYMMDD_HHMM.wav in periods of a minute or more: its start, in seconds of the day, or 0.
fn file_label(path: &str) -> f64 {
    let name = path.as_bytes();
    if name.len() < 4 || !name[name.len() - 4..].eq_ignore_ascii_case(b".wav") {
        return 0.0;
    }
    let stem = &name[..name.len() - 4];
    // An underscore, then the given number of digits, ending the name.
    let digits = |n: usize| {
        stem.len() > n
            && stem[stem.len() - n - 1] == b'_'
            && stem[stem.len() - n..].iter().all(u8::is_ascii_digit)
    };
    let tail = match (digits(6), digits(4)) {
        (true, _) => &stem[stem.len() - 6..],
        (_, true) => &stem[stem.len() - 4..],
        _ => return 0.0,
    };
    let two = |k: usize| {
        tail.get(k..k + 2)
            .map_or(0, |d| u32::from(d[0] - b'0') * 10 + u32::from(d[1] - b'0'))
    };
    let (h, m, s) = (two(0), two(2), two(4));
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

/// The rendered audio, 36 dB down (an arithmetic shift right by 6), over noise, in a buffer of the given seconds: about 12 dB SNR in 2500 Hz, as decoders expect a noise floor (JT65's decodes nothing without one).
///
/// The noise is integer arithmetic, so that every example sends the same samples: a 32-bit xorshift (13, 17, 5) from 1, and each sample the sum of its four bytes less 510.
fn over_noise(audio: &[u8], seconds: f64) -> Vec<u8> {
    let total = (seconds * f64::from(RATE)) as usize;
    let mut x: u32 = 1;
    let mut out = Vec::with_capacity(2 * total);
    for i in 0..total {
        x ^= x << 13; // u32 shifts: the bits shifted out are dropped, as mod 2^32
        x ^= x >> 17;
        x ^= x << 5;
        let noise = x.to_le_bytes().iter().map(|&b| i32::from(b)).sum::<i32>() - 510;
        let signal = match audio.get(2 * i..2 * i + 2) {
            Some(&[low, high]) => i32::from(i16::from_le_bytes([low, high])) >> 6, // arithmetic: floor for negatives
            _ => 0,
        };
        out.extend_from_slice(&((signal + noise) as i16).to_le_bytes());
    }
    out
}

/// The configure that sets the mode up to decode, with the search keys and the calls. These examples have no station, so no calls (the decoders use the starting calls otherwise) but --mycall's, and a configure that carries the mode sets the mode up as WSJT-X does, which can change the search keys and the frequencies, so the search keys go with it.
fn configure(args: &Args, mode: &str, rxfreq: Option<i64>) -> Value {
    let (nfa, nfb) = search(mode);
    let mut message = json!({"t": "configure", "mode": mode, "depth": args.depth, "mycall": args.mycall,
                             "nfa": args.low.unwrap_or(nfa), "nfb": args.high.unwrap_or(nfb),
                             "his_call": "", "his_grid": "", "ntol": 1000});
    if mode != "JTTY" {
        // With the mode, as FreqCal and fast JT9 need it: whole seconds as an integer.
        message["trperiod"] = match args.period {
            Some(period) if period.fract() == 0.0 => json!(period as i64),
            period => json!(period),
        };
    }
    if SUBMODES.contains(&mode) {
        message["submode"] = json!(args.submode);
    }
    if mode == "JT9" {
        message["fast"] = json!(args.fast); // with every JT9 configure that carries the mode
    }
    if let Some(rxfreq) = rxfreq {
        message["rxfreq"] = json!(rxfreq);
    }
    message
}

fn decode(args: &Args) -> u8 {
    let wav = &args.operands[0];
    let audio = match read_wav(Path::new(wav)) {
        Ok(audio) => audio,
        Err(error) => {
            eprintln!("codec_client: {error}");
            return USAGE;
        }
    };
    let Some(mut codec) = start(args, "modes", None) else {
        return FAILED;
    };
    let live = args.command == "live";
    let mut message = configure(args, args.mode, args.freq);
    if live {
        message["live_audio"] = json!(true);
        message["busy_events"] = json!(true);
    }
    codec.input.send(&message);
    let seconds = if args.mode == "JTTY" {
        None
    } else {
        args.period
    };
    run(&mut codec, audio, args.mode, seconds, file_label(wav), live).0
}

/// The render request for the mode.
fn render_request(args: &Args) -> Value {
    let mut request = json!({"t": "render", "id": 1, "freq": args.freq, "rate": RATE});
    let text = args.operands.join(" ");
    if args.mode == "JTTY" {
        request["text"] = json!(text); // JTTY's requests carry no mode
        return request;
    }
    request["mode"] = json!(args.mode);
    let seconds = args.period.map(|period| period as i64); // whole seconds
    match args.mode {
        // One message a slot.
        "FOX" | "SUPERFOX" => request["slots"] = json!(args.operands),
        // A steady tone for the period FreqCal measures it in.
        "TUNE" => request["seconds"] = json!(seconds),
        // WSJT-X's Fixed Tone.
        "ECHO" => request["kind"] = json!("tone"),
        _ => request["text"] = json!(text),
    }
    if SUBMODES.contains(&args.mode) {
        request["submode"] = json!(args.submode);
    }
    if PERIODS.contains(&args.mode) || args.fast {
        request["trperiod"] = json!(seconds);
    }
    if args.fast {
        request["fast"] = json!(true);
    }
    request
}

/// Quit a render that failed, saying why.
fn render_failed(codec: &mut Codec, why: &str) -> u8 {
    eprintln!("codec_client: render failed: {why}");
    codec.quit();
    FAILED
}

/// Send the render request and return what it sent, its sample count and its audio, or the exit status when it fails.
fn request_render(codec: &mut Codec, args: &Args) -> Result<(Vec<String>, u64, Vec<u8>), u8> {
    codec.input.send(&render_request(args));
    let (mut sent, mut samples, mut audio) = (Vec::new(), 0, Vec::new());
    let silence = silence_wait(None); // a reply waits for no decode
    loop {
        // The reply: encoded (JTTY: a segment line a segment), the pcm lines, then rendered; or rejected.
        let line = match codec.next(silence) {
            Ok(line) => line,
            Err(RecvTimeoutError::Timeout) => {
                eprintln!(
                    "codec_client: no line from the codec in {} s: it has hung",
                    silence.as_secs()
                );
                codec.stop();
                return Err(HUNG);
            }
            Err(RecvTimeoutError::Disconnected) => {
                return Err(render_failed(codec, "the codec's output ended"))
            }
        };
        let text = |value: &Value| value.as_str().map(String::from);
        match line["t"].as_str() {
            Some("rejected" | "error") => return Err(render_failed(codec, &line.to_string())),
            // A render that sent nothing has failed too.
            Some("rendered") if sent.is_empty() => {
                return Err(render_failed(codec, &line.to_string()))
            }
            Some("encoded") => {
                let messages = match args.mode {
                    "FOX" => line["slots"].as_array().and_then(|slots| {
                        slots.iter().map(|slot| text(&slot["msgsent"])).collect()
                    }),
                    // SuperFox's encoded line has no msgsent: WSJT-X shows the slots as written.
                    "SUPERFOX" => Some(
                        args.operands
                            .iter()
                            .map(|slot| slot.to_uppercase())
                            .collect(),
                    ),
                    _ => text(&line["msgsent"]).map(|msgsent| vec![msgsent]),
                };
                let (Some(count), Some(messages)) = (line["samples"].as_u64(), messages) else {
                    return Err(render_failed(
                        codec,
                        &format!("an encoded line without samples or msgsent: {line}"),
                    ));
                };
                (samples, sent) = (count, messages);
            }
            Some("segment") => {
                // JTTY: each segment's text and samples.
                let (Some(segment), Some(count)) = (text(&line["text"]), line["samples"].as_u64())
                else {
                    return Err(render_failed(
                        codec,
                        &format!("a segment line without text or samples: {line}"),
                    ));
                };
                sent.push(segment);
                samples += count;
            }
            Some("pcm") => match line["data"].as_str().and_then(base64_decode) {
                Some(pcm) => audio.extend(pcm), // int16 little-endian samples at the requested rate
                None => return Err(render_failed(codec, &line.to_string())),
            },
            Some("rendered") => return Ok((sent, samples, audio)),
            _ => {}
        }
    }
}

fn render(mut args: Args) -> u8 {
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
    let mode = decoded_as(args.mode);
    let Some(mut codec) = start(&args, "encodes", Some(mode)) else {
        return FAILED;
    };
    let (sent, samples, audio) = match request_render(&mut codec, &args) {
        Ok(reply) => reply,
        Err(status) => return status,
    };
    println!("rendered {samples} {}", sent.join(" | "));
    if let Some(out) = &mut out {
        if let Err(error) = out.write(&audio) {
            eprintln!("codec_client: {}: {error}", out.path.display());
            codec.quit();
            return FAILED;
        }
    }
    // Decode it back, over noise: the audio where WSJT-X starts it in its period; JTTY's after a second, with a second after it.
    let (seconds, signal) = match args.period {
        Some(seconds) if args.mode != "JTTY" => {
            let offset = (tx_start(args.mode, seconds, args.fast) * f64::from(RATE)) as usize;
            let mut padded = vec![0u8; 2 * offset];
            padded.extend_from_slice(&audio);
            (Some(seconds), over_noise(&padded, seconds))
        }
        _ => {
            let mut padded = vec![0u8; 2 * RATE as usize];
            padded.extend_from_slice(&audio);
            (
                None,
                over_noise(&padded, audio.len() as f64 / 2.0 / f64::from(RATE) + 2.0),
            )
        }
    };
    if mode == "FST4" {
        // Its F Low and F High around the render.
        args.low = args.freq.map(|freq| freq - 200);
        args.high = args.freq.map(|freq| freq + 200);
    }
    let mut message = configure(&args, mode, if mode == "MSK144" { None } else { args.freq }); // MSK144's Rx Freq is its tones' center
    if args.mode == "SUPERFOX" {
        // A SuperFox Hound decodes it.
        message["contest_type"] = json!("hound");
        message["superfox_mode"] = json!(true);
        message["even_sequence"] = json!(true);
    }
    if args.mode == "ECHO" {
        // The echo's parameters from the keys, not from the audio's first samples as a saved file carries them.
        message["live_audio"] = json!(true);
        message["echo_freq"] = json!(args.freq);
    }
    codec.input.send(&message);
    let (status, messages, measured) = run(&mut codec, signal, mode, seconds, 0.0, false);
    if status != OK {
        return status;
    }
    // Whether a line of the kind (echo or freqcal) has a number at the key that passes: a null, a value that is not finite, never does.
    let measures = |kind: &str, key: &str, passes: fn(f64) -> bool| {
        measured
            .iter()
            .any(|m| m["t"] == kind && m[key].as_f64().is_some_and(passes))
    };
    let missing = match args.mode {
        "TUNE" if !measures("freqcal", "error", |error| error.abs() < 1.0) => {
            let freq = args.freq.unwrap_or_default();
            vec![format!("a measurement within 1 Hz of {freq} Hz")]
        }
        "ECHO" if !measures("echo", "snr", |snr| snr > 0.0) => {
            vec!["an echo above the noise".to_string()]
        }
        "TUNE" | "ECHO" => Vec::new(),
        _ => {
            let words = |text: &str| text.split_whitespace().collect::<Vec<_>>().join(" ");
            sent.into_iter()
                .filter(|text| !messages.iter().any(|m| words(m) == words(text)))
                .collect()
        }
    };
    if !missing.is_empty() {
        eprintln!(
            "codec_client: round trip failed: no decode of {}",
            missing.join(" | ")
        );
        return FAILED;
    }
    OK
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
        Ok(args) if args.command == "render" => render(args),
        Ok(args) => decode(&args),
        Err(error) => {
            eprintln!("{USAGE_TEXT}\ncodec_client: {error}");
            USAGE
        }
    };
    ExitCode::from(status)
}
