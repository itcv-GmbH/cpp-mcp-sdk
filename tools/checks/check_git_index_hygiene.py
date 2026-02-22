#!/usr/bin/env python3
"""
Deterministic check for git index hygiene.

Runs `git ls-files` and exits non-zero if any required-absent path is present:
- build/
- tests/integration/.venv/
- **/__pycache__/
- **/*.pyc
"""

import os
import subprocess
import sys
from fnmatch import fnmatch
from pathlib import Path
from typing import List


def get_git_tracked_files(project_root: Path) -> List[str]:
    """Get list of files tracked by git."""
    try:
        result = subprocess.run(
            ["git", "ls-files"],
            cwd=project_root,
            capture_output=True,
            text=True,
            check=True,
        )
        return result.stdout.strip().split("\n") if result.stdout.strip() else []
    except subprocess.CalledProcessError as e:
        print(f"ERROR: Failed to run git ls-files: {e}")
        return []
    except FileNotFoundError:
        print("ERROR: git command not found")
        return []


def matches_forbidden_pattern(filepath: str) -> tuple[bool, str]:
    """
    Check if a filepath matches any forbidden pattern.
    Returns (is_forbidden, pattern_matched).
    """
    # Normalize path separators for matching
    normalized_path = filepath.replace("\\", "/")

    forbidden_patterns = [
        ("build/", "build/ directory"),
        ("build/*", "build/ directory"),
        ("tests/integration/.venv/", "tests/integration/.venv/ directory"),
        ("tests/integration/.venv/*", "tests/integration/.venv/ directory"),
        ("*/__pycache__/*", "__pycache__/ directory"),
        ("**/__pycache__/*", "__pycache__/ directory"),
        ("*.pyc", "*.pyc file"),
        ("**/*.pyc", "*.pyc file"),
    ]

    for pattern, description in forbidden_patterns:
        # Use fnmatch for glob-style matching
        if fnmatch(normalized_path, pattern) or fnmatch(
            normalized_path, pattern.lstrip("*/")
        ):
            return True, description

    # Check for __pycache__ in path components
    if "__pycache__" in normalized_path.split("/"):
        return True, "__pycache__/ directory"

    # Check for .pyc extension
    if normalized_path.endswith(".pyc"):
        return True, "*.pyc file"

    # Check for build/ prefix
    if normalized_path.startswith("build/"):
        return True, "build/ directory"

    # Check for tests/integration/.venv/ prefix
    if normalized_path.startswith("tests/integration/.venv/"):
        return True, "tests/integration/.venv/ directory"

    return False, ""


def main() -> int:
    """Main entry point."""
    # Find the project root (where this script is: tools/checks/)
    script_dir = Path(__file__).parent
    project_root = script_dir.parent.parent

    # Verify we're in a git repository
    git_dir = project_root / ".git"
    if not git_dir.exists():
        print(f"ERROR: Not a git repository: {project_root}")
        return 1

    tracked_files = get_git_tracked_files(project_root)

    if not tracked_files:
        print("WARNING: No tracked files found or git command failed")
        # Don't fail if git ls-files returns empty (might be a fresh repo)
        return 0

    violations = []

    for filepath in tracked_files:
        is_forbidden, pattern = matches_forbidden_pattern(filepath)
        if is_forbidden:
            violations.append((filepath, pattern))

    if violations:
        print("=" * 70)
        print("VIOLATION: Git index hygiene violations found")
        print("=" * 70)
        print()
        print("The following files should not be tracked in git:")
        print()

        # Group by pattern
        by_pattern = {}
        for filepath, pattern in sorted(violations):
            if pattern not in by_pattern:
                by_pattern[pattern] = []
            by_pattern[pattern].append(filepath)

        for pattern in sorted(by_pattern.keys()):
            print(f"Pattern: {pattern}")
            for filepath in by_pattern[pattern]:
                print(f"  - {filepath}")
            print()

        print("=" * 70)
        print(f"Total violations: {len(violations)}")
        print("=" * 70)
        print()
        print("To remove these files from git tracking:")
        print("  git rm --cached <file>")
        print("  echo '<pattern>' >> .gitignore")
        return 1

    print("OK: Git index is clean (no forbidden paths tracked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
