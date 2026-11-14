// SPDX-License-Identifier: GPL-3.0-or-later
//
// A wsjt-codec client: decode a WAV file, replay one as live audio, or render a message.
//
//   codec_client decode --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
//   codec_client live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
//   codec_client render --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] [--freq HZ] [--out WAV] TEXT
//
// doc/codec/examples/README.md states what each prints and its exit status. C++17, the standard library and the platform's process API (POSIX or Win32), in one file:
//
//   c++ -std=c++17 -O2 -pthread -o codec_client codec_client.cpp
//   x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -o codec_client.exe codec_client.cpp

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// On Windows this example uses the ANSI APIs (CreateProcessA and the like), so its paths must be in the active code page.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // keeps std::min and std::max usable
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600  // the handle list that limits what the codec inherits needs Windows Vista or later
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

const int RATE = 12000;     // the stream's sample rate, Hz
const size_t CHUNK = 1200;  // samples in an audio frame: 0.1 s
const char AUDIO = 0x01, CONTROL = 0x02;  // frame types
// Session header: magic, sample format 0 (int16), 1 channel, rate in kHz (uint16 little-endian).
const char HEADER[8] = {'W', 'S', 'J', 'T', 0, 1, 12, 0};
enum Status { OK = 0, FAILED = 1, USAGE = 2, HUNG = 3 };  // exit status
const int EXIT_SECONDS = 10;  // how long the codec has to exit once its output ends, or after halt on a failure

struct Mode {
  const char *name;
  double period;         // s
  double txStart;        // where WSJT-X starts its transmission in the period, s
  double liveAllowance;  // a live pass's time between progress lines, once one has completed, s
};
const Mode MODES[] = {{"FT8", 15.0, 0.5, 10.0}, {"FT4", 7.5, 0.3, 6.0}};

size_t sampleCount(double seconds) { return size_t(std::lround(seconds * RATE)); }

// ---------------------------------------------------------------- JSON

// A JSON value. The codec's lines are objects; this client writes its requests by hand.
struct Json {
  enum Type { Null, Boolean, Number, String, Array, Object } type = Null;
  bool boolean = false;
  double number = 0;
  std::string text;
  std::vector<Json> items;                            // an array's elements
  std::vector<std::pair<std::string, Json>> members;  // an object's members

  // The member named key, or null.
  const Json &operator[](const std::string &key) const {
    static const Json none;
    for (const auto &member : members)
      if (member.first == key) return member.second;
    return none;
  }
  std::string str() const { return type == String ? text : std::string(); }
  long long integer() const { return std::llround(number); }
  bool isTrue() const { return type == Boolean && boolean; }
  bool contains(const std::string &item) const {
    for (const auto &element : items)
      if (element.str() == item) return true;
    return false;
  }
};

void appendUtf8(std::string &out, unsigned long code) {
  if (code < 0x80) {
    out += char(code);
  } else if (code < 0x800) {
    out += char(0xC0 | (code >> 6));
    out += char(0x80 | (code & 0x3F));
  } else if (code < 0x10000) {
    out += char(0xE0 | (code >> 12));
    out += char(0x80 | ((code >> 6) & 0x3F));
    out += char(0x80 | (code & 0x3F));
  } else {
    out += char(0xF0 | (code >> 18));
    out += char(0x80 | ((code >> 12) & 0x3F));
    out += char(0x80 | ((code >> 6) & 0x3F));
    out += char(0x80 | (code & 0x3F));
  }
}

// Reads one JSON text. The program stays in the C locale, so strtod and printf use '.' as the decimal point.
class JsonReader {
 public:
  explicit JsonReader(const std::string &text) : text_(text) {}

  bool read(Json &value) {
    if (!readValue(value, 0)) return false;
    skipSpace();
    return at_ == text_.size();
  }

 private:
  const std::string &text_;
  size_t at_ = 0;

  void skipSpace() {
    while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\r' || text_[at_] == '\n')) ++at_;
  }

  bool take(char c) {
    skipSpace();
    if (at_ >= text_.size() || text_[at_] != c) return false;
    ++at_;
    return true;
  }

  bool takeWord(const char *word) {
    const size_t length = std::strlen(word);
    if (text_.compare(at_, length, word) != 0) return false;
    at_ += length;
    return true;
  }

  bool readValue(Json &value, int depth) {
    skipSpace();
    if (at_ >= text_.size() || depth > 32) return false;
    if (take('{')) {
      value.type = Json::Object;
      if (take('}')) return true;
      do {
        std::string key;
        Json member;
        skipSpace();
        if (!readString(key) || !take(':') || !readValue(member, depth + 1)) return false;
        value.members.emplace_back(std::move(key), std::move(member));
      } while (take(','));
      return take('}');
    }
    if (take('[')) {
      value.type = Json::Array;
      if (take(']')) return true;
      do {
        Json item;
        if (!readValue(item, depth + 1)) return false;
        value.items.push_back(std::move(item));
      } while (take(','));
      return take(']');
    }
    if (text_[at_] == '"') {
      value.type = Json::String;
      return readString(value.text);
    }
    if (takeWord("true")) {
      value.type = Json::Boolean;
      value.boolean = true;
      return true;
    }
    if (takeWord("false")) {
      value.type = Json::Boolean;
      return true;
    }
    if (takeWord("null")) return true;
    const char *start = text_.c_str() + at_;
    char *end = nullptr;
    value.number = std::strtod(start, &end);
    if (end == start) return false;
    value.type = Json::Number;
    at_ += size_t(end - start);
    return true;
  }

  bool readHex4(unsigned long &code) {
    if (text_.size() - at_ < 4) return false;
    const std::string digits = text_.substr(at_, 4);
    for (char digit : digits)
      if (!std::isxdigit(static_cast<unsigned char>(digit))) return false;
    code = std::strtoul(digits.c_str(), nullptr, 16);
    at_ += 4;
    return true;
  }

  // A string, its escapes decoded to UTF-8.
  bool readString(std::string &out) {
    if (at_ >= text_.size() || text_[at_] != '"') return false;
    ++at_;
    while (at_ < text_.size()) {
      const char c = text_[at_++];
      if (c == '"') return true;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (at_ >= text_.size()) return false;
      unsigned long code = 0, low = 0;
      switch (text_[at_++]) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u':
          if (!readHex4(code)) return false;
          if (code >= 0xD800 && code < 0xDC00 && text_.compare(at_, 2, "\\u") == 0) {  // a surrogate pair
            at_ += 2;
            if (!readHex4(low)) return false;
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
          }
          appendUtf8(out, code);
          break;
        default: return false;
      }
    }
    return false;
  }
};

// A string as a JSON string.
std::string quote(const std::string &text) {
  static const char hex[] = "0123456789abcdef";
  std::string out = "\"";
  for (char c : text) {
    const unsigned char byte = static_cast<unsigned char>(c);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (byte < 0x20) {
      out += "\\u00";
      out += hex[byte >> 4];
      out += hex[byte & 0xF];
    } else {
      out += c;
    }
  }
  return out + '"';
}

std::string fromBase64(const std::string &text) {
  static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  unsigned long bits = 0;
  int count = 0;
  for (char c : text) {
    const size_t value = alphabet.find(c);
    if (value == std::string::npos) continue;  // the '=' padding
    bits = ((bits << 6) | value) & 0xFFFFFF;
    count += 6;
    if (count >= 8) {
      count -= 8;
      out += char((bits >> count) & 0xFF);
    }
  }
  return out;
}

// ---------------------------------------------------------------- frames, labels and WAV files

// A frame: type (1 byte), body length (uint32 little-endian), body.
std::string frame(char type, const std::string &body) {
  const uint32_t n = uint32_t(body.size());
  return std::string{type, char(n & 0xFF), char((n >> 8) & 0xFF), char((n >> 16) & 0xFF), char((n >> 24) & 0xFF)} + body;
}

// A time of day as the configure key utc takes it: "HH:MM:SS".
std::string utc(double seconds) {
  const long long s = static_cast<long long>(seconds) % 86400;
  char text[16];
  std::snprintf(text, sizeof text, "%02lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60);
  return text;
}

// WSJT-X names a saved period YYMMDD_HHMMSS.wav: its start, in seconds of the day, or 0.
double fileLabel(const std::string &path) {
  if (path.size() < 11) return 0;
  std::string tail = path.substr(path.size() - 11);  // _HHMMSS.wav
  for (char &c : tail) c = char(std::tolower(static_cast<unsigned char>(c)));
  if (tail[0] != '_' || tail.compare(7, 4, ".wav") != 0) return 0;
  for (int i = 1; i <= 6; ++i)
    if (!std::isdigit(static_cast<unsigned char>(tail[i]))) return 0;
  const int h = std::stoi(tail.substr(1, 2)), m = std::stoi(tail.substr(3, 2)), s = std::stoi(tail.substr(5, 2));
  return h < 24 && m < 60 && s < 60 ? h * 3600 + m * 60 + s : 0;
}

// The samples of a 12000 Hz mono 16-bit PCM WAV file, 16-bit little-endian as the stream takes them; false, with an error, when it is not one.
bool readWav(const std::string &path, std::string &samples, std::string &error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "cannot open " + path;
    return false;
  }
  const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  auto u16 = [&](size_t at) { return unsigned(uint8_t(data[at])) | unsigned(uint8_t(data[at + 1])) << 8; };
  auto u32 = [&](size_t at) { return u16(at) | static_cast<unsigned long>(u16(at + 2)) << 16; };
  bool pcm = false, found = false;
  unsigned channels = 0, bits = 0;
  unsigned long rate = 0;
  if (data.size() >= 12 && data.compare(0, 4, "RIFF") == 0 && data.compare(8, 4, "WAVE") == 0) {
    for (size_t at = 12; at + 8 <= data.size();) {
      const size_t size = u32(at + 4), body = at + 8;
      if (data.compare(at, 4, "fmt ") == 0 && body + 16 <= data.size()) {
        // PCM is format 1, or WAVE_FORMAT_EXTENSIBLE (0xFFFE) whose subformat, at byte 24, is KSDATAFORMAT_SUBTYPE_PCM.
        static const std::string pcmSubformat("\x01\x00\x00\x00\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71", 16);
        const unsigned format = u16(body);
        pcm = format == 1 || (format == 0xFFFE && size >= 40 && body + 40 <= data.size() && data.compare(body + 24, 16, pcmSubformat) == 0);
        channels = u16(body + 2);
        rate = u32(body + 4);
        bits = u16(body + 14);
      } else if (data.compare(at, 4, "data") == 0) {
        samples = data.substr(body, std::min(size, data.size() - body) & ~size_t(1));
        found = true;
        break;
      }
      at = body + size + (size & 1);  // a chunk is padded to an even length
    }
  }
  if (!found || !pcm || channels != 1 || bits != 16 || rate != RATE) {
    error = path + " is not 12000 Hz mono 16-bit PCM";
    return false;
  }
  return true;
}

bool writeWav(const std::string &path, const std::string &samples) {
  auto u16 = [](unsigned v) { return std::string{char(v & 0xFF), char((v >> 8) & 0xFF)}; };
  auto u32 = [&](uint32_t v) { return u16(v & 0xFFFF) + u16(v >> 16); };
  const uint32_t bytes = uint32_t(samples.size());
  std::ofstream file(path, std::ios::binary);
  file << "RIFF" << u32(36 + bytes) << "WAVE" << "fmt " << u32(16) << u16(1) << u16(1) << u32(RATE) << u32(2 * RATE) << u16(2) << u16(16)
       << "data" << u32(bytes) << samples;
  file.close();
  return bool(file);
}

// The words of the text, separated by one blank.
std::string words(const std::string &text) {
  std::istringstream in(text);
  std::string word, out;
  while (in >> word) out += (out.empty() ? "" : " ") + word;
  return out;
}

// ---------------------------------------------------------------- paths and pipes

#ifdef _WIN32
using Pipe = HANDLE;
const Pipe NO_PIPE = nullptr;
const char *const SEPARATORS = "/\\";
#else
using Pipe = int;
const Pipe NO_PIPE = -1;
const char *const SEPARATORS = "/";
#endif

// The path made absolute: the codec runs in the data directory, where a relative path names another file.
std::string absolute(const std::string &path) {
#ifdef _WIN32
  const DWORD size = GetFullPathNameA(path.c_str(), 0, nullptr, nullptr);
  std::string full(size, '\0');
  const DWORD length = size ? GetFullPathNameA(path.c_str(), size, &full[0], nullptr) : 0;
  if (length == 0 || length >= size) return path;
  full.resize(length);
  return full;
#else
  if (!path.empty() && path[0] == '/') return path;
  std::vector<char> directory(4096);
  while (!getcwd(directory.data(), directory.size())) {
    if (errno != ERANGE) return path;
    directory.resize(2 * directory.size());
  }
  return std::string(directory.data()) + "/" + path;
#endif
}

bool isDirectory(const std::string &path) {
#ifdef _WIN32
  const DWORD attributes = GetFileAttributesA(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat info;
  return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

// Creates the directory and its parents as needed: true when it exists. Creating one that exists fails harmlessly.
bool makeDirectories(const std::string &path) {
  for (size_t at = path.find_first_of(SEPARATORS, 1);; at = path.find_first_of(SEPARATORS, at + 1)) {
    const std::string prefix = path.substr(0, at);
#ifdef _WIN32
    CreateDirectoryA(prefix.c_str(), nullptr);
#else
    mkdir(prefix.c_str(), 0777);
#endif
    if (at == std::string::npos) break;
  }
  return isDirectory(path);
}

// Reads up to size bytes from a pipe: 0 at its end.
size_t readPipe(Pipe pipe, char *buffer, size_t size) {
#ifdef _WIN32
  DWORD n = 0;
  return ReadFile(pipe, buffer, DWORD(size), &n, nullptr) ? n : 0;
#else
  for (;;) {
    const ssize_t n = read(pipe, buffer, size);
    if (n >= 0) return size_t(n);
    if (errno != EINTR) return 0;
  }
#endif
}

// Writes the bytes to a pipe: false when the pipe is broken, its reader having exited.
bool writePipe(Pipe pipe, const char *bytes, size_t size) {
  while (size > 0) {
#ifdef _WIN32
    DWORD n = 0;
    if (!WriteFile(pipe, bytes, DWORD(std::min<size_t>(size, 1 << 20)), &n, nullptr)) return false;
#else
    const ssize_t n = write(pipe, bytes, size);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) return false;
#endif
    bytes += n;
    size -= size_t(n);
  }
  return true;
}

void closePipe(Pipe &pipe) {
  if (pipe == NO_PIPE) return;
#ifdef _WIN32
  CloseHandle(pipe);
#else
  close(pipe);
#endif
  pipe = NO_PIPE;
}

#ifdef _WIN32
// One argument for a Windows command line, quoted so that the C runtime reads it back unchanged.
std::string quoteArgument(const std::string &argument) {
  std::string out = "\"";
  size_t backslashes = 0;
  for (char c : argument) {
    if (c == '\\') {
      ++backslashes;
      continue;
    }
    out.append(c == '"' ? 2 * backslashes + 1 : backslashes, '\\');
    backslashes = 0;
    out += c;
  }
  return out.append(2 * backslashes, '\\') + '"';
}
#endif

// ---------------------------------------------------------------- the codec process

// One line of the codec's standard output: its text and the JSON object it holds.
struct Line {
  std::string text;
  Json json;
};

// One wsjt-codec process: frames go to its stdin; each stdout line comes back as a Line; its stderr goes to ours.
class Codec {
 public:
  enum Got { LINE, NOTHING_YET, END };

  ~Codec() {
    kill();
    wait();
  }

  // Starts the program in the data directory, which is also its -a, -t and -r directory, and sends the session header.
  bool start(const std::string &programPath, const std::string &dataPath, std::string &error) {
    const std::string program = absolute(programPath), data = absolute(dataPath);
    if (!makeDirectories(data)) {
      error = "cannot create " + data;
      return false;
    }
    // One start at a time: a codec started while another's pipes are being made would inherit them, and would hold that codec's input open.
    static std::mutex starting;
    std::lock_guard<std::mutex> hold(starting);
#ifdef _WIN32
    SECURITY_ATTRIBUTES inherited{};
    inherited.nLength = sizeof inherited;
    inherited.bInheritHandle = TRUE;
    HANDLE in = nullptr, out = nullptr, err = nullptr;
    if (!CreatePipe(&in, &input_, &inherited, 0) || !CreatePipe(&output_, &out, &inherited, 0) || !CreatePipe(&errors_, &err, &inherited, 0)) {
      error = "cannot create pipes";
      for (HANDLE made : {in, out, err})
        if (made) CloseHandle(made);
      closePipe(input_);
      closePipe(output_);
      closePipe(errors_);
      return false;
    }
    for (HANDLE own : {input_, output_, errors_}) SetHandleInformation(own, HANDLE_FLAG_INHERIT, 0);
    // The codec inherits only its ends: without a handle list it would inherit every inheritable handle this program holds, its own standard handles included.
    HANDLE ends[] = {in, out, err};
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<char> attributes(size);
    const auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    STARTUPINFOEXA startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = in;
    startup.StartupInfo.hStdOutput = out;
    startup.StartupInfo.hStdError = err;
    std::string command = quoteArgument(program);
    for (const char *option : {" -a ", " -t ", " -r "}) command += option + quoteArgument(data);
    PROCESS_INFORMATION process{};
    const bool initialized = InitializeProcThreadAttributeList(list, 1, 0, &size);
    BOOL started = initialized && UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, ends, sizeof ends, nullptr, nullptr);
    if (started) {
      startup.lpAttributeList = list;
      // CREATE_NO_WINDOW: a console program started from a GUI program opens no console window.
      started = CreateProcessA(nullptr, &command[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, data.c_str(), &startup.StartupInfo, &process);
    }
    const DWORD failure = GetLastError();
    if (initialized) DeleteProcThreadAttributeList(list);
    for (HANDLE theirs : {in, out, err}) CloseHandle(theirs);
    if (!started) {
      error = "Windows error " + std::to_string(failure);
      closePipe(input_);
      closePipe(output_);
      closePipe(errors_);
      return false;
    }
    CloseHandle(process.hThread);
    process_ = process.hProcess;
#else
    if (access(program.c_str(), X_OK) != 0) {
      error = std::strerror(errno);
      return false;
    }
    int in[2], out[2], err[2];
    if (pipe(in) != 0 || pipe(out) != 0 || pipe(err) != 0) {
      error = std::strerror(errno);
      return false;
    }
    for (int fd : {in[0], in[1], out[0], out[1], err[0], err[1]}) fcntl(fd, F_SETFD, FD_CLOEXEC);  // no later codec inherits them; dup2 clears it on 0 to 2
    std::string a = "-a", t = "-t", r = "-r", directory = data, path = program;
    char *argv[] = {&path[0], &a[0], &directory[0], &t[0], &directory[0], &r[0], &directory[0], nullptr};
    pid_ = fork();
    if (pid_ == 0) {  // the codec, until exec: only calls that are safe between fork and exec
      std::signal(SIGPIPE, SIG_DFL);  // this program ignores SIGPIPE; the codec gets the default
      dup2(in[0], 0);
      dup2(out[1], 1);
      dup2(err[1], 2);
      for (int fd : {in[0], in[1], out[0], out[1], err[0], err[1]}) close(fd);
      if (chdir(data.c_str()) == 0) execv(argv[0], argv);
      _exit(127);  // its stdout ends before ready, which the client reports
    }
    for (int theirs : {in[0], out[1], err[1]}) close(theirs);
    input_ = in[1];
    output_ = out[0];
    errors_ = err[0];
    if (pid_ < 0) {
      error = std::strerror(errno);
      closePipe(input_);
      closePipe(output_);
      closePipe(errors_);
      return false;
    }
#endif
    running_ = true;
    // Read stdout and stderr on their own threads: a pipe left full stops the codec.
    outputReader_ = std::thread([this] { readOutput(); });
    errorReader_ = std::thread([this] { readErrors(); });
    write(std::string(HEADER, sizeof HEADER));
    return true;
  }

  // Writes to the codec's stdin. A broken pipe means the codec has exited, and its lines and exit status say why.
  void write(const std::string &bytes) {
    std::lock_guard<std::mutex> lock(inputMutex_);
    if (input_ != NO_PIPE && !writePipe(input_, bytes.data(), bytes.size())) closePipe(input_);
  }

  void send(const std::string &json) { write(frame(CONTROL, json)); }

  bool inputOpen() {
    std::lock_guard<std::mutex> lock(inputMutex_);
    return input_ != NO_PIPE;
  }

  // Decodes what is in, ends the session and closes the codec's input.
  void halt() {
    send("{\"t\":\"halt\"}");
    std::lock_guard<std::mutex> lock(inputMutex_);
    closePipe(input_);
  }

  // The next line, waiting at most timeout for it when one is given.
  Got next(Line &line, std::chrono::milliseconds timeout = std::chrono::milliseconds(-1)) {
    std::unique_lock<std::mutex> lock(linesMutex_);
    auto ready = [this] { return !lines_.empty() || ended_; };
    if (timeout.count() < 0)
      linesReady_.wait(lock, ready);
    else if (!linesReady_.wait_for(lock, timeout, ready))
      return NOTHING_YET;
    if (lines_.empty()) return END;
    line = std::move(lines_.front());
    lines_.pop_front();
    return LINE;
  }

  void kill() {
    if (!running_) return;
#ifdef _WIN32
    TerminateProcess(process_, 1);
#else
    ::kill(pid_, SIGKILL);
#endif
  }

  // Waits at most within for the codec to exit: true once it has, and wait() then returns at once.
  bool exited(std::chrono::milliseconds within) {
    if (!running_) return true;
#ifdef _WIN32
    return WaitForSingleObject(process_, DWORD(within.count())) == WAIT_OBJECT_0;
#else
    const auto deadline = std::chrono::steady_clock::now() + within;
    for (;;) {
      int how = 0;
      const pid_t got = waitpid(pid_, &how, WNOHANG);
      if (got == pid_) {
        status_ = WIFEXITED(how) ? WEXITSTATUS(how) : -WTERMSIG(how);
        running_ = false;
        return true;
      }
      if (got < 0 && errno != EINTR) return true;  // no such child: wait() has nothing to wait for
      if (std::chrono::steady_clock::now() >= deadline) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
#endif
  }

  // Waits for the codec to exit, and returns its exit status (negative: the signal that ended it).
  int wait() {
    if (running_) {
#ifdef _WIN32
      WaitForSingleObject(process_, INFINITE);
      DWORD code = 1;
      GetExitCodeProcess(process_, &code);
      CloseHandle(process_);
      status_ = int(code);
#else
      int how = 0;
      while (waitpid(pid_, &how, 0) < 0 && errno == EINTR) {
      }
      status_ = WIFEXITED(how) ? WEXITSTATUS(how) : -WTERMSIG(how);
#endif
      running_ = false;
    }
    {
      std::lock_guard<std::mutex> lock(inputMutex_);
      closePipe(input_);
    }
    if (outputReader_.joinable()) outputReader_.join();
    if (errorReader_.joinable()) errorReader_.join();
    closePipe(output_);
    closePipe(errors_);
    return status_;
  }

 private:
  void readOutput() {
    std::string pending;
    char buffer[16384];
    while (const size_t n = readPipe(output_, buffer, sizeof buffer)) {
      pending.append(buffer, n);
      size_t end;
      while ((end = pending.find('\n')) != std::string::npos) {
        add(pending.substr(0, end));
        pending.erase(0, end + 1);
      }
    }
    if (!pending.empty()) add(pending);
    std::lock_guard<std::mutex> lock(linesMutex_);
    ended_ = true;
    linesReady_.notify_all();
  }

  void add(std::string text) {
    if (!text.empty() && text.back() == '\r') text.pop_back();  // lines end in CRLF on Windows
    Line line;
    if (!JsonReader(text).read(line.json) || line.json.type != Json::Object) {
      std::fprintf(stderr, "codec_client: not JSON: %s\n", text.c_str());
      return;
    }
    line.text = std::move(text);
    std::lock_guard<std::mutex> lock(linesMutex_);
    lines_.push_back(std::move(line));
    linesReady_.notify_all();
  }

  void readErrors() {  // diagnostics for people, not for parsing
    char buffer[4096];
    while (const size_t n = readPipe(errors_, buffer, sizeof buffer)) {
      std::fwrite(buffer, 1, n, stderr);
      std::fflush(stderr);
    }
  }

#ifdef _WIN32
  HANDLE process_ = nullptr;
#else
  pid_t pid_ = -1;
#endif
  bool running_ = false;
  int status_ = -1;
  Pipe input_ = NO_PIPE, output_ = NO_PIPE, errors_ = NO_PIPE;
  std::mutex inputMutex_, linesMutex_;
  std::condition_variable linesReady_;
  std::deque<Line> lines_;
  bool ended_ = false;
  std::thread outputReader_, errorReader_;
};

// Stops the codec and exits at once with the status, without waiting for the codec's pipes: a process the codec started can hold them open.
[[noreturn]] void abandon(Codec &codec, int status) {
  codec.kill();
  std::fflush(stdout);
  std::_Exit(status);
}

// Waits for the codec's exit for at most EXIT_SECONDS; after that it has hung, and the client stops it and exits with the status.
void awaitExit(Codec &codec, int status) {
  if (codec.exited(std::chrono::seconds(EXIT_SECONDS))) return;
  std::fprintf(stderr, "codec_client: wsjt-codec did not exit within %d s: it has hung\n", EXIT_SECONDS);
  abandon(codec, status);
}

// Quits a session that failed: halt, and stop the codec if it does not exit.
void quit(Codec &codec) {
  codec.halt();
  awaitExit(codec, FAILED);
  codec.wait();
}

// ---------------------------------------------------------------- decoding

// Tells a slow decode from a hung codec by its busy lines (configure's busy_events).
class Watchdog {
  using Clock = std::chrono::steady_clock;

 public:
  explicit Watchdog(const Mode &mode) : mode_(mode) {}

  double allowance() {
    std::lock_guard<std::mutex> lock(mutex_);
    return allowanceNow();
  }

  // A whole period starts, and ends at due on the replay clock: its decode_finished, or a busy line naming it, is due then.
  void periodDue(const std::string &label, Clock::time_point due) {
    std::lock_guard<std::mutex> lock(mutex_);
    waiting_[label] = due;
  }

  void saw(const Json &line) {
    const auto now = Clock::now();
    const std::string kind = line["t"].str();
    std::lock_guard<std::mutex> lock(mutex_);
    if (kind == "busy" && line["busy"].isTrue()) {
      if (sawBusy_) completed_ = true;  // a busy line ends, and completes, the pass before it
      busy_ = sawBusy_ = true;
      busyMode_ = line["mode"].str();
      lastProgress_ = now;
      answer(line["period_end"].str());
    } else if (kind == "busy") {
      if (sawBusy_) completed_ = true;  // busy false ends, and completes, the pass before it too
      busy_ = false;
      idleSince_ = now;
    } else if (kind == "pass_progress" || kind == "pass_skipped" || (kind == "decode" && line["mode"].str() == busyMode_)) {
      lastProgress_ = now;
    } else if (kind == "decode_finished") {
      completed_ = true;
      lastProgress_ = now;
      answer(line["period_end"].str());
    }
  }

  bool hung() {
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    if (busy_) return seconds(now - lastProgress_) > allowanceNow();  // a pass runs: progress lines are due
    if (waiting_.empty()) return false;
    auto oldest = Clock::time_point::max();
    for (const auto &period : waiting_) oldest = std::min(oldest, period.second);
    return seconds(now - std::max(idleSince_, oldest)) > allowanceNow();  // nothing runs: a whole period is due its line from its end
  }

 private:
  static double seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }

  double allowanceNow() const { return completed_ ? mode_.liveAllowance : std::max(300.0, 4 * mode_.period); }

  // A period's first answer. Its later ones, and those for a period not sent whole, find nothing waiting: a period waits from before its first sample, so no answer comes before it.
  void answer(const std::string &label) { waiting_.erase(label); }

  const Mode &mode_;
  std::mutex mutex_;
  bool busy_ = false, sawBusy_ = false, completed_ = false;  // completed: a pass has completed
  std::string busyMode_;
  Clock::time_point lastProgress_ = Clock::now(), idleSince_ = Clock::now();
  std::map<std::string, Clock::time_point> waiting_;  // whole periods not answered yet, and when each is due
};

// Sends the audio a period at a time, each period's label in a configure before its first sample, then halt.
// Live, it starts on the next UTC period boundary, labels each period with its UTC start and sends each frame when its last sample is due, as a receiver's audio arrives.
void sendAudio(Codec &codec, const std::string &audio, const Mode &mode, double label, bool live, Watchdog *watchdog) {
  const size_t period = sampleCount(mode.period), total = audio.size() / 2;
  if (live) {
    const double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    label = (std::floor(now / mode.period) + 1) * mode.period;
    std::this_thread::sleep_for(std::chrono::duration<double>(label - now));
  }
  const auto start = std::chrono::steady_clock::now();
  auto due = [&](size_t sample) {  // when a sample is due on the replay clock
    return start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(double(sample) / RATE));
  };
  // After halt the codec's exit is due as a period's answer is, from the last sample's scheduled time; no line answers it, and it is registered before the first sample, as a codec that stops reading blocks the writer.
  if (watchdog) watchdog->periodDue("halt", due(total));
  for (size_t k = 0, first = 0; first < total; ++k, first += period) {
    const std::string periodLabel = utc(label + k * mode.period);
    const size_t end = std::min(first + period, total);
    if (watchdog && end - first == period) {  // counted from the period's end, not from the last write, which a codec that stops reading blocks
      std::string digits = periodLabel;
      digits.erase(std::remove(digits.begin(), digits.end(), ':'), digits.end());
      watchdog->periodDue(digits, due(end));
    }
    codec.send("{\"t\":\"configure\",\"utc\":" + quote(periodLabel) + "}");
    for (size_t i = first; i < end; i += CHUNK) {
      const size_t j = std::min(i + CHUNK, end);
      if (live) std::this_thread::sleep_until(due(j));
      if (!codec.inputOpen()) return;
      codec.write(frame(AUDIO, audio.substr(2 * i, 2 * (j - i))));
    }
  }
  codec.halt();
}

// Streams the audio, prints the decodes, and returns the exit status; messages gets each decode's message.
int run(Codec &codec, const std::string &audio, const Mode &mode, double label, bool live, std::vector<std::string> &messages) {
  std::unique_ptr<Watchdog> watchdog;
  if (live) watchdog = std::make_unique<Watchdog>(mode);
  std::thread writer([&] { sendAudio(codec, audio, mode, label, live, watchdog.get()); });
  int errors = 0;
  for (;;) {
    Line line;
    const Codec::Got got = codec.next(line, std::chrono::milliseconds(500));
    if (got == Codec::END) break;
    if (watchdog) {
      if (got == Codec::LINE) watchdog->saw(line.json);
      if (watchdog->hung()) {
        std::fprintf(stderr, "codec_client: no progress from the codec in %.0f s: it has hung\n", watchdog->allowance());
        abandon(codec, HUNG);
      }
    }
    const Json &json = line.json;
    const std::string kind = json["t"].str();  // other lines, and kinds this client does not know, are skipped
    if (kind == "decode") {
      messages.push_back(json["message"].str());
      // printf's %.1f rounds DT's value half to even on glibc and macOS, as WSJT-X shows it: 0.25 prints 0.2. A DT that is not finite comes as null.
      char dt[64] = "nan";
      if (json["dt"].type == Json::Number) std::snprintf(dt, sizeof dt, "%.1f", json["dt"].number);
      std::printf("decode %s %lld %s %lld %s\n", json["time"].str().c_str(), json["snr"].integer(), dt, json["freq"].integer(), json["message"].str().c_str());
      std::fflush(stdout);
    } else if (kind == "decode_finished") {
      std::printf("finished %s\n", json["period_end"].str().c_str());
      std::fflush(stdout);
    } else if (kind == "error") {
      ++errors;
      std::fprintf(stderr, "codec_client: codec error: %s\n", line.text.c_str());
    }
  }
  awaitExit(codec, HUNG);  // its output has ended, so its exit is due: the watchdog no longer runs to catch a hang
  const int status = codec.wait();
  writer.join();
  if (status != 0) {
    std::fprintf(stderr, "codec_client: wsjt-codec exited with status %d\n", status);
    return FAILED;
  }
  return errors ? FAILED : OK;
}

// ---------------------------------------------------------------- commands

struct Options {
  std::string command, codec, data = ".", out;
  std::string input;  // the WAV file, or render's TEXT
  const Mode *mode = &MODES[0];
  int depth = 3, freq = 1500;
};

const char USAGE_TEXT[] =
    "usage: codec_client decode --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV\n"
    "       codec_client live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV\n"
    "       codec_client render --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] [--freq HZ] [--out WAV] TEXT\n";

// Starts the codec and waits for its ready line, which lists the modes it decodes (modes) and encodes (encodes).
std::unique_ptr<Codec> startCodec(const Options &options, const char *kinds) {
  auto codec = std::make_unique<Codec>();
  std::string error;
  if (!codec->start(options.codec, options.data, error)) {
    std::fprintf(stderr, "codec_client: cannot start %s: %s\n", options.codec.c_str(), error.c_str());
    return nullptr;
  }
  Line line;
  const Codec::Got got = codec->next(line, std::chrono::seconds(30));
  if (got == Codec::NOTHING_YET) {
    std::fprintf(stderr, "codec_client: no ready line from %s within 30 s\n", options.codec.c_str());
    abandon(*codec, FAILED);
  }
  const bool ended = got == Codec::END;
  if (ended || line.json["t"].str() != "ready" || !line.json[kinds].contains(options.mode->name)) {
    std::fprintf(stderr, "codec_client: no ready line naming %s in %s: %s\n", options.mode->name, kinds, ended ? "end of output" : line.text.c_str());
    quit(*codec);
    return nullptr;
  }
  return codec;
}

std::string configure(const Options &options) {
  // With the mode: these examples have no station, so no calls (the decoders use the starting calls otherwise), and a configure that carries the mode resets nfa, nfb and ntol.
  return "{\"t\":\"configure\",\"mode\":" + quote(options.mode->name) + ",\"depth\":" + std::to_string(options.depth) +
         ",\"mycall\":\"\",\"his_call\":\"\",\"his_grid\":\"\",\"nfa\":200,\"nfb\":4000,\"ntol\":1000";
}

int decode(const Options &options) {
  std::string audio, error;
  if (!readWav(options.input, audio, error)) {
    std::fprintf(stderr, "codec_client: %s\n", error.c_str());
    return USAGE;
  }
  std::unique_ptr<Codec> codec = startCodec(options, "modes");
  if (!codec) return FAILED;
  const bool live = options.command == "live";
  // Integers stay plain JSON integers: a configure ignores or misreads 3.0, without an error.
  codec->send(configure(options) + (live ? ",\"live_audio\":true,\"busy_events\":true}" : "}"));
  std::vector<std::string> messages;
  return run(*codec, audio, *options.mode, fileLabel(options.input), live, messages);
}

int render(const Options &options) {
  // --out must be writable before the codec starts. A file made to find that out is removed, and the audio is written once it is all in.
  const bool existed = !options.out.empty() && std::ifstream(options.out).good();
  if (!options.out.empty()) {
    if (!std::ofstream(options.out, std::ios::binary | std::ios::app)) {
      std::fprintf(stderr, "codec_client: cannot write %s\n", options.out.c_str());
      return USAGE;
    }
    if (!existed) std::remove(options.out.c_str());
  }
  std::unique_ptr<Codec> codec = startCodec(options, "encodes");
  if (!codec) return FAILED;
  const Mode &mode = *options.mode;
  codec->send("{\"t\":\"render\",\"id\":1,\"mode\":" + quote(mode.name) + ",\"text\":" + quote(options.input) + ",\"freq\":" + std::to_string(options.freq) +
              ",\"rate\":" + std::to_string(RATE) + "}");
  Json encoded;
  std::string audio;
  for (;;) {  // the reply: encoded, its pcm lines, then rendered; or rejected
    Line line;
    const bool ended = codec->next(line) == Codec::END;
    const std::string kind = line.json["t"].str();
    if (ended || kind == "rejected" || kind == "error") {
      std::fprintf(stderr, "codec_client: render failed: %s\n", ended ? "end of output" : line.text.c_str());
      quit(*codec);
      return FAILED;
    }
    if (kind == "encoded")
      encoded = line.json;
    else if (kind == "pcm")
      audio += fromBase64(line.json["data"].str());  // int16 little-endian samples at the requested rate
    else if (kind == "rendered")
      break;
  }
  const std::string sent = encoded["msgsent"].str();
  std::printf("rendered %lld %s\n", encoded["samples"].integer(), sent.c_str());
  std::fflush(stdout);
  if (!options.out.empty() && !writeWav(options.out, audio)) {
    std::fprintf(stderr, "codec_client: cannot write %s\n", options.out.c_str());
    if (!existed) std::remove(options.out.c_str());
    quit(*codec);
    return FAILED;
  }
  // Decode it back: the audio where WSJT-X starts it, in a period of silence.
  const size_t period = 2 * sampleCount(mode.period), offset = 2 * sampleCount(mode.txStart);  // bytes
  std::string signal = (std::string(offset, '\0') + audio).substr(0, period);
  signal.resize(period, '\0');
  codec->send(configure(options) + "}");
  std::vector<std::string> messages;
  int status = run(*codec, signal, mode, 0, false, messages);
  const bool found = std::any_of(messages.begin(), messages.end(), [&](const std::string &message) { return words(message) == words(sent); });
  if (status == OK && !found) {
    std::fprintf(stderr, "codec_client: round trip failed: no decode of %s\n", sent.c_str());
    status = FAILED;
  }
  return status;
}

bool toInteger(const std::string &text, int &value) {
  try {
    size_t used = 0;
    value = std::stoi(text, &used);
    return used == text.size();
  } catch (const std::exception &) {
    return false;
  }
}

// Reads the command line into options: false, with the problem, when it is not usable.
bool parse(int argc, char **argv, Options &options, std::string &problem) {
  options.command = argc > 1 ? argv[1] : "";
  const bool render = options.command == "render";
  if (!render && options.command != "decode" && options.command != "live") {
    problem = "the command is decode, live or render";
    return false;
  }
  bool optionsEnded = false;  // after --, every argument is the WAV file or the TEXT
  for (int i = 2; i < argc; ++i) {
    std::string option = argv[i], value;
    if (!optionsEnded && option == "--") {
      optionsEnded = true;
      continue;
    }
    if (optionsEnded || option.compare(0, 2, "--") != 0) {  // the WAV file, or render's TEXT
      if (!options.input.empty()) {
        problem = "more than one " + std::string(render ? "TEXT" : "WAV");
        return false;
      }
      options.input = option;
      continue;
    }
    const size_t equals = option.find('=');
    if (equals != std::string::npos) {  // --option=value
      value = option.substr(equals + 1);
      option.erase(equals);
    } else if (i + 1 == argc) {
      problem = option + " needs a value";
      return false;
    } else {
      value = argv[++i];
    }
    if (option == "--codec") {
      options.codec = value;
    } else if (option == "--data") {
      options.data = value;
    } else if (option == "--mode") {
      options.mode = nullptr;
      for (const Mode &mode : MODES)
        if (value == mode.name) options.mode = &mode;
      if (!options.mode) problem = "--mode is FT8 or FT4";
    } else if (option == "--depth") {
      if (!toInteger(value, options.depth) || options.depth < 1 || options.depth > 3) problem = "--depth is 1, 2 or 3";
    } else if (render && option == "--freq") {
      if (!toInteger(value, options.freq)) problem = "--freq is a whole number of Hz";
    } else if (render && option == "--out") {
      options.out = value;
    } else {
      problem = "unknown option " + option;
    }
    if (!problem.empty()) return false;
  }
  if (options.codec.empty())
    problem = "--codec is required";
  else if (options.input.empty())
    problem = render ? "no TEXT" : "no WAV file";
  return problem.empty();
}

}  // namespace

int main(int argc, char **argv) {
#ifndef _WIN32
  // A pipe takes the lowest free descriptor: with 0, 1 or 2 closed, the codec's stdin, stdout or stderr would be closed at its start.
  for (int fd = 0; fd <= 2; ++fd)
    if (fcntl(fd, F_GETFD) == -1 && errno == EBADF && open("/dev/null", O_RDWR) < 0) return FAILED;
#endif
  for (int i = 1; i < argc && std::strcmp(argv[i], "--") != 0; ++i) {
    if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      std::fputs(USAGE_TEXT, stdout);
      return OK;
    }
  }
  Options options;
  std::string problem;
  if (!parse(argc, argv, options, problem)) {
    std::fprintf(stderr, "codec_client: %s\n%s", problem.c_str(), USAGE_TEXT);
    return USAGE;
  }
  // The decoders' OpenMP threads need the stack WSJT-X gives them. The codec inherits this environment.
  if (!std::getenv("OMP_STACKSIZE")) {
#ifdef _WIN32
    SetEnvironmentVariableA("OMP_STACKSIZE", "10M");
#else
    setenv("OMP_STACKSIZE", "10M", 1);
#endif
  }
#ifndef _WIN32
  std::signal(SIGPIPE, SIG_IGN);  // a write to a codec that has exited fails with EPIPE instead of ending this program
#endif
  return options.command == "render" ? render(options) : decode(options);
}
