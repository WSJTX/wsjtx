#!/usr/bin/env python3
"""Print a GitHub Release change list from the commit subjects of a public release tag.

Usage:
    generate-change-list.py <owner/repo> <tag>

Prints nothing when no earlier release tag exists.
"""
import importlib.util
import re
import subprocess
import sys
from pathlib import Path


def load_script(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


policy = load_script("release_policy", "release-policy.py")

SUBJECT_RE = re.compile(
    r"^(?P<type>[A-Za-z]+)(?:\((?P<scope>[^)]*)\))?(?P<breaking>!)?:\s*(?P<description>\S.*)$"
)
SECTIONS = (("feat", "New features"), ("fix", "Fixes"), ("perf", "Performance"))
INTERNAL_SCOPES = frozenset(("release", "ci", "test", "tests"))
# Pull request numbers in subjects belong to the private repository; on the public
# release page they would link to unrelated items, and @names would notify people.
TRAILING_PR_RE = re.compile(r"\s*\(#\d+\)$")
REFERENCE_RE = re.compile(r"[\w.-]+/[\w.-]+#\d+|\bGH-\d+\b|(?<![\w/])[#@][\w-]+")


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout


def release_tags():
    tags = {}
    for name in git("tag", "--list", "v*").split():
        try:
            tags[name] = policy.classify(name[1:])
        except ValueError:
            continue
    return tags


def range_of(tag):
    """Return the tag the list starts from and the tags whose history is left out."""
    current = policy.classify(tag[1:])
    order = policy.release_order(current)
    tags = release_tags()
    earlier = {name: identity for name, identity in tags.items() if policy.release_order(identity) < order}
    if current["channel"] == "GA":
        earlier = {name: identity for name, identity in earlier.items() if identity["channel"] == "GA"}
    if not earlier:
        return None
    newest = lambda names: max(names, key=lambda name: policy.release_order(tags[name]))
    # Version order ranks an earlier tag of the same X.Y line above every older line's tags.
    base = newest(earlier)
    # Copies of shipped commits sit on the current and the newest older line.
    lines = {current["release_branch"]}
    older = [name for name in earlier if tags[name]["release_branch"] not in lines]
    if older:
        lines.add(tags[newest(older)]["release_branch"])
    scanned = sorted(name for name in earlier if tags[name]["release_branch"] in lines)
    return base, sorted(earlier), scanned, sorted(set(earlier) - set(scanned))


def patch_ids(*revisions):
    """Map each non-merge commit with a non-empty diff to its stable patch ID."""
    log = subprocess.run(["git", "log", "--no-merges", "-p", *revisions], check=True, capture_output=True).stdout
    ids = subprocess.run(["git", "patch-id", "--stable"], input=log, check=True, capture_output=True).stdout
    return {commit: patch for patch, commit in (line.split() for line in ids.decode().splitlines())}


def plain(text):
    text = REFERENCE_RE.sub(lambda match: f"`{match.group(0)}`", text)
    return text.replace("<", "&lt;")


def markdown(text):
    if text.count("`") % 2:
        return plain(text)
    # Leave existing code spans alone.
    parts = text.split("`")
    return "`".join(part if index % 2 else plain(part) for index, part in enumerate(parts))


def entry(match, author):
    text = markdown(TRAILING_PR_RE.sub("", match.group("description")))
    scope = match.group("scope")
    if scope:
        text = f"{markdown(scope)}: {text}"
    if match.group("breaking"):
        text += " (breaking)"
    return f"- {text} ({author})"


def change_list(repo, tag):
    span = range_of(tag)
    if span is None:
        return ""
    base, excluded, scanned, unscanned = span
    candidates = patch_ids(tag, "--not", *excluded)
    shipped = set(patch_ids(*scanned, "--not", *unscanned).values())
    log = git("log", "--reverse", "--no-merges", "--format=%H%x00%aN%x00%s", tag, "--not", *excluded)
    sections = {kind: [] for kind, _ in SECTIONS}
    other = 0
    for line in log.splitlines():
        commit, author, subject = line.split("\0", 2)
        if candidates.get(commit) in shipped:
            continue
        match = SUBJECT_RE.match(subject)
        kind = match.group("type").lower() if match else None
        scopes = {part.strip().lower() for part in match.group("scope").split(",")} if match and match.group("scope") else set()
        if kind not in sections or (scopes and scopes <= INTERNAL_SCOPES):
            other += 1
            continue
        item = entry(match, author)
        if item not in sections[kind]:
            sections[kind].append(item)

    comparison = f"https://github.com/{repo}/compare/{base}...{tag}"
    out = [f"## Changes since {base}"]
    for kind, title in SECTIONS:
        if sections[kind]:
            out += ["", f"### {title}", *sections[kind]]
    out.append("")
    listed = any(sections.values())
    commits = f"{other} other commit" + ("" if other == 1 else "s")
    if other and listed:
        out.append(f"Plus {commits}: [full comparison]({comparison}).")
    elif other:
        out.append(f"No user-facing changes; {commits}: [full comparison]({comparison}).")
    elif listed:
        out.append(f"[Full comparison]({comparison}).")
    else:
        out.append(f"No source changes since {base}.")
    return "\n".join(out) + "\n"


def main(argv):
    if len(argv) != 3:
        sys.stderr.write("usage: generate-change-list.py <owner/repo> <tag>\n")
        return 2
    sys.stdout.write(change_list(argv[1], argv[2]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
