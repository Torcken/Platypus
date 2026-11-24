"""Reproducibility metadata and artifact checks for benchmark publication.

Completeness is distinct from proving that a remote endpoint runs the claimed binary. The
operator supplies serving/hardware facts; local artifacts and client versions are checked here.
"""
from __future__ import annotations

from functools import lru_cache
import hashlib
import math
from pathlib import Path
import re
from typing import Any

PINNED_COMMIT = "2d8a32502909ca065c41c4cf5f2fb1c427c047fc"
TEXT_FIELDS = (
    "model", "quantization", "hardware_tier", "hardware_description", "hardware_os",
    "cache_type_k", "cache_type_v", "server_build_backend", "server_version", "client_version",
)
HASH_FIELDS = ("model_file_sha256", "server_binary_sha256", "client_binary_sha256")
COMPARISON_FIELDS = (
    "model_file_sha256", "quantization", "hardware_tier", "hardware_description", "hardware_os",
    "context_size", "gpu_layers", "cache_type_k", "cache_type_v", "seed", "sampling",
    "llama_cpp_commit", "server_binary_sha256", "server_build_backend", "server_launch_args",
)


@lru_cache(maxsize=32)
def _hash_file(path: str, size: int, modified_ns: int, changed_ns: int, device: int, inode: int) -> str:
    """Cache a streaming digest only while the artifact's identity and timestamps match."""
    del size, modified_ns, changed_ns, device, inode
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_file(path: str | Path) -> str:
    """Hash a local artifact without reading model weights into memory all at once."""
    file = Path(path).resolve(strict=True)
    stat = file.stat()
    if not file.is_file():
        raise ValueError(f"Not a regular artifact file: {file}")
    value = _hash_file(str(file), stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns, stat.st_dev, stat.st_ino)
    after = file.stat()
    if (stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns, stat.st_dev, stat.st_ino) != (
            after.st_size, after.st_mtime_ns, after.st_ctime_ns, after.st_dev, after.st_ino):
        raise ValueError(f"Artifact changed while hashing: {file}")
    return value


def configuration_for_client(config: dict[str, Any], client: str) -> dict[str, Any]:
    """Resolve per-client executable/version overrides without sharing one version across clients."""
    resolved = {key: value for key, value in config.items() if key != "clients"}
    overrides = config.get("clients", {}).get(client, {})
    if not isinstance(overrides, dict):
        raise ValueError("Each clients entry must be an object")
    resolved.update(overrides)
    return resolved


def collect_artifacts(config: dict[str, Any], executable: str | None,
                      version: str | None) -> dict[str, Any]:
    """Check supplied artifact hashes; reject mismatches even for exploratory runs."""
    result = dict(config)
    evidence = {}
    paths = {"model_file": "model_file_sha256", "server_binary": "server_binary_sha256"}
    if executable:
        result["client_binary"] = executable
        paths["client_binary"] = "client_binary_sha256"
    for name, hash_name in paths.items():
        if not result.get(name):
            evidence[hash_name] = False
            continue
        actual = sha256_file(result[name])
        claimed = result.get(hash_name)
        if claimed and claimed != actual:
            raise ValueError(f"Artifact hash mismatch for {name}")
        result[hash_name] = actual
        evidence[hash_name] = True
    if version:
        claimed_version = result.get("client_version")
        if claimed_version and claimed_version != version:
            raise ValueError("Configured client_version differs from --version output")
        result["client_version"] = version
    evidence["client_version"] = bool(version)
    result["artifact_verification"] = evidence
    return result


def metadata_errors(config: dict[str, Any]) -> list[str]:
    """List missing/invalid protocol fields and unchecked local artifact provenance."""
    errors = []
    for field in TEXT_FIELDS:
        value = config.get(field)
        if (not isinstance(value, str) or not value.strip()
                or value.lower().startswith(("unverified", "unknown", "unspecified"))):
            errors.append(f"{field}: required non-placeholder text")
    for field in HASH_FIELDS:
        if not isinstance(config.get(field), str) or not re.fullmatch(r"[0-9a-f]{64}", config[field]):
            errors.append(f"{field}: required lowercase SHA-256")
        if config.get("artifact_verification", {}).get(field) is not True:
            errors.append(f"{field}: local artifact not checked")
    if config.get("artifact_verification", {}).get("client_version") is not True:
        errors.append("client_version: executable --version output not checked")
    if config.get("llama_cpp_tag") != "v1.0.0" or config.get("llama_cpp_commit") != PINNED_COMMIT:
        errors.append("llama.cpp tag/commit must match ADR-002")
    for field, minimum in (("context_size", 1), ("gpu_layers", 0)):
        if type(config.get(field)) is not int or config[field] < minimum:
            errors.append(f"{field}: required integer >= {minimum}")
    if type(config.get("seed")) is not int or config["seed"] < 0:
        errors.append("seed: required fixed nonnegative integer")
    sampling = config.get("sampling")
    if not isinstance(sampling, dict) or not sampling:
        errors.append("sampling: required nonempty settings object")
    else:
        temperature = sampling.get("temperature")
        if (type(temperature) not in (int, float) or not math.isfinite(temperature)
                or not 0 <= temperature <= 2):
            errors.append("sampling.temperature: required finite value in [0, 2]")
        for value in sampling.values():
            if type(value) in (float, int) and not math.isfinite(value):
                errors.append("sampling: settings must be finite")
    arguments = config.get("server_launch_args")
    if (not isinstance(arguments, list) or not arguments
            or any(not isinstance(item, str) for item in arguments)):
        errors.append("server_launch_args: required nonempty argv string list")
    return errors


def comparison_settings(record: dict[str, Any]) -> dict[str, Any]:
    """Return serving/model/hardware settings which must agree for a paired comparison."""
    config = record.get("config", {})
    return {name: config.get(name, record.get(name)) for name in COMPARISON_FIELDS}
