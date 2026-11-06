<p align="center">
  <img src="assets/appicon/mentis-128.png" alt="Mentis" width="72">
</p>

<h1 align="center">Building Mentis</h1>

<p align="center"><a href="README.md">← Back to the README</a></p>

Mentis is a C++20 application built with CMake and Qt 6 Widgets. It runs on Windows, macOS and Linux.

## Requirements

- **Qt 6.8 or newer**, with the modules Widgets, Sql (SQLite driver), Concurrent, Svg, PrintSupport,
  Network and the Linguist tools (for the translations).
- **CMake 3.24 or newer** and, preferably, **Ninja**.
- A **C++20 compiler**: MSVC 2022 or MinGW 64-bit on Windows, Xcode Command Line Tools on macOS,
  GCC or Clang on Linux.

The simplest way to get a consistent setup on all three systems is the
[Qt Online Installer](https://www.qt.io/download-qt-installer-oss): install Qt 6.8 (or newer) for
your compiler, CMake, Ninja and, if you like an IDE, Qt Creator.

## With Qt Creator

Open `CMakeLists.txt`, choose a Desktop kit and press Run. Qt Creator configures CMake by itself.
`CMakePresets.json` offers three configurations:

| Preset | What it builds |
|---|---|
| `debug` | the app only, in Debug (fastest for day-to-day work) |
| `debug-tests` | the app and the tests, in Debug |
| `release` | the app and the tests, optimised |

## From the command line

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

Without `CMAKE_BUILD_TYPE` the build is `Release`. If CMake does not find Qt, pass the path of your
Qt kit:

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="/path/to/Qt/6.x.x/<kit>"
```

Then start `build/Mentis` (`build\Mentis.exe` on Windows).

### Linux packages

Distribution packages work if they provide Qt 6.8 or newer (check with your package manager;
some long-term releases ship older versions). On Debian and Ubuntu the packages are usually
`build-essential cmake ninja-build qt6-base-dev qt6-svg-dev qt6-tools-dev qt6-l10n-tools
libqt6sql6-sqlite`.

### macOS

On Apple Silicon choose the arm64 kit.

## Tests

```bash
ctest --test-dir build -j 8 --output-on-failure
```

The tests need a display; on a machine without one set `QT_QPA_PLATFORM=offscreen`. Tests are
grouped by area (`ctest -L map`, `ctest -L core`, ...). A few slow tests with thousands of topics run
only with `MENTIS_SLOW_TESTS=1`.

## Installing

```bash
cmake --install build --prefix <folder>
```

copies the app with the Qt libraries and plugins it needs into `<folder>`.

### Windows installer

The releases' installer is built from that folder with [Inno Setup 6](https://jrsoftware.org/isinfo.php):

```bash
cmake --install build --config Release --prefix stage
ISCC /DAppVersion=X.Y.Z /DStageDir=%CD%\stage installer\mentis.iss
```

It writes `dist/Mentis-X.Y.Z-setup-x64.exe`, a per-user installer (no administrator rights). The
GitHub release workflow (`.github/workflows/release.yml`) does the same for every `vX.Y.Z` tag.
