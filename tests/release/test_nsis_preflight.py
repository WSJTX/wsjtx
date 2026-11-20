# SPDX-License-Identifier: GPL-3.0-or-later
"""Build and exercise the production NSIS template on an isolated Windows installation."""

import argparse
import contextlib
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def wait_for(predicate, description, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError("Timed out waiting for " + description)


class Windows:
    def __init__(self):
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.user = ctypes.WinDLL("user32", use_last_error=True)
        self.kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                          ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
                                          wintypes.HANDLE]
        self.kernel.CreateFileW.restype = wintypes.HANDLE
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL,
                                           wintypes.LPCWSTR]
        self.kernel.CreateEventW.restype = wintypes.HANDLE
        self.kernel.SetEvent.argtypes = [wintypes.HANDLE]
        self.kernel.ResetEvent.argtypes = [wintypes.HANDLE]
        self.kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.kernel.LoadLibraryW.argtypes = [wintypes.LPCWSTR]
        self.kernel.LoadLibraryW.restype = wintypes.HMODULE
        self.kernel.FreeLibrary.argtypes = [wintypes.HMODULE]
        self.callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        self.user.EnumWindows.argtypes = [self.callback, wintypes.LPARAM]
        self.user.EnumChildWindows.argtypes = [wintypes.HWND, self.callback, wintypes.LPARAM]
        self.user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        self.user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        self.user.IsWindowVisible.argtypes = [wintypes.HWND]
        self.user.IsWindowEnabled.argtypes = [wintypes.HWND]
        self.user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        self.user.SendMessageW.restype = wintypes.LPARAM
        self.user.GetParent.argtypes = [wintypes.HWND]
        self.user.GetParent.restype = wintypes.HWND
        self.user.GetDlgCtrlID.argtypes = [wintypes.HWND]
        self.user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

    def text(self, window):
        value = ctypes.create_unicode_buffer(8192)
        self.user.GetWindowTextW(window, value, len(value))
        return value.value

    def windows(self, process):
        result = []

        @self.callback
        def collect(window, unused):
            pid = wintypes.DWORD()
            self.user.GetWindowThreadProcessId(window, ctypes.byref(pid))
            if pid.value == process.pid and self.user.IsWindowVisible(window):
                result.append(window)
            return True

        self.user.EnumWindows(collect, 0)
        return result

    def controls(self, window):
        result = []

        @self.callback
        def collect(child, unused):
            if self.user.IsWindowVisible(child):
                result.append((child, self.text(child)))
            return True

        self.user.EnumChildWindows(window, collect, 0)
        return result

    def dialog(self, process, fragment):
        for window in self.windows(process):
            controls = self.controls(window)
            if any(fragment in value for _, value in controls):
                return window, controls
        return None

    def click(self, controls, label):
        button = next((window for window, value in controls
                       if value.replace("&", "").strip() == label
                       and self.user.IsWindowEnabled(window)), None)
        require(button, "Missing enabled button: " + label)
        # Notify the dialog without requiring desktop focus or blocking on a modal response.
        require(self.user.PostMessageW(self.user.GetParent(button), 0x0111,
                                       self.user.GetDlgCtrlID(button), button), "Cannot activate " + label)

    @contextlib.contextmanager
    def lock(self, path, share=1):
        handle = self.kernel.CreateFileW(str(path), 0x80000000, share, None, 3, 0, None)
        require(handle != ctypes.c_void_p(-1).value, "Cannot lock " + str(path))
        try:
            yield
        finally:
            self.kernel.CloseHandle(handle)

    @contextlib.contextmanager
    def library(self, path):
        handle = self.kernel.LoadLibraryW(str(path))
        require(handle, "Cannot load fixture DLL: " + str(ctypes.get_last_error()))
        try:
            yield
        finally:
            self.kernel.FreeLibrary(handle)


def registry_snapshot(key_name):
    import winreg
    result = {}
    for hive_name, hive in [("HKLM", winreg.HKEY_LOCAL_MACHINE), ("HKCU", winreg.HKEY_CURRENT_USER)]:
        for view in [winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY]:
            for prefix in ["Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\",
                           "Software\\WSJTX installer tests\\"]:
                path = prefix + key_name

                def visit(path):
                    try:
                        with winreg.OpenKey(hive, path, 0, winreg.KEY_READ | view) as key:
                            children, values, _ = winreg.QueryInfoKey(key)
                            result[f"{hive_name}:{view}:{path}"] = sorted(
                                winreg.EnumValue(key, index) for index in range(values))
                            for index in range(children):
                                visit(path + "\\" + winreg.EnumKey(key, index))
                    except FileNotFoundError:
                        pass

                visit(path)
    return result


def snapshot(destination, key):
    files = {}
    paths = [destination] if destination.exists() else []
    paths.extend(destination.rglob("*"))
    for path in sorted(paths):
        relative = str(path.relative_to(destination))
        files[relative] = {"attributes": path.stat().st_file_attributes,
                           "hash": hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None}
    return {"files": files, "registration": registry_snapshot(key)}


class Suite:
    def __init__(self, args):
        self.args = args
        self.root = args.work.resolve()
        self.root.mkdir(parents=True, exist_ok=False)
        self.identity = "preflight-" + uuid.uuid4().hex
        self.key = "WSJTX-preflight-" + self.identity
        self.destination = self.root / "Installed copy café 日本"
        self.windows = Windows()
        self.results = []
        self.packages = {}
        self.installers = []
        self.temp = self.root / "installer-temp"
        self.temp.mkdir()

    def record(self, name, **details):
        self.results.append({"case": name, "status": "passed", **details})
        (self.root / "results.json").write_text(json.dumps(self.results, indent=2), encoding="utf-8")
        print("PASS " + name, flush=True)

    def command(self, command, log):
        with (self.root / log).open("w", encoding="utf-8") as output:
            subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, check=True)

    def build(self, version, late=False):
        name = f"v{version}" + ("-late" if late else "")
        build = self.root / name
        self.command([self.args.cmake, "-S", str(self.args.source / "tests/release/nsis_preflight"),
                      "-B", str(build), "-G", self.args.generator,
                      "-DSOURCE_DIR=" + str(self.args.source), "-DFIXTURE_ID=" + self.identity,
                      "-DFIXTURE_VERSION=" + str(version), "-DLATE_CONFLICT=" + ("ON" if late else "OFF")],
                     name + "-configure.log")
        self.command([self.args.cmake, "--build", str(build), "--config", "Release"], name + "-build.log")
        self.command([self.args.cpack, "--config", str(build / "CPackConfig.cmake"), "-C", "Release",
                      "-B", str(build)], name + "-package.log")
        self.packages[name] = build / f"fixture-{version}.exe"
        require(self.packages[name].is_file(), "CPack did not produce " + str(self.packages[name]))

    def start(self, package="v2", silent=True):
        command = [str(self.packages[package])]
        if silent:
            command.append("/S")
        # NSIS requires /D to be last and consumes the rest of the command line literally.
        command_line = subprocess.list2cmdline(command) + " /D=" + str(self.destination)
        environment = dict(os.environ, TEMP=str(self.temp), TMP=str(self.temp))
        previous_reports = set((self.temp / "WSJT-X-installer").glob("*.log"))
        process = subprocess.Popen(command_line, env=environment)
        process.previous_reports = previous_reports
        self.installers.append(process)
        return process

    def install(self, package="v2", success=True):
        process = self.start(package)
        try:
            code = process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise AssertionError("Silent installer hung")
        require((code == 0) == success, f"Unexpected installer exit code {code}")
        if success:
            self.check_payload(package)
        else:
            self.check_preflight_report(process)
        return code

    def check_preflight_report(self, process):
        reports = [path.read_text(encoding="utf-16")
                   for path in (self.temp / "WSJT-X-installer").glob("*.log")
                   if path not in process.previous_reports]
        reports = [report for report in reports if f"(process {process.pid})" in report]
        require(len(reports) == 1, "Silent failure did not retain a unique diagnostic log")
        report = reports[0]
        require(f"Destination: {self.destination}\n" in report, "Diagnostic omitted selected destination")
        fields = dict(line.split(": ", 1) for line in report.splitlines() if ": " in line)
        require(fields.get("File"), "Diagnostic omitted blocked path")
        require(int(fields.get("Windows error", "0")) > 0, "Diagnostic omitted Windows error")
        require("No installation files have been changed." in report, "Diagnostic omitted preflight outcome")
        return report

    def check_payload(self, package):
        version = int(package[1])
        build = self.root / package
        for filename in ["wsjtx.exe", "payload.dll"]:
            require((build / filename).read_bytes() == (self.destination / "bin" / filename).read_bytes(),
                    "Incorrect installed " + filename)
        for filename, expected in [("data/00 first.txt", f"First payload version {version}\n"),
                                   ("data/zz last.txt", f"Last payload version {version}\n"),
                                   ("data/Unicode café/dollar $ payload.txt", f"Last payload version {version}\n")]:
            require((self.destination / filename).read_text() == expected, "Incorrect installed " + filename)
        require(not (self.destination / "bin/jt9stream.exe").exists(), "Obsolete executable was not removed")
        settings = self.destination / "user settings.ini"
        if settings.exists():
            require(settings.read_bytes() == b"preserve user settings\n", "User settings changed")

    def baseline(self):
        self.install("v1")
        (self.destination / "bin/jt9stream.exe").write_bytes(b"obsolete executable\n")
        (self.destination / "user settings.ini").write_bytes(b"preserve user settings\n")

    def blocked(self, name, conflict, expected_error=None):
        self.baseline()
        with conflict():
            before = snapshot(self.destination, self.key)
            code = self.install(success=False)
            if expected_error:
                report = self.check_preflight_report(self.installers[-1])
                filename, error = expected_error
                require(f"File: {self.destination / filename}\n" in report, "Diagnostic named the wrong file")
                require(f"Windows error: {error}\n" in report, "Diagnostic reported the wrong Windows error")
            after = snapshot(self.destination, self.key)
            (self.root / f"snapshot-{len(self.results):02}.json").write_text(
                json.dumps({"case": name, "before": before, "after": after}, indent=2), encoding="utf-8")
            require(before == after, name + " changed the installation")
            if self.args.interactive and name in ["read-only payload", "access-denied payload", "directory at payload filename"]:
                process = self.start(silent=False)
                self.navigate(process)
                _, controls = wait_for(lambda: self.windows.dialog(process, "Windows error 5"), "access error dialog")
                text = "\n".join(value for _, value in controls)
                require("while it is in use" not in text, "Access error was described as a running application")
                self.windows.click(controls, "Cancel")
                self.finish(process, success=False)
                require(before == snapshot(self.destination, self.key), "Access error cancellation changed destination")
        self.install()
        self.record(name, failure_exit_code=code)

    def navigate(self, process):
        def advance():
            if process.poll() is not None:
                raise AssertionError("Interactive installer exited before installation")
            for window in self.windows.windows(process):
                controls = self.windows.controls(window)
                labels = [value.replace("&", "").strip() for child, value in controls
                          if self.windows.user.IsWindowEnabled(child)]
                if "Install" in labels:
                    self.windows.click(controls, "Install")
                    return True
                for label in ["I Agree", "Next >"]:
                    if label in labels:
                        self.windows.click(controls, label)
                        time.sleep(0.1)
                        break
            return False
        wait_for(advance, "installation pages")

    def finish(self, process, success):
        dialogs = []

        def close():
            if process.poll() is not None:
                return True
            for window in self.windows.windows(process):
                controls = self.windows.controls(window)
                text = "\n".join(value for _, value in controls)
                if text not in dialogs:
                    dialogs.append(text)
                labels = [value.replace("&", "").strip() for child, value in controls
                          if self.windows.user.IsWindowEnabled(child)]
                if not success:
                    require("Run " not in text, "Failure offered application launch")
                actions = ["OK", "Close", "Finish", "Next >"] if success else ["OK", "Close", "Cancel", "Yes"]
                for label in actions:
                    if label in labels:
                        self.windows.click(controls, label)
                        break
            return False

        wait_for(close, "installer completion")
        require((process.wait() == 0) == success, "Wrong interactive exit status")
        return dialogs

    def interactive(self):
        self.baseline()
        target = self.destination / "data/zz last.txt"
        with self.windows.lock(target):
            before = snapshot(self.destination, self.key)
            process = self.start(silent=False)
            self.navigate(process)
            _, controls = wait_for(lambda: self.windows.dialog(process, "No installation files have been changed"),
                                   "preflight conflict dialog")
            text = "\n".join(value for _, value in controls)
            require(str(target) in text, "Conflict dialog omitted destination filename")
            require(not any("Ignore" in value for _, value in controls), "Preflight offered Ignore")
            self.windows.click(controls, "Cancel")
            dialogs = self.finish(process, success=False)
            require(not any("repair the installation" in value for value in dialogs), "Preflight showed repair advice")
            require(before == snapshot(self.destination, self.key), "Interactive Cancel changed destination")
        self.record("interactive Cancel preserves installation", dialog=text)

        with self.windows.lock(target):
            process = self.start(silent=False)
            self.navigate(process)
            _, controls = wait_for(lambda: self.windows.dialog(process, "No installation files have been changed"),
                                   "first Retry conflict")
        earlier = self.destination / "bin/wsjtx.exe"
        with self.windows.lock(earlier):
            self.windows.click(controls, "Retry")
            _, controls = wait_for(lambda: self.windows.dialog(process, str(earlier)), "complete preflight rerun")
        self.windows.click(controls, "Retry")
        self.finish(process, success=True)
        self.check_payload("v2")
        require((self.destination / "launch-marker.txt").is_file(), "Successful finish did not call the launcher")
        (self.destination / "launch-marker.txt").unlink()
        self.record("Retry reruns the entire preflight and completes")

    def run(self):
        self.build(1)
        self.build(2)
        self.build(2, late=True)
        self.install("v1")
        self.install()
        require((self.destination / "data/00 first.txt").read_text() == "First payload version 2\n", "Upgrade failed")
        require((self.destination / "data/Unicode café/dollar $ payload.txt").is_file(), "Escaped payload missing")
        self.record("fresh install and ordinary upgrade with Unicode and spaces")

        @contextlib.contextmanager
        def running():
            process = subprocess.Popen([str(self.destination / "bin/wsjtx.exe")])
            try:
                require(process.poll() is None, "Fixture executable did not stay running")
                yield
            finally:
                process.terminate()
                process.wait()

        self.blocked("running destination executable", running)
        self.blocked("loaded destination DLL", lambda: self.windows.library(self.destination / "bin/payload.dll"))
        for filename in ["data/zz last.txt", "Uninstall.exe", "bin/jt9stream.exe",
                         "data/Unicode café/dollar $ payload.txt"]:
            self.blocked("locked " + filename, lambda filename=filename: self.windows.lock(self.destination / filename),
                         expected_error=(filename, 32))
        self.blocked("obsolete executable allows writing but denies deletion",
                     lambda: self.windows.lock(self.destination / "bin/jt9stream.exe", share=3))

        @contextlib.contextmanager
        def readonly():
            target = self.destination / "data/zz last.txt"
            os.chmod(target, 0o444)
            try:
                yield
            finally:
                os.chmod(target, 0o666)

        self.blocked("read-only payload", readonly, expected_error=("data/zz last.txt", 5))

        self.baseline()
        os.chmod(self.destination / "bin/jt9stream.exe", 0o444)
        self.install()
        self.record("read-only obsolete executable can be deleted")

        @contextlib.contextmanager
        def readonly_obsolete_with_conflict():
            target = self.destination / "bin/jt9stream.exe"
            os.chmod(target, 0o444)
            try:
                with self.windows.lock(self.destination / "data/zz last.txt"):
                    yield
            finally:
                os.chmod(target, 0o666)

        self.blocked("read-only obsolete attributes survive a blocked upgrade", readonly_obsolete_with_conflict)

        @contextlib.contextmanager
        def denied(filename, right):
            target = self.destination / filename
            sid = subprocess.check_output(["whoami", "/user", "/fo", "csv", "/nh"], text=True).strip().split(",")[-1].strip('"')
            subprocess.run(["icacls", str(target), "/deny", "*" + sid + ":(" + right + ")"], check=True, capture_output=True)
            try:
                if right == "D":
                    subprocess.run(["icacls", str(target.parent), "/deny", "*" + sid + ":(DC)"], check=True, capture_output=True)
                yield
            finally:
                if target.exists():
                    subprocess.run(["icacls", str(target), "/remove:d", "*" + sid], check=True, capture_output=True)
                if right == "D":
                    subprocess.run(["icacls", str(target.parent), "/remove:d", "*" + sid], check=True, capture_output=True)

        self.blocked("access-denied payload", lambda: denied("data/zz last.txt", "WD"),
                     expected_error=("data/zz last.txt", 5))
        self.baseline()
        with denied("bin/jt9stream.exe", "WD"):
            self.install()
        self.record("obsolete executable needs deletion access without content-write access")
        self.baseline()
        before = snapshot(self.destination, self.key)
        with denied("bin/jt9stream.exe", "D"):
            code = self.install(success=False)
        # Restore access before hashing files protected by the deletion ACLs.
        after = snapshot(self.destination, self.key)
        (self.root / "snapshot-delete-denied.json").write_text(
            json.dumps({"before": before, "after": after}, indent=2), encoding="utf-8")
        require(before == after, "Delete denial changed the installation")
        self.install()
        self.record("delete-denied obsolete executable", failure_exit_code=code)

        @contextlib.contextmanager
        def collision():
            target = self.destination / "data/zz last.txt"
            content = target.read_bytes()
            target.unlink()
            target.mkdir()
            try:
                yield
            finally:
                target.rmdir()
                target.write_bytes(content)

        self.blocked("directory at payload filename", collision)

        @contextlib.contextmanager
        def root_collision():
            saved = self.destination.with_name("saved installation")
            self.destination.rename(saved)
            self.destination.write_bytes(b"file instead of installation directory\n")
            try:
                yield
            finally:
                self.destination.unlink()
                saved.rename(self.destination)

        self.blocked("file at installation root", root_collision, expected_error=(".", 267))
        self.baseline()
        original_destination = self.destination
        ancestor = self.root / "ancestor collision"
        ancestor.write_bytes(b"file instead of parent directory\n")
        self.destination = ancestor / "missing" / "selected installation"
        try:
            before = snapshot(original_destination, self.key)
            ancestor_content = ancestor.read_bytes()
            code = self.install(success=False)
            report = self.check_preflight_report(self.installers[-1])
            require(f"File: {ancestor}\n" in report, "Diagnostic omitted blocking ancestor")
            require("Windows error: 267\n" in report, "Diagnostic reported the wrong ancestor error")
            after = snapshot(original_destination, self.key)
            require(before == after, "Ancestor collision changed existing files or registration")
            require(ancestor.read_bytes() == ancestor_content, "Ancestor collision changed the blocking file")
            (self.root / "snapshot-ancestor.json").write_text(
                json.dumps({"before": before, "after": after}, indent=2), encoding="utf-8")
        finally:
            self.destination = original_destination
            ancestor.unlink()
        self.record("file at ancestor of selected installation", failure_exit_code=code)
        other = self.root / "unrelated"
        other.mkdir()
        shutil.copy2(self.destination / "bin/wsjtx.exe", other / "wsjtx.exe")
        process = subprocess.Popen([str(other / "wsjtx.exe")])
        try:
            self.install()
        finally:
            process.terminate()
            process.wait()
        self.record("unrelated running wsjtx.exe does not block")

        ready = self.windows.kernel.CreateEventW(None, True, False, "Local\\" + self.identity + "-ready")
        resume = self.windows.kernel.CreateEventW(None, True, False, "Local\\" + self.identity + "-continue")
        require(ready and resume, "Cannot create post-preflight events")
        try:
            for filename, share in [("data/zz last.txt", 1), ("Uninstall.exe", 1), ("bin/jt9stream.exe", 3)]:
                self.baseline()
                self.windows.kernel.ResetEvent(ready)
                self.windows.kernel.ResetEvent(resume)
                process = self.start("v2-late")
                require(self.windows.kernel.WaitForSingleObject(ready, 30000) == 0, "Installer did not finish preflight")
                with self.windows.lock(self.destination / filename, share=share):
                    self.windows.kernel.SetEvent(resume)
                    code = process.wait(timeout=30)
                    require(code != 0, "Post-preflight conflict reported success: " + filename)
                    require(not (self.destination / "launch-marker.txt").exists(), "Failed installation invoked launcher")
                self.install()
                self.record("post-preflight lock on " + filename + " fails and rerunning repairs installation",
                            failure_exit_code=code)
            if self.args.interactive:
                self.install("v1")
                self.windows.kernel.ResetEvent(ready)
                self.windows.kernel.ResetEvent(resume)
                process = self.start("v2-late", silent=False)
                self.navigate(process)
                require(self.windows.kernel.WaitForSingleObject(ready, 30000) == 0, "Interactive preflight did not finish")
                with self.windows.lock(self.destination / "data/zz last.txt"):
                    self.windows.kernel.SetEvent(resume)
                    _, controls = wait_for(lambda: self.windows.dialog(process, "Error opening file"), "copy failure dialog")
                    require(not any("Ignore" in value for _, value in controls), "Copy failure offered Ignore")
                    labels = [value.replace("&", "").strip() for _, value in controls]
                    self.windows.click(controls, "Cancel" if "Cancel" in labels else "Abort")
                    dialogs = self.finish(process, success=False)
                    require(any("repair the installation" in value for value in dialogs), "Copy failure omitted repair advice")
                    require(not (self.destination / "launch-marker.txt").exists(), "Failed installation invoked launcher")
                self.record("interactive copy failure offers no skip or launch and explains repair")
        finally:
            self.windows.kernel.CloseHandle(ready)
            self.windows.kernel.CloseHandle(resume)
        if self.args.interactive:
            self.install()
            self.interactive()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--work", type=Path, required=True, help="New directory retained with packages and results")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--cpack", default="cpack")
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--interactive", action="store_true", help="Also exercise Retry/Cancel on the current desktop")
    args = parser.parse_args()
    require(sys.platform == "win32", "This integration test requires Windows")
    require(ctypes.windll.shell32.IsUserAnAdmin(), "Run from an elevated development shell")
    args.source = args.source.resolve()
    suite = Suite(args)
    try:
        suite.run()
    except Exception as error:
        suite.results.append({"status": "failed", "error": str(error)})
        (suite.root / "results.json").write_text(json.dumps(suite.results, indent=2), encoding="utf-8")
        raise
    finally:
        for process in suite.installers:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
    print("Results: " + str(suite.root / "results.json"))


if __name__ == "__main__":
    main()
