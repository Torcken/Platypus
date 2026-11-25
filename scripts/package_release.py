#!/usr/bin/env python3
"""Create a versioned, checksummed Platypus release archive."""

from __future__ import annotations

import hashlib
import sys
import tarfile
from pathlib import Path

TARGETS = {
    "linux-x86_64-cpu",
    "linux-x86_64-vulkan",
    "linux-x86_64-cuda12",
    "linux-x86_64-cuda13",
    "macos-arm64-cpu",
    "macos-arm64-metal",
}
CONTENTS = (
    "bin/platypus",
    "bin/llama-server",
    "libexec/platypus/update.py",
    "share/platypus/install-target",
    "share/platypus/profiles",
)


def package(target: str, prefix: Path, output_dir: Path) -> Path:
    """Package an installed prefix for one supported OS/backend target."""
    if target not in TARGETS:
        raise ValueError(f"Unsupported release target: {target}")
    marker = prefix / "share/platypus/install-target"
    if not marker.is_file() or marker.read_text(encoding="utf-8").strip() != target:
        raise ValueError("Installed backend marker does not match the requested release target")
    for relative in CONTENTS:
        path = prefix / relative
        if not path.exists() or path.is_symlink():
            raise ValueError(f"Missing installed release file: {relative}")
    if any(not ((prefix / relative).stat().st_mode & 0o111) for relative in CONTENTS[:2]):
        raise ValueError("Installed Platypus executables must be executable")
    profiles = prefix / "share/platypus/profiles"
    if any(path.is_symlink() for path in profiles.rglob("*")):
        raise ValueError("Installed profiles contain a symlink")

    output_dir.mkdir(parents=True, exist_ok=True)
    archive = output_dir / f"platypus-{target}.tar.gz"
    with tarfile.open(archive, "w:gz", format=tarfile.PAX_FORMAT) as stream:
        for relative in CONTENTS:
            source = prefix / relative
            stream.add(source, arcname=relative, recursive=True, filter=_stable_metadata)
    hasher = hashlib.sha256()
    with archive.open("rb") as source:
        while block := source.read(1024 * 1024):
            hasher.update(block)
    (output_dir / "SHA256SUMS").write_text(
        f"{hasher.hexdigest()}  {archive.name}\n", encoding="ascii"
    )
    return archive


def _stable_metadata(member: tarfile.TarInfo) -> tarfile.TarInfo:
    member.uid = member.gid = 0
    member.uname = member.gname = ""
    member.mtime = 0
    return member


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print("Usage: package_release.py TARGET INSTALL_PREFIX OUTPUT_DIR", file=sys.stderr)
        return 2
    try:
        print(package(argv[1], Path(argv[2]), Path(argv[3])))
    except (OSError, ValueError, tarfile.TarError) as error:
        print(f"Release packaging failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
