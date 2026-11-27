"""Promotion must reuse only successful tests for exactly the released files."""
import copy
import importlib.util
import io
import json
from pathlib import Path
import unittest
import urllib.request
import zipfile

spec = importlib.util.spec_from_file_location("promotion", Path(__file__).parents[1] / ".github/scripts/promotion-validation.py")
promotion = importlib.util.module_from_spec(spec)
spec.loader.exec_module(promotion)

REPO = "owner/invoices"
EVENT = {"number": 42, "pull_request": {"base": {"ref": "main"},
    "head": {"ref": "Development", "sha": "merged-dev", "repo": {"full_name": REPO}}}}
EVIDENCE = {"repository": REPO, "pr": 41, "head_sha": "feature-sha", "tree": "tested-tree"}


class FakeGitHub:
    def __init__(self):
        self.pr = {"number": 41, "merged_at": "2026-11-22", "base": {"ref": "Development"},
                   "merge_commit_sha": "merged-dev", "head": {"sha": "feature-sha"}}
        self.run = {"id": 9, "event": "pull_request", "conclusion": "success",
                    "head_sha": "feature-sha", "html_url": "https://example.invalid/run/9"}
        self.evidence = copy.deepcopy(EVIDENCE)
        self.expired = False
        self.commits = {
            "merged-dev": {"commit": {"tree": {"sha": "tested-tree"}}, "parents": []},
            "sync-merge": {"commit": {"tree": {"sha": "tested-tree"}},
                           "parents": [{"sha": "merged-dev"}, {"sha": "main"}]},
        }

    def read(self, path, binary=False):
        if path.endswith("/pulls?per_page=100"):
            return [self.pr]
        if path.startswith("commits/"):
            return self.commits[path.split("/")[1]]
        if path.startswith("actions/workflows/"):
            return {"workflow_runs": [self.run]}
        if path.startswith("actions/runs/"):
            return {"artifacts": [{"name": "test-results", "expired": self.expired, "id": 8}]}
        assert binary
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w") as bundle:
            bundle.writestr("test-validation.json", json.dumps(self.evidence))
        return stream.getvalue()


class PromotionTests(unittest.TestCase):
    def test_reuses_success_for_identical_merge_tree(self):
        self.assertEqual(promotion.verify(FakeGitHub(), EVENT, REPO, "tested-tree"), "https://example.invalid/run/9")

    def test_reuses_tested_pr_when_sync_merge_preserves_its_tree(self):
        api = FakeGitHub()
        event = copy.deepcopy(EVENT)
        event["pull_request"]["head"]["sha"] = "sync-merge"
        self.assertEqual(promotion.verify(api, event, REPO, "tested-tree"), "https://example.invalid/run/9")

    def test_rejects_sync_merge_that_changes_the_tested_tree(self):
        api = FakeGitHub()
        api.commits["sync-merge"]["commit"]["tree"]["sha"] = "different-tree"
        event = copy.deepcopy(EVENT)
        event["pull_request"]["head"]["sha"] = "sync-merge"
        with self.assertRaisesRegex(ValueError, "merged PR for the exact tree"):
            promotion.verify(api, event, REPO, "different-tree")

    def test_rejects_changed_merge_tree_including_base_changes(self):
        with self.assertRaisesRegex(ValueError, "exact tree being promoted"):
            promotion.verify(FakeGitHub(), EVENT, REPO, "different-tree")

    def test_rejects_other_branches_and_fork_named_development(self):
        for ref, repo in [("feature", REPO), ("Development", "fork/invoices")]:
            event = copy.deepcopy(EVENT)
            event["pull_request"]["head"].update(ref=ref, repo={"full_name": repo})
            with self.assertRaisesRegex(ValueError, "only accepts"):
                promotion.verify(FakeGitHub(), event, REPO, "tested-tree")

    def test_rejects_unreviewed_tip_and_wrong_base(self):
        for field, value in [("merge_commit_sha", "older-merge"), ("merged_at", None), ("base", {"ref": "main"})]:
            api = FakeGitHub()
            api.pr[field] = value
            with self.assertRaisesRegex(ValueError, "merged PR"):
                promotion.verify(api, EVENT, REPO, "tested-tree")

    def test_rejects_failed_cancelled_other_event_and_stale_run(self):
        for field, value in [("conclusion", "failure"), ("conclusion", "cancelled"),
                             ("event", "push"), ("head_sha", "older-sha")]:
            api = FakeGitHub()
            api.run[field] = value
            with self.assertRaisesRegex(ValueError, "No successful"):
                promotion.verify(api, EVENT, REPO, "tested-tree")

    def test_rejects_expired_or_mismatched_evidence(self):
        api = FakeGitHub()
        api.expired = True
        with self.assertRaises(ValueError):
            promotion.verify(api, EVENT, REPO, "tested-tree")
        for key, value in [("repository", "other/repo"), ("pr", 40), ("head_sha", "older-sha")]:
            api = FakeGitHub()
            api.evidence[key] = value
            with self.assertRaises(ValueError):
                promotion.verify(api, EVENT, REPO, "tested-tree")

    def test_records_only_development_tests(self):
        with self.assertRaises(ValueError):
            promotion.record(EVENT, REPO, "tested-tree")
        event = copy.deepcopy(EVENT)
        event.update(number=41)
        event["pull_request"]["base"]["ref"] = "Development"
        event["pull_request"]["head"]["sha"] = "feature-sha"
        self.assertEqual(promotion.record(event, REPO, "tested-tree"), EVIDENCE)

    def test_download_redirect_does_not_forward_github_token(self):
        request = urllib.request.Request("https://api.github.com/artifact", headers={"Authorization": "Bearer test"})
        redirected = promotion.SafeRedirect().redirect_request(request, None, 302, "", {}, "https://storage.example/artifact")
        self.assertIsNone(redirected.get_header("Authorization"))


if __name__ == "__main__":
    unittest.main()
