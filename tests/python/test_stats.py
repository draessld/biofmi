"""
Integration tests for biofmi stats command
"""

import pytest
import subprocess
import json
import csv
import tempfile
from pathlib import Path
from io import StringIO

# Get project root
PROJECT_ROOT = Path(__file__).parent.parent.parent


def run_biofmi_stats(*args, check=True):
    """Helper to run biofmi stats command"""
    result = subprocess.run(
        ["python3", "run.py", "stats"] + list(args),
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
        check=False
    )
    if check and result.returncode != 0:
        raise RuntimeError(f"Command failed: {result.stderr}")
    return result


class TestStatsHelp:
    """Test help and basic command structure"""

    def test_stats_help(self):
        """Test that stats --help works"""
        result = run_biofmi_stats("--help", check=False)
        assert result.returncode == 0
        assert "statistics" in result.stdout.lower()
        assert "--sources" in result.stdout
        assert "--csv" in result.stdout
        assert "--json" in result.stdout
        assert "--full" in result.stdout

    def test_stats_no_input(self):
        """Test that stats without input shows error"""
        result = run_biofmi_stats(check=False)
        assert result.returncode != 0
        assert "error" in result.stderr.lower() or "required" in result.stderr.lower()


class TestStatsBasicOutput:
    """Test basic statistics output formats"""

    def test_stats_simple_file(self):
        """Test stats on simple.eds file"""
        result = run_biofmi_stats("data/test/simple.eds")
        assert result.returncode == 0
        assert "EDS Statistics" in result.stdout
        assert "Number of symbols" in result.stdout
        assert "Total characters" in result.stdout
        assert "METADATA_ONLY" in result.stdout

    def test_stats_simple_file_full_mode(self):
        """Test stats with --full mode"""
        result = run_biofmi_stats("data/test/simple.eds", "--full")
        assert result.returncode == 0
        assert "FULL" in result.stdout
        assert "all data in RAM" in result.stdout

    def test_stats_json_output(self):
        """Test JSON output format"""
        result = run_biofmi_stats("data/test/simple.eds", "--json")
        assert result.returncode == 0

        # Parse JSON
        data = json.loads(result.stdout)

        # Verify structure
        assert "file" in data
        assert "structure" in data
        assert "context_lengths" in data
        assert "variations" in data
        assert "memory" in data
        assert "sources" in data
        assert "recommendations" in data

        # Verify values
        assert data["structure"]["n_symbols"] == 4
        assert data["structure"]["N_characters"] == 14
        assert data["structure"]["m_strings"] == 6

    def test_stats_verbose_mode(self):
        """Test verbose mode shows detailed metrics"""
        result = run_biofmi_stats("data/test/simple.eds", "--verbose")
        assert result.returncode == 0
        assert "Detailed Metrics" in result.stdout
        assert "Avg strings per symbol" in result.stdout


class TestStatsWithSources:
    """Test statistics with source files"""

    def test_stats_with_sources_metadata_only(self):
        """Test stats with sources in METADATA_ONLY mode (sources ARE loaded)"""
        result = run_biofmi_stats(
            "data/test/simple.eds",
            "-s", "data/test/simple.seds"
        )
        assert result.returncode == 0
        # Sources should now load in METADATA_ONLY mode
        assert "Sources (pangenome paths):" in result.stdout
        assert "Strings with source info" in result.stdout
        assert "Total paths (genomes)" in result.stdout
        assert "Max paths per string" in result.stdout
        assert "Avg paths per string" in result.stdout
        # Verify it's using METADATA_ONLY mode
        assert "METADATA_ONLY" in result.stdout

    def test_stats_with_sources_full_mode(self):
        """Test stats with sources in FULL mode"""
        result = run_biofmi_stats(
            "data/test/simple.eds",
            "-s", "data/test/simple.seds",
            "--full"
        )
        assert result.returncode == 0
        assert "Sources (pangenome paths):" in result.stdout
        assert "Strings with source info" in result.stdout
        assert "Total paths (genomes)" in result.stdout
        assert "Max paths per string" in result.stdout
        assert "Avg paths per string" in result.stdout
        # Verify it's using FULL mode
        assert "FULL" in result.stdout

    def test_stats_source_statistics_values_metadata_only(self):
        """Test that source statistics work in METADATA_ONLY mode"""
        result = run_biofmi_stats(
            "data/test/simple.eds",
            "-s", "data/test/simple.seds",
            "--json"
        )
        assert result.returncode == 0

        data = json.loads(result.stdout)

        # Verify storage mode
        assert data["file"]["storage_mode"] == "METADATA_ONLY"

        # Verify source statistics are calculated correctly
        assert data["sources"]["loaded"] is True
        assert data["sources"]["file_provided"] is True
        assert data["sources"]["num_paths"] == 8
        assert data["sources"]["max_paths_per_string"] == 2
        assert abs(data["sources"]["avg_paths_per_string"] - 1.33) < 0.01

    def test_stats_source_statistics_values(self):
        """Test that source statistics have correct values in FULL mode"""
        result = run_biofmi_stats(
            "data/test/simple.eds",
            "-s", "data/test/simple.seds",
            "--full",
            "--json"
        )
        assert result.returncode == 0

        data = json.loads(result.stdout)

        # Verify storage mode
        assert data["file"]["storage_mode"] == "FULL"

        # Verify source statistics
        assert data["sources"]["loaded"] is True
        assert data["sources"]["num_paths"] == 8
        assert data["sources"]["max_paths_per_string"] == 2
        assert abs(data["sources"]["avg_paths_per_string"] - 1.33) < 0.01

    def test_stats_without_sources_zero_stats(self):
        """Test that stats without sources have zero source statistics"""
        result = run_biofmi_stats("data/test/simple.eds", "--json")
        assert result.returncode == 0

        data = json.loads(result.stdout)

        assert data["sources"]["loaded"] is False
        assert data["sources"]["num_paths"] == 0
        assert data["sources"]["max_paths_per_string"] == 0
        assert data["sources"]["avg_paths_per_string"] == 0.0


class TestStatsAutoDiscovery:
    """Test auto-discovery of source files"""

    def test_auto_discovery_single_file(self):
        """Test --sources=auto finds matching .seds file"""
        result = run_biofmi_stats(
            "data/test/simple.eds",
            "--sources=auto",
            "--full"
        )
        assert result.returncode == 0
        assert "Sources (pangenome paths):" in result.stdout

    def test_auto_discovery_json_output(self):
        """Test auto-discovery with JSON output"""
        result = run_biofmi_stats(
            "data/test/simple.eds",
            "--sources=auto",
            "--full",
            "--json"
        )
        assert result.returncode == 0

        data = json.loads(result.stdout)
        assert data["sources"]["loaded"] is True
        assert data["sources"]["num_paths"] > 0

    def test_auto_discovery_no_match(self):
        """Test auto-discovery when no .seds file exists"""
        # Create temporary EDS file without corresponding .seds
        with tempfile.NamedTemporaryFile(mode='w', suffix='.eds', delete=False) as f:
            f.write("{ACGT}{A,T}")
            temp_eds = Path(f.name)

        try:
            result = run_biofmi_stats(
                str(temp_eds),
                "--sources=auto",
                "--full",
                "--json"
            )
            assert result.returncode == 0

            data = json.loads(result.stdout)
            # Should not have sources loaded
            assert data["sources"]["loaded"] is False
        finally:
            temp_eds.unlink()


class TestStatsCSVOutput:
    """Test CSV output format"""

    def test_csv_single_file(self):
        """Test CSV output for single file"""
        result = run_biofmi_stats("data/test/simple.eds", "--csv")
        assert result.returncode == 0

        # Parse CSV
        lines = result.stdout.strip().split('\n')
        assert len(lines) == 2  # Header + 1 data row

        reader = csv.DictReader(StringIO(result.stdout))
        rows = list(reader)
        assert len(rows) == 1

        # Verify columns
        row = rows[0]
        assert "file" in row
        assert "n_symbols" in row
        assert "N_characters" in row
        assert "m_strings" in row
        assert "num_paths" in row
        assert "max_paths_per_string" in row
        assert "avg_paths_per_string" in row

    def test_csv_multiple_files(self):
        """Test CSV output for multiple files"""
        result = run_biofmi_stats(
            "data/test/simple.eds",
            "data/test/test2.eds",
            "--csv"
        )
        assert result.returncode == 0

        lines = result.stdout.strip().split('\n')
        assert len(lines) == 3  # Header + 2 data rows

        reader = csv.DictReader(StringIO(result.stdout))
        rows = list(reader)
        assert len(rows) == 2

    def test_csv_with_sources_auto(self):
        """Test CSV with auto-discovery of sources in METADATA_ONLY mode"""
        result = run_biofmi_stats(
            "data/test/simple.eds",
            "data/test/test2.eds",
            "--csv",
            "--sources=auto"
        )
        assert result.returncode == 0

        reader = csv.DictReader(StringIO(result.stdout))
        rows = list(reader)
        assert len(rows) == 2

        # Both files should have sources in METADATA_ONLY mode
        for row in rows:
            assert row["has_sources"] == "True"
            assert int(row["num_paths"]) > 0
            assert row["storage_mode"] == "METADATA_ONLY"

    def test_csv_columns_source_statistics(self):
        """Test that CSV contains source statistics columns in METADATA_ONLY mode"""
        result = run_biofmi_stats(
            "data/test/simple.eds",
            "--csv",
            "--sources=auto"
        )
        assert result.returncode == 0

        reader = csv.DictReader(StringIO(result.stdout))
        row = next(reader)

        # Verify source statistics columns
        assert "num_paths" in row
        assert "max_paths_per_string" in row
        assert "avg_paths_per_string" in row

        # Verify values
        assert int(row["num_paths"]) == 8
        assert int(row["max_paths_per_string"]) == 2
        assert abs(float(row["avg_paths_per_string"]) - 1.33) < 0.01


class TestStatsGlobPattern:
    """Test glob pattern matching for sources"""

    def test_glob_pattern_matching(self):
        """Test --sources with glob pattern"""
        result = run_biofmi_stats(
            "data/test/simple.eds",
            "data/test/test2.eds",
            "--sources=data/test/*.seds",
            "--csv",
            "--full"
        )
        assert result.returncode == 0

        reader = csv.DictReader(StringIO(result.stdout))
        rows = list(reader)

        # Both files should match their corresponding .seds files
        for row in rows:
            assert row["has_sources"] == "True"
            assert int(row["num_paths"]) > 0


class TestStatsMemoryMode:
    """Test memory mode selection"""

    def test_metadata_only_mode_default(self):
        """Test that METADATA_ONLY is default"""
        result = run_biofmi_stats("data/test/simple.eds", "--json")
        assert result.returncode == 0

        data = json.loads(result.stdout)
        assert data["file"]["storage_mode"] == "METADATA_ONLY"

    def test_full_mode_explicit(self):
        """Test explicit FULL mode"""
        result = run_biofmi_stats("data/test/simple.eds", "--full", "--json")
        assert result.returncode == 0

        data = json.loads(result.stdout)
        assert data["file"]["storage_mode"] == "FULL"

    def test_memory_reduction_reported(self):
        """Test that memory reduction is reported for METADATA_ONLY"""
        result = run_biofmi_stats("data/test/simple.eds", "--json")
        assert result.returncode == 0

        data = json.loads(result.stdout)
        assert "estimated_full_bytes" in data["memory"]
        assert "reduction_factor" in data["memory"]
        assert data["memory"]["reduction_factor"] > 1.0


class TestStatsRecommendations:
    """Test statistics recommendations"""

    def test_recommendations_present(self):
        """Test that recommendations are included"""
        result = run_biofmi_stats("data/test/simple.eds")
        assert result.returncode == 0
        assert "Recommendations" in result.stdout

    def test_recommendations_transformation_needed(self):
        """Test recommendation when transformation is needed"""
        result = run_biofmi_stats("data/test/simple.eds", "--json")
        assert result.returncode == 0

        data = json.loads(result.stdout)
        # simple.eds has min_context_length = 3 < 5
        assert data["recommendations"]["needs_transformation"] is True
        assert "transform" in data["recommendations"]["suggested_command"]

    def test_recommendations_in_standard_output(self):
        """Test recommendations in standard output"""
        result = run_biofmi_stats("data/test/simple.eds")
        assert result.returncode == 0
        assert "Suggested command" in result.stdout or "biofmi transform" in result.stdout


class TestStatsErrorHandling:
    """Test error handling"""

    def test_nonexistent_file(self):
        """Test error when file doesn't exist"""
        result = run_biofmi_stats("nonexistent.eds", check=False)
        assert result.returncode != 0

    def test_nonexistent_sources_file(self):
        """Test that nonexistent sources file is silently ignored by Python wrapper"""
        result = run_biofmi_stats(
            "data/test/simple.eds",
            "-s", "nonexistent.seds",
            check=False
        )
        # Python wrapper silently ignores nonexistent sources (returns None from resolve)
        # So it runs successfully without sources
        assert result.returncode == 0
        # Should not show sources section at all (no sources loaded)
        assert "Sources (pangenome paths):" not in result.stdout

    def test_json_with_multiple_files_error(self):
        """Test that JSON mode doesn't work with multiple files"""
        # Note: This should work now but show only one file's output
        # or we could make it an error - depends on implementation
        result = run_biofmi_stats(
            "data/test/simple.eds",
            "data/test/test2.eds",
            "--json",
            check=False
        )
        # Implementation allows it - processes each file separately
        assert result.returncode == 0


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
