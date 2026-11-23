// SPDX-License-Identifier: GPL-3.0-or-later
//
// A wsjt-codec client: decode a WAV file, replay one as live audio, or render a message.
//
//   codec_client decode --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--low HZ] [--high HZ] [--mycall CALL] WAV
//   codec_client live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
//   codec_client render --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--out WAV] [TEXT...]
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
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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
const int EXIT_SECONDS = 10;      // how long the codec has to exit once its output ends, or after halt on a failure
const int RELEASE_SECONDS = 2;    // how long, once the codec has exited, the client waits for its pipes to close, its output included, and its writes to them to finish: a process the codec started can hold them open
const int SILENCE_SECONDS = 300;  // outside live, how long the codec may send no line: the longest interface.md allows a decode pass between progress lines before one has completed, or four periods when that is longer (silenceBound)
// For the examples' check only (README.md, "The check"), not a setting: CODEC_EXAMPLE_CHECK_SILENCE_SECONDS, a whole number from 1 to 300, replaces the bound, so that a run that checks it need not last 300 s.
std::optional<int> checkSilence() {
  const char *check = std::getenv("CODEC_EXAMPLE_CHECK_SILENCE_SECONDS");
  const size_t n = check ? std::strlen(check) : 0;
  if (n < 1 || n > 3 || !std::all_of(check, check + n, [](char c) { return c >= '0' && c <= '9'; })) return std::nullopt;
  const int seconds = std::atoi(check);
  return seconds >= 1 && seconds <= 300 ? std::optional<int>(seconds) : std::nullopt;
}
const std::optional<int> CHECK_SILENCE = checkSilence();

// Outside live, how long the codec may send no line before the client takes it to have hung, s: 300, or four periods of the given seconds, up to the codec's 1800 s, when that is longer; the check's CHECK_SILENCE replaces it.
double silenceBound(std::optional<double> seconds) {
  if (CHECK_SILENCE) return *CHECK_SILENCE;
  return seconds ? std::max<double>(SILENCE_SECONDS, 4 * std::min(*seconds, 1800.0)) : SILENCE_SECONDS;
}

// Reports a codec silent for the bound, in whole seconds, as Python's %d prints them.
void reportSilence(double bound) { std::fprintf(stderr, "codec_client: no line from the codec in %.0f s: it has hung\n", std::trunc(bound)); }

const char *const DECODES[] = {"FT8", "FT4", "JT9", "JT65", "JT4", "FST4", "FST4W", "Q65", "MSK144", "ECHO", "JTTY", "FREQCAL", "WSPR"};  // what decode takes: the modes ready lists in modes
const char *const ENCODES[] = {"FT8", "FT4", "JT4", "JT9", "JT65", "Q65", "FST4", "FST4W", "WSPR", "MSK144", "ECHO", "TUNE", "FOX", "SUPERFOX", "JTTY"};  // what render takes: those it lists in encodes
const char *const LIVE[] = {"FT8", "FT4"};  // what live takes
// The period, s, when --period gives none: the mode's own, as a configure that carries the mode sets it; FreqCal has none, so the examples take 15 s; TUNE's is the length of its tone. JTTY has no periods.
const std::pair<const char *, double> PERIOD[] = {{"FT8", 15.0}, {"FT4", 7.5}, {"JT9", 60.0}, {"JT65", 60.0}, {"JT4", 60.0}, {"FST4", 60.0}, {"FST4W", 120.0}, {"Q65", 60.0}, {"MSK144", 30.0}, {"ECHO", 3.0}, {"FREQCAL", 15.0}, {"WSPR", 120.0}, {"TUNE", 15.0}, {"FOX", 15.0}, {"SUPERFOX", 15.0}};
const char *const SUBMODES[] = {"JT4", "JT9", "JT65", "Q65"};      // the modes with a submode: A unless --submode gives one
const char *const PERIODS[] = {"Q65", "FST4", "FST4W", "MSK144"};  // the slot modes whose render takes trperiod; fast JT9's does too
const std::pair<const char *, const char *> DECODED_AS[] = {{"TUNE", "FREQCAL"}, {"FOX", "FT8"}, {"SUPERFOX", "FT8"}};  // what render decodes these back with; every other mode, itself
const std::pair<const char *, double> LIVE_ALLOWANCE[] = {{"FT8", 10.0}, {"FT4", 6.0}};  // a live pass's time between progress lines, once one has completed, s

template <size_t N>
bool listed(const char *const (&list)[N], const std::string &name) {
  return std::any_of(std::begin(list), std::end(list), [&](const char *item) { return name == item; });
}

template <typename T, size_t N>
std::optional<T> lookup(const std::pair<const char *, T> (&table)[N], const std::string &name) {
  for (const auto &entry : table)
    if (name == entry.first) return entry.second;
  return std::nullopt;
}

std::string decodedAs(const std::string &mode) {
  const std::optional<const char *> as = lookup(DECODED_AS, mode);
  return as ? *as : mode;
}

// nfa and nfb when --low and --high give none: in FST4 they are its F Low and F High, WSJT-X's 600 and 1400 Hz; elsewhere the waterfall's range.
std::pair<int, int> searchRange(const std::string &mode) { return mode == "FST4" ? std::make_pair(600, 1400) : std::make_pair(200, 4000); }

// The whole samples in the seconds, truncated as Python's int() truncates, so that every example sends as many; none below 0.
size_t samplesIn(double seconds) { return seconds > 0 ? size_t(std::min(seconds * RATE, 1e15)) : 0; }

// The whole seconds, truncated as Python's int() truncates: render's integer keys. The bound is far beyond any period the codec takes.
long long wholeSeconds(double seconds) { return static_cast<long long>(std::max(-1e15, std::min(seconds, 1e15))); }

// Where WSJT-X starts a transmission in its period, s.
double txStart(const std::string &mode, double period, bool fast) {
  if (mode == "MSK144" || mode == "ECHO" || mode == "TUNE" || fast) return 0.0;
  if (mode == "SUPERFOX") return 0.4;
  if (mode == "FT4") return 0.3;
  if (mode == "FT8" || mode == "FOX" || (mode == "FST4" && period == 15) || (mode == "Q65" && (period == 15 || period == 30))) return 0.5;
  return 1.0;
}

// The rendered audio, 36 dB down (an arithmetic shift right by 6), after offset samples of silence, over noise, in a buffer of total samples: about 12 dB SNR in 2500 Hz, as decoders expect a noise floor (JT65's decodes nothing without one).
// The noise is integer arithmetic, so that every example sends the same samples: a 32-bit xorshift (13, 17, 5) from 1, and each sample the sum of its four bytes less 510.
std::string overNoise(const std::string &audio, size_t offset, size_t total) {
  const size_t length = audio.size() / 2;
  std::string out(2 * total, '\0');
  uint32_t x = 1;
  for (size_t i = 0; i < total; ++i) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    int sample = int(x & 255) + int((x >> 8) & 255) + int((x >> 16) & 255) + int(x >> 24) - 510;
    if (i >= offset && i - offset < length) {
      int signal = uint8_t(audio[2 * (i - offset)]) | uint8_t(audio[2 * (i - offset) + 1]) << 8;
      if (signal >= 0x8000) signal -= 0x10000;           // int16 little-endian
      sample += (signal - (signal < 0 ? 63 : 0)) / 64;  // >> 6, rounding down as an arithmetic shift does, which C++17 leaves to the compiler
    }
    const uint16_t bits = uint16_t(sample);
    out[2 * i] = char(bits & 0xFF);
    out[2 * i + 1] = char(bits >> 8);
  }
  return out;
}

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

// A number as the Python example sends a period: a whole number as an integer, 15; otherwise the fewest digits that read back as the value, 7.5.
std::string decimal(double value) {
  char text[400];
  if (value == std::floor(value) && std::fabs(value) < 1e16) {
    std::snprintf(text, sizeof text, "%.0f", value);
    return text;
  }
  for (int digits = 1; digits <= 17; ++digits) {
    std::snprintf(text, sizeof text, "%.*g", digits, value);
    if (std::strtod(text, nullptr) == value) break;
  }
  return text;
}

// A JSON number as the examples print it: one decimal, or nan for null (a value that is not finite). printf's %.1f rounds the value half to even on glibc and macOS, as WSJT-X shows DT: 0.25 prints 0.2.
std::string number(const Json &value) {
  if (value.type != Json::Number) return "nan";
  char text[400];
  std::snprintf(text, sizeof text, "%.1f", value.number);
  return text;
}

// A JSON object, written a member at a time in the order given; each value is JSON text already (quote() for a string).
class Object {
 public:
  Object &set(const std::string &key, const std::string &value) {
    text_ += (text_.empty() ? "{" : ",") + quote(key) + ":" + value;
    return *this;
  }
  std::string str() const { return text_.empty() ? "{}" : text_ + "}"; }

 private:
  std::string text_;
};

std::string join(const std::vector<std::string> &items, const char *separator) {
  std::string out;
  for (size_t i = 0; i < items.size(); ++i) out += (i ? separator : "") + items[i];
  return out;
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

// WSJT-X names a saved period YYMMDD_HHMMSS.wav, or YYMMDD_HHMM.wav in periods of a minute or more: its start, in seconds of the day, or 0.
double fileLabel(const std::string &path) {
  for (const size_t digits : {size_t(6), size_t(4)}) {  // _HHMMSS.wav, then _HHMM.wav
    if (path.size() < digits + 5) continue;
    std::string tail = path.substr(path.size() - digits - 5);
    for (char &c : tail) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (tail[0] != '_' || tail.compare(digits + 1, 4, ".wav") != 0 ||
        !std::all_of(tail.begin() + 1, tail.begin() + 1 + digits, [](char c) { return c >= '0' && c <= '9'; }))
      continue;
    const int h = std::stoi(tail.substr(1, 2)), m = std::stoi(tail.substr(3, 2)), s = digits == 6 ? std::stoi(tail.substr(5, 2)) : 0;
    return h < 24 && m < 60 && s < 60 ? h * 3600 + m * 60 + s : 0;
  }
  return 0;
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

// A thread the client can stop waiting for: one blocked on a pipe that a process the codec started holds open does not end. Its task holds a share of all it uses, so the thread can outlive what started it.
class Worker {
 public:
  Worker() = default;
  explicit Worker(std::function<void()> task) : state_(std::make_shared<State>()) {
    thread_ = std::thread([state = state_, task = std::move(task)] {
      task();
      std::lock_guard<std::mutex> lock(state->mutex);
      state->done = true;
      state->changed.notify_all();
    });
  }
  Worker(Worker &&) = default;
  Worker &operator=(Worker &&) = default;
  ~Worker() {
    if (thread_.joinable()) thread_.detach();
  }

  // Waits for the thread to end, until the deadline at most, then lets go of it: true when it ended.
  bool finish(std::chrono::steady_clock::time_point deadline) {
    if (!thread_.joinable()) return true;
    std::unique_lock<std::mutex> lock(state_->mutex);
    const bool done = state_->changed.wait_until(lock, deadline, [this] { return state_->done; });
    lock.unlock();
    if (done)
      thread_.join();
    else
      thread_.detach();
    return done;
  }

 private:
  struct State {
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false;
  };
  std::shared_ptr<State> state_;
  std::thread thread_;
};

// The codec's standard input. The thread that writes the audio holds a share of it: blocked on the pipe, it can outlive the Codec.
class Input {
 public:
  explicit Input(Pipe pipe) : pipe_(pipe) {}
  Input(const Input &) = delete;
  Input &operator=(const Input &) = delete;
  ~Input() { closePipe(pipe_); }

  // A broken pipe means the codec has exited, and its lines and exit status say why.
  void write(const std::string &bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pipe_ != NO_PIPE && !writePipe(pipe_, bytes.data(), bytes.size())) closePipe(pipe_);
  }

  void send(const std::string &json) { write(frame(CONTROL, json)); }

  bool open() {
    std::lock_guard<std::mutex> lock(mutex_);
    return pipe_ != NO_PIPE;
  }

  // Decodes what is in, ends the session and closes the codec's input.
  void halt() {
    send("{\"t\":\"halt\"}");
    std::lock_guard<std::mutex> lock(mutex_);
    closePipe(pipe_);
  }

 private:
  std::mutex mutex_;
  Pipe pipe_;
};

// The codec's standard output, as lines, for the client to take. The thread that reads the pipe holds a share of it: blocked on the pipe, it can outlive the Codec.
struct Output {
  std::mutex mutex;
  std::condition_variable changed;
  std::deque<Line> lines;
  bool ended = false;  // the pipe has ended
};

void addLine(Output &output, std::string text) {
  if (!text.empty() && text.back() == '\r') text.pop_back();  // lines end in CRLF on Windows
  Line line;
  if (!JsonReader(text).read(line.json) || line.json.type != Json::Object) {
    std::fprintf(stderr, "codec_client: not a JSON object: %s\n", text.c_str());
    return;
  }
  line.text = std::move(text);
  std::lock_guard<std::mutex> lock(output.mutex);
  output.lines.push_back(std::move(line));
  output.changed.notify_all();
}

// Reads the codec's standard output into lines until it ends, then closes the pipe.
void readOutput(Pipe pipe, Output &output) {
  std::string pending;
  char buffer[16384];
  while (const size_t n = readPipe(pipe, buffer, sizeof buffer)) {
    pending.append(buffer, n);
    size_t end;
    while ((end = pending.find('\n')) != std::string::npos) {
      addLine(output, pending.substr(0, end));
      pending.erase(0, end + 1);
    }
  }
  if (!pending.empty()) addLine(output, pending);
  closePipe(pipe);
  std::lock_guard<std::mutex> lock(output.mutex);
  output.ended = true;
  output.changed.notify_all();
}

// Passes the codec's standard error to ours until it ends, then closes the pipe: diagnostics for people, not for parsing.
void readErrors(Pipe pipe) {
  char buffer[4096];
  while (const size_t n = readPipe(pipe, buffer, sizeof buffer)) {
    std::fwrite(buffer, 1, n, stderr);
    std::fflush(stderr);
  }
  closePipe(pipe);
}

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
    Pipe input = NO_PIPE, output = NO_PIPE, errors = NO_PIPE;  // this client's ends
#ifdef _WIN32
    SECURITY_ATTRIBUTES inherited{};
    inherited.nLength = sizeof inherited;
    inherited.bInheritHandle = TRUE;
    HANDLE in = nullptr, out = nullptr, err = nullptr;
    if (!CreatePipe(&in, &input, &inherited, 0) || !CreatePipe(&output, &out, &inherited, 0) || !CreatePipe(&errors, &err, &inherited, 0)) {
      error = "cannot create pipes";
      for (HANDLE made : {in, out, err, input, output, errors})
        if (made) CloseHandle(made);
      return false;
    }
    for (HANDLE own : {input, output, errors}) SetHandleInformation(own, HANDLE_FLAG_INHERIT, 0);
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
      for (Pipe own : {input, output, errors}) closePipe(own);
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
    input = in[1];
    output = out[0];
    errors = err[0];
    if (pid_ < 0) {
      error = std::strerror(errno);
      for (Pipe own : {input, output, errors}) closePipe(own);
      return false;
    }
#endif
    running_ = true;
    input_ = std::make_shared<Input>(input);
    // Read stdout and stderr on their own threads: a pipe left full stops the codec. Each thread closes its pipe when the pipe ends.
    outputReader_ = Worker([output, lines = output_] { readOutput(output, *lines); });
    errorReader_ = Worker([errors] { readErrors(errors); });
    input_->write(std::string(HEADER, sizeof HEADER));
    return true;
  }

  // The codec's standard input, which the thread that writes the audio shares.
  std::shared_ptr<Input> input() const { return input_; }

  void send(const std::string &json) { input_->send(json); }

  void halt() { input_->halt(); }

  // The next line, waiting at most timeout for it. The output ends when its pipe does, or RELEASE_SECONDS after the codec's exit, as a process it started can hold the pipe.
  Got next(Line &line, std::chrono::milliseconds timeout) {
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + timeout;
    for (;;) {
      {
        std::unique_lock<std::mutex> lock(output_->mutex);
        if (output_->changed.wait_until(lock, std::min(deadline, Clock::now() + std::chrono::milliseconds(50)), [this] { return !output_->lines.empty() || output_->ended; })) {
          if (output_->lines.empty()) return END;
          line = std::move(output_->lines.front());
          output_->lines.pop_front();
          return LINE;
        }
      }
      exited(std::chrono::milliseconds(0));
      if (exitSeen_ && Clock::now() >= releaseBy_) return END;  // what the codec wrote before it exited has been read: let go of the pipe
      if (Clock::now() >= deadline) return NOTHING_YET;
    }
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
    if (WaitForSingleObject(process_, DWORD(within.count())) != WAIT_OBJECT_0) return false;
    sawExit();
    return true;
#else
    const auto deadline = std::chrono::steady_clock::now() + within;
    for (;;) {
      int how = 0;
      const pid_t got = waitpid(pid_, &how, WNOHANG);
      if (got == pid_) {
        status_ = WIFEXITED(how) ? WEXITSTATUS(how) : -WTERMSIG(how);
        running_ = false;
        sawExit();
        return true;
      }
      if (got < 0 && errno != EINTR) return true;  // no such child: wait() has nothing to wait for
      if (std::chrono::steady_clock::now() >= deadline) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
#endif
  }

  // Waits for the codec to exit, and returns its exit status (negative: the signal that ended it). Its pipes close as it exits, unless a process it started holds them: the readers have until releaseBy(), and then the client lets go of them.
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
      sawExit();
    }
    outputReader_.finish(releaseBy_);
    errorReader_.finish(releaseBy_);
    return status_;
  }

  // When the client stops waiting for what uses the codec's pipes: RELEASE_SECONDS after the codec's exit.
  std::chrono::steady_clock::time_point releaseBy() const { return releaseBy_; }

 private:
  // The codec has exited: what uses its pipes has RELEASE_SECONDS more, from the first time the client sees the exit.
  void sawExit() {
    if (exitSeen_) return;
    exitSeen_ = true;
    releaseBy_ = std::chrono::steady_clock::now() + std::chrono::seconds(RELEASE_SECONDS);
  }

#ifdef _WIN32
  HANDLE process_ = nullptr;
#else
  pid_t pid_ = -1;
#endif
  bool running_ = false, exitSeen_ = false;
  int status_ = -1;
  std::chrono::steady_clock::time_point releaseBy_;
  std::shared_ptr<Input> input_;
  std::shared_ptr<Output> output_ = std::make_shared<Output>();
  Worker outputReader_, errorReader_;
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
  // The mode's period and its live allowance, s.
  Watchdog(double period, double liveAllowance) : period_(period), liveAllowance_(liveAllowance) {}

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

  double allowanceNow() const { return completed_ ? liveAllowance_ : std::max(300.0, 4 * period_); }

  // A period's first answer. Its later ones, and those for a period not sent whole, find nothing waiting: a period waits from before its first sample, so no answer comes before it.
  void answer(const std::string &label) { waiting_.erase(label); }

  const double period_, liveAllowance_;
  std::mutex mutex_;
  bool busy_ = false, sawBusy_ = false, completed_ = false;  // completed: a pass has completed
  std::string busyMode_;
  Clock::time_point lastProgress_ = Clock::now(), idleSince_ = Clock::now();
  std::map<std::string, Clock::time_point> waiting_;  // whole periods not answered yet, and when each is due
};

// Sends the audio a period of the given seconds at a time, each period's label in a configure before its first sample, then halt; with no period (JTTY), all of it with no label.
// Live, it starts on the next UTC period boundary, labels each period with its UTC start and sends each frame when its last sample is due, as a receiver's audio arrives.
void sendAudio(Input &input, const std::string &audio, std::optional<double> seconds, double label, bool live, Watchdog *watchdog) {
  const size_t total = audio.size() / 2;
  const bool labelled = seconds.has_value();
  const size_t period = labelled ? samplesIn(*seconds) : std::max<size_t>(total, 1);  // --period holds a sample at least
  if (live) {
    const double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    label = (std::floor(now / *seconds) + 1) * *seconds;
    std::this_thread::sleep_for(std::chrono::duration<double>(label - now));
  }
  const auto start = std::chrono::steady_clock::now();
  auto due = [&](size_t sample) {  // when a sample is due on the replay clock
    return start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(double(sample) / RATE));
  };
  // After halt the codec's exit is due as a period's answer is, from the last sample's scheduled time; no line answers it, and it is registered before the first sample, as a codec that stops reading blocks the writer.
  if (watchdog) watchdog->periodDue("halt", due(total));
  for (size_t k = 0, first = 0; first < total; ++k, first += period) {
    const size_t end = std::min(first + period, total);
    if (labelled) {
      const std::string periodLabel = utc(label + k * *seconds);
      if (watchdog && end - first == period) {  // counted from the period's end, not from the last write, which a codec that stops reading blocks
        std::string digits = periodLabel;
        digits.erase(std::remove(digits.begin(), digits.end(), ':'), digits.end());
        watchdog->periodDue(digits, due(end));
      }
      input.send("{\"t\":\"configure\",\"utc\":" + quote(periodLabel) + "}");
    }
    for (size_t i = first; i < end; i += CHUNK) {
      const size_t j = std::min(i + CHUNK, end);
      if (live) std::this_thread::sleep_until(due(j));
      if (!input.open()) return;
      input.write(frame(AUDIO, audio.substr(2 * i, 2 * (j - i))));
    }
  }
  input.halt();
}

// Streams the audio, prints what the codec decodes and measures, and returns the exit status; messages gets each decode's message and each complete JTTY message's text, measured the echo and freqcal lines.
int run(Codec &codec, const std::string &audio, const std::string &mode, std::optional<double> seconds, double label, bool live, std::vector<std::string> &messages,
        std::vector<Json> &measured) {
  std::shared_ptr<Watchdog> watchdog;
  if (live) watchdog = std::make_shared<Watchdog>(lookup(PERIOD, mode).value_or(0), lookup(LIVE_ALLOWANCE, mode).value_or(0));  // live takes FT8 and FT4, which have both
  // The writer holds a share of all it uses: blocked on the codec's input, it can outlive this call.
  Worker writer([input = codec.input(), audio, seconds, label, live, watchdog] { sendAudio(*input, audio, seconds, label, live, watchdog.get()); });
  int errors = 0;
  const double silence = silenceBound(seconds);
  auto heard = std::chrono::steady_clock::now();
  for (;;) {
    Line line;
    const Codec::Got got = codec.next(line, std::chrono::milliseconds(500));
    if (got == Codec::END) break;
    if (got == Codec::LINE) heard = std::chrono::steady_clock::now();
    if (watchdog) {
      if (got == Codec::LINE) watchdog->saw(line.json);
      if (watchdog->hung()) {
        std::fprintf(stderr, "codec_client: no progress from the codec in %.0f s: it has hung\n", watchdog->allowance());
        abandon(codec, HUNG);
      }
    } else if (std::chrono::steady_clock::now() - heard > std::chrono::duration<double>(silence)) {
      reportSilence(silence);
      abandon(codec, HUNG);
    }
    const Json &json = line.json;
    const std::string kind = json["t"].str();  // other lines, and kinds this client does not know, are skipped
    if (kind == "decode") {
      messages.push_back(json["message"].str());
      std::printf("decode %s %lld %s %lld %s\n", json["time"].str().c_str(), json["snr"].integer(), number(json["dt"]).c_str(), json["freq"].integer(), json["message"].str().c_str());
      std::fflush(stdout);
    } else if (kind == "decode_finished") {
      std::printf("finished %s\n", json["period_end"].str().c_str());
      std::fflush(stdout);
    } else if (kind == "echo") {  // Echo: one a period
      std::printf("echo %s %s %s %s %lld\n", json["time"].str().c_str(), number(json["level"]).c_str(), number(json["snr"]).c_str(), number(json["dfreq"]).c_str(), json["quality"].integer());
      std::fflush(stdout);
      measured.push_back(std::move(line.json));
    } else if (kind == "freqcal") {  // FreqCal: one a measurement
      std::printf("freqcal %s %s %s %s %s\n", json["time"].str().c_str(), number(json["freq"]).c_str(), number(json["error"]).c_str(), number(json["level"]).c_str(), number(json["snr"]).c_str());
      std::fflush(stdout);
      measured.push_back(std::move(line.json));
    } else if (kind == "jtty_update" && json["state"].str() != "growing") {  // JTTY: a message's last line; growing ones are its text so far
      if (json["state"].str() == "complete") messages.push_back(json["text"].str());
      std::printf("jtty %s %s %lld %s %s\n", json["state"].str().c_str(), number(json["start"]).c_str(), json["snr"].integer(), number(json["freq"]).c_str(), json["text"].str().c_str());
      std::fflush(stdout);
    } else if (kind == "error") {
      ++errors;
      std::fprintf(stderr, "codec_client: codec error: %s\n", line.text.c_str());
    }
  }
  awaitExit(codec, HUNG);  // its output has ended, so its exit is due: the watchdog no longer runs to catch a hang
  const int status = codec.wait();
  writer.finish(codec.releaseBy());  // then without it: it may be blocked on the codec's input, which a process the codec started holds
  if (status != 0) {
    std::fprintf(stderr, "codec_client: wsjt-codec exited with status %d\n", status);
    return FAILED;
  }
  return errors ? FAILED : OK;
}

// ---------------------------------------------------------------- commands

struct Options {
  std::string command, codec, data = ".", out, mode = "FT8", submode = "A", mycall;
  std::string wav;                     // decode and live: the WAV file
  std::vector<std::string> texts;      // render: the TEXTs
  std::optional<double> period;        // s: --period's, or the mode's; none in JTTY
  bool fast = false;
  int depth = 3;
  std::optional<int> freq, low, high;  // Hz: render's freq always; decode's when given
};

template <size_t N>
std::string choices(const char *const (&modes)[N]) {
  std::string out;
  for (const char *mode : modes) out += (out.empty() ? "" : "|") + std::string(mode);
  return out;
}

std::string usage() {
  return "usage: codec_client decode --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--low HZ] [--high HZ] [--mycall CALL] WAV\n"
         "       codec_client live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV\n"
         "       codec_client render --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--out WAV] [TEXT...]\n"
         "decode's MODE: " + choices(DECODES) + "\nrender's MODE: " + choices(ENCODES) + "\n";
}

// Starts the codec and waits for its ready line, which lists the modes it decodes (modes) and encodes (encodes); decodes, a mode it must decode too.
std::unique_ptr<Codec> startCodec(const Options &options, const char *kinds, const std::string &decodes = std::string()) {
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
  const Json &ready = line.json;
  if (ended || ready["t"].str() != "ready" || !ready[kinds].contains(options.mode) || (!decodes.empty() && !ready["modes"].contains(decodes))) {
    std::fprintf(stderr, "codec_client: no ready line naming %s in %s: %s\n", options.mode.c_str(), kinds, ended ? "end of output" : line.text.c_str());
    codec->kill();  // at once, as when no ready line comes: there is no session to end
    codec->wait();  // its standard error, which may say why, passed on until its pipes close, RELEASE_SECONDS after its exit at most
    return nullptr;
  }
  return codec;
}

// The configure that sets the mode up to decode, with the search keys and the calls; rxfreq, when given, is the receive frequency.
// Sent with the mode: these examples have no station, so no calls (the decoders use the starting calls otherwise) but --mycall's, and a configure that carries the mode sets the mode up as WSJT-X does, which can change the search keys and the frequencies, so the search keys go with it.
// Integers stay plain JSON integers: a configure declines 3.0 for an integer key (configure_type_error).
Object configure(const Options &options, const std::string &mode, std::optional<int> rxfreq) {
  const std::pair<int, int> range = searchRange(mode);
  Object message;
  message.set("t", quote("configure")).set("mode", quote(mode)).set("depth", std::to_string(options.depth)).set("mycall", quote(options.mycall));
  message.set("nfa", std::to_string(options.low.value_or(range.first))).set("nfb", std::to_string(options.high.value_or(range.second)));
  message.set("his_call", quote("")).set("his_grid", quote("")).set("ntol", "1000");
  if (mode != "JTTY" && options.period) message.set("trperiod", decimal(*options.period));  // with the mode, as FreqCal and fast JT9 need it
  if (listed(SUBMODES, mode)) message.set("submode", quote(options.submode));
  if (mode == "JT9") message.set("fast", options.fast ? "true" : "false");  // with every JT9 configure that carries the mode
  if (rxfreq) message.set("rxfreq", std::to_string(*rxfreq));
  return message;
}

int decode(const Options &options) {
  std::string audio, error;
  if (!readWav(options.wav, audio, error)) {
    std::fprintf(stderr, "codec_client: %s\n", error.c_str());
    return USAGE;
  }
  std::unique_ptr<Codec> codec = startCodec(options, "modes");
  if (!codec) return FAILED;
  const bool live = options.command == "live";
  Object message = configure(options, options.mode, options.freq);
  if (live) message.set("live_audio", "true").set("busy_events", "true");
  codec->send(message.str());
  std::vector<std::string> messages;
  std::vector<Json> measured;
  const std::optional<double> seconds = options.mode == "JTTY" ? std::optional<double>() : options.period;
  return run(*codec, audio, options.mode, seconds, fileLabel(options.wav), live, messages, measured);
}

// The render request for the mode.
std::string renderRequest(const Options &options) {
  const std::string &mode = options.mode;
  Object request;
  request.set("t", quote("render")).set("id", "1").set("freq", std::to_string(*options.freq)).set("rate", std::to_string(RATE));
  if (mode == "JTTY") return request.set("text", quote(join(options.texts, " "))).str();  // JTTY's requests carry no mode
  request.set("mode", quote(mode));
  if (mode == "FOX" || mode == "SUPERFOX") {
    std::vector<std::string> slots;
    for (const std::string &text : options.texts) slots.push_back(quote(text));
    request.set("slots", "[" + join(slots, ",") + "]");  // one message a slot
  } else if (mode == "TUNE") {
    request.set("seconds", std::to_string(wholeSeconds(*options.period)));  // a steady tone for the period FreqCal measures it in
  } else if (mode == "ECHO") {
    request.set("kind", quote("tone"));  // WSJT-X's Fixed Tone
  } else {
    request.set("text", quote(join(options.texts, " ")));
  }
  if (listed(SUBMODES, mode)) request.set("submode", quote(options.submode));
  if (listed(PERIODS, mode) || options.fast) request.set("trperiod", std::to_string(wholeSeconds(*options.period)));
  if (options.fast) request.set("fast", "true");
  return request.str();
}

// Whether a file exists, whatever its permissions: a file the client cannot read exists too.
bool exists(const std::string &path) {
#ifdef _WIN32
  return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
  struct stat info;
  return stat(path.c_str(), &info) == 0;
#endif
}

int render(const Options &options) {
  // --out must be writable before the codec starts. A file made to find that out is removed, and the audio is written once it is all in.
  const bool existed = !options.out.empty() && exists(options.out);
  if (!options.out.empty()) {
    if (!std::ofstream(options.out, std::ios::binary | std::ios::app)) {
      std::fprintf(stderr, "codec_client: cannot write %s\n", options.out.c_str());
      return USAGE;
    }
    if (!existed) std::remove(options.out.c_str());
  }
  const std::string mode = decodedAs(options.mode);  // what it decodes the render back with
  std::unique_ptr<Codec> codec = startCodec(options, "encodes", mode);
  if (!codec) return FAILED;
  codec->send(renderRequest(options));
  std::vector<std::string> sent;  // what it sent: the message, FOX's and SUPERFOX's slots, or JTTY's segments
  long long samples = 0;
  std::string audio;
  const double silence = silenceBound(std::nullopt);  // a reply waits for no decode
  for (;;) {  // the reply: encoded (JTTY: a segment line a segment), the pcm lines, then rendered; or rejected
    Line line;
    const Codec::Got got = codec->next(line, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<double>(silence)));
    if (got == Codec::NOTHING_YET) {
      reportSilence(silence);
      abandon(*codec, HUNG);
    }
    const bool ended = got == Codec::END;
    const Json &json = line.json;
    const std::string kind = json["t"].str();
    if (ended || kind == "rejected" || kind == "error" || (kind == "rendered" && sent.empty())) {  // a rendered line with nothing encoded before it is no render
      std::fprintf(stderr, "codec_client: render failed: %s\n", ended ? "end of output" : line.text.c_str());
      quit(*codec);
      return FAILED;
    }
    if (kind == "encoded") {
      samples = json["samples"].integer();
      sent.clear();
      if (options.mode == "FOX") {
        for (const Json &slot : json["slots"].items) sent.push_back(slot["msgsent"].str());
      } else if (options.mode == "SUPERFOX") {
        for (std::string text : options.texts) {  // SuperFox's encoded line has no msgsent: WSJT-X shows the slots as written, upper case
          for (char &c : text) c = char(std::toupper(static_cast<unsigned char>(c)));
          sent.push_back(text);
        }
      } else {
        sent.push_back(json["msgsent"].str());
      }
    } else if (kind == "segment") {  // JTTY: each segment's text and samples
      sent.push_back(json["text"].str());
      samples += json["samples"].integer();
    } else if (kind == "pcm") {
      audio += fromBase64(json["data"].str());  // int16 little-endian samples at the requested rate
    } else if (kind == "rendered") {
      break;
    }
  }
  std::printf("rendered %lld %s\n", samples, join(sent, " | ").c_str());
  std::fflush(stdout);
  if (!options.out.empty() && !writeWav(options.out, audio)) {
    std::fprintf(stderr, "codec_client: cannot write %s\n", options.out.c_str());
    if (!existed) std::remove(options.out.c_str());
    quit(*codec);
    return FAILED;
  }
  // Decode it back, over noise: the audio where WSJT-X starts it in its period; JTTY's after a second, with a second after it.
  std::optional<double> seconds;
  std::string signal;
  if (options.mode == "JTTY") {
    signal = overNoise(audio, RATE, samplesIn(double(audio.size()) / 2 / RATE + 2));
  } else {
    seconds = options.period;
    signal = overNoise(audio, samplesIn(txStart(options.mode, *seconds, options.fast)), samplesIn(*seconds));
  }
  Options back = options;
  if (mode == "FST4") {  // its F Low and F High around the render
    back.low = *options.freq - 200;
    back.high = *options.freq + 200;
  }
  Object message = configure(back, mode, mode == "MSK144" ? std::optional<int>() : options.freq);  // MSK144's Rx Freq is its tones' center
  if (options.mode == "SUPERFOX") message.set("contest_type", quote("hound")).set("superfox_mode", "true").set("even_sequence", "true");  // a SuperFox Hound decodes it
  if (options.mode == "ECHO") message.set("live_audio", "true").set("echo_freq", std::to_string(*options.freq));  // the echo's parameters from the keys, not from the audio's first samples as a saved file carries them
  codec->send(message.str());
  std::vector<std::string> messages;
  std::vector<Json> measured;
  const int status = run(*codec, signal, mode, seconds, 0, false, messages, measured);
  if (status != OK) return status;
  std::vector<std::string> missing;
  if (options.mode == "TUNE") {
    if (std::none_of(measured.begin(), measured.end(), [](const Json &m) { return m["t"].str() == "freqcal" && m["error"].type == Json::Number && std::fabs(m["error"].number) < 1; }))
      missing.push_back("a measurement within 1 Hz of " + std::to_string(*options.freq) + " Hz");
  } else if (options.mode == "ECHO") {
    if (std::none_of(measured.begin(), measured.end(), [](const Json &m) { return m["t"].str() == "echo" && m["snr"].type == Json::Number && m["snr"].number > 0; }))  // a null snr is not above the noise
      missing.push_back("an echo above the noise");
  } else {
    for (const std::string &text : sent)
      if (std::none_of(messages.begin(), messages.end(), [&](const std::string &message) { return words(message) == words(text); })) missing.push_back(text);
  }
  if (!missing.empty()) {
    std::fprintf(stderr, "codec_client: round trip failed: no decode of %s\n", join(missing, " | ").c_str());
    return FAILED;
  }
  return OK;
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

// A number of seconds, finite, as JSON carries numbers.
bool toNumber(const std::string &text, double &value) {
  char *end = nullptr;
  value = std::strtod(text.c_str(), &end);
  return !text.empty() && end == text.c_str() + text.size() && std::isfinite(value);
}

// Reads the command line into options: false, with the problem, when it is not usable.
bool parse(int argc, char **argv, Options &options, std::string &problem) {
  options.command = argc > 1 ? argv[1] : "";
  const bool render = options.command == "render", live = options.command == "live";
  if (!render && !live && options.command != "decode") {
    problem = "the command is decode, live or render";
    return false;
  }
  // What each command takes: decode and render a submode, a period, fast and a frequency; decode the search range and the call; render --out.
  auto takes = [&](const std::string &option) {
    return option == "--codec" || option == "--data" || option == "--mode" || option == "--depth" ||
           (!live && (option == "--submode" || option == "--period" || option == "--fast" || option == "--freq")) ||
           (!live && !render && (option == "--low" || option == "--high" || option == "--mycall")) || (render && option == "--out");
  };
  std::vector<std::string> arguments;  // the WAV file, or render's TEXTs
  bool optionsEnded = false;           // after --, every argument is the WAV file or a TEXT
  for (int i = 2; i < argc; ++i) {
    std::string option = argv[i], value;
    if (!optionsEnded && option == "--") {
      optionsEnded = true;
      continue;
    }
    if (optionsEnded || option.compare(0, 2, "--") != 0) {
      arguments.push_back(option);
      continue;
    }
    const size_t equals = option.find('=');
    const bool given = equals != std::string::npos;  // --option=value
    if (given) {
      value = option.substr(equals + 1);
      option.erase(equals);
    }
    if (!takes(option)) {
      problem = "unknown option " + option;
      return false;
    }
    if (option == "--fast") {  // a flag, with no value
      if (given) {
        problem = "--fast takes no value";
        return false;
      }
      options.fast = true;
      continue;
    }
    if (!given) {
      if (i + 1 == argc) {
        problem = option + " needs a value";
        return false;
      }
      value = argv[++i];
    }
    if (option == "--codec") {
      options.codec = value;
    } else if (option == "--data") {
      options.data = value;
    } else if (option == "--mode") {
      options.mode = value;
      if (!(live ? listed(LIVE, value) : render ? listed(ENCODES, value) : listed(DECODES, value)))
        problem = "--mode is one of " + (live ? choices(LIVE) : render ? choices(ENCODES) : choices(DECODES));
    } else if (option == "--depth") {
      if (!toInteger(value, options.depth) || options.depth < 1 || options.depth > 3) problem = "--depth is 1, 2 or 3";
    } else if (option == "--submode") {
      options.submode = value;
      if (value.size() != 1 || value[0] < 'A' || value[0] > 'H') problem = "--submode is a letter from A to H";
    } else if (option == "--period") {
      double seconds = 0;
      if (toNumber(value, seconds) && seconds * RATE >= 1)
        options.period = seconds;
      else
        problem = "--period must be a number of seconds that holds a sample at least";
    } else if (option == "--freq" || option == "--low" || option == "--high") {
      int hz = 0;
      if (toInteger(value, hz))
        (option == "--freq" ? options.freq : option == "--low" ? options.low : options.high) = hz;
      else
        problem = option + " is a whole number of Hz";
    } else if (option == "--mycall") {
      options.mycall = value;
    } else {
      options.out = value;
    }
    if (!problem.empty()) return false;
  }
  if (options.codec.empty()) {
    problem = "--codec is required";
  } else if (!render) {
    if (arguments.size() == 1)
      options.wav = arguments[0];
    else
      problem = arguments.empty() ? "no WAV file" : "more than one WAV";
  } else {
    // TUNE and ECHO take no TEXT; FOX and SUPERFOX one a slot, and JTTY one or more, which it joins; the other modes one.
    const bool none = options.mode == "TUNE" || options.mode == "ECHO", several = options.mode == "FOX" || options.mode == "SUPERFOX" || options.mode == "JTTY";
    options.texts = arguments;
    if (none ? !arguments.empty() : several ? arguments.empty() : arguments.size() != 1)
      problem = "render --mode " + options.mode + " takes " + (none ? "no TEXT" : several ? "one TEXT or more" : "one TEXT");
  }
  if (!problem.empty()) return false;
  if (live || !options.period) options.period = lookup(PERIOD, options.mode);  // live takes the mode's; JTTY has none
  if (render && !options.freq) options.freq = options.mode == "MSK144" ? 1000 : options.mode == "SUPERFOX" ? 750 : 1500;  // MSK144's tones are at 1000 and 2000 Hz, and SuperFox goes at 750
  return true;
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
      std::fputs(usage().c_str(), stdout);
      return OK;
    }
  }
  Options options;
  std::string problem;
  if (!parse(argc, argv, options, problem)) {
    std::fprintf(stderr, "codec_client: %s\n%s", problem.c_str(), usage().c_str());
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
