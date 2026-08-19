#!/usr/bin/env python3
"""Fetch the exact StopWatch vendor components used by the firmware."""

from __future__ import annotations

import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "vendor"


def run(*args: str, cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, check=check, text=True, capture_output=True)


def is_applied(repo: Path, patch: Path) -> bool:
    return run("git", "apply", "--reverse", "--check", str(patch), cwd=repo, check=False).returncode == 0


def fetch(name: str, item: dict[str, object]) -> None:
    repo = VENDOR / name
    url = str(item["url"])
    ref = str(item["ref"])
    if not (repo / ".git").is_dir():
        run("git", "clone", "--filter=blob:none", url, str(repo))
    run("git", "fetch", "--depth=1", "origin", ref, cwd=repo)
    run("git", "checkout", "--detach", "FETCH_HEAD", cwd=repo)
    head = run("git", "rev-parse", "HEAD", cwd=repo).stdout.strip()
    if head != ref:
        raise RuntimeError(f"{name}: expected {ref}, got {head}")
    if item.get("submodules"):
        run("git", "submodule", "update", "--init", "--recursive", cwd=repo)
    if patch_name := item.get("patch"):
        patch = ROOT / "patches" / str(patch_name)
        if not is_applied(repo, patch):
            run("git", "apply", "--check", str(patch), cwd=repo)
            run("git", "apply", str(patch), cwd=repo)
    print(f"{name}: {head}")


def main() -> None:
    lock = json.loads((ROOT / "deps.lock.json").read_text())
    VENDOR.mkdir(exist_ok=True)
    for name, item in lock.items():
        fetch(name, item)


if __name__ == "__main__":
    main()
