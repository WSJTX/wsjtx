#!/usr/bin/env python3
"""Apply the Boost.Log queue publication and reclamation ordering fix."""

import argparse
import hashlib
import json
from pathlib import Path
import re


CPP = "libs/log/src/threadsafe_queue.cpp"
HEADER = "boost/log/detail/threadsafe_queue.hpp"
SUPPORTED = {109200: "1.92.0"}
SOURCE_HASHES = {
    CPP: "41016c88edb36906bdad6ca9712ddc60f5ed88f4c59de2cc786413b7ce4a52a1",
    HEADER: "658523c6458c567a1d09d24baa7d4780a783f06650918de6fd85ed8170ec2594",
}
PATCHED_HASHES = {
    CPP: "3d344b854a7499f6664242fcbb69614b9ed586b1f8c29c4bd8721396792a8c8f",
    HEADER: SOURCE_HASHES[HEADER],
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def patch_source(root):
    match = re.search(r"^#define BOOST_VERSION (\d+)\s*$",
                      (root / "boost/version.hpp").read_text(), re.MULTILINE)
    version = int(match.group(1)) if match else 0
    if version not in SUPPORTED:
        raise ValueError(f"Unsupported Boost version: {version}")
    original = (root / CPP).read_bytes()
    if digest((root / HEADER).read_bytes()) != SOURCE_HASHES[HEADER]:
        raise ValueError(f"Unrecognized Boost.Log queue source: {HEADER}")
    if digest(original) == SOURCE_HASHES[CPP]:
        patched = original.replace(
            b"        p->next.store(next, boost::memory_order_relaxed);",
            b"        // Publish payload writes and order this access before node reclamation.\n"
            b"        p->next.store(next, boost::memory_order_release);",
        ).replace(b"return p->next.load(boost::memory_order_relaxed);",
                  b"return p->next.load(boost::memory_order_acquire);")
    else:
        patched = original
    if digest(patched) != PATCHED_HASHES[CPP]:
        raise ValueError(f"Unrecognized or partially patched Boost.Log queue source: {CPP}")
    if patched != original:
        (root / CPP).write_bytes(patched)
    return {
        "boost_version": SUPPORTED[version],
        "patch": "boost-log-queue-release-acquire-v1",
        "patch_helper_sha256": digest(Path(__file__).read_bytes()),
        "files": {name: {"original_sha256": SOURCE_HASHES[name],
                         "patched_sha256": PATCHED_HASHES[name]}
                  for name in (HEADER, CPP)},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Extracted Boost source root")
    parser.add_argument("--manifest", type=Path, help="Write patch provenance JSON")
    args = parser.parse_args()
    try:
        output = json.dumps(patch_source(args.source), indent=2, sort_keys=True) + "\n"
    except (OSError, ValueError) as error:
        parser.exit(1, f"Boost.Log queue patch failed: {error}\n")
    if args.manifest:
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(output)
    print(output, end="")


if __name__ == "__main__":
    main()
