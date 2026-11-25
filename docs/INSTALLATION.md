# Installation

Platypus v1.0 targets Linux x86_64 and macOS arm64. Windows is not a supported v1.0 target.
The Linux source installer builds into `~/.local` and needs no root access:

```sh
./scripts/install-linux.sh
```

Install CMake 3.24+, Ninja, a C++20 compiler, libcurl, OpenSSL Crypto, SQLite development headers,
Git, Python 3.11+, and ripgrep before running it. Override the install path with
`PLATYPUS_INSTALL_PREFIX` or the build directory with `PLATYPUS_BUILD_DIR`. The script builds the
Platypus client and installs the checked-in catalog and profiles; it does not download model files
or install llama-server.

After install, add `~/.local/bin` to `PATH`, then inspect available models and hardware-aware
recommendations:

```sh
platypus models list
platypus models recommend --preference balanced
platypus models download ornith-1.0-9b-q4_k_m
```

For other environments, build from source using the commands in the README. The macOS build is
covered by CI, but a Homebrew formula and assisted-free macOS setup have not yet been validated.
Launch the pinned llama-server manually as described in ADR-002. Five-machine installation
acceptance, including AMD 6 GB, M1 16 GB, and Titan V hardware, is a human exit criterion and remains
open.
