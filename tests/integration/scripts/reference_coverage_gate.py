#!/usr/bin/env python3
"""
Coverage gate for MCP reference SDK integration tests.

This script parses the COVERAGE.md file and verifies that all required
protocol surface items from the MCP 2025-11-25 specification are mapped
to at least one test.
"""

import argparse
import re
import sys
from pathlib import Path
from typing import Optional


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify protocol surface coverage for MCP reference SDK integration tests"
    )
    parser.add_argument(
        "--coverage-file",
        required=True,
        help="Path to the COVERAGE.md file",
    )
    return parser.parse_args()


def extract_protocol_items(content: str) -> tuple[list[str], list[str]]:
    """
    Extract protocol items from COVERAGE.md.

    Returns:
        Tuple of (requests list, notifications list)
    """
    requests = []
    notifications = []

    # Find the Requests section and extract items
    requests_match = re.search(
        r"### Requests\s*\n\s*\|.*?\s*\|\s*\n(.*?)(?=###|\Z)",
        content,
        re.DOTALL | re.IGNORECASE,
    )
    if requests_match:
        requests_section = requests_match.group(1)
        # Match protocol item in the first column, excluding separator lines
        item_pattern = re.compile(r"^\|\s*`?([^`|\n]+)`?\s*\|", re.MULTILINE)
        requests = item_pattern.findall(requests_section)
        # Filter out empty strings and separator lines (just dashes)
        requests = [
            item.strip()
            for item in requests
            if item.strip() and not re.match(r"^-+$", item.strip())
        ]

    # Find the Notifications section and extract items
    notifications_match = re.search(
        r"### Notifications\s*\n\s*\|.*?\s*\|\s*\n(.*?)(?=###|\Z)",
        content,
        re.DOTALL | re.IGNORECASE,
    )
    if notifications_match:
        notifications_section = notifications_match.group(1)
        item_pattern = re.compile(r"^\|\s*`?([^`|\n]+)`?\s*\|", re.MULTILINE)
        notifications = item_pattern.findall(notifications_section)
        # Filter out empty strings and separator lines (just dashes)
        notifications = [
            item.strip()
            for item in notifications
            if item.strip() and not re.match(r"^-+$", item.strip())
        ]

    return requests, notifications


def extract_test_mapping(content: str) -> dict[str, list[str]]:
    """
    Extract test to protocol item mappings from the Test Mapping section.

    Returns:
        Dictionary mapping test names to list of protocol items they cover
    """
    mapping = {}

    # Find the Test Mapping section
    mapping_match = re.search(
        r"### Test Mapping.*?\n(.*?)(?=##|\Z)", content, re.DOTALL | re.IGNORECASE
    )

    if not mapping_match:
        return mapping

    mapping_section = mapping_match.group(1)

    # Skip the template comment if present
    mapping_section = re.sub(
        r"<!--.*?-->", "", mapping_section, flags=re.DOTALL
    ).strip()

    if not mapping_section or mapping_section.startswith("(To be populated)"):
        return mapping

    # Parse each line: test_name: item1, item2, ...
    line_pattern = re.compile(r"^([^:\s]+):\s*(.+)$", re.MULTILINE)
    for match in line_pattern.finditer(mapping_section):
        test_name = match.group(1).strip()
        items_str = match.group(2).strip()
        # Split by comma and clean up
        items = [item.strip() for item in items_str.split(",")]
        items = [item for item in items if item]
        mapping[test_name] = items

    return mapping


def check_coverage(
    requests: list[str], notifications: list[str], test_mapping: dict[str, list[str]]
) -> tuple[bool, list[str]]:
    """
    Check that all protocol items are covered by at least one test.

    Returns:
        Tuple of (all_covered, uncovered_items)
    """
    all_items = requests + notifications
    covered_items: set[str] = set()

    for items in test_mapping.values():
        covered_items.update(items)

    uncovered = [item for item in all_items if item not in covered_items]

    return len(uncovered) == 0, uncovered


def main() -> int:
    args = parse_args()
    coverage_file = Path(args.coverage_file)

    if not coverage_file.is_file():
        print(f"Error: Coverage file not found: {coverage_file}", file=sys.stderr)
        return 1

    content = coverage_file.read_text(encoding="utf-8")

    requests, notifications = extract_protocol_items(content)

    if not requests and not notifications:
        print("Error: Could not find protocol items in coverage file", file=sys.stderr)
        return 1

    test_mapping = extract_test_mapping(content)

    all_covered, uncovered = check_coverage(requests, notifications, test_mapping)

    if all_covered:
        print("All protocol surface items are covered by tests.")
        return 0
    else:
        print("ERROR: The following protocol surface items are not covered:")
        for item in sorted(uncovered):
            print(f"  - {item}")
        print()
        print(f"Total uncovered: {len(uncovered)}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
