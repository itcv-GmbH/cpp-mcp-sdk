#!/usr/bin/env python3
"""
Deterministic check for namespace-to-path mapping in public headers.

Scans all include/mcp/**/*.hpp files and validates that each top-level
type declaration (class, struct, enum) is enclosed by the expected namespace
derived from the file path.

Expected namespace mapping:
- include/mcp/<module>/...     -> namespace mcp::<module>::...
- include/mcp/<module>/detail/... -> namespace mcp::<module>::detail::...
- include/mcp/detail/...       -> namespace mcp::detail::...

Also detects double detail (mcp::detail::detail) which is prohibited.
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
    Replace preprocessor directives with whitespace, including #if 0 blocks.

    Preserves line and column positions by replacing non-newline characters
    with spaces. Properly handles conditional compilation by tracking
    #if/#ifdef/#ifndef/#elif/#else/#endif blocks and excluding content
    from inactive branches.

    Only the first true branch in a conditional chain (#if/#elif/#else)
    is active. Once a branch is taken, subsequent branches are inactive.
    """
    lines = content.splitlines(keepends=True)
    sanitized_lines = []

    # Track preprocessor conditional state
    # Each element is a tuple (is_active, was_taken) where:
    # - is_active: whether current branch is active
    # - was_taken: whether any branch in this conditional chain was already taken
    conditional_stack: List[Tuple[bool, bool]] = []
    current_branch_active = True

    for line in lines:
        stripped = line.lstrip()

        if stripped.startswith("#"):
            # Check for conditional directives
            directive_match = re.match(
                r"#\s*(if|ifdef|ifndef|elif|else|endif)\b", stripped
            )

            if directive_match:
                directive = directive_match.group(1)

                if directive in ("if", "ifdef", "ifndef"):
                    # Check if this is #if 0 or #ifdef 0 (always false)
                    if directive == "if":
                        # Check for #if 0 or #if (0)
                        rest = stripped[directive_match.end() :].strip()
                        if rest == "0" or rest == "(0)":
                            conditional_stack.append((False, False))
                        else:
                            conditional_stack.append((True, True))
                    elif directive == "ifdef" or directive == "ifndef":
                        # For #ifdef 0 or #ifndef 0, 0 is not a defined macro
                        rest = stripped[directive_match.end() :].strip()
                        if rest == "0":
                            # #ifdef 0 is always false, #ifndef 0 is always true
                            is_active = directive == "ifndef"
                            conditional_stack.append((is_active, is_active))
                        else:
                            conditional_stack.append((True, True))

                    # Update current branch state
                    current_branch_active = all(
                        active for active, _ in conditional_stack
                    )

                elif directive == "elif":
                    # Pop the previous if/elif and check if a branch was taken
                    was_taken = False
                    if conditional_stack:
                        _, was_taken = conditional_stack.pop()

                    # If a previous branch was taken, this elif is inactive
                    if was_taken:
                        conditional_stack.append((False, True))
                    else:
                        # Check for #elif 0
                        rest = stripped[directive_match.end() :].strip()
                        if rest == "0" or rest == "(0)":
                            conditional_stack.append((False, False))
                        else:
                            conditional_stack.append((True, True))

                    current_branch_active = all(
                        active for active, _ in conditional_stack
                    )

                elif directive == "else":
                    # Pop the previous if/elif and check if a branch was taken
                    was_taken = False
                    if conditional_stack:
                        _, was_taken = conditional_stack.pop()

                    # If a previous branch was taken, else is inactive
                    # Otherwise, else is always active (inverse of previous)
                    if was_taken:
                        conditional_stack.append((False, True))
                    else:
                        conditional_stack.append((True, True))

                    current_branch_active = all(
                        active for active, _ in conditional_stack
                    )

                elif directive == "endif":
                    # Pop the conditional
                    if conditional_stack:
                        conditional_stack.pop()
                    current_branch_active = all(
                        active for active, _ in conditional_stack
                    )

            # Replace the directive line with spaces (preserving newlines)
            sanitized_line = "".join("\n" if ch == "\n" else " " for ch in line)
            sanitized_lines.append(sanitized_line)
        else:
            # Regular line - preserve only if current branch is active
            if current_branch_active:
                sanitized_lines.append(line)
            else:
                # Replace with spaces (preserving newlines)
                sanitized_line = "".join("\n" if ch == "\n" else " " for ch in line)
                sanitized_lines.append(sanitized_line)

    return "".join(sanitized_lines)


def tokenize_cpp(content: str) -> List[Tuple[str, str, int]]:
    """
    Tokenize a C++ source string into (kind, value, line_number) tuples.

    Emits tokens needed for namespace layout validation.
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


def derive_expected_namespace(rel_path: Path) -> List[str]:
    """
    Derive the expected namespace components from the header path.

    Args:
        rel_path: Path relative to include/ (e.g., mcp/client/client_class.hpp)

    Returns:
        List of namespace components (e.g., ["mcp", "client"])
    """
    # Remove filename to get directory parts
    path_parts = list(rel_path.parent.parts)

    # The path should start with 'mcp' after include/
    # path_parts[0] should be 'mcp'
    if not path_parts or path_parts[0] != "mcp":
        return []

    # Build namespace from remaining parts
    # include/mcp/<module>/... -> mcp::<module>::...
    namespace_parts = ["mcp"]

    for part in path_parts[1:]:
        namespace_parts.append(part)

    return namespace_parts


def format_namespace(namespace_parts: List[str]) -> str:
    """Format namespace parts as a string."""
    return "::".join(namespace_parts)


def check_double_detail(namespace_parts: List[str]) -> bool:
    """
    Check if namespace contains double detail (mcp::detail::detail).

    Returns True if double detail is detected.
    """
    for i in range(len(namespace_parts) - 1):
        if namespace_parts[i] == "detail" and namespace_parts[i + 1] == "detail":
            return True
    return False


def find_namespace_violations(
    content: str, expected_namespace: List[str]
) -> List[Tuple[str, str, int, List[str]]]:
    """
    Find top-level declarations that are not in the expected namespace.

    Args:
        content: The sanitized file content
        expected_namespace: List of expected namespace parts

    Returns:
        List of (declaration_type, name, line_number, actual_namespace) tuples
        for violations
    """
    violations: List[Tuple[str, str, int, List[str]]] = []

    sanitized = strip_preprocessor_directives(content)
    tokens = tokenize_cpp(sanitized)

    # Track scope: each element is ("namespace", parts_count), "type", or "other"
    # For namespaces, we track how many parts were pushed to properly unwind nested ns
    scope_stack: List[Tuple[str, int]] = []
    # Track namespace stack: list of namespace names we're currently inside
    namespace_stack: List[str] = []

    # Track pending state
    pending_namespace = False
    pending_namespace_name: Optional[str] = None
    pending_type: Optional[dict] = None

    # Track template state
    expect_template_params = False
    template_param_depth = 0

    # Track the previous token for context
    previous_token_kind: Optional[str] = None
    previous_token_value: Optional[str] = None

    # Declaration keywords we care about
    DECL_KEYWORDS = {"class", "struct", "enum"}

    i = 0
    while i < len(tokens):
        token_kind, token_value, line_num = tokens[i]

        # Handle template
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

        # Handle namespace keyword
        if token_kind == "identifier" and token_value == "namespace":
            pending_namespace = True
            pending_namespace_name = None

        # Handle namespace name after 'namespace' keyword
        elif pending_namespace and token_kind == "identifier":
            if pending_namespace_name is None:
                # First part of namespace name
                pending_namespace_name = token_value
            else:
                # Could be part of nested namespace declaration (namespace mcp::client)
                # For now, we just track the first name we see
                pass

        # Handle :: in namespace declaration (e.g., namespace mcp::client)
        elif pending_namespace and token_kind == "symbol" and token_value == "::":
            # Continue collecting namespace parts
            pass

        # Handle opening brace
        if token_kind == "symbol" and token_value == "{":
            # Handle type definition opening BEFORE namespace handling
            # This ensures we check violations at the correct scope level
            type_entered = False
            if pending_type is not None:
                # This is a type definition with a body
                # Check violation BEFORE pushing to scope stack (at top level, depth should be 0)
                type_scope_depth = sum(1 for s, _ in scope_stack if s == "type")
                if type_scope_depth == 0:
                    # Check namespace
                    actual_namespace = list(namespace_stack)
                    if actual_namespace != expected_namespace:
                        violations.append(
                            (
                                pending_type["keyword"],
                                pending_type["name"],
                                pending_type["line"],
                                actual_namespace,
                            )
                        )
                scope_stack.append(("type", 0))
                pending_type = None
                type_entered = True

            # Handle namespace opening
            elif pending_namespace and pending_namespace_name:
                # Handle nested namespace syntax: namespace mcp::client {
                # We need to look back to collect all parts
                namespace_parts = collect_namespace_parts(tokens, i)
                namespace_stack.extend(namespace_parts)
                # Track how many parts we pushed so we can pop the right amount
                scope_stack.append(("namespace", len(namespace_parts)))
                pending_namespace = False
                pending_namespace_name = None

            # If neither type nor namespace, it's something else
            elif not type_entered:
                scope_stack.append(("other", 0))
            expect_template_params = False

        # Handle closing brace
        elif token_kind == "symbol" and token_value == "}":
            if scope_stack:
                scope_kind, parts_count = scope_stack.pop()
                if scope_kind == "namespace" and namespace_stack:
                    # Pop the correct number of namespace parts
                    # For nested namespaces like `namespace a::b {`, we pushed
                    # multiple parts and need to pop all of them
                    for _ in range(parts_count):
                        if namespace_stack:
                            namespace_stack.pop()
            pending_namespace = False
            pending_type = None
            expect_template_params = False

        # Handle semicolon
        elif token_kind == "symbol" and token_value == ";":
            if pending_type is not None:
                # Forward declaration, not a definition - ignore it
                pending_type = None
            pending_namespace = False
            expect_template_params = False

        # Handle type declarations (class, struct, enum)
        elif (
            token_kind == "identifier"
            and token_value in DECL_KEYWORDS
            and template_param_depth == 0
            and pending_type is None
        ):
            # Check if this is an enum class (C++11 enum class)
            # In that case, we want to capture the type name after 'class'
            is_enum_class = token_value == "enum"

            if is_enum_class:
                # Look ahead to see if next non-whitespace is 'class'
                j = i + 1
                while j < len(tokens) and tokens[j][0] != "identifier":
                    if tokens[j][1] not in {
                        "::",
                        "<",
                        ">",
                        "{",
                        "}",
                        ";",
                        ":",
                        "(",
                        ")",
                        ",",
                        "[",
                        "]",
                        "=",
                    }:
                        break
                    j += 1
                if j < len(tokens) and tokens[j][1] == "class":
                    # This is 'enum class', skip the 'class' keyword
                    i = j
                    token_kind, token_value, line_num = tokens[i]

            # Check if we're at top level (not inside a class/struct body)
            type_scope_depth = sum(1 for s, _ in scope_stack if s == "type")
            if type_scope_depth == 0:
                pending_type = {
                    "keyword": token_value if not is_enum_class else "enum",
                    "name": None,
                    "line": line_num,
                }

        # Handle type name after class/struct/enum keyword
        elif (
            pending_type is not None
            and token_kind == "identifier"
            and pending_type["name"] is None
        ):
            pending_type["name"] = token_value

        # Handle semicolon - forward declarations don't count
        elif token_kind == "symbol" and token_value == ";":
            if pending_type is not None:
                # Forward declaration, not a definition - ignore it
                pending_type = None

        previous_token_kind = token_kind
        previous_token_value = token_value
        i += 1

    return violations


def collect_namespace_parts(
    tokens: List[Tuple[str, str, int]], open_brace_idx: int
) -> List[str]:
    """
    Collect namespace parts before an opening brace.

    Given tokens like: [..., namespace, mcp, ::, client, ::, detail, {, ...]
    and open_brace_idx pointing to {, returns ["mcp", "client", "detail"]
    """
    parts = []

    # Walk backwards from the opening brace
    i = open_brace_idx - 1

    # Skip whitespace-equivalent tokens we didn't emit (we don't emit whitespace)
    # We just walk back collecting identifiers separated by ::

    expect_identifier = True
    while i >= 0:
        token_kind, token_value, _ = tokens[i]

        if expect_identifier:
            if token_kind == "identifier" and token_value != "namespace":
                parts.append(token_value)
                expect_identifier = False
            elif token_kind == "symbol" and token_value == "::":
                # Expected identifier but got ::, this is invalid syntax but we handle it
                break
            else:
                # Something else, stop collecting
                break
        else:
            # Expecting ::
            if token_kind == "symbol" and token_value == "::":
                expect_identifier = True
            else:
                # Something else, stop collecting
                break

        i -= 1

    # Parts were collected in reverse order
    parts.reverse()
    return parts


def check_file(
    filepath: Path, project_root: Path
) -> Tuple[Path, List[Tuple[str, str, int, List[str]]], Optional[List[str]]]:
    """
    Check a single header file for namespace violations.

    Returns:
        (relative_path, violations, double_detail_error)
        - violations: list of (type_keyword, type_name, line_num, actual_namespace)
        - double_detail_error: namespace parts if double detail detected, else None
    """
    rel_path = filepath.relative_to(project_root / "include")
    content = filepath.read_text(encoding="utf-8", errors="replace")

    # Derive expected namespace from path
    expected_namespace = derive_expected_namespace(rel_path)

    # Check for double detail in expected namespace
    if check_double_detail(expected_namespace):
        return rel_path, [], expected_namespace

    # Strip comments and strings
    stripped_content = strip_comments_and_strings(content)

    # Find violations
    violations = find_namespace_violations(stripped_content, expected_namespace)

    return rel_path, violations, None


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

    namespace_violations: List[Tuple[Path, List[Tuple[str, str, int, List[str]]]]] = []
    double_detail_violations: List[Tuple[Path, List[str]]] = []

    for filepath in header_files:
        rel_path, violations, double_detail = check_file(filepath, project_root)

        if double_detail is not None:
            double_detail_violations.append((rel_path, double_detail))
        elif violations:
            namespace_violations.append((rel_path, violations))

    exit_code = 0

    # Report namespace layout violations
    if namespace_violations:
        print("=" * 70)
        print("VIOLATION: Namespace layout does not match file path")
        print("=" * 70)
        print()

        for rel_path, violations in sorted(namespace_violations):
            print(f"{rel_path}:")
            for type_keyword, type_name, line_num, actual_namespace in violations:
                expected_ns = derive_expected_namespace(rel_path)
                actual_ns_str = (
                    format_namespace(actual_namespace)
                    if actual_namespace
                    else "(global)"
                )
                expected_ns_str = format_namespace(expected_ns)
                print(f"  - {type_keyword} {type_name} (line {line_num})")
                print(f"    Expected: namespace {expected_ns_str}")
                print(f"    Actual:   namespace {actual_ns_str}")
            print()

        print("=" * 70)
        print(f"Total files with namespace violations: {len(namespace_violations)}")
        print("=" * 70)
        exit_code = 1

    # Report double detail violations
    if double_detail_violations:
        if namespace_violations:
            print()

        print("=" * 70)
        print("VIOLATION: Double detail namespace (mcp::detail::detail) detected")
        print("=" * 70)
        print()

        for rel_path, namespace_parts in sorted(double_detail_violations):
            ns_str = format_namespace(namespace_parts)
            print(f"{rel_path}: namespace {ns_str}")

        print()
        print("=" * 70)
        print(f"Total files with double detail: {len(double_detail_violations)}")
        print("=" * 70)
        exit_code = 1

    if exit_code == 0:
        print("OK: All public headers follow namespace layout conventions")

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
