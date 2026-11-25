"""Offline tests for private release packaging and update safety."""

from __future__ import annotations

import hashlib
import io
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))

import package_release
import update


class ReleaseUpdateTests(unittest.TestCase):
    def test_package_includes_expected_files_and_checksum(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            prefix = root / "prefix"
            for relative in package_release.CONTENTS:
                path = prefix / relative
                if relative.endswith("profiles"):
                    path.mkdir(parents=True)
                    (path / "catalog.json").write_text("{}", encoding="utf-8")
                else:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(
                        "linux-x86_64-cpu\n"
                        if relative.endswith("install-target")
                        else "binary",
                        encoding="utf-8",
                    )
                    if relative in package_release.CONTENTS[:2]:
                        path.chmod(0o755)
            archive_path = package_release.package(
                "linux-x86_64-cpu", prefix, root / "dist"
            )
            with tarfile.open(archive_path, "r:gz") as archive:
                self.assertIn("bin/platypus", archive.getnames())
                self.assertIn("share/platypus/profiles/catalog.json", archive.getnames())
            extracted = root / "extracted"
            extracted.mkdir()
            update.extract_release(archive_path, extracted, "linux-x86_64-cpu")
            self.assertTrue((extracted / "bin/platypus").is_file())
            self.assertTrue((extracted / "libexec/platypus/update.py").is_file())
            checksum_text = (root / "dist/SHA256SUMS").read_bytes()
            self.assertEqual(
                update.checksum_for(checksum_text, archive_path.name),
                hashlib.sha256(archive_path.read_bytes()).hexdigest(),
            )

    def test_package_rejects_a_mismatched_target_marker(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            prefix = root / "prefix"
            (prefix / "share/platypus").mkdir(parents=True)
            (prefix / "share/platypus/install-target").write_text(
                "linux-x86_64-vulkan\n", encoding="ascii"
            )
            with self.assertRaisesRegex(ValueError, "does not match"):
                package_release.package("linux-x86_64-cpu", prefix, root / "dist")

    def test_archive_rejects_path_traversal_and_links(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive_path = Path(temporary) / "bad.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                member = tarfile.TarInfo("../../outside")
                member.size = 1
                archive.addfile(member, io.BytesIO(b"x"))
            with tarfile.open(archive_path, "r:gz") as archive:
                with self.assertRaisesRegex(ValueError, "unsafe"):
                    update._validated_members(archive)

            link_path = Path(temporary) / "link.tar.gz"
            with tarfile.open(link_path, "w:gz") as archive:
                member = tarfile.TarInfo("bin/platypus")
                member.type = tarfile.SYMTYPE
                member.linkname = "../../outside"
                archive.addfile(member)
            with tarfile.open(link_path, "r:gz") as archive:
                with self.assertRaisesRegex(ValueError, "unexpected entry"):
                    update._validated_members(archive)

    def test_release_asset_url_rejects_an_external_host(self) -> None:
        self.assertEqual(
            update.release_asset_url(
                {
                    "url": "https://api.github.com/repos/Torcken/Platypus/releases/assets/17"
                }
            ),
            "https://api.github.com/repos/Torcken/Platypus/releases/assets/17",
        )
        with self.assertRaisesRegex(ValueError, "trusted GitHub endpoint"):
            update.release_asset_url(
                {"url": "https://attacker.example/repos/Torcken/Platypus/releases/assets/1"}
            )

    def test_install_transaction_rolls_back_on_failed_version_check(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            prefix = root / "prefix"
            staged = root / "staged"
            (prefix / "bin").mkdir(parents=True)
            (prefix / "share/platypus/profiles").mkdir(parents=True)
            (prefix / "bin/platypus").write_text("old app", encoding="utf-8")
            (prefix / "bin/llama-server").write_text("old server", encoding="utf-8")
            (prefix / "libexec/platypus").mkdir(parents=True)
            (prefix / "libexec/platypus/update.py").write_text("old updater", encoding="utf-8")
            (prefix / "share/platypus/install-target").write_text("old\n", encoding="ascii")
            (prefix / "share/platypus/profiles/catalog.json").write_text("old", encoding="utf-8")
            (staged / "bin").mkdir(parents=True)
            (staged / "libexec/platypus").mkdir(parents=True)
            (staged / "share/platypus/profiles").mkdir(parents=True)
            app = staged / "bin/platypus"
            app.write_text("#!/bin/sh\necho wrong-version\n", encoding="utf-8")
            app.chmod(0o755)
            (staged / "bin/llama-server").write_text("new server", encoding="utf-8")
            (staged / "libexec/platypus/update.py").write_text("new updater", encoding="utf-8")
            (staged / "share/platypus/install-target").write_text("target\n", encoding="ascii")
            (staged / "share/platypus/profiles/catalog.json").write_text("new", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "version startup check"):
                update.install_transaction(prefix, staged, "1.2.3")
            self.assertEqual((prefix / "bin/platypus").read_text(encoding="utf-8"), "old app")
            self.assertEqual(
                (prefix / "share/platypus/profiles/catalog.json").read_text(encoding="utf-8"),
                "old",
            )
            self.assertEqual(
                (prefix / "libexec/platypus/update.py").read_text(encoding="utf-8"),
                "old updater",
            )

    def test_install_transaction_replaces_the_managed_release_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            prefix = root / "prefix"
            staged = root / "staged"
            (prefix / "share/platypus/profiles").mkdir(parents=True)
            (prefix / "share/platypus/profiles/local-custom.toml").write_text(
                "custom", encoding="utf-8"
            )
            (staged / "bin").mkdir(parents=True)
            (staged / "libexec/platypus").mkdir(parents=True)
            (staged / "share/platypus/profiles").mkdir(parents=True)
            app = staged / "bin/platypus"
            app.write_text("#!/bin/sh\nprintf '1.2.3\\n'\n", encoding="utf-8")
            app.chmod(0o755)
            server = staged / "bin/llama-server"
            server.write_text("new pinned server", encoding="utf-8")
            server.chmod(0o755)
            (staged / "libexec/platypus/update.py").write_text("new updater", encoding="utf-8")
            (staged / "share/platypus/install-target").write_text(
                "linux-x86_64-cpu\n", encoding="ascii"
            )
            (staged / "share/platypus/profiles/catalog.json").write_text(
                "new catalog", encoding="utf-8"
            )
            update.install_transaction(prefix, staged, "1.2.3")
            installed_app = (prefix / "bin/platypus").read_text(encoding="utf-8")
            self.assertIn("1.2.3", installed_app)
            self.assertEqual(
                (prefix / "libexec/platypus/update.py").read_text(encoding="utf-8"),
                "new updater",
            )
            self.assertEqual(
                (prefix / "share/platypus/profiles/catalog.json").read_text(encoding="utf-8"),
                "new catalog",
            )
            self.assertEqual(
                (prefix / "share/platypus/profiles/local-custom.toml").read_text(
                    encoding="utf-8"
                ),
                "custom",
            )


if __name__ == "__main__":
    unittest.main()
