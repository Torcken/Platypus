#!/usr/bin/env python3
"""Fetch and safely install a Platypus GitHub Release update."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request
from pathlib import Path, PurePosixPath
from urllib.parse import urlsplit

REPOSITORY = "Torcken/Platypus"
API_ROOT = f"https://api.github.com/repos/{REPOSITORY}"
MAX_ARCHIVE_BYTES = 1024 * 1024 * 1024
MAX_EXTRACTED_BYTES = 2 * 1024 * 1024 * 1024
MAX_ARCHIVE_MEMBERS = 4096
TARGETS = {
    "linux-x86_64-cpu",
    "linux-x86_64-vulkan",
    "linux-x86_64-cuda12",
    "linux-x86_64-cuda13",
    "macos-arm64-cpu",
    "macos-arm64-metal",
}


class RollbackError(RuntimeError):
    """Restoration failed; the staging directory must be retained for recovery."""


class GithubRedirectHandler(urllib.request.HTTPRedirectHandler):
    """Allow only HTTPS GitHub asset redirects and drop API credentials cross-host."""

    def redirect_request(self, request, response, code, message, headers, new_url):
        target = urlsplit(new_url)
        target_host = (target.hostname or "").lower()
        if target.scheme != "https" or not (
            target_host == "api.github.com" or target_host.endswith(".githubusercontent.com")
        ):
            raise urllib.error.URLError("GitHub asset redirected to an untrusted host")
        redirected = super().redirect_request(request, response, code, message, headers, new_url)
        if redirected and target_host != (urlsplit(request.full_url).hostname or "").lower():
            redirected.remove_header("Authorization")
        return redirected


def github_token() -> str | None:
    """Read an explicit token or the current GitHub CLI credential without displaying it."""
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if token:
        return token.strip() or None
    gh = shutil.which("gh")
    if not gh:
        return None
    try:
        result = subprocess.run(
            [gh, "auth", "token", "--hostname", "github.com"],
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    return result.stdout.strip() if result.returncode == 0 and result.stdout.strip() else None


def app_environment() -> dict[str, str]:
    """Keep private release credentials out of the application's child environment."""
    environment = os.environ.copy()
    environment.pop("GH_TOKEN", None)
    environment.pop("GITHUB_TOKEN", None)
    return environment


def request_bytes(url: str, token: str | None, accept: str, limit: int) -> bytes:
    headers = {
        "Accept": accept,
        "User-Agent": "Platypus-updater",
        "X-GitHub-Api-Version": "2026-03-10",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, headers=headers)
    opener = urllib.request.build_opener(GithubRedirectHandler())
    with opener.open(request, timeout=45) as response:
        body = response.read(limit + 1)
    if len(body) > limit:
        raise ValueError("GitHub response exceeds the updater size limit")
    return body


def release_asset_url(asset: dict[str, object]) -> str:
    """Accept only asset endpoints for this repository before attaching credentials."""
    url = asset.get("url")
    if not isinstance(url, str):
        raise ValueError("Release asset has no API download URL")
    parsed = urlsplit(url)
    prefix = f"/repos/{REPOSITORY}/releases/assets/"
    asset_id = parsed.path.removeprefix(prefix)
    if (
        parsed.scheme != "https"
        or parsed.hostname != "api.github.com"
        or parsed.username
        or parsed.password
        or parsed.port not in (None, 443)
        or parsed.query
        or parsed.fragment
        or not asset_id.isdigit()
    ):
        raise ValueError("Release asset URL is not a trusted GitHub endpoint")
    return url


def release_target() -> str:
    """Return the exact build target recorded by the installer."""
    if sys.platform == "darwin" and os.uname().machine == "arm64":
        expected_prefix = "macos-arm64-"
    elif sys.platform.startswith("linux") and os.uname().machine == "x86_64":
        expected_prefix = "linux-x86_64-"
    else:
        raise ValueError("Updates are supported only on Linux x86_64 and macOS arm64")
    prefix = Path(__file__).resolve().parents[2]
    marker = prefix / "share/platypus/install-target"
    try:
        target = marker.read_text(encoding="ascii").strip()
    except OSError as error:
        raise ValueError(
            "Cannot read install-target; reinstall Platypus before updating"
        ) from error
    if target not in TARGETS or not target.startswith(expected_prefix):
        raise ValueError(f"Unsupported installed release target: {target or '(empty)'}")
    return target


def checksum_for(checksums: bytes, filename: str) -> str:
    """Extract one unambiguous lowercase SHA-256 entry from SHA256SUMS."""
    try:
        text = checksums.decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("Release checksums are not ASCII") from error
    matches = re.findall(r"^([0-9a-fA-F]{64})[ \t]+\*?([^\r\n]+)$", text, re.MULTILINE)
    selected = [digest.lower() for digest, name in matches if name.strip() == filename]
    if len(selected) != 1:
        raise ValueError(f"Release checksums do not contain exactly one entry for {filename}")
    return selected[0]


def _validated_members(archive: tarfile.TarFile) -> list[tarfile.TarInfo]:
    members = []
    for member in archive:
        members.append(member)
        if len(members) > MAX_ARCHIVE_MEMBERS:
            raise ValueError("Release archive contains too many entries")
    required = {
        "bin/platypus",
        "bin/llama-server",
        "libexec/platypus/update.py",
        "share/platypus/install-target",
    }
    total = 0
    seen: set[str] = set()
    for member in members:
        path = PurePosixPath(member.name)
        normalized = path.as_posix().rstrip("/")
        if path.is_absolute() or ".." in path.parts or normalized in seen:
            raise ValueError("Release archive contains an unsafe or duplicate path")
        seen.add(normalized)
        allowed = (
            normalized in required
            or normalized == "libexec/platypus"
            or normalized == "share/platypus/profiles"
            or normalized.startswith("share/platypus/profiles/")
        )
        if not allowed or not (member.isfile() or member.isdir()):
            raise ValueError(f"Release archive contains an unexpected entry: {member.name}")
        total += member.size
        if total > MAX_EXTRACTED_BYTES:
            raise ValueError("Release archive exceeds the extraction size limit")
    if not required.issubset(seen) or "share/platypus/profiles/catalog.json" not in seen:
        raise ValueError("Release archive is missing required Platypus files")
    return members


def extract_release(archive_path: Path, destination: Path, target: str) -> None:
    """Extract only regular files and directories from the expected release layout."""
    with tarfile.open(archive_path, "r:gz") as archive:
        members = _validated_members(archive)
        for member in members:
            relative = PurePosixPath(member.name)
            output = destination.joinpath(*relative.parts)
            if member.isdir():
                output.mkdir(parents=True, exist_ok=True)
                continue
            output.parent.mkdir(parents=True, exist_ok=True)
            source = archive.extractfile(member)
            if source is None:
                raise ValueError("Cannot read a release archive member")
            with source, output.open("xb") as file:
                shutil.copyfileobj(source, file, length=1024 * 1024)
            output.chmod(member.mode & 0o755)
    marker = destination / "share/platypus/install-target"
    if marker.read_text(encoding="ascii").strip() != target:
        raise ValueError("Release archive backend does not match this installation")


def install_transaction(prefix: Path, staged: Path, version: str) -> None:
    """Replace managed application files and roll back if installation or startup fails."""
    destinations = [
        Path("bin/platypus"),
        Path("bin/llama-server"),
        Path("libexec/platypus/update.py"),
        Path("share/platypus/install-target"),
    ]
    staged_profiles = staged / "share/platypus/profiles"
    if not staged_profiles.is_dir():
        raise ValueError("Release is missing model profiles")
    destinations.extend(
        Path("share/platypus/profiles") / path.relative_to(staged_profiles)
        for path in sorted(staged_profiles.rglob("*"))
        if path.is_file() and not path.is_symlink()
    )
    backup = staged / "backup"
    backup.mkdir()
    changed: list[tuple[Path, Path | None]] = []
    try:
        for relative in destinations:
            source = staged / relative
            destination = prefix / relative
            current = destination
            while current != prefix:
                if current.is_symlink():
                    raise ValueError(f"Refusing to update through symlink: {current}")
                current = current.parent
            if not source.exists():
                raise ValueError(f"Release is missing: {relative}")
            destination.parent.mkdir(parents=True, exist_ok=True)
            previous = backup / relative
            if destination.exists():
                previous.parent.mkdir(parents=True, exist_ok=True)
                os.replace(destination, previous)
            else:
                previous = None
            changed.append((destination, previous))
            os.replace(source, destination)
        check = subprocess.run(
            [str(prefix / "bin/platypus"), "--version"],
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
            env=app_environment(),
        )
        if check.returncode != 0 or check.stdout.strip() != version:
            raise ValueError("Updated Platypus failed its version startup check")
    except BaseException as original:
        failures = []
        for destination, previous in reversed(changed):
            try:
                if previous is not None:
                    # Atomic replacement also preserves the backup if restoration fails.
                    os.replace(previous, destination)
                else:
                    destination.unlink(missing_ok=True)
            except OSError as error:
                failures.append(f"{destination}: {error}")
        if failures:
            raise RollbackError(
                f"Update failed ({original}); recovery files retained at {backup}: "
                + "; ".join(failures)
            ) from original
        raise


def update() -> int:
    """Find, verify, and install the latest release for this installation target."""
    token = github_token()
    headers = "application/vnd.github+json"
    try:
        prefix = Path(__file__).resolve().parents[2]
        target = release_target()
        current = subprocess.run(
            [str(prefix / "bin/platypus"), "--version"],
            check=True,
            capture_output=True,
            text=True,
            timeout=10,
            env=app_environment(),
        ).stdout.strip()
        release_body = request_bytes(
            f"{API_ROOT}/releases/latest", token, headers, 1024 * 1024
        )
        release = json.loads(release_body)
        tag = release.get("tag_name", "")
        match = re.fullmatch(r"v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", tag)
        if not match:
            raise ValueError("Latest release has an invalid semantic version tag")
        version = tag.removeprefix("v")
        if version == current:
            print(f"Platypus {current} is already up to date.")
            return 0
        assets = {}
        for asset in release.get("assets", []):
            name = asset.get("name")
            if name in assets:
                raise ValueError(f"Latest release has duplicate asset names: {name}")
            assets[name] = asset
        filename = f"platypus-{target}.tar.gz"
        if filename not in assets or "SHA256SUMS" not in assets:
            raise ValueError(f"Latest release does not provide {target}")
        checksum_asset = assets["SHA256SUMS"]
        checksum_body = request_bytes(release_asset_url(checksum_asset), token,
                                      "application/octet-stream", 1024 * 1024)
        expected = checksum_for(checksum_body, filename)
        archive_asset = assets[filename]
        api_digest = archive_asset.get("digest", "")
        if api_digest and api_digest != f"sha256:{expected}":
            raise ValueError("Release checksum disagrees with GitHub's asset digest")
        with tempfile.TemporaryDirectory(prefix="platypus-update-") as temporary:
            temp = Path(temporary)
            archive_path = temp / filename
            request = urllib.request.Request(
                release_asset_url(archive_asset),
                headers={
                    "Accept": "application/octet-stream",
                    "User-Agent": "Platypus-updater",
                    "X-GitHub-Api-Version": "2026-03-10",
                    **({"Authorization": f"Bearer {token}"} if token else {}),
                },
            )
            opener = urllib.request.build_opener(GithubRedirectHandler())
            digest = hashlib.sha256()
            size = 0
            with opener.open(request, timeout=45) as response, archive_path.open("xb") as output:
                while block := response.read(1024 * 1024):
                    size += len(block)
                    if size > MAX_ARCHIVE_BYTES:
                        raise ValueError("Release archive exceeds the download size limit")
                    digest.update(block)
                    output.write(block)
            if digest.hexdigest() != expected:
                raise ValueError("Release archive SHA-256 does not match SHA256SUMS")
            prefix.mkdir(parents=True, exist_ok=True)
            staging = Path(tempfile.mkdtemp(prefix=".platypus-update-", dir=prefix))
            preserve_recovery = False
            try:
                extract_release(archive_path, staging, target)
                install_transaction(prefix, staging, version)
            except RollbackError:
                preserve_recovery = True
                raise
            finally:
                if not preserve_recovery:
                    shutil.rmtree(staging, ignore_errors=True)
        print(f"Updated Platypus {current} to {version} ({target}).")
        return 0
    except urllib.error.HTTPError as error:
        detail = f"GitHub returned HTTP {error.code}"
        if error.code in (401, 403, 404):
            detail += "; check GH_TOKEN or `gh auth login` and repository read access"
        print(f"Platypus update failed: {detail}", file=sys.stderr)
        return 1
    except (OSError, KeyError, ValueError, RollbackError, urllib.error.URLError,
            subprocess.SubprocessError, tarfile.TarError) as error:
        detail = str(error).strip() or error.__class__.__name__
        if not token and "HTTP Error 404" in detail:
            detail += "; confirm private repository access with GH_TOKEN or `gh auth login`"
        print(f"Platypus update failed: {detail}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(update())
