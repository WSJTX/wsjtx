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
 *   java -jar codec-client.jar decode --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
 *   java -jar codec-client.jar live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV
 *   java -jar codec-client.jar render --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] [--freq HZ] [--out WAV] TEXT
 * </pre>
 *
 * doc/codec/examples/README.md states what each prints and its exit status. Java 17 or later; JSON through Jackson.
 */
public final class CodecClient {
    static final int RATE = 12000;                                                      // the stream's sample rate, Hz
    static final Map<String, Double> PERIOD = Map.of("FT8", 15.0, "FT4", 7.5);          // period, s
    static final Map<String, Double> TX_START = Map.of("FT8", 0.5, "FT4", 0.3);         // where WSJT-X starts its transmission in the period, s
    static final Map<String, Double> LIVE_ALLOWANCE = Map.of("FT8", 10.0, "FT4", 6.0);  // a live pass's time between progress lines, once one has completed
    static final int READY_SECONDS = 30;                                                // how long the client waits for ready
    static final int EXIT_SECONDS = 10;                                                 // how long the codec has to exit once its output ends, or after halt on a failure
    static final int CHUNK = 1200;                                                      // samples in an audio frame: 0.1 s

    static final byte AUDIO = 0x01, CONTROL = 0x02;  // frame types
    // Session header: magic, sample format 0 (int16), 1 channel, rate in kHz (uint16 little-endian).
    static final byte[] HEADER = {'W', 'S', 'J', 'T', 0, 1, RATE / 1000, 0};

    static final int OK = 0, FAILED = 1, USAGE = 2, HUNG = 3;  // exit status

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

    /** DT as WSJT-X shows it: one decimal, rounded half to even on the value as received. */
    static String oneDecimal(JsonNode dt) {
        if (!dt.isNumber()) {
            return "nan";  // the codec writes null for a value that is not finite
        }
        double value = dt.doubleValue();
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
                // Jackson writes an int as a plain JSON integer: a configure ignores or misreads 3.0, without an error.
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

        /** The codec's exit status once it exits, within EXIT_SECONDS; null, with the codec stopped, when it does not. */
        Integer exitStatus() throws InterruptedException {
            if (process.waitFor(EXIT_SECONDS, TimeUnit.SECONDS)) {
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
     * Send audio a period at a time, each period's label in a configure before its first sample, then halt.
     *
     * <p>Live, start on the next UTC period boundary, label each period with its UTC start and send each frame when its last sample is due, as a receiver's audio arrives.
     */
    static void sendAudio(Codec codec, byte[] audio, String mode, double label, boolean live, Watchdog watchdog) throws InterruptedException {
        double seconds = PERIOD.get(mode);
        int period = (int) (seconds * RATE);
        int samples = audio.length / 2;
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
            String periodLabel = utc(label + k * seconds);
            int end = Math.min(first + period, samples);
            if (watchdog != null && end - first == period) {  // due from its scheduled end, not from its last write, which a codec that stops reading blocks
                watchdog.periodStarts(periodLabel.replace(":", ""), start + (double) end / RATE);
            }
            codec.send(JSON.createObjectNode().put("t", "configure").put("utc", periodLabel));
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

    /** Stream the audio, print the decodes, add the messages decoded to messages, and return the exit status. */
    static int run(Codec codec, byte[] audio, String mode, double label, boolean live, List<String> messages) throws InterruptedException {
        Watchdog watchdog = live ? new Watchdog(mode) : null;
        Thread writer = daemon(() -> {
            try {
                sendAudio(codec, audio, mode, label, live, watchdog);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        });
        int errors = 0;
        while (true) {
            JsonNode line = codec.lines.poll(500, TimeUnit.MILLISECONDS);
            if (line == Codec.END) {
                break;
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
        writer.join();
        if (status != 0) {
            ERR.println("codec_client: wsjt-codec exited with status " + status);
            return FAILED;
        }
        return errors > 0 ? FAILED : OK;
    }

    /** Start the codec and wait for its ready line, which lists the modes it decodes and encodes; null when it does not come. */
    static Codec start(Options options, String kinds) throws InterruptedException {
        Codec codec;
        try {
            codec = new Codec(options.codec, options.data);
        } catch (IOException | InvalidPathException e) {
            ERR.println("codec_client: cannot start " + options.codec + ": " + e.getMessage());
            return null;
        }
        JsonNode line = codec.lines.poll(READY_SECONDS, TimeUnit.SECONDS);
        boolean named = false;
        for (JsonNode mode : line == null ? List.<JsonNode>of() : line.path(kinds)) {
            named |= mode.asText().equals(options.mode);
        }
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

    /** WSJT-X names a saved period YYMMDD_HHMMSS.wav: its start, in seconds of the day, or 0. */
    static int fileLabel(String path) {
        Matcher match = Pattern.compile("_(\\d\\d)(\\d\\d)(\\d\\d)\\.wav$", Pattern.CASE_INSENSITIVE).matcher(path);
        if (!match.find()) {
            return 0;
        }
        int h = Integer.parseInt(match.group(1)), m = Integer.parseInt(match.group(2)), s = Integer.parseInt(match.group(3));
        return h < 24 && m < 60 && s < 60 ? h * 3600 + m * 60 + s : 0;
    }

    /** The configure that sets the mode. These examples have no station, so no calls (the decoders use the starting calls otherwise), and a configure that carries the mode resets nfa, nfb and ntol. */
    static ObjectNode modeConfigure(Options options) {
        return JSON.createObjectNode().put("t", "configure").put("mode", options.mode).put("depth", options.depth)
                .put("mycall", "").put("his_call", "").put("his_grid", "").put("nfa", 200).put("nfb", 4000).put("ntol", 1000);
    }

    static int decode(Options options) throws InterruptedException {
        byte[] audio;
        try {
            audio = readWav(options.target);
        } catch (IOException | UnsupportedAudioFileException e) {
            ERR.println("codec_client: " + e.getMessage());
            return USAGE;
        }
        Codec codec = start(options, "modes");
        if (codec == null) {
            return FAILED;
        }
        boolean live = options.command.equals("live");
        ObjectNode configure = modeConfigure(options);
        if (live) {
            configure.put("live_audio", true).put("busy_events", true);
        }
        codec.send(configure);
        return run(codec, audio, options.mode, fileLabel(options.target), live, new ArrayList<>());
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
        Codec codec = start(options, "encodes");
        if (codec == null) {
            return FAILED;
        }
        codec.send(JSON.createObjectNode().put("t", "render").put("id", 1).put("mode", options.mode).put("text", options.target).put("freq", options.freq).put("rate", RATE));
        JsonNode encoded = null;
        ByteArrayOutputStream pcm = new ByteArrayOutputStream();
        while (true) {  // the reply: encoded, its pcm lines, then rendered; or rejected
            JsonNode line = codec.lines.take();
            String kind = line.path("t").asText();
            if (line == Codec.END || kind.equals("rejected") || kind.equals("error") || kind.equals("rendered") && encoded == null) {
                ERR.println("codec_client: render failed: " + (line == Codec.END ? "the output ended" : line));
                codec.quit();
                return FAILED;
            }
            if (kind.equals("encoded")) {
                encoded = line;
            } else if (kind.equals("pcm")) {
                pcm.writeBytes(Base64.getDecoder().decode(line.path("data").asText()));  // int16 little-endian samples at the requested rate
            } else if (kind.equals("rendered")) {
                break;
            }
        }
        byte[] audio = pcm.toByteArray();
        String sent = encoded.path("msgsent").asText();
        OUT.println("rendered " + encoded.path("samples").asLong() + " " + sent);
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
        // Decode it back: the audio where WSJT-X starts it, in a period of silence.
        byte[] signal = new byte[2 * (int) (PERIOD.get(options.mode) * RATE)];
        int offset = 2 * (int) (TX_START.get(options.mode) * RATE);
        System.arraycopy(audio, 0, signal, offset, Math.min(audio.length, signal.length - offset));
        codec.send(modeConfigure(options));
        List<String> messages = new ArrayList<>();
        int status = run(codec, signal, options.mode, 0, false, messages);
        if (status == OK && messages.stream().map(CodecClient::words).noneMatch(words(sent)::equals)) {
            ERR.println("codec_client: round trip failed: no decode of " + sent);
            status = FAILED;
        }
        return status;
    }

    static final String USAGE_TEXT = String.join(System.lineSeparator(),
            "usage: codec_client decode --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV",
            "       codec_client live   --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] WAV",
            "       codec_client render --codec PATH [--data DIR] [--mode FT8|FT4] [--depth N] [--freq HZ] [--out WAV] TEXT");

    /** The command line; --name VALUE or --name=VALUE, in any order around the WAV file or TEXT. */
    static final class Options {
        String command;
        String codec;
        String data = ".";
        String mode = "FT8";
        int depth = 3;
        int freq = 1500;
        String out;
        String target;  // the WAV file, or render's TEXT

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
            boolean render = options.command.equals("render");
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
                if (!List.of("--codec", "--data", "--mode", "--depth").contains(name) && !(render && List.of("--freq", "--out").contains(name))) {
                    throw new IllegalArgumentException("unknown option " + name);
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
                    case "--freq" -> options.freq = Integer.parseInt(value);
                    default -> options.out = value;
                }
            }
            if (options.codec == null) {
                throw new IllegalArgumentException("--codec is required");
            }
            if (!PERIOD.containsKey(options.mode)) {
                throw new IllegalArgumentException("--mode is FT8 or FT4");
            }
            if (options.depth < 1 || options.depth > 3) {
                throw new IllegalArgumentException("--depth is 1, 2 or 3");
            }
            if (positional.size() != 1) {
                throw new IllegalArgumentException(render ? "give one TEXT" : "give one WAV file");
            }
            options.target = positional.get(0);
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
