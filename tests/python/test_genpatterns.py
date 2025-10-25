"""
Integration tests for biofmi-genpatterns tool
"""

import subprocess
import pytest
from pathlib import Path
import tempfile
import os


# Path to the C++ tool
CPP_TOOL = Path(__file__).parent.parent.parent / "build" / "tools" / "biofmi-genpatterns"
PYTHON_CLI = Path(__file__).parent.parent.parent / "run.py"
TEST_DATA_DIR = Path(__file__).parent.parent.parent / "data" / "test"


@pytest.fixture
def simple_eds():
    """Path to simple test EDS file"""
    return TEST_DATA_DIR / "simple.eds"


@pytest.fixture
def output_file():
    """Temporary output file for patterns"""
    with tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False) as f:
        yield Path(f.name)
    # Cleanup
    if Path(f.name).exists():
        os.unlink(f.name)


def test_genpatterns_help():
    """Test help message"""
    result = subprocess.run(
        [str(CPP_TOOL), "--help"],
        capture_output=True,
        text=True
    )
    assert result.returncode == 0
    assert "Generate random patterns from EDS" in result.stdout
    assert "--input" in result.stdout
    assert "--output" in result.stdout
    assert "--count" in result.stdout
    assert "--length" in result.stdout


def test_genpatterns_basic(simple_eds, output_file):
    """Test basic pattern generation"""
    result = subprocess.run(
        [str(CPP_TOOL), "-i", str(simple_eds), "-o", str(output_file), "-n", "5", "-l", "10"],
        capture_output=True,
        text=True
    )

    assert result.returncode == 0
    assert "Successfully generated 5 patterns" in result.stderr

    # Check output file exists and has correct number of patterns
    assert output_file.exists()
    patterns = output_file.read_text().strip().split('\n')
    assert len(patterns) == 5

    # Check pattern length (should be 10)
    for pattern in patterns:
        assert len(pattern) == 10
        assert all(c in 'ACGT' for c in pattern)


def test_genpatterns_custom_count(simple_eds, output_file):
    """Test with custom pattern count"""
    result = subprocess.run(
        [str(CPP_TOOL), "-i", str(simple_eds), "-o", str(output_file), "-n", "20", "-l", "8"],
        capture_output=True,
        text=True
    )

    assert result.returncode == 0
    patterns = output_file.read_text().strip().split('\n')
    assert len(patterns) == 20
    for pattern in patterns:
        assert len(pattern) == 8


def test_genpatterns_long_pattern(simple_eds, output_file):
    """Test with pattern length exceeding EDS size"""
    result = subprocess.run(
        [str(CPP_TOOL), "-i", str(simple_eds), "-o", str(output_file), "-n", "3", "-l", "20"],
        capture_output=True,
        text=True
    )

    # Should still succeed but with warning
    assert result.returncode == 0
    assert "Warning" in result.stderr or "warning" in result.stderr.lower()

    # Patterns should be generated (may be truncated/wrapped)
    assert output_file.exists()
    patterns = output_file.read_text().strip().split('\n')
    assert len(patterns) == 3


def test_genpatterns_default_values(simple_eds, output_file):
    """Test with default count and length values"""
    result = subprocess.run(
        [str(CPP_TOOL), "-i", str(simple_eds), "-o", str(output_file)],
        capture_output=True,
        text=True
    )

    assert result.returncode == 0
    patterns = output_file.read_text().strip().split('\n')
    # Default count is 100
    assert len(patterns) == 100
    # Default length is 10
    for pattern in patterns:
        assert len(pattern) == 10


def test_genpatterns_missing_input():
    """Test with missing input file"""
    result = subprocess.run(
        [str(CPP_TOOL), "-i", "nonexistent.eds", "-o", "/tmp/out.txt"],
        capture_output=True,
        text=True
    )

    assert result.returncode != 0
    assert "does not exist" in result.stderr or "Error" in result.stderr


def test_genpatterns_missing_args():
    """Test with missing required arguments"""
    result = subprocess.run(
        [str(CPP_TOOL), "-i", "test.eds"],
        capture_output=True,
        text=True
    )

    # Missing output file
    assert result.returncode != 0


def test_genpatterns_zero_count(simple_eds, output_file):
    """Test with zero pattern count"""
    result = subprocess.run(
        [str(CPP_TOOL), "-i", str(simple_eds), "-o", str(output_file), "-n", "0"],
        capture_output=True,
        text=True
    )

    assert result.returncode != 0
    assert "must be greater than 0" in result.stderr


def test_genpatterns_zero_length(simple_eds, output_file):
    """Test with zero pattern length"""
    result = subprocess.run(
        [str(CPP_TOOL), "-i", str(simple_eds), "-o", str(output_file), "-l", "0"],
        capture_output=True,
        text=True
    )

    assert result.returncode != 0
    assert "must be greater than 0" in result.stderr


def test_genpatterns_via_python_cli(simple_eds, output_file):
    """Test pattern generation via Python CLI"""
    result = subprocess.run(
        ["python3", str(PYTHON_CLI), "genpatterns",
         "-i", str(simple_eds), "-o", str(output_file), "-n", "5", "-l", "12"],
        capture_output=True,
        text=True
    )

    assert result.returncode == 0
    assert output_file.exists()
    patterns = output_file.read_text().strip().split('\n')
    assert len(patterns) == 5
    for pattern in patterns:
        assert len(pattern) == 12


def test_genpatterns_patterns_are_valid(simple_eds, output_file):
    """Test that generated patterns only contain valid characters"""
    result = subprocess.run(
        [str(CPP_TOOL), "-i", str(simple_eds), "-o", str(output_file), "-n", "10", "-l", "10"],
        capture_output=True,
        text=True
    )

    assert result.returncode == 0
    patterns = output_file.read_text().strip().split('\n')

    # All patterns should only contain A, C, G, T
    valid_chars = set('ACGT')
    for pattern in patterns:
        assert set(pattern).issubset(valid_chars), f"Invalid characters in pattern: {pattern}"


def test_genpatterns_reproducibility(simple_eds, output_file):
    """Test that patterns are random (different runs produce different results)"""
    # First run
    result1 = subprocess.run(
        [str(CPP_TOOL), "-i", str(simple_eds), "-o", str(output_file), "-n", "5", "-l", "10"],
        capture_output=True,
        text=True
    )
    assert result1.returncode == 0
    patterns1 = output_file.read_text().strip().split('\n')

    # Second run (new file)
    output_file2 = output_file.with_suffix('.txt2')
    result2 = subprocess.run(
        [str(CPP_TOOL), "-i", str(simple_eds), "-o", str(output_file2), "-n", "5", "-l", "10"],
        capture_output=True,
        text=True
    )
    assert result2.returncode == 0
    patterns2 = output_file2.read_text().strip().split('\n')

    # Patterns should be different (very high probability)
    # At least some should differ
    assert patterns1 != patterns2, "Patterns should be random"

    # Cleanup
    output_file2.unlink()


def test_genpatterns_large_count(simple_eds, output_file):
    """Test with large pattern count"""
    result = subprocess.run(
        [str(CPP_TOOL), "-i", str(simple_eds), "-o", str(output_file), "-n", "1000", "-l", "10"],
        capture_output=True,
        text=True
    )

    assert result.returncode == 0
    patterns = output_file.read_text().strip().split('\n')
    assert len(patterns) == 1000


def test_genpatterns_output_format(simple_eds, output_file):
    """Test that output format is one pattern per line"""
    result = subprocess.run(
        [str(CPP_TOOL), "-i", str(simple_eds), "-o", str(output_file), "-n", "5", "-l", "10"],
        capture_output=True,
        text=True
    )

    assert result.returncode == 0
    content = output_file.read_text()

    # Check no extra whitespace or empty lines
    patterns = content.strip().split('\n')
    assert len(patterns) == 5
    for pattern in patterns:
        assert pattern.strip() == pattern  # No leading/trailing whitespace
        assert '\t' not in pattern  # No tabs


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
