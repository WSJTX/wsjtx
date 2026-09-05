![WSJT-X](wsjtx_icon.webp)

# Weak Signal Communication Software

## Programs

*WSJT-X*, *MAP65*, and *QMAP* are open-source, multi-platform programs designed for weak-signal digital communication by amateur radio. *WSJT-X* works with a standard SSB transceiver, while *MAP65* and *QMAP* use wideband SDR-style hardware. The programs are open source, free of charge, and licensed under the GNU General Public License. These program names are trademarks of the WSJT Development Team; see [TRADEMARK.md](TRADEMARK.md) for our policy on their use.

They can be built and used on Windows, macOS, and Linux.

*WSJT-X* implements communication protocols or "modes" called **FST4**, **FST4W**, **FT4**, **FT8**, **JT4**, **JT9**, **JT65**, **JTTY**, **MSK144**, **Q65**, **WSPR**, and **Echo**. Its two-way modes are designed for making reliable, confirmed QSOs in a wide variety of weak-signal propagation circumstances. Most use timed Transmit/Receive sequences of specific lengths, synchronized with UTC. **JTTY** supports fast RTTY-like contest exchanges and other keyboard-to-keyboard communication. **WSPR** and **FST4W** are for probing potential propagation paths with low-power transmissions, and **Echo** is for detecting and measuring reflections of your own signals from the Moon.

*MAP65* implements a wideband receiver for **JT65** and **Q65** signals, optimized for EME on the VHF/UHF bands. It can be used together with *Linrad* (by SM5BSZ) or with direct input from a soundcard, FUNcube Dongle, or similar hardware. The program decodes all **JT65** or **Q65** signals in a passband up to 90 kHz wide, producing a sorted band map of decoded callsigns. In a dual-polarization system, *MAP65* optimally matches the linear polarization angle of each signal, thereby eliminating problems with Faraday rotation and spatial polarization offsets. *MAP65* also handles T/R switching and generates suitable messages and audio waveforms for the selected mode.

*QMAP* is similar to *MAP65* in providing wideband reception of signals over a full EME sub-band. It works cooperatively with *WSJT-X*, supporting the **Q65** mode in both 30-second and 60-second submodes. The *QMAP* + *WSJT-X* combination provides full rig control and Doppler compensation for the EME (Earth-Moon-Earth) path.

## Mode Details

**JT4**, **JT9**, and **JT65** use nearly identical message structure, efficient compression of messages for minimal QSOs, and 60-second T/R sequences. **JT4** and **JT65** were designed for EME on the VHF, UHF, and microwave bands, while **JT9** is optimized for the MF and HF bands. **JT9** is about 1 dB more sensitive than **JT65** while using less than 10% of the bandwidth.

**FT4** and **FT8** use T/R cycles of only 7.5 and 15 s, respectively. They have become extremely popular for world-wide DXing on the HF bands. **MSK144** is designed for Meteor Scatter on the VHF bands. **Q65** offers submodes with T/R sequence lengths from 15 seconds to 5 minutes, and a wide range of tone spacings. Particular Q65 submodes are highly recommended for EME, ionospheric scatter, and other weak signal work on VHF, UHF, and microwave bands. These modes include message formats explicitly supporting nonstandard callsigns and some popular radio contests.

**FST4** and **FST4W** are designed particularly for the LF and MF bands. On these bands their fundamental sensitivities are better than other *WSJT-X* modes with the same sequence lengths, approaching the theoretical limits for their rates of information throughput. **FST4** is optimized for two-way QSOs, while **FST4W** is for quasi-beacon transmissions of **WSPR**-style messages. **FST4** and **FST4W** do not require the strict, independent time synchronization and phase locking of modes like EbNaut.

The [WSJT-X User Guide](https://wsjtx.github.io/wsjtx/guide-full.html) documents operating procedures, mode details, configuration, and platform installation.

## Building from source

The [source-build guide](doc/user_guide/en/install-from-source.adoc) is the
maintained reference for developer builds, including the supported Windows
MSYS2 environment, required dependencies, tests, installation, and common
failures. Contributors should also read [CONTRIBUTING.md](CONTRIBUTING.md) for
the public and private repository workflow.

## Support and contributing

The [project website](https://wsjtx.github.io/wsjtx/) provides release downloads
and documentation. The User Guide is the first place to look for operating and
setup help. Questions and general discussion can go to
the [WSJT-X user group](https://wsjtx.groups.io/g/main) or the
[wsjt-devel mailing list](https://sourceforge.net/p/wsjt/mailman/wsjt-devel/).
[GitHub Discussions](https://github.com/WSJTX/wsjtx/discussions) is also
available.

Source code, bug reports, and pull requests are hosted in the public
[WSJTX/wsjtx repository](https://github.com/WSJTX/wsjtx). See the
[contribution guide](https://github.com/WSJTX/wsjtx/blob/master/CONTRIBUTING.md)
for repository and development guidance.

## License

WSJT-X, MAP65, and QMAP are free software licensed under the GNU General Public
License version 3. See [COPYING](COPYING) for the full license.

## Code Signing Policy

Windows installers for GA releases are Authenticode-signed. Free code signing is provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/). Signed installers are built from the source in this repository by GitHub Actions and signed only through that verified pipeline.
