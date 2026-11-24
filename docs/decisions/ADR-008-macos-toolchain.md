# ADR-008: macOS C++20 toolchain

**Status:** accepted for CI configuration — 2026-11-24; execution unverified

Use Homebrew LLVM with libc++ on the macOS arm64 CI runner. Configure must still compile
an actual std::jthread/stop_token program; compiler version strings alone are insufficient.
Native AppleClang remains usable whenever that check passes. Do not introduce a threading shim
or silently disable cancellation. No macOS machine is available in this session, so the CI
configuration and library discovery paths require their first CI run before being called verified.
