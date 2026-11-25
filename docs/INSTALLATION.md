# Installation

Platypus v1.0 targets Linux x86_64 and macOS arm64. Windows is not a supported v1.0 target.
Clone the source from [Torcken/Platypus](https://github.com/Torcken/Platypus). The Linux source
installer builds into `~/.local` and needs no root access:

```sh
git clone https://github.com/Torcken/Platypus.git
cd Platypus
./scripts/install-linux.sh
```

Install CMake 3.24+, Ninja, a C++20 compiler, libcurl, OpenSSL Crypto, SQLite development headers,
Git, Python 3.11+, and ripgrep before running it. Override the install path with
`PLATYPUS_INSTALL_PREFIX` or the build directory with `PLATYPUS_BUILD_DIR`. The script builds the
Platypus client, installs the checked-in catalog and profiles, then builds the pinned llama-server
at the exact revision in ADR-002. The server build automatically selects Metal on macOS, CUDA when
the CUDA toolkit and an NVIDIA GPU are available, Vulkan when its development tools and a Linux
render device are available, and CPU otherwise. On Linux, set `PLATYPUS_LLAMA_BACKEND=cpu`, `cuda`,
or `vulkan` to override detection; on macOS, use `metal` or `cpu`. These builds require network
access to fetch the pinned llama.cpp source. `PLATYPUS_BUILD_JOBS` controls server build parallelism
(default `2`).
The installer does not download model files.

On Apple silicon, use `scripts/install-macos.sh` from the checked-out repository. It uses the same
prefix and build-directory overrides, builds Platypus and the pinned llama-server with Metal, and
does not require root access. Install CMake 3.24+, Ninja, Xcode Command Line Tools, Git, and the
project's Homebrew development dependencies first. Because the repository is private during
development, contributors and beta testers need authorized GitHub access to clone it.

After install, add `~/.local/bin` to `PATH`, then inspect available models and hardware-aware
recommendations:

```sh
platypus setup
platypus models list
platypus models recommend --preference balanced
platypus models download ornith-1.0-9b-q4_k_m
```

Setup runs a read-only `README.md` summary task against the downloaded model before opening the
interactive session. If the sample task fails, it reports the failure and continues to the session.

For other environments, build from source using the commands in the README. A Homebrew formula and
assisted-free setup on real macOS hardware have not yet been validated.
Launch the pinned llama-server manually as described in ADR-002. Five-machine installation
acceptance, including AMD 6 GB, M1 16 GB, and Titan V hardware, is a human exit criterion and remains
open.

## Private beta updates

Version-tagged releases are private while the repository is private. Sign in with GitHub CLI on
each machine (`gh auth login --hostname github.com`) or provide a short-lived `GH_TOKEN` with access
to the repository's releases (a fine-grained token needs repository Contents read access), then run:

```sh
platypus update
```

The updater selects the installed OS/backend bundle, verifies its SHA-256, and updates the client,
server, updater, catalog, and profiles. It does not touch downloaded models or project files. Linux x86_64
supports CPU, Vulkan, CUDA 12, and CUDA 13 release bundles; macOS arm64 supports Metal. A local
macOS CPU-only source build is supported, but the private release channel currently publishes Metal
bundles only.
