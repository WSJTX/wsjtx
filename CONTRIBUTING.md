# Contributing to WSJT-X

How to build from source, submit changes, and work with the team.

## Building from Source

WSJT-X is a C++/Fortran application built with CMake and Qt5. It depends on Hamlib, which must be built from source before building WSJT-X.

### Prerequisites

**All platforms:**
- CMake 3.16+
- Qt 5.12+ (Core, Widgets, Multimedia, SerialPort, Network, Sql, LinguistTools)
- FFTW 3 (single precision — `libfftw3f`)
- Boost C++ libraries
- A Fortran compiler (gfortran)
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
cmake -DCMAKE_PREFIX_PATH="$HOME/hamlib-prefix" \
  -DWSJT_SKIP_MANPAGES=ON \
  -DWSJT_GENERATE_DOCS=OFF \
  ../src
cmake --build .
```

On macOS, add Qt5 and other Homebrew paths to `CMAKE_PREFIX_PATH`:
```bash
cmake -DCMAKE_PREFIX_PATH="$HOME/hamlib-prefix;$(brew --prefix qt@5);$(brew --prefix fftw);$(brew --prefix boost)" \
  -DCMAKE_Fortran_COMPILER=$(brew --prefix gcc)/bin/gfortran \
  -DWSJT_SKIP_MANPAGES=ON \
  -DWSJT_GENERATE_DOCS=OFF \
  ../src
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

## Repository Structure

| Repo | Branch | Purpose |
|------|--------|---------|
| `WSJTX/wsjtx-internal` (private) | `develop` | Active development |
| `WSJTX/wsjtx` (public) | `master` | Tagged releases only |

Development happens on `develop` in wsjtx-internal. The public repo receives code only at release time via the automated release pipeline. External contributors fork the public repo.

## Submitting Changes

### Team members

1. Create a branch from `develop`:
   - `feat-<description>` for new features
   - `fix-<description>` for bug fixes
   - `<issue-number>-<description>` if addressing a GitHub issue

2. Push your branch to wsjtx-internal and open a PR against `develop`.

3. CI runs automatically on the PR — it builds on macOS arm64, macOS x86_64, Linux x86_64, Linux aarch64, and Windows x86_64.

4. Another team member reviews. Merge when approved.

### External contributors

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

## Coding Conventions

Observed from existing code:

- **C++:** Qt-style naming. Classes use `PascalCase`, methods use `camelCase`. Header guards use `FILENAME_HPP_` format.
- **Fortran:** Traditional Fortran style. Signal processing and codec code lives in `lib/`.
- **Indentation:** 2 spaces in C++, standard Fortran indentation in `.f90` files.
- **Comments:** Descriptive block comments above classes and functions. Inline comments where logic is non-obvious.
- **License:** All source files are GPL-3.0. New files should include the appropriate license header.

## Reporting Bugs

Issues and development work are tracked on `wsjtx-internal` (a private repo for the core team). External contributors without `wsjtx-internal` access should file issues on the [public repo](https://github.com/WSJTX/wsjtx/issues); a team member will move the issue to `wsjtx-internal` if triage reveals development work to be done.

When filing, please include:

- WSJT-X version and operating system
- Steps to reproduce
- Expected vs. actual behavior
- Relevant log output or screenshots

## Communication

The development team communicates primarily via email. For GitHub-specific items (PRs, issues), use GitHub. For broader discussions about features, protocols, or project direction, email the group.

## License

WSJT-X is licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.html). By submitting a pull request, you agree that your contribution is licensed under the same terms.
