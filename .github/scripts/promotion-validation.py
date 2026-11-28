"""Reuse passing Development PR tests only for the exact tested Git tree."""
import argparse
import io
import json
import os
from pathlib import Path
import subprocess
import time
import urllib.parse
import urllib.request
import zipfile

WAIT_TIMEOUT_SECONDS = 5 * 60 * 60 + 30 * 60
POLL_INTERVAL_SECONDS = 60


class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, url):
        redirected = super().redirect_request(request, fp, code, message, headers, url)
        if redirected and urllib.parse.urlsplit(url).netloc != urllib.parse.urlsplit(request.full_url).netloc:
            redirected.remove_header("Authorization")
        return redirected


class GitHub:
    def __init__(self, repository, token):
        self.root = f"https://api.github.com/repos/{repository}/"
        self.token = token

    def read(self, path, binary=False):
        request = urllib.request.Request(self.root + path, headers={
            "Authorization": f"Bearer {self.token}",
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
        })
        with urllib.request.build_opener(SafeRedirect()).open(request, timeout=60) as response:
            body = response.read()
        return body if binary else json.loads(body)


def record(event, repository, tree):
    if event.get("ref") != "refs/heads/Development":
        raise ValueError("Test evidence must come from a push to Development.")
    commit = event.get("after")
    if not isinstance(commit, str) or not commit:
        raise ValueError("Development push event has no commit SHA.")
    return {"repository": repository, "ref": event["ref"], "commit": commit, "tree": tree}


def verify(api, event, repository, tree, timeout_seconds=WAIT_TIMEOUT_SECONDS):
    pr = event["pull_request"]
    head = pr["head"]
    if pr["base"]["ref"] != "main" or head["ref"] != "Development" or head["repo"]["full_name"] != repository:
        raise ValueError("main only accepts a PR from this repository's Development branch.")
    commit_sha = head["sha"]
    commit = api.read(f"commits/{commit_sha}")
    if commit["commit"]["tree"]["sha"] != tree:
        raise ValueError("Sync Development with main before promoting; its tree differs from the PR merge tree.")

    expected = {"repository": repository, "ref": "refs/heads/Development",
                "commit": commit_sha, "tree": tree}
    deadline = time.monotonic() + timeout_seconds
    wait_reported = False
    while True:
        runs = api.read("actions/workflows/ci.yml/runs?event=push&per_page=100&head_sha="
                        + commit_sha)["workflow_runs"]
        pending = not runs
        for run in runs:
            if (run.get("event") != "push" or run.get("head_branch") != "Development"
                    or run.get("head_sha") != commit_sha):
                continue
            if run.get("status") != "completed":
                pending = True
                continue
            if run.get("conclusion") != "success":
                continue
            artifacts = api.read(f"actions/runs/{run['id']}/artifacts?per_page=100")["artifacts"]
            for artifact in artifacts:
                if artifact["name"] != "test-results" or artifact["expired"]:
                    continue
                archive = api.read(f"actions/artifacts/{artifact['id']}/zip", binary=True)
                with zipfile.ZipFile(io.BytesIO(archive)) as bundle:
                    if "test-validation.json" not in bundle.namelist():
                        continue
                    evidence = json.loads(bundle.read("test-validation.json"))
                if evidence == expected:
                    return run["html_url"]
        remaining = deadline - time.monotonic()
        if not pending or remaining <= 0:
            break
        if not wait_reported:
            print(f"Waiting for successful Development tests for commit {commit_sha}.", flush=True)
            wait_reported = True
        time.sleep(min(POLL_INTERVAL_SECONDS, remaining))
    raise ValueError("No successful Development push test run matches this exact tree. "
                     "Wait for the Development tests to pass, then promote the same tree.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["record", "verify"])
    parser.add_argument("--output", default="test-results/test-validation.json")
    args = parser.parse_args()
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8"))
    repository = os.environ["GITHUB_REPOSITORY"]
    tree = subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"], text=True).strip()
    if args.mode == "record":
        output = Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(record(event, repository, tree)), encoding="utf-8")
    else:
        run = verify(GitHub(repository, os.environ["GH_TOKEN"]), event, repository, tree)
        message = f"Reused passing Development tests for Git tree `{tree}`: {run}\n"
        print(message)
        if summary := os.environ.get("GITHUB_STEP_SUMMARY"):
            with open(summary, "a", encoding="utf-8") as output:
                output.write(message)


if __name__ == "__main__":
    main()
