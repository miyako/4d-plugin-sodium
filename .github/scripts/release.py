#!/usr/bin/env python3
"""Validate source versions and publish only a completed, immutable build."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
VERSION_PATTERN = r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"


def version(root=ROOT):
    text = (root / "VERSION").read_text()
    if not re.fullmatch(VERSION_PATTERN + r"\n?", text):
        raise ValueError("VERSION must contain strict X.Y.Z (no leading zeros)")
    cmake = (root / "sodium/CMakeLists.txt").read_text()
    required = [
        'file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" PLUGIN_VERSION)',
        'MACOSX_BUNDLE_BUNDLE_VERSION "${PLUGIN_VERSION}"',
        'MACOSX_BUNDLE_SHORT_VERSION_STRING "${PLUGIN_VERSION}"',
    ]
    if any(line not in cmake for line in required):
        raise ValueError("CMake bundle metadata must derive from VERSION")
    tree = ET.parse(root / "sodium/Info.plist.in")
    items = list(tree.getroot().find("dict"))
    values = {items[i].text: items[i + 1].text for i in range(0, len(items), 2)}
    for key, variable in (
        ("CFBundleVersion", "MACOSX_BUNDLE_BUNDLE_VERSION"),
        ("CFBundleShortVersionString", "MACOSX_BUNDLE_SHORT_VERSION_STRING"),
    ):
        if values.get(key) != "${" + variable + "}":
            raise ValueError(f"{key} must use the coordinated CMake version")
    return text.rstrip("\n")


def bump(mode, root=ROOT):
    parts = list(map(int, version(root).split(".")))
    index = {"major": 0, "minor": 1, "patch": 2}[mode]
    parts[index] += 1
    parts[index + 1:] = [0] * (2 - index)
    new = ".".join(map(str, parts))
    (root / "VERSION").write_text(new + "\n")
    return new


def run(*args):
    return subprocess.run(args, check=True, text=True, capture_output=True).stdout


def available(repo, tag):
    if not re.fullmatch(VERSION_PATTERN, tag.removeprefix("v")) or not tag.startswith("v"):
        raise ValueError("Invalid release tag")
    endpoint = f"git/ref/tags/{tag}"
    result = subprocess.run(
        ["gh", "api", "--include", f"repos/{repo}/{endpoint}"],
        text=True, capture_output=True,
    )
    status = re.search(r"^HTTP/\S+ (\d{3})", result.stdout, re.MULTILINE)
    if status and status[1] == "200" and result.returncode == 0:
        raise ValueError(f"{tag} already exists: {endpoint}")
    if not (status and status[1] == "404" and result.returncode != 0):
        raise RuntimeError(f"Cannot establish availability of {endpoint}: "
                           f"{result.stderr.strip()} {result.stdout.strip()}")
    pages = json.loads(run("gh", "api", "--paginate", "--slurp",
                           f"repos/{repo}/releases?per_page=100"))
    if not isinstance(pages, list) or not pages:
        raise ValueError("Malformed paginated releases response")
    for page in pages:
        if not isinstance(page, list):
            raise ValueError("Malformed releases page")
        for item in page:
            if (not isinstance(item, dict)
                    or not isinstance(item.get("tag_name"), str)
                    or not item["tag_name"]):
                raise ValueError("Malformed release metadata")
            if item["tag_name"] == tag:
                raise ValueError(f"{tag} already exists: release (including drafts)")


def publish(repo, tag, sha, assets):
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise ValueError("Expected the immutable selected commit SHA")
    if run("git", "rev-parse", "HEAD").strip() != sha:
        raise ValueError("Checkout does not match the selected commit")
    if tag != "v" + version():
        raise ValueError("Tag does not match source metadata")
    for asset in assets:
        if not Path(asset).is_file() or Path(asset).stat().st_size == 0:
            raise ValueError(f"Missing or empty release artifact: {asset}")
    available(repo, tag)
    run("git", "tag", tag, sha)
    run("git", "push", "origin", f"refs/tags/{tag}:refs/tags/{tag}")
    try:
        run("gh", "api", "--method", "POST", f"repos/{repo}/releases",
            "-f", f"tag_name={tag}", "-f", f"target_commitish={sha}",
            "-f", f"name={tag}", "-f", f"body=Built from {sha}.",
            "-F", "draft=false", "-F", "prerelease=false")
        run("gh", "release", "upload", tag, *assets, "--repo", repo)
    except subprocess.CalledProcessError:
        message = (f"Tag {tag} was pushed at {sha}, but release creation/upload failed. "
                   "Inspect the remote tag/release/assets and recover manually; "
                   "this workflow never deletes or overwrites publication state.")
        if os.environ.get("GITHUB_STEP_SUMMARY"):
            with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
                summary.write(message + "\n")
        print(message, file=sys.stderr)
        raise


def notarized(path):
    result = json.loads(Path(path).read_text())
    if result.get("status") != "Accepted":
        raise ValueError(f"Notarization was not accepted: {result}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["version", "bump", "check", "publish", "notarized"])
    parser.add_argument("--mode", choices=["patch", "minor", "major"])
    parser.add_argument("--repo")
    parser.add_argument("--tag")
    parser.add_argument("--sha")
    parser.add_argument("--assets", nargs=2)
    parser.add_argument("--result")
    args = parser.parse_args()
    if args.command == "version":
        print(version())
    elif args.command == "notarized":
        if not args.result:
            parser.error("notarized requires --result")
        notarized(args.result)
    elif args.command == "bump":
        if not args.mode:
            parser.error("bump requires --mode")
        print(bump(args.mode))
    else:
        if not args.repo or not args.tag:
            parser.error("check/publish require --repo and --tag")
        if args.command == "check":
            available(args.repo, args.tag)
        else:
            if not args.sha or not args.assets:
                parser.error("publish requires --sha and --assets")
            publish(args.repo, args.tag, args.sha, args.assets)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError, ET.ParseError,
            subprocess.CalledProcessError) as error:
        print(f"Release error: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError):
            print(error.stderr, file=sys.stderr)
        sys.exit(1)
