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

        # Track if this line has a namespace declaration
        has_namespace = namespace_pattern.search(line)

        # First: check for namespace declaration with opening brace
        if has_namespace:
            ns_match = namespace_pattern.search(line)
            if ns_match and "{" in line[ns_match.end() :]:
                scope_stack.append(("namespace", None))

        # Get current type nesting depth from scope stack (before processing this line)
        base_type_depth = sum(1 for s in scope_stack if s[0] == "type")

        # Find all type keywords on this line
        type_matches = list(type_keyword_pattern.finditer(line))

        # Process each type keyword in order, tracking brace depth at each position
        for match in type_matches:
            keyword = match.group(1)  # 'class' or 'struct'
            type_name = match.group(2)
            type_start = match.start()

            # Skip if preceded by 'enum' (enum class/struct)
            preceding = line[:type_start].strip()
            if preceding.endswith("enum"):
                continue

            # Skip template specializations
            if "template" in preceding:
                continue

            # Calculate brace depth at this keyword's position:
            # Count braces before this keyword on this line
            line_before_keyword = line[:type_start]
            open_on_line_before = line_before_keyword.count("{")
            close_on_line_before = line_before_keyword.count("}")

            # Check if any preceding type keywords on this line opened a brace before this keyword
            types_with_braces_before = 0
            for prev_match in type_matches:
                if prev_match.end() < type_start:
                    # Check if there's a '{' between the end of previous type and this keyword
                    text_between = line[prev_match.end() : type_start]
                    if "{" in text_between:
                        types_with_braces_before += 1

            # Total type depth at this position = base depth + types that opened braces
            total_type_depth = base_type_depth + types_with_braces_before

            # Count only top-level types (not nested inside another type)
            if total_type_depth == 0:
                types.append((type_name, line_num))

        # After processing all type keywords, update scope stack based on all braces
        open_braces = line.count("{")
        close_braces = line.count("}")

        # Process opening braces - determine if they belong to types or other constructs
        brace_positions = [i for i, c in enumerate(line) if c == "{"]

        for brace_pos in brace_positions:
            # Check if this brace follows a type definition
            matched_type_name = None
            for match in type_matches:
                match_end = match.end()
                if match_end <= brace_pos:
                    text_between = line[match_end:brace_pos]
                    stripped_between = text_between.strip()
                    # Type braces are either immediately after the type name or
                    # after a constructor initializer list (starting with ':')
                    if stripped_between == "" or stripped_between.startswith(":"):
                        matched_type_name = match.group(2)
                        break

            if matched_type_name:
                scope_stack.append(("type", matched_type_name))
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
