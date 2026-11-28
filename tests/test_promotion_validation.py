"""Promotion must reuse successful Development push tests for the exact tree."""
import copy
import importlib.util
import io
import json
from pathlib import Path
import unittest
import urllib.request
import zipfile
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("promotion", Path(__file__).parents[1] / ".github/scripts/promotion-validation.py")
promotion = importlib.util.module_from_spec(spec)
spec.loader.exec_module(promotion)

REPO = "owner/invoices"
EVENT = {"number": 42, "pull_request": {"base": {"ref": "main"},
    "head": {"ref": "Development", "sha": "dev-commit", "repo": {"full_name": REPO}}}}
PUSH_EVENT = {"ref": "refs/heads/Development", "after": "dev-commit"}
EVIDENCE = {"repository": REPO, "ref": "refs/heads/Development",
            "commit": "dev-commit", "tree": "tested-tree"}


class FakeGitHub:
    def __init__(self):
        self.run = {"id": 9, "event": "push", "status": "completed", "conclusion": "success",
                    "head_sha": "dev-commit", "head_branch": "Development",
                    "html_url": "https://example.invalid/run/9"}
        self.run_responses = [[self.run]]
        self.evidence = copy.deepcopy(EVIDENCE)
        self.expired = False
        self.commits = {"dev-commit": {"commit": {"tree": {"sha": "tested-tree"}}}}

    def read(self, path, binary=False):
        if path.startswith("commits/"):
            return self.commits[path.split("/")[1]]
        if path.startswith("actions/workflows/"):
            response = self.run_responses.pop(0) if len(self.run_responses) > 1 else self.run_responses[0]
            return {"workflow_runs": response}
        if path.startswith("actions/runs/"):
            return {"artifacts": [{"name": "test-results", "expired": self.expired, "id": 8}]}
        assert binary
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w") as bundle:
            bundle.writestr("test-validation.json", json.dumps(self.evidence))
        return stream.getvalue()


class PromotionTests(unittest.TestCase):
    def test_reuses_success_for_identical_development_push_tree(self):
        self.assertEqual(promotion.verify(FakeGitHub(), EVENT, REPO, "tested-tree"),
                         "https://example.invalid/run/9")

    def test_waits_for_in_progress_development_tests(self):
        api = FakeGitHub()
        api.run_responses = [
            [{"id": 9, "event": "push", "status": "in_progress", "head_sha": "dev-commit",
              "head_branch": "Development"}],
            [api.run],
        ]
        with patch.object(promotion.time, "sleep", return_value=None):
            self.assertEqual(promotion.verify(api, EVENT, REPO, "tested-tree"),
                             "https://example.invalid/run/9")

    def test_rejects_merge_tree_different_from_development_commit(self):
        with self.assertRaisesRegex(ValueError, "tree differs from the PR merge tree"):
            promotion.verify(FakeGitHub(), EVENT, REPO, "different-tree")

    def test_rejects_other_branches_and_fork_named_development(self):
        for ref, repo in [("feature", REPO), ("Development", "fork/invoices")]:
            event = copy.deepcopy(EVENT)
            event["pull_request"]["head"].update(ref=ref, repo={"full_name": repo})
            with self.assertRaisesRegex(ValueError, "only accepts"):
                promotion.verify(FakeGitHub(), event, REPO, "tested-tree", timeout_seconds=0)

    def test_rejects_failed_or_wrong_branch_test_run(self):
        for field, value in [("conclusion", "failure"), ("head_branch", "feature")]:
            api = FakeGitHub()
            api.run[field] = value
            with self.assertRaisesRegex(ValueError, "No successful Development push test run"):
                promotion.verify(api, EVENT, REPO, "tested-tree", timeout_seconds=0)

    def test_rejects_expired_or_mismatched_evidence(self):
        api = FakeGitHub()
        api.expired = True
        with self.assertRaisesRegex(ValueError, "No successful Development push test run"):
            promotion.verify(api, EVENT, REPO, "tested-tree", timeout_seconds=0)
        for key, value in [("repository", "other/repo"), ("commit", "older-sha"), ("tree", "older-tree")]:
            api = FakeGitHub()
            api.evidence[key] = value
            with self.assertRaisesRegex(ValueError, "No successful Development push test run"):
                promotion.verify(api, EVENT, REPO, "tested-tree", timeout_seconds=0)

    def test_records_only_development_pushes(self):
        self.assertEqual(promotion.record(PUSH_EVENT, REPO, "tested-tree"), EVIDENCE)
        for event in [{"ref": "refs/heads/feature", "after": "commit"}, EVENT,
                      {"ref": "refs/heads/Development"}]:
            with self.assertRaises(ValueError):
                promotion.record(event, REPO, "tested-tree")

    def test_download_redirect_does_not_forward_github_token(self):
        request = urllib.request.Request("https://api.github.com/artifact", headers={"Authorization": "Bearer test"})
        redirected = promotion.SafeRedirect().redirect_request(request, None, 302, "", {}, "https://storage.example/artifact")
        self.assertIsNone(redirected.get_header("Authorization"))


if __name__ == "__main__":
    unittest.main()
