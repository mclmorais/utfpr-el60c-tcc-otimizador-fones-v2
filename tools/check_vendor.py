#!/usr/bin/env python3
"""Compare the ST code in Drivers/ with what the latest STM32CubeF7 release uses.

The versions in use come from the "Código da ST" table in README.md. Each
component is compared with the version the newest STM32CubeF7 tag pins, not
with the newest tag of its own repository: some repositories have newer major
versions (stm32-otm8009a v2.x, for example) written for a BSP API that the F7
does not use.

Set GITHUB_TOKEN to raise the GitHub API rate limit (60 requests per hour
without it; one run makes about ten).

Usage: tools/check_vendor.py   # exits 1 when an update is available
"""

import json
import os
import re
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OWNER = "STMicroelectronics"
CUBE = "STM32CubeF7"
CMSIS_CORE_PATH = "Drivers/CMSIS/Include"

VERSION = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)$")
ROW = re.compile(r"^\| (?P<name>[^|]+?) \| `(?P<folder>[^`]+)` \| (?P<version>[^ |]+) \| (?P<origin>.+) \|$")
REPO_LINK = re.compile(r"github\.com/" + OWNER + r"/([\w.-]+)\)")
CUBE_VERSION = re.compile(CUBE + r"\]\([^)]+\) (v[\d.]+)")


def repo_key(name):
    # .gitmodules and the README spell some names differently (cmsis_device_f7 vs cmsis-device-f7).
    return name.lower().removesuffix(".git").replace("_", "-")


def fetch(url, accept="application/vnd.github+json"):
    request = urllib.request.Request(url, headers={"Accept": accept})
    if "GITHUB_TOKEN" in os.environ and "api.github.com" in url:
        request.add_header("Authorization", "Bearer " + os.environ["GITHUB_TOKEN"])
    with urllib.request.urlopen(request) as response:
        return response.read().decode("utf-8")


def tags(repo):
    """Maps commit hash to release tag, for tags named like v1.2.3."""
    out = subprocess.run(["git", "ls-remote", "--tags", f"https://github.com/{OWNER}/{repo}.git"],
                         capture_output=True, text=True, check=True).stdout
    by_commit = {}
    for line in out.splitlines():
        commit, ref = line.split("\t")
        tag = ref.removeprefix("refs/tags/").removesuffix("^{}")
        if VERSION.match(tag):
            # Annotated tags list the tag object and then the peeled commit (^{}), which wins.
            by_commit[commit] = tag
    return by_commit


def newest(versions):
    return max(versions, key=lambda v: tuple(map(int, VERSION.match(v).groups())))


def readme_components():
    with open(os.path.join(ROOT, "README.md"), encoding="utf-8") as f:
        readme = f.read()
    pinned_cube = CUBE_VERSION.search(readme).group(1)
    rows = []
    for line in readme.splitlines():
        match = ROW.match(line.strip())
        if match and match["folder"].startswith("Drivers/"):
            repo = REPO_LINK.search(match["origin"]).group(1)
            rows.append((match["name"], match["version"], repo))
    return pinned_cube, rows


def cube_versions(cube_tag):
    """Maps repository name to the version cube_tag uses, plus the in-tree CMSIS Core."""
    raw = f"https://raw.githubusercontent.com/{OWNER}/{CUBE}/{cube_tag}"
    paths = {}
    path = None
    for line in fetch(f"{raw}/.gitmodules", accept="*/*").splitlines():
        line = line.strip()
        if line.startswith("path = "):
            path = line.removeprefix("path = ")
        elif line.startswith("url = "):
            paths[repo_key(line.rsplit("/", 1)[1])] = path

    def pinned(repo):
        api = f"https://api.github.com/repos/{OWNER}/{CUBE}/contents/{paths[repo_key(repo)]}?ref={cube_tag}"
        commit = json.loads(fetch(api))["sha"]
        return tags(repo).get(commit, f"commit {commit[:7]}")

    core_cm7 = fetch(f"{raw}/{CMSIS_CORE_PATH}/core_cm7.h", accept="*/*")
    core_version = re.search(r"@version\s+V([\d.]+)", core_cm7).group(1)
    return pinned, core_version


def main():
    pinned_cube, rows = readme_components()
    latest_cube = newest(set(tags(CUBE).values()))
    pinned, core_version = cube_versions(latest_cube)

    outdated = pinned_cube != latest_cube
    print(f"{CUBE}: README {pinned_cube}, latest {latest_cube}")
    for name, ours, repo in rows:
        theirs = core_version if repo == CUBE else pinned(repo)
        status = "ok" if ours == theirs else "UPDATE"
        outdated |= ours != theirs
        print(f"  {status:6} {name}: {ours} -> {theirs}")
    sys.exit(1 if outdated else 0)


if __name__ == "__main__":
    main()
