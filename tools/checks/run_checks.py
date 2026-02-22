#!/usr/bin/env python3
"""
Main runner for all deterministic checks.

Runs all checks and returns non-zero if any check fails.
Provides deterministic output ordering.
"""

import subprocess
import sys
from pathlib import Path
from typing import List, Tuple


# List of check scripts to run (in deterministic order)
CHECK_SCRIPTS = [
    "check_public_header_one_type.py",
    "check_include_policy.py",
    "check_git_index_hygiene.py",
]


def run_check(script_path: Path) -> Tuple[str, int, str]:
    """
    Run a single check script.

    Returns (script_name, exit_code, output).
    """
    script_name = script_path.name

    try:
        result = subprocess.run(
            [sys.executable, str(script_path)],
            capture_output=True,
            text=True,
            check=False,
            timeout=60,  # 60 second timeout per check
        )
        return script_name, result.returncode, result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        return script_name, -1, f"ERROR: {script_name} timed out after 60 seconds"
    except Exception as e:
        return script_name, -1, f"ERROR: Failed to run {script_name}: {e}"


def main() -> int:
    """Main entry point."""
    # Find the tools/checks directory
    script_dir = Path(__file__).parent

    print("=" * 70)
    print("Running deterministic enforcement checks")
    print("=" * 70)
    print()

    results: List[Tuple[str, int, str]] = []

    for check_script in CHECK_SCRIPTS:
        script_path = script_dir / check_script

        if not script_path.exists():
            print(f"WARNING: Check script not found: {check_script}")
            continue

        print(f"Running {check_script}...")
        script_name, exit_code, output = run_check(script_path)
        results.append((script_name, exit_code, output))
        print()

    # Print summary
    print("=" * 70)
    print("CHECK RESULTS SUMMARY")
    print("=" * 70)
    print()

    failed_checks = []
    passed_checks = []

    for script_name, exit_code, output in results:
        if exit_code != 0:
            failed_checks.append((script_name, exit_code, output))
            print(f"[FAIL] {script_name}")
        else:
            passed_checks.append(script_name)
            print(f"[PASS] {script_name}")

    print()

    # Print details for failed checks
    if failed_checks:
        print("-" * 70)
        print("FAILED CHECK DETAILS")
        print("-" * 70)
        print()

        for script_name, exit_code, output in failed_checks:
            print(f"=== {script_name} (exit code {exit_code}) ===")
            print(output)
            print()

    print("=" * 70)
    print(f"Total checks: {len(results)}")
    print(f"Passed: {len(passed_checks)}")
    print(f"Failed: {len(failed_checks)}")
    print("=" * 70)

    if failed_checks:
        print()
        print("Some checks failed. Please fix the violations above.")
        return 1

    print()
    print("All checks passed!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
