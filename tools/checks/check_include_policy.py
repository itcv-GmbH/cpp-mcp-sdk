#!/usr/bin/env python3
"""
Deterministic check for include policy violations.

Scans src/**/*.{cpp,hpp} and tests/**/*.{cpp,hpp} files and exits non-zero if:
1. Any #include contains '../' or '..\' (directory-traversing relative includes)
2. Any test file includes 'src/' paths
"""

import re
import sys
from pathlib import Path
from typing import List, Tuple


def find_include_violations(
    filepath: Path, project_root: Path
) -> List[Tuple[int, str, str]]:
    """
    Find include violations in a file.

    Returns list of (line_number, line_content, violation_type) tuples.
    """
    violations = []
    content = filepath.read_text(encoding="utf-8", errors="replace")
    lines = content.split("\n")

    rel_path = filepath.relative_to(project_root)
    rel_path_parts = rel_path.parts

    # Check if this is a test file using Path.parts for cross-platform comparison
    is_test_file = len(rel_path_parts) >= 1 and rel_path_parts[0] == "tests"

    # Pattern to match #include directives with various quote/bracket styles
    include_pattern = re.compile(r'^\s*#\s*include\s+["\u003c]([^"\u003e]+)["\u003e]')

    for line_num, line in enumerate(lines, 1):
        match = include_pattern.match(line)
        if not match:
            continue

        include_path = match.group(1)
        include_path_parts = Path(include_path).parts

        # Check for directory-traversing relative includes
        # Check raw string for both '../' and '..\' to catch violations regardless of host OS
        if (
            ".." in include_path_parts
            or "../" in include_path
            or "..\\" in include_path
        ):
            violations.append((line_num, line.strip(), "directory_traversal"))
            continue

        # Check if test file includes src/ paths using Path.parts
        if (
            is_test_file
            and len(include_path_parts) >= 1
            and include_path_parts[0] == "src"
        ):
            violations.append((line_num, line.strip(), "test_includes_src"))

    return violations


def main() -> int:
    """Main entry point."""
    # Find the project root (where this script is: tools/checks/)
    script_dir = Path(__file__).parent
    project_root = script_dir.parent.parent

    # Collect source files from src/ and tests/
    source_dirs = [
        project_root / "src",
        project_root / "tests",
    ]

    source_files = []
    for source_dir in source_dirs:
        if source_dir.exists():
            source_files.extend(source_dir.rglob("*.cpp"))
            source_files.extend(source_dir.rglob("*.hpp"))

    source_files = sorted(source_files)

    if not source_files:
        print("WARNING: No source files found in src/ or tests/")
        return 0

    all_violations = []

    for filepath in source_files:
        rel_path = filepath.relative_to(project_root)
        violations = find_include_violations(filepath, project_root)

        if violations:
            all_violations.append((rel_path, violations))

    if all_violations:
        print("=" * 70)
        print("VIOLATION: Include policy violations found")
        print("=" * 70)
        print()

        dir_traversal_count = 0
        test_includes_src_count = 0

        for rel_path, violations in sorted(all_violations):
            print(f"{rel_path}:")
            for line_num, line_content, violation_type in violations:
                if violation_type == "directory_traversal":
                    print(f"  Line {line_num}: {line_content}")
                    print(f"    ^ Contains directory-traversing relative include (../)")
                    dir_traversal_count += 1
                elif violation_type == "test_includes_src":
                    print(f"  Line {line_num}: {line_content}")
                    print(f"    ^ Test file should not include src/ paths directly")
                    test_includes_src_count += 1
            print()

        print("=" * 70)
        print(f"Total directory-traversal violations: {dir_traversal_count}")
        print(f"Total test-includes-src violations: {test_includes_src_count}")
        print(f"Total files with violations: {len(all_violations)}")
        print("=" * 70)
        return 1

    print("OK: All includes follow the policy")
    return 0


if __name__ == "__main__":
    sys.exit(main())
