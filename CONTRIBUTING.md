# Contributing to WSJT-X

## Overview

**WSJT-X** and its sister programs **MAP65** and **QMAP** are open-source applications designed to facilitate weak-signsl ham radio communication. The project was started in 2001 and became an open-source effort in 2005. Our core development team has grown slowly over time, and in May 2026 consists of seven members. 

We welcome contributions to these programs from outside the core development team. This document provides a brief summary of how the WSJT-X GitHub repositories are organized, how the development team operates, and how outside contributions can be made.

## Repository Structure

We use two GitHub repositories. Public site https://github.com/WSJTX/wsjtx receives updated code whenever a general availability (GA) program version is released. Anyone can submit  [Issues](https://github.com/WSJTX/wsjtx/issues) to be raised and discussed, as well as Bug Reports and [Pull Requests](https://github.com/WSJTX/wsjtx/pulls). External contributors should fork the public repo and submit pull requests based on that code.

Development work by team members, including acceptance testing of outside pull requests, takes place in a private repository. At any particular time team members may be developing or experimenting with program features that are not yet ready for distribution or general use.

## Building from Source
Source code for WSJT-X, MAP65, and QMAP together amounts to more than 130,000 lines of C++, 75,000 lines of Fortran, and 18,000 lines of C. User interfaces use the Qt framework and are mostly written in C++, while signal processing and message encoding/decoding are done in Fortran. Required external libraries include FFTW3, Boost, libUSB, and Hamlib. The applications and related utility programs are multi-platform and can be built and used on the Windows, macOS, and Linux. We use CMake and Qt5, and we build Hamlib locally before building WSJT-X.

### Prerequisites

**All platforms:**
- CMake 3.16+
- Qt 5.12+ (Core, Widgets, Multimedia, SerialPort, Network, Sql, LinguistTools)
- FFTW 3 (single precision — `libfftw3f`)
- Boost C++ libraries
- Compilers for C++ and FORTRAN, typically gcc and gfortran
- Git

**Linux (Debian/Ubuntu):**
```
sudo apt install build-essential cmake gfortran \
  qtbase5-dev qttools5-dev qtmultimedia5-dev libqt5serialport5-dev \
  libfftw3-dev libboost-all-dev libusb-1.0-0-dev libudev-dev \
  autoconf automake libtool pkg-config texinfo
```

**macOS:**
```
brew install cmake gcc qt@5 fftw boost libusb autoconf automake libtool pkg-config texinfo
```
Xcode command-line tools are also required (`xcode-select --install`).

**Windows:**
Windows builds use the [Hamlib SDK](https://sourceforge.net/projects/hamlib-sdk/), which provides all prerequisite libraries and MinGW tooling. See the INSTALL file for detailed Windows instructions.

### Building Hamlib

WSJT-X requires a specific Hamlib version. Check `ci.yml` in `.github/workflows/` for the current `hamlib_branch` value — this is what CI builds against and what your local build should match.

```bash
# Replace HAMLIB_BRANCH with the current value from ci.yml (e.g., "4.7.1"):
HAMLIB_BRANCH="4.7.1"

mkdir -p ~/hamlib-prefix/build
cd ~/hamlib-prefix
git clone https://github.com/Hamlib/Hamlib src
cd src
git checkout $HAMLIB_BRANCH
./bootstrap
mkdir ../build && cd ../build
../src/configure --prefix=$HOME/hamlib-prefix \
  --disable-shared --enable-static \
  --without-cxx-binding --disable-winradio \
  CFLAGS="-g -O2 -fdata-sections -ffunction-sections" \
  LDFLAGS="-Wl,--gc-sections"
make
make install-strip
```

### Building WSJT-X

**Team members** (with access to wsjtx-internal):
```bash
git clone https://github.com/WSJTX/wsjtx-internal.git ~/wsjtx-prefix/src
```

**External contributors** (fork the public repo first):
```bash
git clone https://github.com/YOUR_USERNAME/wsjtx.git ~/wsjtx-prefix/src
cd ~/wsjtx-prefix/src
git remote add upstream https://github.com/WSJTX/wsjtx.git
```

Then build:
```bash
mkdir -p ~/wsjtx-prefix/build && cd ~/wsjtx-prefix/build
cmake -DCMAKE_PREFIX_PATH="$HOME/hamlib-prefix" ../src
cmake --build .
```

On macOS, add Qt5 and other Homebrew paths to `CMAKE_PREFIX_PATH`:
```bash
cmake -DCMAKE_PREFIX_PATH="$HOME/hamlib-prefix;$(brew --prefix qt@5);$(brew --prefix fftw);$(brew --prefix boost)" \
  -DCMAKE_Fortran_COMPILER=$(brew --prefix gcc)/bin/gfortran ../src
```

### Updating and Rebuilding

```bash
# Update Hamlib
cd ~/hamlib-prefix/src && git pull
cd ~/hamlib-prefix/build && make && make install-strip

# Update WSJT-X
cd ~/wsjtx-prefix/src && git pull
cd ~/wsjtx-prefix/build && cmake --build .
```
## Bug Reports

Users and external contributors should file bug reports as Issues on the [public repo](https://github.com/WSJTX/wsjtx/issues). A team member will forward the Issue to `wsjtx-internal` if triage reveals development work to be done.

When filing an Issue, please include:

- WSJT-X version and operating system
- Steps to reproduce
- Expected vs. actual behavior
- Relevant log output or screenshots

## Pull Requests

### Basic steps for external contributors

1. Fork `WSJTX/wsjtx` on GitHub.

2. Create a branch, make your changes, push to your fork.

3. Open a PR from your fork to `WSJTX/wsjtx` targeting `master`.

4. A team member reviews the PR. If accepted, they port the change to wsjtx-internal where it enters the normal development flow and CI validation.

5. The change reaches the public repo at the next tagged release.

This indirection exists because the public repo only receives code at release time. Merging directly to `master` would put it out of sync with internal development.

### General guidelines

- **One PR per logical change.** Don't bundle unrelated fixes.
- **Test your changes.** Build on your platform and verify the application runs. If your change affects decoding, test with known `.wav` files.
- **Include in the PR description:** what the change does, why, which platforms you tested on, and related issue numbers.
- **Be patient.** The core developers are volunteers with other commitments. PRs may take days or weeks to review.

### Coding Conventions

- **C++:** Qt-style naming. Classes use `PascalCase`, methods use `camelCase`. Header guards use `FILENAME_HPP_` format.
- **Fortran:** Traditional Fortran style. Signal processing and codec code lives in `lib/`.
- **Indentation:** 2 spaces in C++, standard Fortran indentation in `.f90` files.
- **Comments:** Descriptive block comments above classes and functions. Inline comments where logic is non-obvious.
- **License:** All source files are GPL-3.0. New files should include the appropriate license header.

