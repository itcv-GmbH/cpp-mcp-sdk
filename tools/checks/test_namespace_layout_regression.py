#!/usr/bin/env python3
"""
Regression tests for namespace layout check bugs.

Tests for:
1. Nested namespace handling (namespace a::b {})
2. Scope leakage prevention
3. Preprocessor conditional block handling (#if 0)
"""

import sys
import tempfile
from pathlib import Path

# Add parent directory to path to import the module
sys.path.insert(0, str(Path(__file__).parent))

from check_public_header_namespace_layout import (
    find_namespace_violations,
    strip_preprocessor_directives,
    strip_comments_and_strings,
)


def test_nested_namespace_basic():
    """Test that nested namespace syntax is handled correctly."""
    content = """
namespace mcp::client {
class Client {};
} // namespace mcp::client
"""
    expected = ["mcp", "client"]
    violations = find_namespace_violations(content, expected)
    assert len(violations) == 0, f"Expected no violations, got: {violations}"
    print("✓ test_nested_namespace_basic passed")


def test_nested_namespace_triple():
    """Test triple-nested namespace syntax."""
    content = """
namespace mcp::client::detail {
class Helper {};
} // namespace mcp::client::detail
"""
    expected = ["mcp", "client", "detail"]
    violations = find_namespace_violations(content, expected)
    assert len(violations) == 0, f"Expected no violations, got: {violations}"
    print("✓ test_nested_namespace_triple passed")


def test_nested_namespace_scope_leakage():
    """Test that nested namespace doesn't leak scope."""
    content = """
namespace mcp {
class Good {};
}

namespace mcp::client {
class Client {};
}

namespace mcp {
class AlsoGood {};
}
"""
    # Test 1: Client is in mcp::client, so it should be a violation when expected is mcp
    expected_mcp = ["mcp"]
    violations = find_namespace_violations(content, expected_mcp)
    client_violations = [v for v in violations if v[1] == "Client"]
    assert len(client_violations) == 1, (
        f"Client should be a violation for expected mcp: {violations}"
    )

    # Test 2: AlsoGood should NOT be a violation - it's in mcp, not mcp::client
    # This tests that the scope from mcp::client was properly closed
    alsogood_violations = [v for v in violations if v[1] == "AlsoGood"]
    assert len(alsogood_violations) == 0, (
        f"AlsoGood should not be a violation: {alsogood_violations}"
    )

    # Test 3: When expected is mcp::client, Client should NOT be a violation
    expected_client = ["mcp", "client"]
    violations_client = find_namespace_violations(content, expected_client)
    client_violations = [v for v in violations_client if v[1] == "Client"]
    assert len(client_violations) == 0, (
        f"Client should not be a violation for expected mcp::client: {violations_client}"
    )

    print("✓ test_nested_namespace_scope_leakage passed")


def test_sequential_namespaces():
    """Test that sequential namespaces don't interfere."""
    content = """
namespace outer {
namespace inner1 {
class A {};
}
namespace inner2 {
class B {};
}
}
"""
    # A should be in outer::inner1, B in outer::inner2
    # Both should report violations since expected is ["mcp", "client"]
    expected = ["mcp", "client"]
    violations = find_namespace_violations(content, expected)
    # Both A and B should be violations since they're in wrong namespaces
    assert len(violations) == 2, f"Expected 2 violations, got: {violations}"
    print("✓ test_sequential_namespaces passed")


def test_if_zero_exclusion():
    """Test that #if 0 blocks are excluded."""
    content = """
#if 0
namespace wrong {
class Inactive {};
}
#endif

namespace mcp {
class Active {};
}
"""
    expected = ["mcp"]
    violations = find_namespace_violations(content, expected)
    # Inactive class should not be detected due to #if 0
    assert len(violations) == 0, f"Expected no violations, got: {violations}"
    print("✓ test_if_zero_exclusion passed")


def test_ifdef_zero_exclusion():
    """Test that #ifdef 0 blocks are excluded."""
    content = """
#ifdef 0
namespace wrong {
class Inactive {};
}
#endif

namespace mcp {
class Active {};
}
"""
    expected = ["mcp"]
    violations = find_namespace_violations(content, expected)
    # Inactive class should not be detected due to #ifdef 0
    assert len(violations) == 0, f"Expected no violations, got: {violations}"
    print("✓ test_ifdef_zero_exclusion passed")


def test_ifndef_zero_inclusion():
    """Test that #ifndef 0 blocks are included (since 0 is not defined)."""
    content = """
#ifndef 0
namespace wrong {
class ShouldBeDetected {};
}
#endif

namespace mcp {
class Active {};
}
"""
    expected = ["mcp"]
    violations = find_namespace_violations(content, expected)
    # ShouldBeDetected should be a violation
    assert len(violations) == 1, f"Expected 1 violation, got: {violations}"
    assert violations[0][1] == "ShouldBeDetected"
    print("✓ test_ifndef_zero_inclusion passed")


def test_nested_if_blocks():
    """Test nested #if blocks."""
    content = """
#if 1
namespace mcp {
class Good {};
#if 0
class Bad {};
#endif
}
#endif
"""
    expected = ["mcp"]
    violations = find_namespace_violations(content, expected)
    # Bad should not be detected since it's inside #if 0
    assert len(violations) == 0, f"Expected no violations, got: {violations}"
    print("✓ test_nested_if_blocks passed")


def test_mixed_namespace_styles():
    """Test mix of nested and non-nested namespace styles."""
    content = """
namespace mcp {
namespace client {
class A {};
}
}

namespace mcp::client {
class B {};
}

namespace mcp {
namespace client {
class C {};
}
}
"""
    expected = ["mcp", "client"]
    violations = find_namespace_violations(content, expected)
    assert len(violations) == 0, f"Expected no violations, got: {violations}"
    print("✓ test_mixed_namespace_styles passed")


def test_scope_balance_multiple_files():
    """Test that scope doesn't leak between different namespace blocks."""
    content = """
namespace mcp::client {
class Client1 {};
} // closes mcp::client

// Back at global scope

namespace mcp::client {
class Client2 {};
} // closes mcp::client

// Should still be at global scope
namespace mcp {
class Server {};
}
"""
    # Client1 and Client2 are in mcp::client (correct)
    # Server is in mcp (correct for that block)
    violations_client = find_namespace_violations(content, ["mcp", "client"])
    violations_mcp = find_namespace_violations(content, ["mcp"])

    # Client1 and Client2 should not be violations for mcp::client
    client_violations = [v for v in violations_client if "Client" in v[1]]
    assert len(client_violations) == 0, (
        f"Unexpected client violations: {client_violations}"
    )
    print("✓ test_scope_balance_multiple_files passed")


def test_complex_nested_with_preprocessor():
    """Test complex scenario with nested namespaces and preprocessor."""
    content = """
#if defined(SOME_FEATURE)
namespace feature {
class Conditional {};
}
#endif

namespace mcp::client::detail {
#if 0
class Hidden {};
#endif
class Visible {};
}

namespace mcp::client {
class Public {};
}
"""
    expected = ["mcp", "client", "detail"]
    violations = find_namespace_violations(content, expected)
    # Visible should not be a violation for mcp::client::detail
    # But Public should be a violation since it's in mcp::client
    visible_violations = [v for v in violations if v[1] == "Visible"]
    assert len(visible_violations) == 0, (
        f"Visible should not be a violation: {visible_violations}"
    )
    print("✓ test_complex_nested_with_preprocessor passed")


def test_elif_after_true_if():
    """Test that #elif is inactive when previous #if was true."""
    content = """
#if 1
namespace mcp { class Active {}; }
#elif 1
namespace wrong { class ShouldBeInactive {}; }
#endif
"""
    expected = ["mcp"]
    violations = find_namespace_violations(content, expected)
    # ShouldBeInactive should not be detected since elif is inactive
    inactive_violations = [v for v in violations if v[1] == "ShouldBeInactive"]
    assert len(inactive_violations) == 0, (
        f"ShouldBeInactive should not be detected: {inactive_violations}"
    )
    # Active should not be a violation since it's in correct namespace
    active_violations = [v for v in violations if v[1] == "Active"]
    assert len(active_violations) == 0, (
        f"Active should not be a violation: {active_violations}"
    )
    print("✓ test_elif_after_true_if passed")


def test_elif_chain():
    """Test that only first true #elif is active."""
    content = """
#if 0
namespace wrong { class Inactive1 {}; }
#elif 1
namespace mcp { class Active {}; }
#elif 1
namespace wrong { class Inactive2 {}; }
#endif
"""
    expected = ["mcp"]
    violations = find_namespace_violations(content, expected)
    # Only Active should exist and it should not be a violation
    assert len(violations) == 0, f"Expected no violations, got: {violations}"
    # Verify stripping
    result = strip_preprocessor_directives(content)
    assert "Inactive1" not in result, "Inactive1 should be stripped"
    assert "Inactive2" not in result, "Inactive2 should be stripped"
    assert "Active" in result, "Active should be preserved"
    print("✓ test_elif_chain passed")


def test_elif_after_false_if():
    """Test that #elif is active when previous #if was false."""
    content = """
#if 0
namespace wrong { class Inactive1 {}; }
#elif 1
namespace mcp { class Active {}; }
#else
namespace wrong { class Inactive2 {}; }
#endif
"""
    expected = ["mcp"]
    violations = find_namespace_violations(content, expected)
    # Only Active should exist and it should not be a violation
    assert len(violations) == 0, f"Expected no violations, got: {violations}"
    print("✓ test_elif_after_false_if passed")


def test_else_after_true_if():
    """Test that #else is inactive when previous branch was taken."""
    content = """
#if 1
namespace mcp { class Active {}; }
#else
namespace wrong { class ShouldBeInactive {}; }
#endif
"""
    expected = ["mcp"]
    violations = find_namespace_violations(content, expected)
    # ShouldBeInactive should not be detected
    inactive_violations = [v for v in violations if v[1] == "ShouldBeInactive"]
    assert len(inactive_violations) == 0, (
        f"ShouldBeInactive should not be detected: {inactive_violations}"
    )
    print("✓ test_else_after_true_if passed")


def test_elif_multiple_false_then_true():
    """Test multiple #elif branches with middle one being true."""
    content = """
#if 0
namespace wrong { class Inactive1 {}; }
#elif 0
namespace wrong { class Inactive2 {}; }
#elif 1
namespace mcp { class Active {}; }
#elif 1
namespace wrong { class Inactive3 {}; }
#else
namespace wrong { class Inactive4 {}; }
#endif
"""
    expected = ["mcp"]
    violations = find_namespace_violations(content, expected)
    assert len(violations) == 0, f"Expected no violations, got: {violations}"
    # Verify stripping
    result = strip_preprocessor_directives(content)
    assert "Inactive1" not in result, "Inactive1 should be stripped"
    assert "Inactive2" not in result, "Inactive2 should be stripped"
    assert "Inactive3" not in result, "Inactive3 should be stripped"
    assert "Inactive4" not in result, "Inactive4 should be stripped"
    assert "Active" in result, "Active should be preserved"
    print("✓ test_elif_multiple_false_then_true passed")


def test_nested_elif():
    """Test #elif inside nested conditionals."""
    content = """
#if 1
namespace mcp {
#if 0
class Bad1 {};
#elif 1
class Good {};
#else
class Bad2 {};
#endif
}
#endif
"""
    expected = ["mcp"]
    violations = find_namespace_violations(content, expected)
    # Good is in namespace mcp, which matches expected, so no violation
    assert len(violations) == 0, f"Expected no violations, got: {violations}"
    # Verify stripping
    result = strip_preprocessor_directives(content)
    assert "Bad1" not in result, "Bad1 should be stripped"
    assert "Bad2" not in result, "Bad2 should be stripped"
    assert "Good" in result, "Good should be preserved"
    print("✓ test_nested_elif passed")


def test_elif_inside_false_block():
    """Test that #elif inside a false #if block is also inactive."""
    content = """
#if 0
namespace wrong {
#if 0
class Bad1 {};
#elif 1
class Bad2 {};
#endif
}
#endif
namespace mcp { class Good {}; }
"""
    expected = ["mcp"]
    violations = find_namespace_violations(content, expected)
    # Good should not be a violation
    assert len(violations) == 0, f"Expected no violations, got: {violations}"
    # Verify stripping
    result = strip_preprocessor_directives(content)
    assert "Bad1" not in result, "Bad1 should be stripped"
    assert "Bad2" not in result, "Bad2 should be stripped"
    assert "Good" in result, "Good should be preserved"
    print("✓ test_elif_inside_false_block passed")


def run_all_tests():
    """Run all regression tests."""
    print("Running namespace layout check regression tests...")
    print("=" * 60)

    tests = [
        test_nested_namespace_basic,
        test_nested_namespace_triple,
        test_nested_namespace_scope_leakage,
        test_sequential_namespaces,
        test_if_zero_exclusion,
        test_ifdef_zero_exclusion,
        test_ifndef_zero_inclusion,
        test_nested_if_blocks,
        test_mixed_namespace_styles,
        test_scope_balance_multiple_files,
        test_complex_nested_with_preprocessor,
        test_elif_after_true_if,
        test_elif_chain,
        test_elif_after_false_if,
        test_else_after_true_if,
        test_elif_multiple_false_then_true,
        test_nested_elif,
        test_elif_inside_false_block,
    ]

    passed = 0
    failed = 0

    for test in tests:
        try:
            test()
            passed += 1
        except AssertionError as e:
            print(f"✗ {test.__name__} FAILED: {e}")
            failed += 1
        except Exception as e:
            print(f"✗ {test.__name__} ERROR: {e}")
            failed += 1

    print("=" * 60)
    print(f"Results: {passed} passed, {failed} failed")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(run_all_tests())
