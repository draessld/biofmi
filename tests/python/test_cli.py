"""
Tests for Python CLI
"""

import pytest
import subprocess
from pathlib import Path

def test_run_help():
    """Test that run.py --help works"""
    result = subprocess.run(
        ["python3", "run.py", "--help"],
        cwd=Path(__file__).parent.parent.parent,
        capture_output=True,
        text=True
    )
    assert result.returncode == 0
    assert "BIO-FMI" in result.stdout

def test_transform_help():
    """Test transform command help"""
    result = subprocess.run(
        ["python3", "run.py", "transform", "--help"],
        cwd=Path(__file__).parent.parent.parent,
        capture_output=True,
        text=True
    )
    assert result.returncode == 0
    assert "transform" in result.stdout.lower()

# TODO: Add more tests as functionality is implemented
