# Integrating wsjt-codec

This guide is for building wsjt-codec into a product: evaluating it, shipping it and running it. [interface.md](interface.md) specifies what a client sends and reads, and the [example clients](examples/README.md) implement it. It covers Linux, macOS and Windows; mobile apps are not covered, as the codec runs as a child process of its client.

## Evaluate in ten minutes

A release on the [releases page](https://github.com/WSJTX/wsjtx/releases) carries a codec tarball for each platform, the source and `SHA256SUMS`. Set `VERSION` to the release whose `doc/codec/` you are reading, and `TARGET` to your platform as the file names spell it, such as `linux-x86_64`, `linux-aarch64`, `arm64-macOS` or `windows-x86_64`. The commands are for a POSIX shell; where `shasum` is missing, `sha256sum -c --ignore-missing SHA256SUMS` does the same. Download the codec, the source and the checksums, check them, and unpack:

```sh
VERSION=<release>
TARGET=linux-aarch64
URL=https://github.com/WSJTX/wsjtx/releases/download/v$VERSION
curl -fL --remote-name-all $URL/SHA256SUMS $URL/wsjtx-$VERSION-$TARGET-codec.tar.gz $URL/wsjtx-$VERSION-src.tar.gz
shasum -a 256 -c --ignore-missing SHA256SUMS
tar -xzf wsjtx-$VERSION-$TARGET-codec.tar.gz
tar -xzf wsjtx-$VERSION-src.tar.gz
```

`shasum` prints `OK` for each tarball. Then download a recording from the samples WSJT-X offers for download, listed in [contents_3.3.json](https://wsjtx.github.io/wsjtx/samples/contents_3.3.json). This one is a 15 s FT8 period:

```sh
curl -fLO https://wsjtx.github.io/wsjtx/samples/FT8/210703_133430.wav
```

Decode it with the [Python example](examples/README.md#running-an-example), which needs Python 3.8 or later. `CODEC` is the program: in the tarball's `bin/` on Linux, and at the top of the tarball's directory on macOS and Windows:

```sh
CODEC=wsjtx-$VERSION-$TARGET-codec/bin/wsjt-codec   # macOS and Windows: wsjtx-$VERSION-$TARGET-codec/wsjt-codec
python3 wsjtx-$VERSION/doc/codec/examples/python/codec_client.py decode --codec $CODEC --data codec-data 210703_133430.wav > decodes.txt
```

`decodes.txt` holds a `decode` line for each message and `finished 133430` ([Output](examples/README.md#output)). The source's `test/fixtures/ft8_210703_133430_mtd.expected.txt` lists the 20 messages the project's tests expect from this recording. Count those decoded:

```sh
sed -n 's/^.*~  //p' wsjtx-$VERSION/test/fixtures/ft8_210703_133430_mtd.expected.txt > expected.txt
grep -c -F -f expected.txt decodes.txt
```

It prints `20`.

## Pin, roll back, bundle or fetch

- A release is named by its tag, `v` and the version. Its `SHA256SUMS` and `release-manifest.json` give each tarball's sha256; the manifest adds its size, the tag and the commit.
- `wsjt-codec -v` prints the release and the commit's first six hex digits, for example `wsjt-codec 3.3.0-beta5 (revision d89fe3)`. The release identifies the interface, and `doc/codec/` at its tag describes it ([Versions and unknown names](interface.md#versions-and-unknown-names)).
- To pin a release, record its tag and the tarball's sha256, and check `-v` when the codec starts.
- To roll back, ship the earlier release's tarball and drive it as its own `doc/codec/` describes.
- To bundle the codec, ship the tarball's directory as it unpacks; its `README.txt` gives the layout to keep.
- To fetch the codec at install or first run, download the tarball, check its sha256 and size against `release-manifest.json`, unpack it, and run `-v`.
- The codec decodes WSPR itself (`"mode":"WSPR"`). `wsprd`, which also decodes WSPR from WAV and `.c2` files, still ships in its own tarball, `wsjtx-$VERSION-$TARGET-wsprd.tar.gz`: pin, bundle or fetch it the same way if a client still runs it.

## Signing and minimum system

Each tarball's `README.txt` gives, for its release and platform, the system the programs need and, on macOS and Windows, how they are signed. Read it for each release you ship. On macOS, a tarball downloaded with a browser is quarantined, and its `README.txt` says how to clear it; `curl` sets no quarantine.

## Platforms

### Windows

- `wsjt-codec.exe` is a console program. Windows creates a console window for a console program started from one that has no console, such as a GUI application, unless the parent passes `CREATE_NO_WINDOW` (0x08000000) to `CreateProcess`. Node's `windowsHide: true` passes it when none of the child's standard streams is inherited, and Java's `ProcessBuilder` passes it when the child's standard streams are pipes. Rust's `std::process::Command` does not: call `creation_flags(0x0800_0000)` from `std::os::windows::process::CommandExt`. In Python, pass `creationflags=subprocess.CREATE_NO_WINDOW` to `Popen`.
- Standard output's lines end in CR LF. Split them at the LF and drop the CR.

### Pipes and exit status

- Read standard output and standard error as they come, each on its own: a full pipe stops the codec ([What every client does](examples/README.md#what-every-client-does)). Once `spectrum`, `ref_spectrum` or `prepared_audio` has been on, standard output's lines queue instead: the codec stops only once 16 MiB of lines other than live rows wait, and on live audio at most 20 rows wait, the oldest discarded (`rows_dropped`).
- To end a session, send `halt` or close standard input; the codec drains the period in flight and exits.
- The codec's standard output ends when it exits. Bound the wait for its exit status from there, and stop a codec that has not exited: the examples allow 10 s. A watchdog that reads the lines stops when they end, so the exit needs a deadline of its own.
- Treat a broken pipe as the codec having exited: stop writing, read its remaining lines and take its exit status. On Linux and macOS, a C or C++ client ignores `SIGPIPE`, or the failed write ends the client. In Node.js, attach an `'error'` listener to the child's stdin, or the failed write ends the client.
- [Exit status](interface.md#exit-status) gives the codec's statuses. The end of standard output before `ready` is a failed start, whatever the status.

## Directories

- Give each running codec its own `-a`, `-t` and working directory. Keep `-a` across runs: the codec reads its FFTW wisdom from there at the start and saves it there, and MSK144's training, the reference spectrum, `refspec.dat`, and WSPR's hash table, `hashtable.txt`, which resolves type 3 calls from session to session, go there too. The decoders' temporary files go to `-t`; JT4 can write `fort.77` to its working directory, and a file named `rx_messages.txt` there is a test hook ([Directories and files](interface.md#directories-and-files)).
- The codec only reads `-r`, so codecs can share it. The codec tarball carries `ALLCALL7.TXT`, which the multithreaded FT8 decoder reads from `-r`: point `-r` at the tarball's directory, or copy the file to yours.
- `CALL3.TXT`, which JT4 and JT65 read from `-a`, is the client's to supply. The source has one at its top level.

## Memory and threads

- Each codec process holds its own buffers; [Memory](interface.md#memory) lists them, with measured peaks.
- Set `OMP_STACKSIZE=10M` in the codec's environment ([Environment](interface.md#environment)).
- A codec reads its standard input on one thread, runs the session on another and decodes on a third. Once a configure has turned `spectrum`, `ref_spectrum` or `prepared_audio` on, another thread writes its standard output for the rest of the session. Some decoders add OpenMP threads, each with the stack `OMP_STACKSIZE` gives it. Large FFTs use `-m` threads, 1 by default.
- The multithreaded FT8 decoder (`multithreaded_ft8`) runs `ft8_threads` threads: 0 chooses from the CPU count, at most 12; 1 to 12 runs up to that many, at most the CPU count. A configure takes 0 to 24, and 13 to 24 run one thread. Each codec chooses on its own, so with several codecs on one host set `ft8_threads` for each.

## The clock

- From a file (`live_audio` false) the codec does not read the clock.
- On live audio, FT8's early passes stop by the host's clock, as WSJT-X's do: the pass at half-symbol 41 at 13.4 s into the 15 s period, and the subtraction step of the pass at half-symbol 47 at 14.3 s. Keep the host's clock accurate, for example with NTP, and start and label each period on its UTC boundary ([Live audio and files](interface.md#live-audio-and-files)).

## Changes and issues

- Each release's page lists the changes since the release before it, and marks "(breaking)" a change that breaks existing clients. A change to the interface updates `doc/codec/` with it.
- Report issues in the WSJT-X [issue tracker](https://github.com/WSJTX/wsjtx/issues), with the output of `wsjt-codec -v`.

## License and names

- WSJT-X, wsjt-codec included, is free software under the GNU General Public License version 3 ([COPYING](../../COPYING)). Each tarball carries `COPYING`, and its `README.txt` names the release's source. Its `THIRD-PARTY.txt` lists the bundled libraries, such as FFTW, and their licenses.
- GPLv3 section 5, "Conveying Modified Source Versions", ends with a paragraph on aggregates, and section 6, "Conveying Non-Source Forms", covers conveying object code, including in User Products. How they apply to a particular product is a question for your counsel.
- [license.adoc](../common/license.adoc) says: "Others who make fair use of our work under terms of the GNU General Public License must display the following copyright notice prominently". Each tarball's `README.txt` ends with that notice.
- The example clients carry `SPDX-License-Identifier: GPL-3.0-or-later`.
- WSJT and WSJT-X are trademarks of the WSJT Development Team. [TRADEMARK.md](../../TRADEMARK.md) says when a build may carry the names.
