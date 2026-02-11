#!/usr/bin/env python3

import argparse
import hashlib
import os
import subprocess
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create a local venv with pinned reference Python SDK dependencies"
    )
    parser.add_argument(
        "--venv", required=True, help="Path to virtual environment directory"
    )
    parser.add_argument(
        "--requirements", required=True, help="Path to pinned requirements file"
    )
    return parser.parse_args()


def venv_python_path(venv_dir: Path) -> Path:
    if os.name == "nt":
        return venv_dir / "Scripts" / "python.exe"
    return venv_dir / "bin" / "python"


def requirements_digest(requirements_path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(requirements_path.read_bytes())
    return digest.hexdigest()


def run_checked(command: list[str]) -> None:
    env = os.environ.copy()
    env["PIP_DISABLE_PIP_VERSION_CHECK"] = "1"
    subprocess.check_call(command, env=env)


def main() -> int:
    args = parse_args()
    venv_dir = Path(args.venv).resolve()
    requirements_path = Path(args.requirements).resolve()
    stamp_path = venv_dir / ".requirements.sha256"

    if not requirements_path.is_file():
        raise FileNotFoundError(f"Requirements file not found: {requirements_path}")

    python_path = venv_python_path(venv_dir)
    if not python_path.exists():
        run_checked([sys.executable, "-m", "venv", str(venv_dir)])

    expected_digest = requirements_digest(requirements_path)
    if (
        stamp_path.is_file()
        and stamp_path.read_text(encoding="utf-8").strip() == expected_digest
    ):
        print(f"Reference SDK venv already provisioned at {venv_dir}")
        return 0

    run_checked(
        [
            str(python_path),
            "-m",
            "pip",
            "install",
            "--upgrade",
            "-r",
            str(requirements_path),
        ]
    )

    stamp_path.write_text(expected_digest, encoding="utf-8")
    print(f"Provisioned reference SDK venv at {venv_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
