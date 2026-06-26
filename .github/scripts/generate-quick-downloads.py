#!/usr/bin/env python3
"""Generate the "Quick Downloads" markdown block for a WSJT-X GitHub Release.

The release workflow attaches every build artifact to the Release, which makes
the assets list long and hard for an end user to navigate (installers sit among
standalone command-line utilities, raw binaries, and source). This emits a
short, curated per-platform installer picker to prepend to the auto-generated
release notes.

It is *asset-driven*: platforms and architectures are derived from the asset
filenames passed in, not hard-coded. New build variants (for example an added
Linux architecture such as armhf) therefore appear in Quick Downloads
automatically, with no edit to this script.

The block is emitted on stdout, ending at the last platform line (no trailing
separator); the caller joins it to the auto-generated notes.

Usage:
    generate-quick-downloads.py <owner/repo> <tag> <asset-path-or-name>...

Only installer-grade assets are listed (.pkg / win64.exe / .deb / .rpm /
.AppImage) plus the source tarball. Per-mode utility tarballs and raw
command-line binaries are intentionally omitted; they remain in the full
assets list below the notes.
"""
import os
import re
import sys

# Architectures listed first, in this order; any others are appended sorted.
ARCH_ORDER = ["x86_64", "aarch64", "armhf", "armv7l", "arm64", "i686"]


def main(argv):
    if len(argv) < 4:
        sys.stderr.write(
            "usage: generate-quick-downloads.py <owner/repo> <tag> <asset>...\n"
        )
        return 2

    repo, tag = argv[1], argv[2]
    names = sorted({os.path.basename(p) for p in argv[3:] if p.strip()})

    def url(name):
        return f"https://github.com/{repo}/releases/download/{tag}/{name}"

    macos = {}      # label -> filename
    windows = {}    # label -> filename
    deb = {}        # arch  -> filename
    rpm = {}        # arch  -> filename
    appimage = {}   # arch  -> filename
    source = None

    for n in names:
        if n.endswith("-arm64-macOS.pkg"):
            macos["Apple Silicon"] = n
        elif n.endswith("-x86_64-macOS.pkg"):
            macos["Intel"] = n
        elif re.search(r"-win\d+\.exe$", n):           # installer, not raw jt9.exe
            windows["64-bit"] = n
        elif n.endswith("-src.tar.gz"):
            source = n
        elif (m := re.search(r"-linux-([A-Za-z0-9_]+)\.deb$", n)):
            deb[m.group(1)] = n
        elif (m := re.search(r"-linux-([A-Za-z0-9_]+)\.AppImage$", n)):
            appimage[m.group(1)] = n
        elif (m := re.search(r"\.([A-Za-z0-9_]+)\.rpm$", n)):
            rpm[m.group(1)] = n
        # Anything else (utility tarballs, raw binaries) is intentionally
        # excluded; it stays in the full assets list.

    def arches(d):
        known = [a for a in ARCH_ORDER if a in d]
        extra = sorted(a for a in d if a not in ARCH_ORDER)
        return known + extra

    def arch_line(label, d):
        return f"- {label}: " + " · ".join(
            f"[{a}]({url(d[a])})" for a in arches(d)
        )

    out = [
        "## Quick Downloads",
        "",
        "Choose the installer for your platform. The full assets list below also",
        "includes standalone command-line utilities and source code for developers.",
    ]

    if macos:
        out += ["", "### macOS"]
        if "Apple Silicon" in macos:
            out.append(f"- Apple Silicon: [{macos['Apple Silicon']}]({url(macos['Apple Silicon'])})")
        if "Intel" in macos:
            out.append(f"- Intel: [{macos['Intel']}]({url(macos['Intel'])})")

    if windows:
        out += ["", "### Windows"]
        out.append(f"- 64-bit: [{windows['64-bit']}]({url(windows['64-bit'])})")

    if deb or rpm or appimage:
        out += ["", "### Linux"]
        if deb:
            out.append(arch_line("Debian / Ubuntu (.deb)", deb))
        if rpm:
            out.append(arch_line("Fedora / RHEL (.rpm)", rpm))
        if appimage:
            out.append(arch_line("AppImage (universal)", appimage))

    if source:
        out += ["", "### Source", f"- [{source}]({url(source)})"]

    # If nothing matched (no installer-grade assets), emit nothing so the
    # caller leaves the auto-generated notes untouched.
    if len(out) == 4:
        return 0

    sys.stdout.write("\n".join(out) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
