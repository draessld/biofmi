# Installation

BioFMI is a C++17 project built with CMake. It depends on
[SDSL-lite](https://github.com/simongog/sdsl-lite) for the FM-index and on Boost
`program_options` for its command-line parsing. Everything else is vendored or
part of the toolchain.

---

## 1. Requirements

| Requirement | Minimum | Notes |
|---|---|---|
| C++ compiler | C++17 | GCC 9+ or Clang 10+; tested on GCC 13 |
| CMake | 3.10 | |
| SDSL-lite | 2.1.1 | Provides `csa_wt<>`, bit vectors, rank/select |
| divsufsort / divsufsort64 | — | Suffix-array construction; installed *by* SDSL |
| Boost | any recent | Only `program_options` is used |
| OpenMP | optional | Enables parallel sections where available |
| Python | 3.9+ | Only for the experiment harness, not for the tools |

`git` is required too, because EDSParser is a **submodule** rather than a
vendored copy.

---

## 2. Quickest route

```bash
git clone --recursive https://github.com/draessld/biofmi.git
cd biofmi
./INSTALL.sh
```

`INSTALL.sh` initialises submodules, configures a Release build under `build/`,
compiles, and reports what it found. Binaries land in `build/tools/`.

!!! warning "`--recursive` is not optional"
    `src/cpp/CMakeLists.txt` resolves EDSParser through the fixed relative path
    `external/edsparser/src/cpp/lib`. A sibling checkout elsewhere on disk will
    **not** be used, and a non-recursive clone fails at configure time with a
    missing-directory error rather than a helpful one. If you already cloned
    without it:

    ```bash
    git submodule update --init --recursive
    ```

---

## 3. Manual build

```bash
git submodule update --init --recursive
mkdir -p build && cd build
cmake ../src/cpp -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Outputs:

| Path | Contents |
|---|---|
| `build/tools/` | `biofmi-build`, `biofmi-locate`, and the test executables |
| `build/lib/` | the static library the tools link against |

Install to a prefix if you want the tools on `PATH`:

```bash
cmake --install . --prefix ~/.local
```

---

## 4. Installing SDSL-lite

SDSL is not on most distributions. Build it from source; its installer takes a
prefix and puts headers in `<prefix>/include` and libraries in `<prefix>/lib`:

```bash
git clone --depth 1 --branch v2.1.1 https://github.com/simongog/sdsl-lite.git
cd sdsl-lite
./install.sh "$HOME"          # -> ~/include/sdsl, ~/lib/libsdsl.a
```

`$HOME` is a deliberate choice: BioFMI's CMake searches `$ENV{HOME}/include` and
`$ENV{HOME}/lib` explicitly, alongside `/usr/local` and `/usr`, so an
install there is found with no extra flags and needs no root.

Installing divsufsort separately is unnecessary — SDSL's installer builds and
installs both `libdivsufsort` and `libdivsufsort64` from its own `external/`.

---

## 5. When CMake cannot find Boost

BioFMI finds SDSL through explicit search paths, but Boost through
`find_package(Boost REQUIRED COMPONENTS program_options)`, which searches
**system paths only**. If your Boost lives under `$HOME` — common on shared
machines and HPC nodes where you cannot install system packages — a plain
configure fails outright:

```
Could NOT find Boost (missing: Boost_INCLUDE_DIR program_options)
```

Point CMake at the prefix:

```bash
cmake ../src/cpp -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$HOME
```

On Debian/Ubuntu with root, the system package avoids the issue entirely:

```bash
sudo apt-get install -y build-essential cmake libboost-program-options-dev
```

---

## 6. Verifying the build

Do not skip this. A BioFMI built against a mismatched EDSParser can produce a
*well-formed but wrong* index without erroring, and the test suite is what
catches that.

```bash
cd build
ctest --output-on-failure
```

Expect **9 tests, all passing**, in roughly 17 seconds. `test_locate_fuzz`
accounts for about 15 of those; skip it during a tight edit loop with
`ctest -E fuzz`.

EDSParser has its own suite, registered in a subdirectory:

```bash
cd external/edsparser/build/src/cpp && ctest --output-on-failure   # 7 tests
cd external/edsparser && for t in tests/e2e/test_*.sh; do bash "$t"; done   # 9 suites
```

!!! danger "Release builds and `assert()`"
    The default build type is Release, which defines `NDEBUG` and compiles every
    `assert()` to nothing. The test targets therefore add `-UNDEBUG` explicitly.
    Do not remove it: before it was added, the suite ran, printed `PASSED`, and
    verified nothing.

---

## 7. EDSParser tools

Index *building* needs an l-EDS, and producing one is EDSParser's job. Its tools
are built alongside BioFMI:

```bash
cd external/edsparser
mkdir -p build && cd build
cmake ../src/cpp -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
```

This gives `msa2eds`, `vcf2eds`, `eds2leds`, `edsparser-stats`,
`edsparser-genpatterns`, `genrandomeds` and `edsparser-source-transform` in
`external/edsparser/build/tools/`.

!!! tip "Check tool provenance before trusting a result"
    EDSParser tools carry a build stamp:

    ```bash
    eds2leds --version    # COMMIT=<sha> COMMIT_DATE=<iso8601> DIRTY=<0|1>
    ```

    This exists because an installed `eds2leds` predating the 2026-08-04
    complement fix silently emitted l-EDS containing strings no genome carries.
    Prefer `build/tools/` over anything on `PATH`, and never publish a
    measurement taken with `DIRTY=1`.
