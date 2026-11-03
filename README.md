# Weak Signal Communication Software

## Programs
*WSJT-X*, *MAP65*, and *QMAP* are open-source, multi-platform programs designed for weak-signal digital communication by amateur radio. *WSJT-X* works with a standard SSB transceiver, while *MAP65* and *QMAP* use wideband SDR-style hardware. The programs are open source, free of charge, and licensed under the GNU General Public License.

*WSJT-X* implements communication protocols or "modes" called **FST4**, **FST4W**, **FT4**, **FT8**, **JT4**, **JT9**, **JT65**, **MSK144**, **Q65**, **WSPR**, and **Echo**. The first nine modes were designed for making reliable, confirmed QSOs in a wide variety of weak-signal propagation circumstances. These modes use timed Transmit/Receive sequences of specific lengths, synchronized with UTC. **WSPR** mode is for probing potential propagation paths with low-power transmissions, and **Echo** mode is for detecting and measuring reflections of your own signals from the Moon.

*MAP65* implements a wideband receiver for **JT65** and **Q65** signals, optimized for EME on the VHF/UHF bands. It can be used together with *Linrad* (by SM5BSZ) or with direct input from a soundcard, FUNcube Dongle, or similar hardware. The program decodes all **JT65** or **Q65** signals in a passband up to 90 kHz wide, producing a sorted band map of decoded callsigns. In a dual-polarization system, *MAP65* optimally matches the linear polarization angle of each signal, thereby eliminating problems with Faraday rotation and spatial polarization offsets. *MAP65* also handles T/R switching and generates suitable messages and audio waveforms for the selected mode.

*QMAP* is similar to *MAP65* in providing wideband reception of signals over a full EME sub-band. It works cooperatively with *WSJT-X*, supporting the **Q65** mode in both 30-second and 60-second submodes. The *QMAP* + *WSJT-X* combination provides full rig control and Doppler compensation for the EME (Earth-Moon-Earth) path.


## Mode Details
**JT4**, **JT9**, and **JT65** use nearly identical message structure, efficient compression of messages for minimal QSOs, and 60-second T/R sequences. **JT4** and **JT65** were designed for EME on the VHF, UHF, and microwave bands, while **JT9** is optimized for the MF and HF bands. **JT9** is about 2 dB more sensitive than **JT65** while using less than 10% of the bandwidth.

**FT4** and **FT8** use T/R cycles of only 7.5 and 15 s, respectively. They have become extremely popular for world-wide DXing on the HF bands. **MSK144** is designed for Meteor Scatter on the VHF bands. **Q65** offers submodes with T/R sequence lengths from 15 seconds to 5 minutes, and a wide range of tone spacings. Particular Q65 submodes are highly recommended for EME, ionospheric scatter, and other weak signal work on VHF, UHF, and microwave bands. These modes include message formats explicitly supporting nonstandard callsigns and some popular radio contests.

**FST4** and **FST4W** are designed particularly for the LF and MF bands. On these bands their fundamental sensitivities are better than other *WSJT-X* modes with the same sequence lengths, approaching the theoretical limits for their rates of information throughput. **FST4** is optimized for two-way QSOs, while **FST4W** is for quasi-beacon transmissions of **WSPR**-style messages. **FST4** and **FST4W** do not require the strict, independent time synchronization and phase locking of modes like EbNaut.


## License
Licensed under the GNU General Public License v3.0 — see [COPYING](COPYING).

## Releases on this fork

The existing `Release` workflow publishes only to the repository running it
(`cpicoto/wsjtx` on this fork), using the automatic `GITHUB_TOKEN`. It does not
push to upstream or require `CROSS_REPO_TOKEN`.

Enable GitHub Actions on the fork, commit and push the workflow changes, then
push a `build/vX.Y.Z` tag whose numeric version matches `CMakeLists.txt`.
For the current `3.0.2.0` project version:

```sh
git tag build/v3.0.2
git push origin build/v3.0.2
```

Use `build/v3.0.2-rc1` for a prerelease. The workflow reuses the existing
Linux x86_64/aarch64, Windows x86_64, and macOS Apple Silicon/Intel builds,
and publishes only after all five platform builds and installer checks succeed.
Assets include Linux AppImage/DEB/RPM packages, Windows EXE installers, and
macOS PKG installers. All releases include a source archive.
macOS signing and notarization remain optional and use the existing signing
secrets when configured; Windows installers use the existing self-signed
certificate, which is not publicly trusted.

Integration release candidates may include unmerged PRs on a separate branch;
the original PRs remain open. Mobile additions are included as source only:
the existing release workflow does not build or validate iOS or Android apps.
If an asset upload fails, the release remains a draft; remove the incomplete
draft before rerunning the release job.

## Trademark
"WSJT-X," "MAP65," and "QMAP" are trademarks of the WSJT Development Team — see [TRADEMARK.md](TRADEMARK.md) for our policy on use of these names.