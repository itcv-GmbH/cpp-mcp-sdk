#!/usr/bin/env python3
"""
Deterministic check for one-type-per-public-header policy.

Scans all include/mcp/**/*.hpp files and exits non-zero if any file
defines more than one top-level class or struct.
"""

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
            while i < n and content[i] != "\n":
                result.append(" ")
                i += 1
            if i < n:
                result.append(content[i])
                i += 1
            continue

        # Check for multi-line comment
        if i + 1 < n and content[i : i + 2] == "/*":
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
    Count top-level class and struct definitions.

    A type is considered "top-level" if it's not inside another class/struct definition.
    Types inside namespaces ARE considered top-level (namespaces don't create type nesting).
    Forward declarations (e.g., "class Foo;") are excluded.

    Returns list of (type_name, line_number) tuples for top-level types.
    """
    types = []
    lines = content.split("\n")

    # Track the scope stack: each entry is ('namespace'|'type'|'other', name)
    scope_stack = []

    # Pattern to detect namespace keyword
    namespace_pattern = re.compile(r"\bnamespace\b")
    # Pattern to match class or struct keyword followed by name
    type_keyword_pattern = re.compile(r"\b(class|struct)\s+(\w+)")
    # Pattern to detect forward declaration: class Foo; or struct Foo;
    forward_decl_pattern = re.compile(r"\b(class|struct)\s+\w+\s*;")

    for line_num, line in enumerate(lines, 1):
        stripped = line.strip()

        # Skip empty lines and preprocessor directives
        if not stripped or stripped.startswith("#"):
            continue

        # Skip forward declarations (class Foo; or struct Foo;)
        if forward_decl_pattern.search(line):
            continue

        # Count braces on this line
        open_braces = line.count("{")
        close_braces = line.count("}")

        # Check if this line starts a namespace
        if namespace_pattern.search(line):
            # Check if there's an opening brace on this line or we need to wait
            if open_braces > 0:
                scope_stack.append(("namespace", None))
                open_braces -= 1
            else:
                # Multi-line namespace declaration - mark that we're expecting a brace
                scope_stack.append(("namespace_pending", None))

        # Check if we need to convert pending namespace to active
        if (
            scope_stack
            and scope_stack[-1][0] == "namespace_pending"
            and open_braces > 0
        ):
            scope_stack[-1] = ("namespace", None)
            open_braces -= 1

        # Check for type definitions BEFORE processing remaining braces
        # A type is at "top-level" if there's no 'type' entry in the scope stack
        type_count_in_stack = sum(1 for s in scope_stack if s[0] == "type")

        for match in type_keyword_pattern.finditer(line):
            keyword = match.group(1)  # 'class' or 'struct'
            type_name = match.group(2)

            # Skip if preceded by 'enum' (enum class/struct)
            start_pos = match.start()
            preceding = line[:start_pos].strip()
            if preceding.endswith("enum"):
                continue

            # Skip template specializations
            if "template" in preceding:
                continue

            # A type is "top-level" if type_count_in_stack == 0
            if type_count_in_stack == 0:
                types.append((type_name, line_num))

        # Process remaining opening braces
        for _ in range(open_braces):
            # Determine if this is a type scope or other scope
            # If we just saw a class/struct keyword on this line, it's a type scope
            if type_keyword_pattern.search(line):
                # Find the last type name on this line
                matches = list(type_keyword_pattern.finditer(line))
                if matches:
                    last_match = matches[-1]
                    type_name = last_match.group(2)
                    scope_stack.append(("type", type_name))
                else:
                    scope_stack.append(("other", None))
            else:
                scope_stack.append(("other", None))

        # Process closing braces
        for _ in range(close_braces):
            if scope_stack:
                scope_stack.pop()

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
