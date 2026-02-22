#!/usr/bin/env python3
"""
Deterministic check for one-type-per-public-header policy.

Scans all include/mcp/**/*.hpp files and exits non-zero if any file
defines more than one top-level class or struct.
"""

import os
import re
import sys
from pathlib import Path
from typing import List, Tuple


def strip_comments_and_strings(content: str) -> str:
    """
    Strip C++ comments and string literals from content.
    Returns the content with comments and strings replaced by spaces
    to preserve position information.
    """
    result = []
    i = 0
    n = len(content)

    while i < n:
        # Check for single-line comment
        if i + 1 < n and content[i : i + 2] == "//":
            # Skip until end of line
            while i < n and content[i] != "\n":
                result.append(" ")
                i += 1
            if i < n:
                result.append(content[i])
                i += 1
            continue

        # Check for multi-line comment
        if i + 1 < n and content[i : i + 2] == "/*":
            # Skip until */
            result.append(" ")
            result.append(" ")
            i += 2
            while i < n and not (content[i - 1 : i + 1] == "*/"):
                if content[i] == "\n":
                    result.append("\n")
                else:
                    result.append(" ")
                i += 1
            if i < n:
                result.append(" ")
                i += 1
            continue

        # Check for raw string literal R"delimiter(content)delimiter"
        raw_string_match = re.match(r'R"([^()\\\s]{0,16})\(', content[i:])
        if raw_string_match:
            delimiter = raw_string_match.group(1)
            end_marker = f'){delimiter}"'
            i += raw_string_match.end()
            # Replace with spaces, preserving newlines
            while i < n:
                if content[i : i + len(end_marker)] == end_marker:
                    result.extend([" "] * len(end_marker))
                    i += len(end_marker)
                    break
                if content[i] == "\n":
                    result.append("\n")
                else:
                    result.append(" ")
                i += 1
            continue

        # Check for double-quoted string literal
        if content[i] == '"':
            result.append(" ")
            i += 1
            while i < n:
                if content[i] == "\\" and i + 1 < n:
                    # Escape sequence
                    result.append(" ")
                    result.append(" ")
                    i += 2
                elif content[i] == '"':
                    result.append(" ")
                    i += 1
                    break
                else:
                    if content[i] == "\n":
                        result.append("\n")
                    else:
                        result.append(" ")
                    i += 1
            continue

        # Check for single-quoted character literal
        if content[i] == "'":
            result.append(" ")
            i += 1
            while i < n:
                if content[i] == "\\" and i + 1 < n:
                    result.append(" ")
                    result.append(" ")
                    i += 2
                elif content[i] == "'":
                    result.append(" ")
                    i += 1
                    break
                else:
                    if content[i] == "\n":
                        result.append("\n")
                    else:
                        result.append(" ")
                    i += 1
            continue

        result.append(content[i])
        i += 1

    return "".join(result)


def count_top_level_types(content: str) -> List[Tuple[str, int]]:
    """
    Count top-level class and struct declarations.
    Uses a brace-depth state machine to track nesting.

    Returns list of (type_name, line_number) tuples for top-level types.
    """
    # Pattern to match class or struct declarations (but not enum class/enum struct)
    # Matches: class Name, struct Name, but NOT enum class Name, enum struct Name
    # Also handles templates by ignoring lines starting with 'template'
    type_pattern = re.compile(r"\b(class|struct)\s+(\w+)", re.MULTILINE)

    types = []
    brace_depth = 0
    lines = content.split("\n")

    # First pass: track brace depth for each position
    line_brace_depths = []
    current_depth = 0

    for line in lines:
        line_brace_depths.append(current_depth)
        # Count braces, ignoring braces in comments/strings
        for char in line:
            if char == "{":
                current_depth += 1
            elif char == "}":
                current_depth -= 1

    # Second pass: find type declarations at brace depth 0
    for line_num, line in enumerate(lines, 1):
        # Skip preprocessor directives
        stripped = line.strip()
        if stripped.startswith("#"):
            continue

        # Skip template declarations (the actual type follows)
        if stripped.startswith("template"):
            continue

        # Skip using declarations
        if stripped.startswith("using "):
            continue

        # Skip typedef declarations
        if stripped.startswith("typedef "):
            continue

        # Find all potential type declarations on this line
        for match in type_pattern.finditer(line):
            keyword = match.group(1)  # 'class' or 'struct'
            type_name = match.group(2)

            # Check if preceded by 'enum' (for enum class/struct)
            start_pos = match.start()
            preceding = line[:start_pos].strip()
            if preceding.endswith("enum"):
                continue

            # Check if brace depth at this line is 0 (top-level)
            if line_brace_depths[line_num - 1] == 0:
                types.append((type_name, line_num))

    return types


def check_file(filepath: Path) -> Tuple[Path, List[Tuple[str, int]]]:
    """Check a single header file and return violations."""
    content = filepath.read_text(encoding="utf-8", errors="replace")

    # Strip comments and strings
    stripped_content = strip_comments_and_strings(content)

    # Count top-level types
    types = count_top_level_types(stripped_content)

    return filepath, types


def main() -> int:
    """Main entry point."""
    # Find the project root (where this script is: tools/checks/)
    script_dir = Path(__file__).parent
    project_root = script_dir.parent.parent

    # Find all .hpp files in include/mcp/
    include_dir = project_root / "include" / "mcp"
    if not include_dir.exists():
        print(f"ERROR: Include directory not found: {include_dir}")
        return 1

    header_files = sorted(include_dir.rglob("*.hpp"))

    if not header_files:
        print("WARNING: No .hpp files found in include/mcp/")
        return 0

    violations = []

    for filepath in header_files:
        rel_path = filepath.relative_to(project_root)
        file_path, types = check_file(filepath)

        if len(types) > 1:
            violations.append((rel_path, types))

    if violations:
        print("=" * 70)
        print("VIOLATION: Public header files must define only one top-level type")
        print("=" * 70)
        print()

        for rel_path, types in sorted(violations):
            print(f"{rel_path}: {len(types)} top-level types defined")
            for type_name, line_num in types:
                print(f"  - {type_name} (line {line_num})")
            print()

        print("=" * 70)
        print(f"Total files with violations: {len(violations)}")
        print("=" * 70)
        return 1

    print("OK: All public headers define exactly one top-level type")
    return 0


if __name__ == "__main__":
    sys.exit(main())
