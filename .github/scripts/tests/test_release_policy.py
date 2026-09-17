import importlib.util
import json
import io
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "release-policy.py"
SPEC = importlib.util.spec_from_file_location("release_policy", SCRIPT)
release_policy = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(release_policy)


class ReleasePolicyTest(unittest.TestCase):
    def test_classifies_ga_and_rc(self):
        self.assertEqual(release_policy.classify("3.2.0")["channel"], "GA")
        identity = release_policy.classify("3.2.0-rc2")
        self.assertEqual(identity["channel"], "RC")
        self.assertEqual(identity["rc_number"], "2")
        self.assertEqual(identity["release_branch"], "release/3.2")

    def test_rejects_non_release_versions(self):
        for version in ("v3.2.0", "3.2.0-beta1", "3.2", "3.2.0-rc0"):
            with self.subTest(version=version), self.assertRaises(ValueError):
                release_policy.classify(version)

    def test_validates_tracked_release_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "release-state.txt").write_text(
                "version=3.2.0\n"
                "channel=RC\n"
                "rc=1\n"
                "revision=$Format:%H$\n"
            )
            identity = release_policy.validate_source(root, "3.2.0-rc1")
            self.assertEqual(identity["channel"], "RC")
            with self.assertRaisesRegex(ValueError, "release channel"):
                release_policy.validate_source(root, "3.2.0")
            (root / "release-state.txt").write_text(
                "version=3.2.0\nchannel=RC\nrc=1\nrevision=" + "a" * 40 + "\n"
            )
            with self.assertRaisesRegex(ValueError, "revision"):
                release_policy.validate_source(root, "3.2.0-rc1")

    def test_read_state_cli_accepts_single_digit_rc(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "release-state.txt").write_text(
                "version=3.2.0\n"
                "channel=RC\n"
                "rc=1\n"
                "revision=$Format:%H$\n"
            )
            result = subprocess.run(
                [sys.executable, SCRIPT, "read-state", "--root", root],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                json.loads(result.stdout),
                {
                    "version": "3.2.0",
                    "channel": "RC",
                    "rc": "1",
                    "revision": "$Format:%H$",
                },
            )

            (root / "release-state.txt").write_text(
                "version=3.2.0\n"
                "channel=RC\n"
                "rc=1beta\n"
                "revision=$Format:%H$\n"
            )
            result = subprocess.run(
                [sys.executable, SCRIPT, "read-state", "--root", root],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("positive RC number", result.stderr)

    def test_validates_exported_tar_and_zip_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            commit = "b" * 40
            state = f"version=3.2.0\nchannel=RC\nrc=1\nrevision={commit}\n".encode()
            tar_path = root / "source.tar.gz"
            with tarfile.open(tar_path, "w:gz") as archive:
                info = tarfile.TarInfo("wsjtx-3.2.0-rc1/release-state.txt")
                info.size = len(state)
                archive.addfile(info, io.BytesIO(state))
            zip_path = root / "source.zip"
            with zipfile.ZipFile(zip_path, "w") as archive:
                archive.writestr("wsjtx-3.2.0-rc1/release-state.txt", state)
            release_policy.validate_archive(tar_path, "3.2.0-rc1", commit)
            release_policy.validate_archive(zip_path, "3.2.0-rc1", commit)
            with self.assertRaisesRegex(ValueError, "revision"):
                release_policy.validate_archive(tar_path, "3.2.0-rc1", "c" * 40)

    def test_distribution_gate_rejects_unsigned_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            version = "3.2.0-rc1"
            for name in release_policy.expected_assets(version, True):
                target = root / name
                target.mkdir()
                suffix = ".pkg" if "macOS" in name else ".AppImage" if "linux" in name else ".exe"
                (target / f"artifact{suffix}").write_bytes(b"signed")
            (root / "extra").mkdir()
            (root / "extra" / "unexpected-unsigned.pkg").write_bytes(b"unsigned")
            with self.assertRaisesRegex(ValueError, "unsigned macOS"):
                release_policy.find_asset_files(root, version, True)

    def test_manifest_binds_assets_to_tag_and_commit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            version = "3.2.0"
            for name in release_policy.expected_assets(version, True):
                target = root / name
                target.mkdir()
                suffix = ".pkg" if "macOS" in name else ".AppImage" if "linux" in name else ".exe"
                (target / f"{name}{suffix}" if not name.endswith(suffix) else target / name).write_bytes(name.encode())
            for arch in ("x86_64", "aarch64", "armhf"):
                for package_type in ("deb", "rpm"):
                    target = root / f"wsjtx-{version}-linux-{arch}-{package_type}"
                    target.mkdir()
                    (target / f"wsjtx-{arch}.{package_type}").write_bytes(arch.encode())
            (root / f"wsjtx-{version}-src.tar.gz").write_bytes(b"source")
            args = type("Args", (), {
                "artifacts": str(root), "version": version, "repository": "WSJTX/wsjtx",
                "commit": "a" * 40, "run_id": "123",
                "linux_x86_64_digest": "sha256:" + "1" * 64,
                "linux_aarch64_digest": "sha256:" + "2" * 64,
                "linux_armhf_cross_digest": "sha256:" + "3" * 64,
                "linux_armhf_digest": "sha256:" + "4" * 64,
                "macos_mode": "distribution",
            })()
            release_policy.write_manifest(args)
            manifest = json.loads((root / "release-manifest.json").read_text())
            self.assertEqual(manifest["tag"], "v3.2.0")
            self.assertEqual(manifest["commit"], "a" * 40)
            self.assertEqual(manifest["linux_builders"]["armhf_cross"], "sha256:" + "3" * 64)
            self.assertEqual(manifest["linux_builders"]["armhf_runtime"], "sha256:" + "4" * 64)
            self.assertEqual(manifest["macos_signing"]["mode"], "distribution")
            self.assertEqual(manifest["macos_signing"]["replaceable_assets"], [])
            self.assertEqual(len(manifest["assets"]), 13)

    def test_manual_macos_assets_keep_release_names_without_immutable_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            version = "3.2.0"
            for name in release_policy.public_expected_assets(version, "validation"):
                target = root / name
                target.mkdir()
                if "macOS" in name:
                    filename = name.replace("-unsigned", "")
                elif "linux" in name:
                    filename = f"{name}.AppImage"
                else:
                    filename = "wsjtx-win64.exe"
                (target / filename).write_bytes(name.encode())
            for arch in ("x86_64", "aarch64", "armhf"):
                for package_type in ("deb", "rpm"):
                    target = root / f"wsjtx-{version}-linux-{arch}-{package_type}"
                    target.mkdir()
                    (target / f"wsjtx-{arch}.{package_type}").write_bytes(arch.encode())
            (root / f"wsjtx-{version}-src.tar.gz").write_bytes(b"source")
            args = type("Args", (), {
                "artifacts": str(root), "version": version, "repository": "WSJTX/wsjtx",
                "commit": "a" * 40, "run_id": "123",
                "linux_x86_64_digest": "sha256:" + "1" * 64,
                "linux_aarch64_digest": "sha256:" + "2" * 64,
                "linux_armhf_cross_digest": "sha256:" + "3" * 64,
                "linux_armhf_digest": "sha256:" + "4" * 64,
                "macos_mode": "validation",
            })()

            release_policy.write_manifest(args)

            manifest = json.loads((root / "release-manifest.json").read_text())
            replaceable = {
                f"wsjtx-{version}-arm64-macOS.pkg",
                f"wsjtx-{version}-x86_64-macOS.pkg",
            }
            release_names = {path.name for path in release_policy.release_files(root, version, "validation")}
            immutable_names = {entry["name"] for entry in manifest["assets"]}
            checksum_names = {
                line.split("  ", 1)[1]
                for line in (root / "SHA256SUMS").read_text().splitlines()
            }
            self.assertEqual(manifest["macos_signing"]["mode"], "manual")
            self.assertEqual(set(manifest["macos_signing"]["replaceable_assets"]), replaceable)
            self.assertTrue(replaceable <= release_names)
            self.assertTrue(replaceable.isdisjoint(immutable_names))
            self.assertTrue(replaceable.isdisjoint(checksum_names))
            self.assertIn("wsjtx-win64.exe", immutable_names)

    def test_signing_reports_bind_hashes_and_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            version = "3.2.0-rc1"
            commit = "b" * 40
            tag = f"v{version}"
            for arch in ("arm64", "x86_64"):
                package_dir = root / f"wsjtx-{version}-{arch}-macOS.pkg"
                package_dir.mkdir()
                package = package_dir / f"wsjtx-{version}-{arch}-macOS.pkg"
                package.write_bytes(arch.encode())
                report_dir = root / f"macos-signing-report-{version}-{arch}"
                report_dir.mkdir()
                (report_dir / "macos-signing-report.json").write_text(json.dumps({
                    "mode": "distribution", "git_sha": commit, "artifact": package.name,
                    "sha256": release_policy.hash_file(package), "notarization": {"status": "Accepted"},
                    "stapled": True, "gatekeeper_accepted": True, "team_id": "ABCDE12345",
                    "application_certificate_sha1": "A" * 40,
                    "installer_certificate_sha1": "B" * 40,
                }))
            installer_dir = root / f"wsjtx-{version}-windows-x86_64-installer-signed"
            installer_dir.mkdir()
            installer = installer_dir / "wsjtx.exe"
            installer.write_bytes(b"windows")
            request_dir = root / f"wsjtx-{version}-windows-signing-request"
            request_dir.mkdir()
            (request_dir / "request.json").write_text(json.dumps({
                "policy": "release-signing", "commit": commit, "tag": tag,
            }))
            verification_dir = root / f"wsjtx-{version}-windows-signing-verification"
            verification_dir.mkdir()
            (verification_dir / "verification.json").write_text(json.dumps({
                "commit": commit, "tag": tag, "artifact": installer.name,
                "sha256": release_policy.hash_file(installer), "status": "Valid",
                "timestamp_thumbprint": "1234", "signer_subject": "WSJT-X",
                "signer_thumbprint": "5678", "identity_verified": True,
            }))
            release_policy.verify_signing_reports(root, version, commit, tag)
            (verification_dir / "verification.json").write_text("{}")
            with self.assertRaisesRegex(ValueError, "release ref"):
                release_policy.verify_signing_reports(root, version, commit, tag)

    def test_validation_reports_allow_unsigned_macos_with_signed_windows(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            version = "3.2.0-rc1"
            commit = "b" * 40
            tag = f"v{version}"
            for arch in ("arm64", "x86_64"):
                package_dir = root / f"wsjtx-{version}-{arch}-macOS-unsigned.pkg"
                package_dir.mkdir()
                package = package_dir / f"wsjtx-{version}-{arch}-macOS.pkg"
                package.write_bytes(arch.encode())
                report_dir = root / f"macos-signing-report-{version}-{arch}"
                report_dir.mkdir()
                (report_dir / "macos-signing-report.json").write_text(json.dumps({
                    "mode": "validation", "git_sha": commit, "artifact": package.name,
                    "sha256": release_policy.hash_file(package), "signed": False,
                    "notarized": False, "publishable": False,
                }))
            installer_dir = root / f"wsjtx-{version}-windows-x86_64-installer-signed"
            installer_dir.mkdir()
            installer = installer_dir / "wsjtx.exe"
            installer.write_bytes(b"windows")
            request_dir = root / f"wsjtx-{version}-windows-signing-request"
            request_dir.mkdir()
            (request_dir / "request.json").write_text(json.dumps({
                "policy": "release-signing", "commit": commit, "tag": tag,
            }))
            verification_dir = root / f"wsjtx-{version}-windows-signing-verification"
            verification_dir.mkdir()
            (verification_dir / "verification.json").write_text(json.dumps({
                "commit": commit, "tag": tag, "artifact": installer.name,
                "sha256": release_policy.hash_file(installer), "status": "Valid",
                "timestamp_thumbprint": "1234", "signer_subject": "WSJT-X",
                "signer_thumbprint": "5678", "identity_verified": True,
            }))

            release_policy.verify_signing_reports(root, version, commit, tag, "validation")

            report = json.loads(
                (root / f"macos-signing-report-{version}-arm64" / "macos-signing-report.json").read_text()
            )
            report["signed"] = True
            (root / f"macos-signing-report-{version}-arm64" / "macos-signing-report.json").write_text(
                json.dumps(report)
            )
            with self.assertRaisesRegex(ValueError, "unsigned package"):
                release_policy.verify_signing_reports(root, version, commit, tag, "validation")


if __name__ == "__main__":
    unittest.main()
