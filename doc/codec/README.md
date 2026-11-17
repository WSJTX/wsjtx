# wsjt-codec

wsjt-codec is the WSJT-X decoder and encoder as a program of its own. A client starts it as a child process, writes 12 kHz audio and JSON control frames to its standard input, and reads its decodes and encodings as JSON lines from its standard output. It decodes and encodes the modes its `ready` line lists, WSPR among them.

These documents describe the codec built from the same source. `wsjt-codec -v` prints its release.

| Document | Contents |
| --- | --- |
| [interface.md](interface.md) | The command line, the environment and files, the session header and frames, receiving decodes, spectrum rows and equalization, encoding with `pack` and `render`, JTTY, WSPR, and every name: control messages, configure and request keys, output lines and their fields, error codes and rejected reasons. |
| [integration.md](integration.md) | Building the codec into a product: a ten-minute evaluation, pinning, bundling or fetching a release, signing and minimum system, Windows, pipes and exit status, directories, memory and threads, the clock, changes and issues, and the license and names. |
| [Example clients](examples/README.md) | One command line (`decode`, `live`, `render`) in each language, and the check that keeps every example to it. |

Report issues in the WSJT-X [issue tracker](https://github.com/WSJTX/wsjtx/issues), with the output of `wsjt-codec -v`.
