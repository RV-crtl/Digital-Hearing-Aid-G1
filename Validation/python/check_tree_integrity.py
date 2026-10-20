#!/usr/bin/env python3
"""Audit all tracked Markdown links and case-insensitive path collisions.

GitHub's Linux CI is case-sensitive; developers commonly import on Windows.
This checks the *full tracked documentation tree*, unlike the historic
four-page navigation check. It does not test external websites or anchors.
"""
from __future__ import annotations
import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[2]
LINK = re.compile(r"!?\[[^\]]*\]\((<[^>]+>|[^)]+)\)")


def tracked_paths(root: Path) -> list[str]:
    raw = subprocess.check_output(["git", "ls-files", "-z"], cwd=root)
    return [part.decode("utf-8") for part in raw.split(b"\0") if part]


def resolve_link(source: str, raw: str) -> str | None:
    """Return a normalized repo-relative path, None for external/anchor links."""
    dest = raw.strip()
    if dest.startswith("<") and ">" in dest:
        dest = dest[1:dest.index(">")]
    else:
        dest = dest.split(' "', 1)[0].split(" '", 1)[0].strip()
    if not dest or dest.startswith(("#", "/", "//")) or re.match(r"^[a-z][\w+.-]*:", dest, re.I):
        return None
    dest = unquote(dest.split("#", 1)[0].split("?", 1)[0])
    if not dest:
        return None
    parts = source.split("/")[:-1]
    for part in dest.replace("\\", "/").split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            if not parts:
                return "__OUTSIDE_REPOSITORY__"
            parts.pop()
        else:
            parts.append(part)
    return "/".join(parts)


def check(root: Path, filenames: list[str]) -> list[str]:
    errors = []
    paths = set(filenames)
    canonical = {}
    for path in sorted(paths):
        old = canonical.setdefault(path.casefold(), path)
        if old != path:
            errors.append(f"Case-insensitive filename collision: {old} and {path}")
    for filename in sorted(p for p in paths if p.lower().endswith(".md")):
        text = (root / filename).read_text(encoding="utf-8")
        for link in LINK.finditer(text):
            rel = resolve_link(filename, link.group(1))
            if rel is not None and rel not in paths and not (root / rel).is_dir():
                line = text.count("\n", 0, link.start()) + 1
                errors.append(f"{filename}:{line}: missing relative link target {rel}")
    return errors


def main() -> int:
    try:
        errors = check(ROOT, tracked_paths(ROOT))
    except (OSError, subprocess.CalledProcessError, UnicodeError) as exc:
        print(f"FAIL: tracked documentation scan could not finish: {exc}")
        return 2
    for issue in errors:
        print("FAIL:", issue)
    if errors:
        return 1
    print("PASS: all tracked Markdown relative file links and Windows case-insensitive paths")
    print("NOTE: external URLs, heading anchors and files missing from the git index are not validated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
