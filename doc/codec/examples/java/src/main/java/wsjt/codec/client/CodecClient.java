// SPDX-License-Identifier: GPL-3.0-or-later
package wsjt.codec.client;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.io.BufferedReader;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.PrintStream;
import java.io.UncheckedIOException;
import java.math.BigDecimal;
import java.math.RoundingMode;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.sound.sampled.AudioFileFormat;
import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioInputStream;
import javax.sound.sampled.AudioSystem;
import javax.sound.sampled.UnsupportedAudioFileException;

/**
 * A wsjt-codec client: decode a WAV file, replay one as live audio, or render a message.
 *
 * <pre>
 *   java -jar codec-client.jar decode --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--low HZ] [--high HZ] [--mycall CALL] WAV
 *   java -jar codec-client.jar live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
 *   java -jar codec-client.jar render --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--out WAV] [TEXT...]
 * </pre>
 *
 * doc/codec/examples/README.md states what each prints and its exit status. Java 17 or later; JSON through Jackson.
 */
public final class CodecClient {
    static final int RATE = 12000;                                                      // the stream's sample rate, Hz
    static final List<String> DECODES = List.of("FT8", "FT4", "JT9", "JT65", "JT4", "FST4", "FST4W", "Q65", "MSK144", "ECHO", "JTTY", "FREQCAL", "WSPR");  // what decode takes: the modes ready lists in modes
    static final List<String> ENCODES = List.of("FT8", "FT4", "JT4", "JT9", "JT65", "Q65", "FST4", "FST4W", "WSPR", "MSK144", "ECHO", "TUNE", "FOX", "SUPERFOX", "JTTY");  // what render takes: those it lists in encodes
    static final List<String> LIVE = List.of("FT8", "FT4");                             // what live takes
    // The period, s, when --period gives none: the mode's own, as a configure that carries the mode sets it; FreqCal has none, so the examples take 15 s; TUNE's is the length of its tone. JTTY has no periods.
    static final Map<String, Double> PERIOD = Map.ofEntries(Map.entry("FT8", 15.0), Map.entry("FT4", 7.5), Map.entry("JT9", 60.0), Map.entry("JT65", 60.0), Map.entry("JT4", 60.0), Map.entry("FST4", 60.0), Map.entry("FST4W", 120.0), Map.entry("Q65", 60.0), Map.entry("MSK144", 30.0), Map.entry("ECHO", 3.0), Map.entry("FREQCAL", 15.0), Map.entry("WSPR", 120.0), Map.entry("TUNE", 15.0), Map.entry("FOX", 15.0), Map.entry("SUPERFOX", 15.0));
    static final List<String> SUBMODES = List.of("JT4", "JT9", "JT65", "Q65");          // the modes with a submode: A unless --submode gives one
    static final List<String> PERIODS = List.of("Q65", "FST4", "FST4W", "MSK144");      // the slot modes whose render takes trperiod; fast JT9's does too
    static final Map<String, String> DECODED_AS = Map.of("TUNE", "FREQCAL", "FOX", "FT8", "SUPERFOX", "FT8");  // what render decodes these back with; every other mode, itself
    static final Map<String, Double> LIVE_ALLOWANCE = Map.of("FT8", 10.0, "FT4", 6.0);  // a live pass's time between progress lines, once one has completed
    static final int READY_SECONDS = 30;                                                // how long the client waits for ready
    static final int EXIT_SECONDS = 10;                                                 // how long the codec has to exit once its output ends, or after halt on a failure
    static final int RELEASE_SECONDS = 2;                                               // how long, once the codec has exited, the client waits for the end of its output and for its writes to its input: a process the codec started can hold the pipes
    static final int SILENCE_SECONDS = 300;                                             // outside live, how long the codec may send no line: the longest interface.md allows a decode pass between progress lines before one has completed, or four periods when that is longer (silenceBound)
    static final Integer CHECK_SILENCE = checkSilence();                                // the check's silence bound, or null
    static final int CHUNK = 1200;                                                      // samples in an audio frame: 0.1 s
    // Sent with the mode: these examples have no station, so no calls (the decoders use the starting calls otherwise) but --mycall's, and a configure that carries the mode sets the mode up as WSJT-X does, which can change the search keys and the frequencies, so the search keys go with it.
    static final int NTOL = 1000;
    static final Map<String, int[]> SEARCH = Map.of("FST4", new int[] {600, 1400});    // nfa and nfb when --low and --high give none: in FST4 they are its F Low and F High, WSJT-X's 600 and 1400 Hz; elsewhere the waterfall's range, 200 and 4000 Hz
    static final int[] WATERFALL = {200, 4000};

    static final byte AUDIO = 0x01, CONTROL = 0x02;  // frame types
    // Session header: magic, sample format 0 (int16), 1 channel, rate in kHz (uint16 little-endian).
    static final byte[] HEADER = {'W', 'S', 'J', 'T', 0, 1, RATE / 1000, 0};

    static final int OK = 0, FAILED = 1, USAGE = 2, HUNG = 3;  // exit status

    /** For the examples' check only (README.md, "The check"), not a setting: CODEC_EXAMPLE_CHECK_SILENCE_SECONDS, a whole number from 1 to 300, replaces the silence bound, so that a run that checks it need not last 300 s; null when it is not set or not one of those. */
    static Integer checkSilence() {
        String check = System.getenv("CODEC_EXAMPLE_CHECK_SILENCE_SECONDS");
        if (check != null && check.matches("[0-9]{1,3}")) {
            int seconds = Integer.parseInt(check);
            if (seconds >= 1 && seconds <= 300) {
                return seconds;
            }
        }
        return null;
    }

    /** Outside live, how long the codec may send no line before the client takes it to have hung, s: 300, or four periods of the given seconds (0: no periods), up to the codec's 1800 s, when that is longer; the check's CHECK_SILENCE replaces it. */
    static double silenceBound(double seconds) {
        if (CHECK_SILENCE != null) {
            return CHECK_SILENCE;
        }
        return seconds != 0 ? Math.max(SILENCE_SECONDS, 4 * Math.min(seconds, 1800)) : SILENCE_SECONDS;
    }

    static final ObjectMapper JSON = new ObjectMapper();
    static final PrintStream OUT = new PrintStream(new FileOutputStream(FileDescriptor.out), true, StandardCharsets.UTF_8);
    static final PrintStream ERR = new PrintStream(new FileOutputStream(FileDescriptor.err), true, StandardCharsets.UTF_8);

    private CodecClient() {
    }

    /** A frame: type (1 byte), body length (uint32 little-endian), body. */
    static byte[] frame(byte kind, byte[] body, int offset, int length) {
        return ByteBuffer.allocate(5 + length).order(ByteOrder.LITTLE_ENDIAN).put(kind).putInt(length).put(body, offset, length).array();
    }

    /** A time of day as the configure key utc takes it: "HH:MM:SS". */
    static String utc(double seconds) {
        long s = (long) seconds % 86400;
        return String.format(Locale.ROOT, "%02d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    }

    /** Where WSJT-X starts a transmission in its period, s. */
    static double txStart(String mode, double period, boolean fast) {
        if (List.of("MSK144", "ECHO", "TUNE").contains(mode) || fast) {
            return 0.0;
        }
        if (mode.equals("SUPERFOX")) {
            return 0.4;
        }
        if (mode.equals("FT4")) {
            return 0.3;
        }
        if (mode.equals("FT8") || mode.equals("FOX") || mode.equals("FST4") && period == 15 || mode.equals("Q65") && (period == 15 || period == 30)) {
            return 0.5;
        }
        return 1.0;
    }

    /** The audio after the given seconds of silence. */
    static byte[] delayed(byte[] audio, double seconds) {
        byte[] out = new byte[2 * (int) (seconds * RATE) + audio.length];
        System.arraycopy(audio, 0, out, out.length - audio.length, audio.length);
        return out;
    }

    /**
     * The rendered audio, 36 dB down (an arithmetic shift right by 6), over noise, in a buffer of the given seconds: about 12 dB SNR in 2500 Hz, as decoders expect a noise floor (JT65's decodes nothing without one).
     *
     * <p>The noise is integer arithmetic, so that every example sends the same samples: a 32-bit xorshift (13, 17, 5) from 1, and each sample the sum of its four bytes less 510; >>> is the unsigned shift the xorshift takes.
     */
    static byte[] overNoise(byte[] audio, double seconds) {
        int total = (int) (seconds * RATE);
        int samples = audio.length / 2;
        ByteBuffer signal = ByteBuffer.wrap(audio).order(ByteOrder.LITTLE_ENDIAN);
        ByteBuffer out = ByteBuffer.allocate(2 * total).order(ByteOrder.LITTLE_ENDIAN);
        int x = 1;
        for (int i = 0; i < total; i++) {
            x ^= x << 13;
            x ^= x >>> 17;
            x ^= x << 5;
            int noise = (x & 255) + (x >>> 8 & 255) + (x >>> 16 & 255) + (x >>> 24) - 510;
            out.putShort((short) ((i < samples ? signal.getShort(2 * i) >> 6 : 0) + noise));
        }
        return out.array();
    }

    /** A JSON number as the examples print it: one decimal, rounded half to even on the value as received, as WSJT-X shows DT, or nan for null. */
    static String oneDecimal(JsonNode number) {
        if (!number.isNumber()) {
            return "nan";  // the codec writes null for a value that is not finite
        }
        double value = number.doubleValue();
        // String.format("%.1f") rounds a tie up: 0.25 would print 0.3, where WSJT-X shows 0.2.
        String text = new BigDecimal(value).setScale(1, RoundingMode.HALF_EVEN).toPlainString();
        // A negative value that rounds to zero keeps its sign, as C's printf keeps it.
        return Math.copySign(1.0, value) < 0 && !text.startsWith("-") ? "-" + text : text;
    }

    static String words(String text) {
        return String.join(" ", text.trim().split("\\s+"));
    }

    static double now() {
        return System.nanoTime() / 1e9;
    }

    static void sleep(double seconds) throws InterruptedException {
        if (seconds > 0) {
            Thread.sleep((long) Math.ceil(seconds * 1000));
        }
    }

    static Thread daemon(Runnable task) {
        Thread thread = new Thread(task);
        thread.setDaemon(true);
        thread.start();
        return thread;
    }

    /** One wsjt-codec process: frames go to its stdin; each stdout line comes back as a JSON object. */
    static final class Codec {
        static final JsonNode END = JSON.createObjectNode();  // the end of the codec's output
        final Process process;
        final BlockingQueue<JsonNode> lines = new LinkedBlockingQueue<>();
        private final OutputStream input;
        private volatile boolean inputOpen = true;
        private long exitedAt;            // when the client saw the codec's exit (System.nanoTime())
        private boolean exitSeen;

        Codec(String path, String dataDir) throws IOException {
            Path data = Path.of(dataDir).toAbsolutePath().normalize();
            Files.createDirectories(data);
            String dir = data.toString();
            // One directory for the codec's data (-a), temporary (-t) and read-only (-r) files, and its working directory, where it writes files of its own.
            ProcessBuilder builder = new ProcessBuilder(Path.of(path).toAbsolutePath().normalize().toString(), "-a", dir, "-t", dir, "-r", dir);
            builder.directory(data.toFile());
            // The decoders' OpenMP threads need the stack WSJT-X gives them.
            builder.environment().putIfAbsent("OMP_STACKSIZE", "10M");
            // On Windows, Java starts a child whose standard streams are all pipes with CREATE_NO_WINDOW: the codec opens no console window.
            process = builder.start();
            input = process.getOutputStream();
            // Read stdout and stderr on their own threads: a pipe left full stops the codec.
            daemon(() -> {
                readLines(process.getInputStream(), this::received);
                lines.add(END);
            });
            daemon(() -> readLines(process.getErrorStream(), ERR::println));  // diagnostics for people, not for parsing
            write(HEADER);
        }

        /** Each line of a stream, decoded as UTF-8; readLine takes the CRLF of Windows too. */
        private static void readLines(InputStream stream, Consumer<String> consumer) {
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(stream, StandardCharsets.UTF_8))) {
                for (String text = reader.readLine(); text != null; text = reader.readLine()) {
                    consumer.accept(text);
                }
            } catch (IOException e) {
                // the pipe closed: the end of the stream
            }
        }

        private void received(String text) {
            JsonNode line = null;
            try {
                line = JSON.readTree(text);
            } catch (JsonProcessingException e) {
                // not JSON: reported below
            }
            if (line != null && line.isObject()) {
                lines.add(line);
            } else {
                ERR.println("codec_client: not a JSON object: " + text);
            }
        }

        boolean inputOpen() {
            return inputOpen;
        }

        /** The next line; END once the codec's output has ended, or RELEASE_SECONDS after the codec has exited, as a process it started can hold the pipe; null when none comes within the time. */
        JsonNode next(long time, TimeUnit unit) throws InterruptedException {
            long deadline = System.nanoTime() + unit.toNanos(time);
            while (true) {
                JsonNode line = lines.poll(Math.min(TimeUnit.MILLISECONDS.toNanos(50), Math.max(0, deadline - System.nanoTime())), TimeUnit.NANOSECONDS);
                if (line != null) {
                    return line;
                }
                if (!process.isAlive()) {
                    sawExit();
                }
                if (exitSeen && System.nanoTime() - exitedAt >= TimeUnit.SECONDS.toNanos(RELEASE_SECONDS)) {
                    return END;  // what the codec wrote before it exited has been read: let go of the pipe
                }
                if (System.nanoTime() - deadline >= 0) {
                    return null;
                }
            }
        }

        synchronized void write(byte[] data) {
            if (!inputOpen) {
                return;
            }
            try {
                input.write(data);
                input.flush();
            } catch (IOException e) {  // a broken pipe: the codec has exited, and its output and exit status say why
                inputOpen = false;
            }
        }

        void send(ObjectNode message) {
            byte[] body;
            try {
                // Jackson writes an int as a plain JSON integer: a configure declines 3.0 for an integer key (configure_type_error).
                body = JSON.writeValueAsBytes(message);
            } catch (JsonProcessingException e) {
                throw new UncheckedIOException(e);
            }
            write(frame(CONTROL, body, 0, body.length));
        }

        /** Decode what is in, end the session and close the codec's input. */
        synchronized void halt() {
            send(JSON.createObjectNode().put("t", "halt"));
            if (inputOpen) {
                inputOpen = false;
                try {
                    input.close();
                } catch (IOException e) {
                    // the codec has exited
                }
            }
        }

        /** The codec has exited: what uses its pipes has RELEASE_SECONDS more, from the first time the client sees the exit. */
        private void sawExit() {
            if (!exitSeen) {
                exitSeen = true;
                exitedAt = System.nanoTime();
            }
        }

        /** What is left, in nanoseconds, of RELEASE_SECONDS from the codec's exit as the client first saw it; all of it when the client has not seen the exit. */
        long releaseLeft() {
            long release = TimeUnit.SECONDS.toNanos(RELEASE_SECONDS);
            return exitSeen ? release - (System.nanoTime() - exitedAt) : release;
        }

        /** The codec's exit status once it exits, within EXIT_SECONDS; null, with the codec stopped, when it does not. */
        Integer exitStatus() throws InterruptedException {
            if (process.waitFor(EXIT_SECONDS, TimeUnit.SECONDS)) {
                sawExit();
                return process.exitValue();
            }
            ERR.println("codec_client: wsjt-codec did not exit within " + EXIT_SECONDS + " s: it has hung");
            kill();
            return null;
        }

        /** Quit a session that failed: halt, and stop the codec if it does not exit. */
        void quit() throws InterruptedException {
            halt();
            exitStatus();
        }

        /** Stop the codec without waiting for its pipes, which a process the codec started can hold open. */
        void kill() throws InterruptedException {
            // Through its ProcessHandle, which only signals it: Process.destroyForcibly also closes the codec's input, and that waits for a write in progress, which a process holding the pipe can block for good.
            process.toHandle().destroyForcibly();
            process.waitFor(5, TimeUnit.SECONDS);
        }
    }

    static String textOrNull(JsonNode line, String key) {
        JsonNode value = line.get(key);
        return value == null || value.isNull() ? null : value.asText();
    }

    /** Tells a slow decode from a hung codec by the busy lines (configure's busy_events). */
    static final class Watchdog {
        private final String mode;
        private boolean busy;
        private String busyMode;
        private boolean completed;                                    // a pass has completed
        private double lastProgress = now();
        private double idleSince = now();
        private final Map<String, Double> waiting = new HashMap<>();  // whole periods not answered yet: label -> when the period's last sample is due

        Watchdog(String mode) {
            this.mode = mode;
        }

        synchronized double allowance() {
            return completed ? LIVE_ALLOWANCE.get(mode) : Math.max(300.0, 4 * PERIOD.get(mode));
        }

        /** A whole period starts, before its first sample is sent; its last sample is due at end (now()'s clock), whether or not the codec reads it. */
        synchronized void periodStarts(String label, double end) {
            waiting.put(label, end);
        }

        private void answer(String label) {
            waiting.remove(label);  // an answer for a period not waiting changes nothing
        }

        synchronized void saw(JsonNode line) {
            double now = now();
            String kind = line.path("t").asText();
            if (kind.equals("busy") && line.path("busy").asBoolean()) {
                if (busyMode != null) {
                    completed = true;  // a busy line ends, and completes, the pass before it
                }
                busy = true;
                busyMode = textOrNull(line, "mode");
                lastProgress = now;
                answer(textOrNull(line, "period_end"));
            } else if (kind.equals("busy")) {
                if (busyMode != null) {
                    completed = true;  // busy false ends, and completes, the pass before it too
                }
                busy = false;
                idleSince = now;
            } else if (kind.equals("pass_progress") || kind.equals("pass_skipped") || kind.equals("decode") && Objects.equals(textOrNull(line, "mode"), busyMode)) {
                lastProgress = now;
            } else if (kind.equals("decode_finished")) {
                completed = true;
                lastProgress = now;
                answer(textOrNull(line, "period_end"));
            }
        }

        synchronized boolean hung() {
            double now = now();
            if (busy) {  // a pass runs: progress lines are due
                return now - lastProgress > allowance();
            }
            if (!waiting.isEmpty()) {  // nothing runs: a period whose last sample is due is due its answer
                return now - Math.max(idleSince, Collections.min(waiting.values())) > allowance();
            }
            return false;
        }
    }

    /**
     * Send audio a period of the given seconds at a time, each period's label in a configure before its first sample, then halt; with no period (0 s: JTTY), all of it with no label.
     *
     * <p>Live, start on the next UTC period boundary, label each period with its UTC start and send each frame when its last sample is due, as a receiver's audio arrives.
     */
    static void sendAudio(Codec codec, byte[] audio, double seconds, double label, boolean live, Watchdog watchdog) throws InterruptedException {
        int samples = audio.length / 2;
        int period = seconds != 0 ? (int) (seconds * RATE) : Math.max(samples, 1);
        if (live) {
            double boundary = (Math.floor(System.currentTimeMillis() / 1000.0 / seconds) + 1) * seconds;
            sleep(boundary - System.currentTimeMillis() / 1000.0);
            label = boundary;
        }
        double start = now();
        // After halt the codec's exit is due as a period's answer is, from the last sample's scheduled time; no line answers it, and it is registered before the first sample, as a codec that stops reading blocks the writer.
        if (watchdog != null) {
            watchdog.periodStarts("halt", start + (double) samples / RATE);
        }
        for (int k = 0, first = 0; first < samples; k++, first += period) {
            int end = Math.min(first + period, samples);
            if (seconds != 0) {
                String periodLabel = utc(label + k * seconds);
                if (watchdog != null && end - first == period) {  // due from its scheduled end, not from its last write, which a codec that stops reading blocks
                    watchdog.periodStarts(periodLabel.replace(":", ""), start + (double) end / RATE);
                }
                codec.send(JSON.createObjectNode().put("t", "configure").put("utc", periodLabel));
            }
            for (int i = first; i < end; i += CHUNK) {
                int j = Math.min(i + CHUNK, end);
                if (live) {
                    sleep(start + (double) j / RATE - now());
                }
                if (!codec.inputOpen()) {
                    return;
                }
                codec.write(frame(AUDIO, audio, 2 * i, 2 * (j - i)));
            }
        }
        codec.halt();
    }

    /** Stream the audio a period of the given seconds at a time (0: JTTY's, no periods), print what the codec decodes and measures, add the messages decoded (JTTY's complete ones among them) to messages and the echo and freqcal lines to measured, and return the exit status. */
    static int run(Codec codec, byte[] audio, String mode, double seconds, double label, boolean live, List<String> messages, List<JsonNode> measured) throws InterruptedException {
        Watchdog watchdog = live ? new Watchdog(mode) : null;
        Thread writer = daemon(() -> {
            try {
                sendAudio(codec, audio, seconds, label, live, watchdog);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        });
        int errors = 0;
        double heard = now();
        while (true) {
            JsonNode line = codec.next(500, TimeUnit.MILLISECONDS);
            if (line == Codec.END) {
                break;
            }
            if (line != null) {
                heard = now();
            }
            if (watchdog != null) {
                if (line != null) {
                    watchdog.saw(line);
                }
                if (watchdog.hung()) {
                    ERR.println("codec_client: no progress from the codec in " + Math.round(watchdog.allowance()) + " s: it has hung");
                    codec.kill();
                    return HUNG;  // without joining the threads, which may be blocked on the codec's pipes
                }
            } else if (now() - heard > silenceBound(seconds)) {
                ERR.println("codec_client: no line from the codec in " + (long) silenceBound(seconds) + " s: it has hung");
                codec.kill();
                return HUNG;  // without joining the threads, which may be blocked on the codec's pipes
            }
            if (line == null) {
                continue;
            }
            switch (line.path("t").asText()) {  // other lines, and kinds this client does not know, are skipped
                case "decode" -> {
                    String message = line.path("message").asText();
                    messages.add(message);
                    OUT.println("decode " + line.path("time").asText() + " " + line.path("snr").asLong() + " " + oneDecimal(line.path("dt")) + " " + line.path("freq").asLong() + " " + message);
                }
                case "decode_finished" -> OUT.println("finished " + line.path("period_end").asText());
                case "echo" -> {  // Echo: one a period
                    measured.add(line);
                    OUT.println("echo " + line.path("time").asText() + " " + oneDecimal(line.path("level")) + " " + oneDecimal(line.path("snr")) + " " + oneDecimal(line.path("dfreq")) + " " + line.path("quality").asLong());
                }
                case "freqcal" -> {  // FreqCal: one a measurement
                    measured.add(line);
                    OUT.println("freqcal " + line.path("time").asText() + " " + oneDecimal(line.path("freq")) + " " + oneDecimal(line.path("error")) + " " + oneDecimal(line.path("level")) + " " + oneDecimal(line.path("snr")));
                }
                case "jtty_update" -> {  // JTTY: a message's last line; growing ones are its text so far
                    String state = line.path("state").asText();
                    if (!state.equals("growing")) {
                        String text = line.path("text").asText();
                        if (state.equals("complete")) {
                            messages.add(text);
                        }
                        OUT.println("jtty " + state + " " + oneDecimal(line.path("start")) + " " + line.path("snr").asLong() + " " + oneDecimal(line.path("freq")) + " " + text);
                    }
                }
                case "error" -> {
                    errors++;
                    ERR.println("codec_client: codec error: " + line);
                }
                default -> {
                }
            }
        }
        Integer status = codec.exitStatus();  // its output has ended, so its exit is due: the watchdog no longer runs to catch a hang
        if (status == null) {
            return HUNG;  // without joining the writer, which may be blocked on the codec's pipe
        }
        TimeUnit.NANOSECONDS.timedJoin(writer, codec.releaseLeft());  // what is left of the codec's RELEASE_SECONDS (none: no wait), then without it: it may be blocked on the codec's input, which a process the codec started holds
        if (status != 0) {
            ERR.println("codec_client: wsjt-codec exited with status " + status);
            return FAILED;
        }
        return errors > 0 ? FAILED : OK;
    }

    /** Whether the line's list under the key names the mode. */
    static boolean names(JsonNode line, String key, String mode) {
        boolean named = false;
        for (JsonNode listed : line == null ? List.<JsonNode>of() : line.path(key)) {
            named |= listed.asText().equals(mode);
        }
        return named;
    }

    /** Start the codec and wait for its ready line, which lists the modes it decodes and encodes; decodes, a mode it must decode too, or null; null when it does not come. */
    static Codec start(Options options, String kinds, String decodes) throws InterruptedException {
        Codec codec;
        try {
            codec = new Codec(options.codec, options.data);
        } catch (IOException | InvalidPathException e) {
            ERR.println("codec_client: cannot start " + options.codec + ": " + e.getMessage());
            return null;
        }
        JsonNode line = codec.next(READY_SECONDS, TimeUnit.SECONDS);
        boolean named = names(line, kinds, options.mode) && (decodes == null || names(line, "modes", decodes));
        if (line == null || !line.path("t").asText().equals("ready") || !named) {
            String got = line == null ? "none within " + READY_SECONDS + " s" : line == Codec.END ? "the output ended" : line.toString();
            ERR.println("codec_client: no ready line naming " + options.mode + " in " + kinds + ": " + got);
            codec.kill();
            return null;
        }
        return codec;
    }

    /**
     * The samples of a 12000 Hz mono 16-bit PCM WAV file: int16 little-endian, the stream's format.
     *
     * <p>AudioSystem reads format 1 and WAVE_FORMAT_EXTENSIBLE, whose subformat GUID it compares whole: only the PCM subformat gives PCM_SIGNED. Its AU and AIFF readers give big-endian samples, which are refused here too.
     */
    static byte[] readWav(String path) throws IOException, UnsupportedAudioFileException {
        try (AudioInputStream wav = AudioSystem.getAudioInputStream(new File(path))) {
            AudioFormat format = wav.getFormat();
            if (format.getChannels() != 1 || format.getSampleSizeInBits() != 16 || format.getSampleRate() != RATE
                    || !format.getEncoding().equals(AudioFormat.Encoding.PCM_SIGNED) || format.isBigEndian()) {
                throw new UnsupportedAudioFileException(path + " is not 12000 Hz mono 16-bit PCM");
            }
            return wav.readAllBytes();
        }
    }

    static void writeWav(String path, byte[] audio) throws IOException {
        AudioFormat format = new AudioFormat(RATE, 16, 1, true, false);  // signed 16-bit little-endian mono
        try (AudioInputStream stream = new AudioInputStream(new ByteArrayInputStream(audio), format, audio.length / 2)) {
            AudioSystem.write(stream, AudioFileFormat.Type.WAVE, new File(path));
        }
    }

    /** WSJT-X names a saved period YYMMDD_HHMMSS.wav, or YYMMDD_HHMM.wav in periods of a minute or more: its start, in seconds of the day, or 0. */
    static int fileLabel(String path) {
        Matcher match = Pattern.compile("_(\\d\\d)(\\d\\d)(\\d\\d)?\\.wav$", Pattern.CASE_INSENSITIVE).matcher(path);
        if (!match.find()) {
            return 0;
        }
        int h = Integer.parseInt(match.group(1)), m = Integer.parseInt(match.group(2)), s = match.group(3) == null ? 0 : Integer.parseInt(match.group(3));
        return h < 24 && m < 60 && s < 60 ? h * 3600 + m * 60 + s : 0;
    }

    /** The configure that sets the mode up to decode, with the search keys and the calls; rxfreq, the receive frequency, or null for none. */
    static ObjectNode configure(Options options, String mode, Integer rxfreq) {
        int[] search = SEARCH.getOrDefault(mode, WATERFALL);
        ObjectNode message = JSON.createObjectNode().put("t", "configure").put("mode", mode);
        if (!mode.equals("JTTY")) {  // with the mode, as FreqCal and fast JT9 need it; a whole number of seconds as an integer
            if (options.period % 1 != 0) {
                message.put("trperiod", options.period);
            } else {
                message.put("trperiod", options.period.longValue());
            }
        }
        if (SUBMODES.contains(mode)) {
            message.put("submode", options.submode);
        }
        if (mode.equals("JT9")) {
            message.put("fast", options.fast);  // with every JT9 configure that carries the mode
        }
        message.put("depth", options.depth).put("mycall", options.mycall).put("his_call", "").put("his_grid", "").put("ntol", NTOL)
                .put("nfa", options.low == null ? search[0] : options.low).put("nfb", options.high == null ? search[1] : options.high);
        if (rxfreq != null) {
            message.put("rxfreq", rxfreq);
        }
        return message;
    }

    static int decode(Options options) throws InterruptedException {
        byte[] audio;
        try {
            audio = readWav(options.target);
        } catch (IOException | UnsupportedAudioFileException e) {
            ERR.println("codec_client: " + e.getMessage());
            return USAGE;
        }
        Codec codec = start(options, "modes", null);
        if (codec == null) {
            return FAILED;
        }
        boolean live = options.command.equals("live");
        ObjectNode configure = configure(options, options.mode, options.freq);
        if (live) {
            configure.put("live_audio", true).put("busy_events", true);
        }
        codec.send(configure);
        double seconds = options.mode.equals("JTTY") ? 0 : options.period;
        return run(codec, audio, options.mode, seconds, fileLabel(options.target), live, new ArrayList<>(), new ArrayList<>());
    }

    /** The render request for the mode. */
    static ObjectNode renderRequest(Options options) {
        ObjectNode request = JSON.createObjectNode().put("t", "render").put("id", 1);
        if (options.mode.equals("JTTY")) {  // JTTY's requests carry no mode
            return request.put("text", String.join(" ", options.texts)).put("freq", options.freq).put("rate", RATE);
        }
        request.put("mode", options.mode);
        switch (options.mode) {
            case "FOX", "SUPERFOX" -> options.texts.forEach(request.putArray("slots")::add);  // one message a slot
            case "TUNE" -> request.put("seconds", options.period.intValue());  // a steady tone for the period FreqCal measures it in
            case "ECHO" -> request.put("kind", "tone");  // WSJT-X's Fixed Tone
            default -> request.put("text", String.join(" ", options.texts));
        }
        if (SUBMODES.contains(options.mode)) {
            request.put("submode", options.submode);
        }
        if (PERIODS.contains(options.mode) || options.fast) {
            request.put("trperiod", options.period.intValue());
        }
        if (options.fast) {
            request.put("fast", true);
        }
        return request.put("freq", options.freq).put("rate", RATE);
    }

    static int render(Options options) throws InterruptedException {
        // --out must be writable before the codec starts. A file made to find that out is removed, and the audio is written once it is all in.
        File out = options.out == null ? null : new File(options.out);
        boolean existed = out != null && out.exists();
        if (out != null) {
            try {
                new FileOutputStream(out, true).close();  // appending keeps an existing file as it is
            } catch (IOException e) {
                ERR.println("codec_client: cannot write " + options.out + ": " + e.getMessage());
                return USAGE;
            }
            if (!existed) {
                out.delete();
            }
        }
        Codec codec = start(options, "encodes", DECODED_AS.getOrDefault(options.mode, options.mode));
        if (codec == null) {
            return FAILED;
        }
        codec.send(renderRequest(options));
        List<String> sent = new ArrayList<>();  // the messages as sent
        long samples = 0;
        ByteArrayOutputStream pcm = new ByteArrayOutputStream();
        while (true) {  // the reply: encoded (JTTY: a segment line a segment), the pcm lines, then rendered; or rejected
            JsonNode line = codec.next((long) silenceBound(0), TimeUnit.SECONDS);  // a reply waits for no decode
            if (line == null) {
                ERR.println("codec_client: no line from the codec in " + (long) silenceBound(0) + " s: it has hung");
                codec.kill();
                return HUNG;
            }
            String kind = line.path("t").asText();
            if (line == Codec.END || kind.equals("rejected") || kind.equals("error") || kind.equals("rendered") && sent.isEmpty()) {
                ERR.println("codec_client: render failed: " + (line == Codec.END ? "the output ended" : line));
                codec.quit();
                return FAILED;
            }
            if (kind.equals("encoded")) {
                samples = line.path("samples").asLong();
                sent.clear();
                if (options.mode.equals("FOX")) {
                    line.path("slots").forEach(slot -> sent.add(slot.path("msgsent").asText()));
                } else if (options.mode.equals("SUPERFOX")) {
                    options.texts.forEach(slot -> sent.add(slot.toUpperCase(Locale.ROOT)));  // SuperFox's encoded line has no msgsent: WSJT-X shows the slots as written
                } else {
                    sent.add(line.path("msgsent").asText());
                }
            } else if (kind.equals("segment")) {  // JTTY: each segment's text and samples
                sent.add(line.path("text").asText());
                samples += line.path("samples").asLong();
            } else if (kind.equals("pcm")) {
                pcm.writeBytes(Base64.getDecoder().decode(line.path("data").asText()));  // int16 little-endian samples at the requested rate
            } else if (kind.equals("rendered")) {
                break;
            }
        }
        byte[] audio = pcm.toByteArray();
        OUT.println("rendered " + samples + " " + String.join(" | ", sent));
        if (out != null) {
            try {
                writeWav(options.out, audio);
            } catch (IOException e) {
                ERR.println("codec_client: cannot write " + options.out + ": " + e.getMessage());
                if (!existed) {
                    out.delete();
                }
                codec.quit();
                return FAILED;
            }
        }
        // Decode it back, over noise: the audio where WSJT-X starts it in its period; JTTY's after a second, with a second after it.
        String mode = DECODED_AS.getOrDefault(options.mode, options.mode);
        double seconds;  // 0: JTTY's, no periods
        byte[] signal;
        if (options.mode.equals("JTTY")) {
            seconds = 0;
            signal = overNoise(delayed(audio, 1), (double) audio.length / 2 / RATE + 2);
        } else {
            seconds = options.period;
            signal = overNoise(delayed(audio, txStart(options.mode, seconds, options.fast)), seconds);
        }
        if (mode.equals("FST4")) {
            options.low = options.freq - 200;  // its F Low and F High around the render
            options.high = options.freq + 200;
        }
        ObjectNode configure = configure(options, mode, mode.equals("MSK144") ? null : options.freq);  // MSK144's Rx Freq is its tones' center
        if (options.mode.equals("SUPERFOX")) {
            configure.put("contest_type", "hound").put("superfox_mode", true).put("even_sequence", true);  // a SuperFox Hound decodes it
        }
        if (options.mode.equals("ECHO")) {
            configure.put("live_audio", true).put("echo_freq", options.freq);  // the echo's parameters from the keys, not from the audio's first samples as a saved file carries them
        }
        codec.send(configure);
        List<String> messages = new ArrayList<>();
        List<JsonNode> measured = new ArrayList<>();
        int status = run(codec, signal, mode, seconds, 0, false, messages, measured);
        if (status != OK) {
            return status;
        }
        List<String> missing = new ArrayList<>();
        if (options.mode.equals("TUNE")) {
            if (measured.stream().noneMatch(m -> m.path("t").asText().equals("freqcal") && m.path("error").isNumber() && Math.abs(m.path("error").doubleValue()) < 1)) {
                missing.add("a measurement within 1 Hz of " + options.freq + " Hz");
            }
        } else if (options.mode.equals("ECHO")) {
            if (measured.stream().noneMatch(m -> m.path("t").asText().equals("echo") && m.path("snr").isNumber() && m.path("snr").doubleValue() > 0)) {
                missing.add("an echo above the noise");
            }
        } else {
            for (String text : sent) {
                if (messages.stream().map(CodecClient::words).noneMatch(words(text)::equals)) {
                    missing.add(text);
                }
            }
        }
        if (!missing.isEmpty()) {
            ERR.println("codec_client: round trip failed: no decode of " + String.join(" | ", missing));
            return FAILED;
        }
        return OK;
    }

    static final String USAGE_TEXT = String.join(System.lineSeparator(),
            "usage: codec_client decode --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--low HZ] [--high HZ] [--mycall CALL] WAV",
            "       codec_client live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV",
            "       codec_client render --codec PATH [--data DIR] [--mode MODE] [--submode X] [--period S] [--fast] [--depth N] [--freq HZ] [--out WAV] [TEXT...]",
            "decode's MODE: " + String.join(", ", DECODES),
            "render's MODE: " + String.join(", ", ENCODES) + "; FOX and SUPERFOX take one TEXT a slot, TUNE and ECHO none, JTTY one or more, the others one");

    /** The command line; --name VALUE or --name=VALUE, in any order around the WAV file or TEXT. */
    static final class Options {
        String command;
        String codec;
        String data = ".";
        String mode = "FT8";
        int depth = 3;
        String submode = "A";  // JT4, JT9, JT65 and Q65: the submode
        Double period;         // the period, s: the mode's unless --period gives one; null in JTTY, which has none
        boolean fast;          // JT9: fast JT9
        Integer freq;          // decode: the receive frequency, or null for the mode's; render: the lowest tone
        Integer low;           // decode: the lowest frequency searched, or null for the mode's
        Integer high;          // decode: the highest frequency searched, or null for the mode's
        String mycall = "";    // decode: the station's call, for the decoders' a priori decoding
        String out;
        String target;         // the WAV file
        List<String> texts = new ArrayList<>();  // render's TEXTs

        /** The options, or null for --help; an IllegalArgumentException says what is not usable. */
        static Options parse(String[] args) {
            Options options = new Options();
            if (args.length > 0 && (args[0].equals("-h") || args[0].equals("--help"))) {
                return null;
            }
            if (args.length == 0 || !List.of("decode", "live", "render").contains(args[0])) {
                throw new IllegalArgumentException("the first argument is decode, live or render");
            }
            options.command = args[0];
            boolean render = options.command.equals("render"), decode = options.command.equals("decode");
            List<String> known = new ArrayList<>(List.of("--codec", "--data", "--mode", "--depth"));
            if (!options.command.equals("live")) {
                known.addAll(List.of("--submode", "--period", "--fast", "--freq"));
            }
            known.addAll(render ? List.of("--out") : decode ? List.of("--low", "--high", "--mycall") : List.of());
            List<String> positional = new ArrayList<>();
            for (int i = 1; i < args.length; i++) {
                String arg = args[i];
                if (arg.equals("-h") || arg.equals("--help")) {
                    return null;
                }
                if (arg.equals("--")) {
                    positional.addAll(Arrays.asList(args).subList(i + 1, args.length));
                    break;
                }
                if (!arg.startsWith("--")) {
                    positional.add(arg);
                    continue;
                }
                int equals = arg.indexOf('=');
                String name = equals < 0 ? arg : arg.substring(0, equals);
                if (!known.contains(name)) {
                    throw new IllegalArgumentException("unknown option " + name);
                }
                if (name.equals("--fast")) {  // a flag, which takes no value
                    if (equals >= 0) {
                        throw new IllegalArgumentException("--fast takes no value");
                    }
                    options.fast = true;
                    continue;
                }
                if (equals < 0 && i + 1 == args.length) {
                    throw new IllegalArgumentException(name + " needs a value");
                }
                String value = equals < 0 ? args[++i] : arg.substring(equals + 1);
                switch (name) {
                    case "--codec" -> options.codec = value;
                    case "--data" -> options.data = value;
                    case "--mode" -> options.mode = value;
                    case "--depth" -> options.depth = Integer.parseInt(value);
                    case "--submode" -> options.submode = value;
                    case "--period" -> options.period = Double.valueOf(value);
                    case "--freq" -> options.freq = Integer.valueOf(value);
                    case "--low" -> options.low = Integer.valueOf(value);
                    case "--high" -> options.high = Integer.valueOf(value);
                    case "--mycall" -> options.mycall = value;
                    default -> options.out = value;
                }
            }
            if (options.codec == null) {
                throw new IllegalArgumentException("--codec is required");
            }
            List<String> modes = render ? ENCODES : decode ? DECODES : LIVE;
            if (!modes.contains(options.mode)) {
                throw new IllegalArgumentException("--mode is one of " + String.join(", ", modes));
            }
            if (options.depth < 1 || options.depth > 3) {
                throw new IllegalArgumentException("--depth is 1, 2 or 3");
            }
            if (!options.submode.matches("[A-H]")) {
                throw new IllegalArgumentException("--submode is one of A to H");
            }
            if (options.period == null) {  // live takes none
                options.period = PERIOD.get(options.mode);
            } else if (!(Double.isFinite(options.period) && options.period * RATE >= 1)) {
                throw new IllegalArgumentException("--period must be a number of seconds that holds a sample at least");
            }
            if (!render) {
                if (positional.size() != 1) {
                    throw new IllegalArgumentException("give one WAV file");
                }
                options.target = positional.get(0);
                return options;
            }
            if (options.freq == null) {
                options.freq = options.mode.equals("MSK144") ? 1000 : options.mode.equals("SUPERFOX") ? 750 : 1500;  // MSK144's tones are at 1000 and 2000 Hz, and SuperFox goes at 750
            }
            int wants = List.of("TUNE", "ECHO").contains(options.mode) ? 0 : List.of("FOX", "SUPERFOX", "JTTY").contains(options.mode) ? -1 : 1;  // the TEXTs the mode takes; -1: one or more
            if (wants < 0 ? positional.isEmpty() : positional.size() != wants) {
                throw new IllegalArgumentException("render --mode " + options.mode + " takes " + (wants == 0 ? "no TEXT" : wants == 1 ? "one TEXT" : "one TEXT or more"));
            }
            options.texts = positional;
            return options;
        }
    }

    public static void main(String[] args) throws InterruptedException {
        Options options;
        try {
            options = Options.parse(args);
        } catch (IllegalArgumentException e) {  // a NumberFormatException too
            ERR.println(USAGE_TEXT);
            ERR.println("codec_client: " + e.getMessage());
            System.exit(USAGE);
            return;
        }
        if (options == null) {
            OUT.println(USAGE_TEXT);
            System.exit(OK);
            return;
        }
        System.exit(options.command.equals("render") ? render(options) : decode(options));
    }
}
