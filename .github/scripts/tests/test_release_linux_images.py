#!/usr/bin/env python3
"""Release dependency selection and registry-copy regression checks."""
import hashlib
import io
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("release_linux_images", Path(__file__).parents[1] / "release-linux-images.py")
images = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(images)
GENERATION = "validated-build-20260925-123-1"
DIGEST = "sha256:" + "a" * 64
RECIPE = "b" * 64


def selection():
    return {"schema": 2, "generation": GENERATION, "images": {
        key: {"reference": f"{images.PUBLIC}/{spec[0]}@{DIGEST}", "recipe_sha256": RECIPE}
        for key, spec in images.IMAGES.items()
    }}


def manifests():
    common = {"generation": GENERATION.removeprefix("validated-"), "recipe_sha256": RECIPE,
              "toolchain_id": "gcc13-armhf-abc", "runtime_sha256": "c" * 64}
    runtime = dict(common)
    runtime.update({"cross_" + key: common[key] for key in ("generation", "recipe_sha256", "toolchain_id")})
    return [dict(common), dict(common),
            dict(common, distro="bookworm", time_bits="32"), dict(runtime, distro="bookworm", time_bits="32"),
            dict(common, distro="trixie", time_bits="64"), dict(runtime, distro="trixie", time_bits="64")]


class SelectionTests(unittest.TestCase):
    def setUp(self):
        self.fingerprint = patch.object(images, "fingerprint", return_value=RECIPE)
        self.fingerprint.start()
        self.addCleanup(self.fingerprint.stop)

    def test_complete_selection_and_anonymous_reads(self):
        with patch.object(images, "anonymous_digest", return_value=DIGEST) as remote:
            self.assertEqual(images.validate(selection(), True), selection())
            self.assertEqual(remote.call_count, 6)

    def test_missing_malformed_and_untrusted_selection(self):
        cases = []
        lock = selection(); del lock["images"]["armhf_cross"]; cases.append(lock)
        lock = selection(); del lock["images"]["armhf_trixie_runtime"]; cases.append(lock)
        lock = selection(); lock["schema"] = 1; cases.append(lock)
        lock = selection(); lock["images"]["x86_64"] = None; cases.append(lock)
        lock = selection(); lock["generation"] = "stable"; cases.append(lock)
        lock = selection(); lock["images"]["x86_64"]["reference"] = f"{images.PRIVATE}/linux-noble@{DIGEST}"; cases.append(lock)
        lock = selection(); lock["images"]["x86_64"]["reference"] = f"{images.PUBLIC}/linux-noble:stable"; cases.append(lock)
        lock = selection(); lock["images"]["aarch64"]["recipe_sha256"] = "c" * 64; cases.append(lock)
        for lock in cases:
            with self.subTest(lock=lock), self.assertRaises(ValueError):
                images.validate(lock)

    def test_remote_digest_mismatch(self):
        with patch.object(images, "anonymous_digest", return_value="sha256:" + "c" * 64), self.assertRaisesRegex(ValueError, "digest mismatch"):
            images.validate(selection(), True)

    def test_provenance_binds_exact_lock_bytes_and_six_builders(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "lock.json"
            path.write_text(json.dumps(selection()))
            first = images.provenance(path)
            self.assertEqual(first["builders"], dict.fromkeys(images.IMAGES, DIGEST))
            path.write_text(json.dumps(selection(), indent=2))
            self.assertNotEqual(first["lock_sha256"], images.provenance(path)["lock_sha256"])

    def test_workflow_outputs_bind_each_distro_digest(self):
        with tempfile.TemporaryDirectory() as directory:
            lock = Path(directory) / "lock.json"
            output = Path(directory) / "output"
            selected = selection()
            selected["images"]["armhf_trixie_runtime"]["reference"] = f"{images.PUBLIC}/linux-armv7-trixie@sha256:{'c' * 64}"
            lock.write_text(json.dumps(selected))
            with patch("sys.argv", ["helper", "validate", str(lock), "--github-output", str(output)]), \
                 patch("sys.stdout", new_callable=io.StringIO):
                images.main()
            outputs = dict(line.split("=", 1) for line in output.read_text().splitlines())
            self.assertEqual(outputs["armhf_digest"], DIGEST)
            self.assertEqual(outputs["armhf_cross_digest"], DIGEST)
            self.assertEqual(outputs["armhf_trixie_digest"], "sha256:" + "c" * 64)
            self.assertEqual(outputs["armhf_trixie_cross_digest"], DIGEST)

    def test_image_validation_selects_the_matching_distro(self):
        for key, distro in (("armhf_runtime", "bookworm"), ("armhf_trixie_runtime", "trixie")):
            with patch.object(images, "run", side_effect=["4.7.2", "", "generation=" + GENERATION.removeprefix("validated-"), ""]) as command:
                images.image_manifest("image@" + DIGEST, images.IMAGES[key], RECIPE, GENERATION)
            self.assertIn("ARMHF_DISTRO=" + distro, command.call_args_list[1].args[0])

    def test_verified_image_is_removed(self):
        with patch.object(images, "run", side_effect=["4.7.2", "", "generation=" + GENERATION.removeprefix("validated-"), ""]) as command:
            images.image_manifest("image@" + DIGEST, images.IMAGES["x86_64"], RECIPE, GENERATION)
        self.assertEqual(command.call_args_list[-1].args[0], ["docker", "image", "rm", "image@" + DIGEST])

    def test_candidate_provenance_mismatch_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            lock = Path(directory) / "lock.json"
            record = Path(directory) / "provenance.json"
            lock.write_text(json.dumps(selection()))
            expected = images.provenance(lock)
            record.write_text(json.dumps({"linux_images": expected}))
            with patch("sys.argv", ["helper", "verify-provenance", str(lock), str(record)]):
                images.main()
                expected["builders"]["armhf_cross"] = "sha256:" + "c" * 64
                record.write_text(json.dumps({"linux_images": expected}))
                with self.assertRaisesRegex(ValueError, "provenance does not match"):
                    images.main()

    def test_existing_output_is_never_reused(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch("sys.argv", ["helper", "prepare", GENERATION, "--output-directory", directory]), \
                 patch.object(images, "prepare") as prepare, \
                 self.assertRaisesRegex(ValueError, "must not already exist"):
                images.main()
            prepare.assert_not_called()

    def prepare(self, existing=None, manifest_values=None):
        def inspect(reference, anonymous=False, missing_ok=False):
            return existing if missing_ok else DIGEST
        with patch.object(images, "inspect", side_effect=inspect), \
             patch.object(images, "image_manifest", side_effect=manifest_values or manifests()), \
             patch.object(images, "anonymous_digest", return_value=DIGEST), \
             patch.object(images, "run") as command:
            result = images.prepare(GENERATION)
            return result, command.call_args_list

    def test_copy_preserves_all_manifests_and_digest(self):
        (lock, _), commands = self.prepare()
        self.assertEqual(lock, selection())
        self.assertEqual(len(commands), 6)
        for call in commands:
            args = call.args[0]
            self.assertEqual(args[:4], ["skopeo", "copy", "--all", "--preserve-digests"])
            self.assertIn("@" + DIGEST, args[4])
            self.assertTrue(args[5].endswith(":release-deps-" + GENERATION))

    def test_identical_rerun_skips_copy(self):
        _, commands = self.prepare(existing=DIGEST)
        self.assertEqual(commands, [])

    def test_conflicting_retained_tag_fails(self):
        with self.assertRaisesRegex(ValueError, "Conflicting retained"):
            self.prepare(existing="sha256:" + "c" * 64)

    def test_mixed_armhf_pair_fails_before_copy(self):
        for index in (3, 5):
            for field in ("cross_generation", "cross_recipe_sha256", "cross_toolchain_id", "runtime_sha256", "distro", "time_bits"):
                values = manifests()
                values[index][field] = "wrong"
                with self.subTest(index=index, field=field), self.assertRaisesRegex(ValueError, "pair mismatch"):
                    self.prepare(manifest_values=values)

    def test_partial_copy_failure_emits_no_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "output"
            with patch("sys.argv", ["helper", "prepare", GENERATION, "--output-directory", str(output)]), \
                 patch.object(images, "inspect", side_effect=lambda ref, anonymous=False, missing_ok=False: None if missing_ok else DIGEST), \
                 patch.object(images, "image_manifest", side_effect=manifests()), \
                 patch.object(images, "run", side_effect=["", subprocess.CalledProcessError(1, "copy", stderr="denied")]), \
                 self.assertRaises(subprocess.CalledProcessError):
                images.main()
            self.assertFalse(output.exists())

    def test_registry_inspection_hashes_exact_manifest_without_platform_selection(self):
        raw = b'{"mediaType":"application/vnd.oci.image.index.v1+json","manifests":[]}\n'
        result = subprocess.CompletedProcess([], 0, stdout=raw)
        with patch.object(images.subprocess, "run", return_value=result) as command:
            digest = images.inspect("ghcr.io/wsjtx/wsjtx/linux-armv7-bookworm:test", anonymous=True)
        self.assertEqual(digest, "sha256:" + hashlib.sha256(raw).hexdigest())
        self.assertIn("--raw", command.call_args.args[0])
        self.assertIn("--no-creds", command.call_args.args[0])

    def test_authentication_error_is_not_missing_tag(self):
        with patch.object(images.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "inspect", stderr=b"unauthorized")), self.assertRaises(subprocess.CalledProcessError):
            images.inspect("test", missing_ok=True)

    def test_only_manifest_unknown_allows_first_copy(self):
        with patch.object(images.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "inspect", stderr=b"manifest unknown")):
            self.assertIsNone(images.inspect("test", missing_ok=True))


if __name__ == "__main__":
    unittest.main()
