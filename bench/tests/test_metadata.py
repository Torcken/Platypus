"""Artifact provenance, baseline publication and comparability regressions."""
import copy
import hashlib
from pathlib import Path
import tempfile
import unittest
from bench.metadata import collect_artifacts, configuration_for_client, metadata_errors, PINNED_COMMIT
from bench.report import ReportGenerator, paired_bootstrap


class MetadataTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        for name in ("model", "server", "client"):
            (self.root / name).write_text(name)
        self.config = {
            "model": "local", "model_file": str(self.root / "model"),
            "server_binary": str(self.root / "server"), "quantization": "Q4_K_M",
            "hardware_tier": "fixture", "hardware_description": "test hardware",
            "hardware_os": "test OS", "cache_type_k": "f16", "cache_type_v": "f16",
            "server_build_backend": "CPU", "server_version": "v1.0.0 fixture",
            "llama_cpp_tag": "v1.0.0", "llama_cpp_commit": PINNED_COMMIT,
            "context_size": 16384, "gpu_layers": 0, "seed": 42,
            "sampling": {"temperature": .3}, "server_launch_args": ["server", "-m", "model"],
        }

    def tearDown(self):
        self.temporary.cleanup()

    def checked(self):
        return collect_artifacts(self.config, str(self.root / "client"), "client 1.0")

    def test_checked_artifacts_provide_hashes_without_loading_them_as_text(self):
        result = self.checked()
        self.assertEqual(metadata_errors(result), [])
        self.assertEqual(result["model_file_sha256"], hashlib.sha256(b"model").hexdigest())
        self.assertEqual(result["client_version"], "client 1.0")

    def test_hash_mismatch_and_claimed_client_version_are_rejected(self):
        for config in (dict(self.config, model_file_sha256="0" * 64),
                       dict(self.config, client_version="wrong")):
            with self.assertRaises(ValueError):
                collect_artifacts(config, str(self.root / "client"), "client 1.0")

    def test_changed_artifact_is_rehashed(self):
        original = self.checked()["model_file_sha256"]
        (self.root / "model").write_text("other")
        self.assertNotEqual(self.checked()["model_file_sha256"], original)

    def test_missing_nonfinite_or_wrongly_typed_metadata_is_incomplete(self):
        self.assertTrue(metadata_errors({}))
        for field, value in (("context_size", True), ("seed", -1),
                             ("sampling", {"temperature": float("nan")}),
                             ("server_launch_args", "server -m file"),
                             ("llama_cpp_commit", "different")):
            config = dict(self.checked(), **{field: value})
            self.assertTrue(metadata_errors(config), field)

    def test_client_overrides_do_not_reuse_another_clients_version(self):
        config = dict(self.config, clients={"a": {"client_version": "1"},
                                          "b": {"client_version": "2"}})
        self.assertEqual(configuration_for_client(config, "a")["client_version"], "1")
        self.assertEqual(configuration_for_client(config, "b")["client_version"], "2")
        self.assertNotIn("clients", configuration_for_client(config, "a"))

    def test_baseline_report_rejects_incomplete_provenance_before_overwriting_output(self):
        path = self.root / "report.md"
        path.write_text("existing")
        with self.assertRaises(ValueError):
            ReportGenerator().generate_markdown_report([
                {"task_id": "x", "success": True, "metadata_status": "complete", "config": {}}
            ], str(path), baseline=True)
        self.assertEqual(path.read_text(), "existing")
        record = {"task_id": "x", "success": True, "metadata_status": "complete",
                  "config": self.checked()}
        text = ReportGenerator().generate_markdown_report([record], str(path), baseline=True)
        self.assertNotIn("Exploratory", text)
        with self.assertRaises(ValueError):
            ReportGenerator().generate_markdown_report(
                [{**record, "client": "fake"}], str(path), baseline=True)

    def test_paired_comparisons_reject_settings_changes_and_missing_tasks(self):
        left = [{"task_id": "x", "success": False, "config": self.checked()}]
        right = copy.deepcopy(left)
        right[0]["config"]["sampling"]["temperature"] = .9
        with self.assertRaises(ValueError):
            paired_bootstrap(left, right, samples=20)
        with self.assertRaises(ValueError):
            paired_bootstrap(left + [{**left[0], "task_id": "missing"}], left, samples=20)

    def test_reused_label_cannot_merge_incompatible_settings(self):
        config = dict(self.checked(), label="same")
        records = [{"task_id": "x", "config": config},
                   {"task_id": "x", "config": dict(config, context_size=8192)}]
        with self.assertRaises(ValueError):
            ReportGenerator().generate_markdown_report(records, str(self.root / "report.md"))
