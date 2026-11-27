"""Reuse passing Development PR tests only for the exact tested Git tree."""
import argparse
import io
import json
import os
from pathlib import Path
import subprocess
import urllib.parse
import urllib.request
import zipfile


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
    pr = event["pull_request"]
    if pr["base"]["ref"] != "Development":
        raise ValueError("Test evidence must come from a Development PR.")
    return {"repository": repository, "pr": event["number"], "head_sha": pr["head"]["sha"], "tree": tree}


def verify(api, event, repository, tree):
    pr = event["pull_request"]
    head = pr["head"]
    if pr["base"]["ref"] != "main" or head["ref"] != "Development" or head["repo"]["full_name"] != repository:
        raise ValueError("main only accepts a PR from this repository's Development branch.")
    # A no-op sync merge can sit on top of a tested PR without changing its tree.
    # Only reuse a PR's evidence when its merge tree is identical to the promoted tree.
    sources = []
    commit_sha = head["sha"]
    while commit_sha:
        commit = api.read(f"commits/{commit_sha}")
        if commit["commit"]["tree"]["sha"] != tree:
            break
        candidates = api.read(f"commits/{commit_sha}/pulls?per_page=100")
        source = next((item for item in candidates if item.get("merged_at")
                       and item["base"]["ref"] == "Development"
                       and item["merge_commit_sha"] == commit_sha), None)
        if source:
            sources.append(source)
        parents = commit.get("parents", [])
        commit_sha = parents[0]["sha"] if parents else None
    if not sources:
        raise ValueError("Development must contain a merged PR for the exact tree being promoted.")
    for source in sources:
        runs = api.read("actions/workflows/ci.yml/runs?event=pull_request&status=success&per_page=100&head_sha="
                        + source["head"]["sha"])["workflow_runs"]
        expected = {"repository": repository, "pr": source["number"],
                    "head_sha": source["head"]["sha"], "tree": tree}
        for run in runs:
            if run["conclusion"] != "success" or run["event"] != "pull_request" or run["head_sha"] != source["head"]["sha"]:
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
    raise ValueError("No successful Development test run matches this exact merge tree. "
                     "Sync Development with main and validate a new PR into Development before promoting.")


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
