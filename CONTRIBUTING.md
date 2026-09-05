# Contributing to WSJT-X

WSJT-X, MAP65, and QMAP are open-source applications for weak-signal amateur
radio communication. Contributions to the programs and their documentation are
welcome.

## Choose the correct repository

WSJT-X development uses two repositories with different purposes:

- The public [WSJTX/wsjtx](https://github.com/WSJTX/wsjtx) repository is
  updated when a general-availability release is published. External
  contributors should fork this repository and base pull requests on its
  `master` branch.
- The core team develops and tests future releases in a private repository.
  Its current source can be newer than the public repository and is not yet
  distributed or supported as a release.

A public clone therefore contains the latest published source, not a live copy
of the private development branch. A core team member will port an accepted
external contribution into the private repository for integration and CI
testing.

## Build and test your change

Read the [source-build guide](doc/user_guide/en/install-from-source.adoc) before
setting up a toolchain. It is the maintained source for dependencies and build
commands; build recipes are intentionally not duplicated here.

WSJT-X is a mixed C++, C, and Fortran project built with CMake and Qt 5. A
normal developer cycle uses an out-of-source build directory:

```bash
cmake -S . -B build  # Add the options from the platform guide.
cmake --build build
ctest --test-dir build --output-on-failure
```

Reconfigure after changing CMake files or pulling changes that affect
dependencies. Use a fresh build directory after changing compiler, Qt
installation, MSYS2 runtime, or other foundational toolchain components;
CMake intentionally caches those choices.

A local developer build shows whether a change compiles and passes tests on
your machine.

## Report a bug

File public bug reports in the
[issue tracker](https://github.com/WSJTX/wsjtx/issues). Include:

- the WSJT-X version, operating system, and architecture;
- exact steps to reproduce the problem;
- expected and actual behavior;
- relevant logs or screenshots; and
- whether the problem occurs in an installed build or only in a build tree.

A team member will move the investigation into the private repository when it
requires unreleased development work.

## Submit a pull request

1. Fork `WSJTX/wsjtx` and create a branch from public `master`.
2. Make one logical change and test it on the affected platform.
3. Push the branch to your fork and open a pull request against
   `WSJTX/wsjtx:master`.
4. Explain what the change does, why it is needed, which platforms and tests
   you used, and any related issue numbers.

Merging an external contribution directly into public `master` would put the
public release snapshot out of sync with internal development. Instead, the
team reviews it publicly, integrates it privately, and publishes it with a
subsequent release.

## Follow project conventions

- Match the naming and formatting already used in the files you change.
- C++ code generally uses two-space indentation, `PascalCase` class names, and
  `camelCase` method names. Preserve local conventions when a file differs.
- Preserve the established Fortran style in the signal-processing and codec
  libraries.
- Write comments for non-obvious constraints or behavior, not to narrate code
  that is already clear from its names and structure.
- Add the appropriate GPL-3.0 license header to new source files.

## Keep changes reviewable

- Keep each pull request to one logical change.
- Add or update tests when behavior changes.
- Test decoding changes with known sample files and report the cases used.
- Avoid broad formatting or cleanup mixed with a functional change.
