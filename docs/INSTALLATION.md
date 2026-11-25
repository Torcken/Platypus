# Installation

Platypus v1.0 targets Linux x86_64 and macOS arm64. Windows is not a supported v1.0 target.
The Linux source installer builds into `~/.local` and needs no root access:

```sh
./scripts/install-linux.sh
```

Install CMake 3.24+, Ninja, a C++20 compiler, libcurl, OpenSSL Crypto, SQLite development headers,
Git, Python 3.11+, and ripgrep before running it. Override the install path with
`PLATYPUS_INSTALL_PREFIX` or the build directory with `PLATYPUS_BUILD_DIR`. The script builds the
Platypus client, installs the checked-in catalog and profiles, then builds the pinned llama-server
at the exact revision in ADR-002. The default backend is CPU. Set
`PLATYPUS_LLAMA_BACKEND=cuda` with a CUDA toolkit or `PLATYPUS_LLAMA_BACKEND=vulkan` with Vulkan
development files before running the installer. These builds require network access to fetch the
pinned llama.cpp source. `PLATYPUS_BUILD_JOBS` controls server build parallelism (default `2`).
The installer does not download model files.

After install, add `~/.local/bin` to `PATH`, then inspect available models and hardware-aware
recommendations:

```sh
platypus setup
platypus models list
platypus models recommend --preference balanced
platypus models download ornith-1.0-9b-q4_k_m
```

For other environments, build from source using the commands in the README. The macOS build is
covered by CI, but a Homebrew formula and assisted-free macOS setup have not yet been validated.
Launch the pinned llama-server manually as described in ADR-002. Five-machine installation
acceptance, including AMD 6 GB, M1 16 GB, and Titan V hardware, is a human exit criterion and remains
open.
