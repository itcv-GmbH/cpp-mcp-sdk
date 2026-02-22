#!/usr/bin/env python3
"""Unit tests for check_public_header_one_type.py."""

import sys
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from check_public_header_one_type import (
    count_top_level_types,
    strip_comments_and_strings,
)


def parse_types(source: str):
    stripped = strip_comments_and_strings(source)
    return count_top_level_types(stripped)


class CountTopLevelTypesTests(unittest.TestCase):
    def test_counts_multiple_top_level_types_on_same_line(self):
        types = parse_types("class A {}; class B {};")
        self.assertEqual([name for name, _ in types], ["A", "B"])

    def test_excludes_forward_declaration_per_declaration(self):
        types = parse_types("class A; class B {};")
        self.assertEqual([name for name, _ in types], ["B"])

    def test_excludes_nested_type_allman_style(self):
        types = parse_types(
            """
class A
{
  struct B {};
};
"""
        )
        self.assertEqual([name for name, _ in types], ["A"])

    def test_excludes_nested_type_knr_style(self):
        types = parse_types("class A { struct B {}; };")
        self.assertEqual([name for name, _ in types], ["A"])


if __name__ == "__main__":
    unittest.main()
