#!/usr/bin/env python3
"""
Deterministic check for one-type-per-public-header policy.

Scans all include/mcp/**/*.hpp files and exits non-zero if any file
defines more than one top-level class or struct.
"""

import re
import sys
from pathlib import Path
from typing import List, Optional, Tuple


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


def strip_preprocessor_directives(content: str) -> str:
    """
    Replace preprocessor directive lines with whitespace.

    Preserves line and column positions by replacing non-newline characters
    with spaces.
    """
    lines = content.splitlines(keepends=True)
    sanitized_lines = []

    for line in lines:
        if line.lstrip().startswith("#"):
            sanitized_line = "".join("\n" if ch == "\n" else " " for ch in line)
            sanitized_lines.append(sanitized_line)
        else:
            sanitized_lines.append(line)

    return "".join(sanitized_lines)


def tokenize_cpp(content: str) -> List[Tuple[str, str, int]]:
    """
    Tokenize a C++ source string into (kind, value, line_number) tuples.

    Only emits the tokens needed by the one-type-per-header parser.
    """
    tokens: List[Tuple[str, str, int]] = []
    i = 0
    line_num = 1
    n = len(content)

    while i < n:
        ch = content[i]

        if ch == "\n":
            line_num += 1
            i += 1
            continue

        if ch.isspace():
            i += 1
            continue

        if ch.isalpha() or ch == "_":
            start = i
            i += 1
            while i < n and (content[i].isalnum() or content[i] == "_"):
                i += 1
            tokens.append(("identifier", content[start:i], line_num))
            continue

        if ch == ":" and i + 1 < n and content[i + 1] == ":":
            tokens.append(("symbol", "::", line_num))
            i += 2
            continue

        if ch in "{};<>:(),[]=":
            tokens.append(("symbol", ch, line_num))

        i += 1

    return tokens


def count_top_level_types(content: str) -> List[Tuple[str, int]]:
    """
    Count top-level class and struct definitions.

    A type is considered "top-level" if it's not inside another class/struct definition.
    Types inside namespaces ARE considered top-level (namespaces don't create type nesting).
    Forward declarations (e.g., "class Foo;") are excluded.

    Returns list of (type_name, line_number) tuples for top-level types.
    """
    types: List[Tuple[str, int]] = []

    sanitized = strip_preprocessor_directives(content)
    tokens = tokenize_cpp(sanitized)

    scope_stack: List[str] = []
    type_scope_depth = 0

    pending_namespace = False
    pending_type: Optional[dict] = None

    expect_template_params = False
    template_param_depth = 0

    previous_token_kind: Optional[str] = None
    previous_token_value: Optional[str] = None

    for token_kind, token_value, line_num in tokens:
        if (
            token_kind == "identifier"
            and token_value == "template"
            and pending_type is None
        ):
            expect_template_params = True

        if token_kind == "symbol" and token_value == "<":
            if expect_template_params:
                template_param_depth = 1
                expect_template_params = False
            elif template_param_depth > 0:
                template_param_depth += 1
        elif token_kind == "symbol" and token_value == ">":
            if template_param_depth > 0:
                template_param_depth -= 1

        if pending_type is not None:
            if token_kind == "identifier" and pending_type["name"] is None:
                pending_type["name"] = token_value

            if token_kind == "symbol" and token_value == "{":
                if (
                    pending_type["name"]
                    and pending_type["top_level"]
                    and type_scope_depth == 0
                ):
                    types.append((pending_type["name"], pending_type["line"]))

                scope_stack.append("type")
                type_scope_depth += 1

                pending_type = None
                pending_namespace = False
                expect_template_params = False

            elif token_kind == "symbol" and token_value == ";":
                pending_type = None
                pending_namespace = False
                expect_template_params = False

            elif token_kind == "symbol" and token_value == "}":
                pending_type = None
                pending_namespace = False
                expect_template_params = False
                if scope_stack:
                    popped = scope_stack.pop()
                    if popped == "type":
                        type_scope_depth -= 1

            previous_token_kind = token_kind
            previous_token_value = token_value
            continue

        if token_kind == "identifier" and token_value == "namespace":
            pending_namespace = True

        elif (
            token_kind == "identifier"
            and token_value in {"class", "struct"}
            and template_param_depth == 0
        ):
            is_enum_type = (
                previous_token_kind == "identifier" and previous_token_value == "enum"
            )

            if not is_enum_type:
                pending_type = {
                    "name": None,
                    "line": line_num,
                    "top_level": type_scope_depth == 0,
                }

        elif token_kind == "symbol" and token_value == "{":
            if pending_namespace:
                scope_stack.append("namespace")
            else:
                scope_stack.append("other")

            pending_namespace = False
            expect_template_params = False

        elif token_kind == "symbol" and token_value == "}":
            pending_namespace = False
            expect_template_params = False
            if scope_stack:
                popped = scope_stack.pop()
                if popped == "type":
                    type_scope_depth -= 1

        elif token_kind == "symbol" and token_value == ";":
            pending_namespace = False
            expect_template_params = False

        previous_token_kind = token_kind
        previous_token_value = token_value

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
