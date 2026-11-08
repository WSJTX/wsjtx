# Windows installer preflight regression

This fixture packages a small executable, DLL, and several ordinary files with
the production NSIS template. It is separate from the launcher-configuration
test because it executes installers and checks Windows file-sharing behavior.
It does not need the full application dependencies.

From an elevated Windows development shell with CMake, CPack, Ninja, a C
compiler, NSIS, and native Python on PATH:

```powershell
python tests/release/test_nsis_preflight.py --work D:/Development/nsis-preflight-run --interactive
```

The work directory must not exist. The runner retains the fixture installers,
build logs, isolated installation, and `results.json` there. Each run uses a
unique registration key and Start menu folder. It never installs over WSJT-X
or touches its settings. To remove the fixture after inspection, run its
`Uninstall.exe /S` and remove that run's work directory.

The default suite checks fresh installation and upgrade in a path containing
spaces and non-ASCII characters; locks on a running executable, loaded DLL,
ordinary files, uninstaller, and obsolete executable; read-only and denied-write
files; directory collisions; and an unrelated running executable named
`wsjtx.exe`. Before each blocked attempt it records all existing destination
file hashes and attributes, directories, and the fixture's installation
registration in both registry views and hives, then requires an exact match
after failure. These comparisons are retained in `snapshot-*.json`. Releasing
each conflict must allow a successful upgrade. Files in place of the selected
installation directory or one of its ancestors are also rejected.
Deletion-only checks allow a read-only obsolete executable or one denying
content writes to be removed. A blocked upgrade must retain its original
attributes. Silent preflight failures must retain a Unicode diagnostic log
with the blocked path and Windows error, including failures during initialization.
The runner gives its installers a private temporary directory under the work
directory and retains these logs in `installer-temp/WSJT-X-installer`.

The late-conflict fixture uses CPack's existing preinstall hook to signal a
named event after preflight and wait for the runner. In separate attempts, the
runner locks an ordinary payload file, the uninstaller, and the obsolete
executable before releasing the installer. These check that copying cannot
skip a failure and that failed uninstaller creation or obsolete-file deletion
cannot return success. Those failures may leave mixed payload versions;
rerunning after releasing each lock must complete the upgrade. The event hook
exists only in this fixture.

`--interactive` additionally drives only windows owned by the fixture installer
process. It checks that the preflight dialog names the file, offers Retry and
Cancel without Ignore, and does not show repair advice after cancellation.
It releases the first lock, locks an earlier file, and chooses Retry to verify
the entire preflight runs again. Releasing the second lock must allow completion.
Access failures must show their Windows error instead of claiming the file is
in use. A post-preflight copy failure must omit Ignore, show repair advice,
and suppress finish-page launch. A fixture launcher writes a marker when the
production template calls it, so the test can check launch occurs only after
successful completion without starting a long-running application.
Run this option in an accessible Windows desktop session.

The platform-independent `test_nsis_preflight_manifest` CTest checks generated
manifest coverage, deterministic output, filename escaping, and generation
failures. The Windows suite is intentionally explicit rather than registered
in ordinary CTest runs because it requires elevation and optionally a desktop.
Full-package WSJT-X upgrade and finish-page launch behavior still require
separate release validation; this fixture does not establish those results.
