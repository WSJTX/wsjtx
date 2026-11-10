#!/usr/bin/env python3
"""
release_download_stats.py -- Download statistics for GitHub releases of WSJT-X.

Uses the same GitHub API data as tooomm.github.io/github-release-stats, but
classifies each asset by package type and platform and summarizes per release.

Examples (from the repository root):
  python3 ci-cd/release_download_stats.py                   # per-release summary
  python3 ci-cd/release_download_stats.py --detail v3.0.2   # every asset of one release
  python3 ci-cd/release_download_stats.py --csv stats.csv   # also write per-asset CSV
  python3 ci-cd/release_download_stats.py --save snap.json  # save counts for --diff
  python3 ci-cd/release_download_stats.py --diff snap.json  # downloads since snapshot

A GitHub token is optional; GITHUB_TOKEN, GH_TOKEN or `gh auth token` is used
if available, otherwise the unauthenticated API limit (60 requests/hour) applies.

GitHub reports only cumulative counts per asset, so downloads over a time
interval need two snapshots (--save now, --diff later).
"""

import argparse
import csv
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request
from collections import defaultdict
from datetime import datetime, timezone

API = "https://api.github.com/repos/{repo}/releases?per_page=100&page={page}"
PLATFORMS = ["Windows", "Win32", "macOS arm64", "macOS x86_64",
             "Linux x86_64", "Linux aarch64", "Linux armhf"]


def github_token():
    tok = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if tok:
        return tok
    if shutil.which("gh"):
        try:
            out = subprocess.run(["gh", "auth", "token"], capture_output=True,
                                 text=True, timeout=10)
            if out.returncode == 0:
                return out.stdout.strip()
        except (OSError, subprocess.SubprocessError):
            pass
    return None


def fetch_releases(repo):
    headers = {"Accept": "application/vnd.github+json",
               "User-Agent": "wsjtx-release-stats"}
    tok = github_token()
    if tok:
        headers["Authorization"] = f"Bearer {tok}"
    releases, page = [], 1
    while True:
        req = urllib.request.Request(API.format(repo=repo, page=page), headers=headers)
        with urllib.request.urlopen(req, timeout=30) as r:
            batch = json.load(r)
        releases += batch
        if len(batch) < 100:
            return releases
        page += 1


def classify(name):
    """Return (kind, platform) for a release asset name."""
    n = name.lower()
    if n in ("sha256sums", "release-manifest.json") or n.endswith((".txt", ".html", ".json")):
        return "docs", None
    if n.endswith("-src.tar.gz") or n.endswith(".orig.tar.gz"):
        return "source", None
    if not (re.match(r"wsjtx[-_]\d", n) and n.endswith((".exe", ".msi", ".pkg", ".dmg",
                                                   ".deb", ".rpm", ".appimage"))):
        return "tools", None
    if n.endswith((".exe", ".msi")):
        return "installer", "Win32" if "win32" in n else "Windows"
    if n.endswith((".pkg", ".dmg")):
        return "installer", "macOS arm64" if re.search(r"arm64|arm-darwin", n) else "macOS x86_64"
    if re.search(r"aarch64|arm64", n):
        return "installer", "Linux aarch64"
    if re.search(r"armhf|armv7", n):
        return "installer", "Linux armhf"
    return "installer", "Linux x86_64"


def summarize(rel):
    s = defaultdict(int)
    for a in rel["assets"]:
        kind, plat = classify(a["name"])
        s[kind] += a["download_count"]
        if plat:
            s[plat] += a["download_count"]
    return s


def asset_counts(releases):
    return {r["tag_name"]: {a["name"]: a["download_count"] for a in r["assets"]}
            for r in releases}


def print_summary(releases, now):
    cols = ["Windows", "macOS", "Linux"]
    print(f"{'Release':<16}{'Published':<12}{'Type':<5}{'Installers':>11}"
          + "".join(f"{c:>9}" for c in cols)
          + f"{'Source':>8}{'Tools':>7}{'Days':>6}{'Inst/day':>9}")
    tot = defaultdict(int)
    for r in releases:
        s = summarize(r)
        pub = datetime.fromisoformat(r["published_at"].replace("Z", "+00:00"))
        days = max((now - pub).total_seconds() / 86400.0, 0.01)
        win = s["Windows"] + s["Win32"]
        mac = s["macOS arm64"] + s["macOS x86_64"]
        lin = s["Linux x86_64"] + s["Linux aarch64"] + s["Linux armhf"]
        typ = "pre" if r["prerelease"] else "rel"
        print(f"{r['tag_name']:<16}{pub:%Y-%m-%d}  {typ:<5}{s['installer']:>11,}"
              f"{win:>9,}{mac:>9,}{lin:>9,}{s['source']:>8,}{s['tools']:>7,}"
              f"{days:>6.0f}{s['installer'] / days:>9.1f}")
        for k, v in s.items():
            tot[k] += v
    print()
    print(f"Installer downloads by platform, all {len(releases)} releases listed:")
    total = tot["installer"] or 1
    for p in PLATFORMS:
        if tot[p]:
            print(f"  {p:<14}{tot[p]:>10,}  {100.0 * tot[p] / total:5.1f}%")
    print(f"  {'Total':<14}{tot['installer']:>10,}")
    print(f"  (plus {tot['source']:,} source, {tot['tools']:,} tool/component, "
          f"{tot['docs']:,} docs/checksum downloads)")


def print_detail(rel):
    print(f"{rel['tag_name']}  published {rel['published_at'][:10]}"
          f"{'  (pre-release)' if rel['prerelease'] else ''}")
    rows = sorted(rel["assets"], key=lambda a: -a["download_count"])
    for a in rows:
        kind, plat = classify(a["name"])
        print(f"  {a['download_count']:>8,}  {kind:<10}{plat or '':<15}{a['name']}")
    print(f"  {sum(a['download_count'] for a in rows):>8,}  total")


def print_diff(old, releases):
    then = datetime.fromisoformat(old["taken"])
    print(f"Downloads since snapshot of {then:%Y-%m-%d %H:%M} UTC:")
    for r in releases:
        before = old["counts"].get(r["tag_name"], {})
        d = defaultdict(int)
        for a in r["assets"]:
            kind, plat = classify(a["name"])
            delta = a["download_count"] - before.get(a["name"], 0)
            d[kind] += delta
            if plat:
                d[plat] += delta
        if any(d.values()):
            plats = ", ".join(f"{p} {d[p]:,}" for p in PLATFORMS if d[p])
            print(f"  {r['tag_name']:<16}{d['installer']:>8,} installers  ({plats})")


def main():
    p = argparse.ArgumentParser(description="Summarize GitHub release download counts.")
    p.add_argument("--repo", default="WSJTX/wsjtx", help="owner/repository")
    p.add_argument("--no-pre", action="store_true", help="omit pre-releases")
    p.add_argument("--detail", metavar="TAG", help="list every asset of one release")
    p.add_argument("--csv", metavar="FILE", help="write per-asset counts to CSV")
    p.add_argument("--save", metavar="FILE", help="save a snapshot of current counts")
    p.add_argument("--diff", metavar="FILE", help="show downloads since a saved snapshot")
    a = p.parse_args()

    try:
        releases = fetch_releases(a.repo)
    except Exception as e:
        sys.exit(f"Could not fetch releases for {a.repo}: {e}")
    releases = [r for r in releases if not r["draft"] and not (a.no_pre and r["prerelease"])]
    releases.sort(key=lambda r: r["published_at"], reverse=True)
    now = datetime.now(timezone.utc)

    if a.detail:
        rel = next((r for r in releases if r["tag_name"] == a.detail), None)
        if not rel:
            sys.exit(f"No release tagged {a.detail}")
        print_detail(rel)
    elif a.diff:
        with open(a.diff, encoding="utf-8") as f:
            print_diff(json.load(f), releases)
    else:
        print_summary(releases, now)

    if a.csv:
        with open(a.csv, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["release", "published", "prerelease", "asset", "kind",
                        "platform", "downloads", "retrieved"])
            for r in releases:
                for x in r["assets"]:
                    kind, plat = classify(x["name"])
                    w.writerow([r["tag_name"], r["published_at"][:10], r["prerelease"],
                                x["name"], kind, plat or "", x["download_count"],
                                now.isoformat(timespec="seconds")])
        print(f"\nWrote {a.csv}")
    if a.save:
        with open(a.save, "w", encoding="utf-8") as f:
            json.dump({"repo": a.repo, "taken": now.isoformat(timespec="seconds"),
                       "counts": asset_counts(releases)}, f, indent=1)
        print(f"\nSaved snapshot to {a.save}")


if __name__ == "__main__":
    main()
