"""Regressions for installer checkout and update recovery failures."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import update


class RecoveryTests(unittest.TestCase):
    def prepare(self, root):
        prefix, staged = root / "prefix", root / "staged"
        paths = ["bin/platypus", "bin/llama-server", "libexec/platypus/update.py",
                 "share/platypus/install-target", "share/platypus/profiles/catalog.json"]
        for base, content in [(prefix, "old"), (staged, "new")]:
            for path in paths:
                file = base / path
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_text(content)
        return prefix, staged

    def test_interrupt_restores_all_original_files(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix, staged = self.prepare(Path(directory))
            with patch.object(update.subprocess, "run", side_effect=KeyboardInterrupt):
                with self.assertRaises(KeyboardInterrupt):
                    update.install_transaction(prefix, staged, "1.2.3")
            self.assertEqual((prefix / "bin/platypus").read_text(), "old")
            self.assertEqual((prefix / "share/platypus/profiles/catalog.json").read_text(), "old")

    def test_restore_failure_preserves_backup_and_restores_other_files(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix, staged = self.prepare(Path(directory))
            original_replace = os.replace

            def replace(source, destination):
                if source == staged / "backup/bin/platypus":
                    raise PermissionError("injected restore failure")
                return original_replace(source, destination)

            with patch.object(update.os, "replace", side_effect=replace):
                with patch.object(update.subprocess, "run", side_effect=OSError("cannot start")):
                    with self.assertRaisesRegex(update.RollbackError, "recovery files retained"):
                        update.install_transaction(prefix, staged, "1.2.3")
            self.assertEqual((staged / "backup/bin/platypus").read_text(), "old")
            self.assertEqual((prefix / "bin/llama-server").read_text(), "old")


class CheckoutTests(unittest.TestCase):
    def test_fresh_clone_pins_revision_and_dirty_pinned_checkout_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            upstream = root / "upstream"
            upstream.mkdir()
            env = dict(os.environ, GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1",
                       GIT_AUTHOR_NAME="Test", GIT_AUTHOR_EMAIL="test@example.invalid",
                       GIT_COMMITTER_NAME="Test", GIT_COMMITTER_EMAIL="test@example.invalid")
            def git(*args):
                return subprocess.check_output(
                    ["git", "-C", str(upstream), *args],
                    env=env, stderr=subprocess.DEVNULL, text=True).strip()
            git("init")
            (upstream / "source.txt").write_text("pinned")
            git("add", ".")
            git("commit", "-m", "pinned")
            pin = git("rev-parse", "HEAD")
            (upstream / "source.txt").write_text("later")
            git("commit", "-am", "later")

            scripts = root / "scripts"
            scripts.mkdir()
            script = (ROOT / "scripts/build-llama-server.sh").read_text()
            script = script.replace("2d8a32502909ca065c41c4cf5f2fb1c427c047fc", pin)
            script = script.replace("https://github.com/ggml-org/llama.cpp.git", upstream.as_uri())
            (scripts / "build-llama-server.sh").write_text(script)
            (scripts / "server-backend-selection.sh").write_text(
                (ROOT / "scripts/server-backend-selection.sh").read_text())
            stubs = root / "stubs"
            stubs.mkdir()
            for command in ("cmake", "ninja"):
                stub = stubs / command
                stub.write_text("#!/bin/sh\nexit 0\n")
                stub.chmod(0o755)
            env.update(PATH=str(stubs) + os.pathsep + env["PATH"],
                       PLATYPUS_LLAMA_BACKEND="cpu", PLATYPUS_BUILD_DIR=str(root / "build"),
                       PLATYPUS_INSTALL_PREFIX=str(root / "install"))
            env.pop("PLATYPUS_RELEASE_TARGET", None)
            command = ["bash", str(scripts / "build-llama-server.sh")]
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            checkout = root / "build/_deps/llama.cpp/source.txt"
            self.assertEqual(checkout.read_text(), "pinned")
            checkout.write_text("local changes")
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("modified llama.cpp checkout", result.stderr)
            self.assertEqual(checkout.read_text(), "local changes")


if __name__ == "__main__":
    unittest.main()
