#!/usr/bin/env python3
"""
BIO-FMI CLI - Command-line interface for BIO-FMI indexing and querying
Orchestrates C++ tools for efficient processing of genomic data.
"""

import argparse
import subprocess
import sys
import os
from pathlib import Path
from typing import Optional, List
import json
import logging
import datetime
import time
import threading


# ===================================================================
# Logging Setup - FILE ONLY (no console output to preserve stdout/stderr)
# ===================================================================

def setup_logger():
    """Setup file-only logger for performance tracking"""
    logger = logging.getLogger('biofmi')
    logger.setLevel(logging.DEBUG)

    # Only add handler if not already configured (avoid duplicates)
    if not logger.handlers:
        # File handler only (NO console output)
        log_file = Path(__file__).parent / 'log.log'
        file_handler = logging.FileHandler(log_file, mode='a', encoding='utf-8')
        file_handler.setLevel(logging.DEBUG)

        # Format: ISO 8601 timestamp + message
        formatter = logging.Formatter(
            fmt='%(asctime)s - %(levelname)s - %(message)s',
            datefmt='%Y-%m-%d %H:%M:%S'
        )
        file_handler.setFormatter(formatter)
        logger.addHandler(file_handler)

        # Prevent propagation to root logger (avoid accidental console output)
        logger.propagate = False

    return logger


def get_session_id():
    """Generate unique session ID from timestamp (YYYYMMDD_HHMMSS_microseconds)"""
    return datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f')


# Initialize logger
logger = setup_logger()


# ===================================================================
# Performance Monitor - Track execution time and peak memory
# ===================================================================

class PerformanceMonitor:
    """Monitor subprocess performance (time, peak memory)"""

    def __init__(self):
        self.start_time = None
        self.end_time = None
        self.peak_memory_bytes = 0
        self.monitoring = True
        self._psutil_available = False

        # Check if psutil is available
        try:
            import psutil
            self._psutil_available = True
        except ImportError:
            logger.warning("psutil not available - memory tracking disabled")

    def _monitor_memory(self, process):
        """Background thread to track peak memory"""
        if not self._psutil_available:
            return

        import psutil

        while self.monitoring:
            try:
                if process.is_running():
                    mem = process.memory_info().rss
                    self.peak_memory_bytes = max(self.peak_memory_bytes, mem)
                    time.sleep(0.5)  # Check every 0.5 seconds
                else:
                    break
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                break
            except Exception as e:
                logger.debug(f"Memory monitoring error: {e}")
                break

    def run_and_monitor(self, cmd: List[str]) -> int:
        """Execute command and monitor performance"""
        self.start_time = time.time()

        if self._psutil_available:
            # Use psutil for monitoring
            import psutil

            try:
                process = psutil.Popen(cmd)

                # Start monitoring thread
                monitor_thread = threading.Thread(
                    target=self._monitor_memory,
                    args=(process,),
                    daemon=True
                )
                monitor_thread.start()

                # Wait for completion
                returncode = process.wait()
                self.end_time = time.time()
                self.monitoring = False
                monitor_thread.join(timeout=1.0)

                return returncode

            except Exception as e:
                logger.error(f"Error running with psutil: {e}")
                # Fallback to subprocess
                return subprocess.run(cmd, check=False).returncode
        else:
            # Fallback: no memory monitoring
            result = subprocess.run(cmd, check=False)
            self.end_time = time.time()
            return result.returncode

    @property
    def duration(self):
        """Execution duration in seconds"""
        if self.start_time and self.end_time:
            return self.end_time - self.start_time
        return 0

    @property
    def peak_memory_mb(self):
        """Peak memory usage in MB"""
        return self.peak_memory_bytes / (1024 * 1024)

    @property
    def peak_memory_gb(self):
        """Peak memory usage in GB"""
        return self.peak_memory_bytes / (1024 * 1024 * 1024)

    def format_memory(self):
        """Format memory for display (auto-select MB or GB)"""
        if self.peak_memory_gb >= 1.0:
            return f"{self.peak_memory_gb:.2f}GB"
        else:
            return f"{self.peak_memory_mb:.1f}MB"


# ===================================================================
# Main CLI Class
# ===================================================================

class BioFMICLI:
    """Main CLI orchestrator for BIO-FMI tools"""

    def __init__(self):
        self.cpp_tools_dir = Path(__file__).parent / "build" / "tools"
        self.check_installation()

    def check_installation(self):
        """Check if C++ tools are built"""
        if not self.cpp_tools_dir.exists():
            print("Error: C++ tools not found. Please run INSTALL.sh first.", file=sys.stderr)
            print(f"Expected location: {self.cpp_tools_dir}", file=sys.stderr)
            sys.exit(1)

    def run_cpp_tool(self, tool_name: str, args: List[str]) -> int:
        """Execute a C++ tool with performance monitoring and logging"""
        tool_path = self.cpp_tools_dir / tool_name

        if not tool_path.exists():
            logger.error(f"Tool not found: {tool_name} at {tool_path}")
            print(f"Error: Tool '{tool_name}' not found at {tool_path}", file=sys.stderr)
            return 1

        # Generate session ID for this invocation
        session_id = get_session_id()

        # Log command start
        logger.info(f"[{session_id}] START {tool_name} | args={args}")

        try:
            # Monitor performance
            monitor = PerformanceMonitor()
            returncode = monitor.run_and_monitor([str(tool_path)] + args)

            # Log completion with metrics
            logger.info(
                f"[{session_id}] END {tool_name} | "
                f"duration={monitor.duration:.2f}s | "
                f"peak_memory={monitor.format_memory()} | "
                f"exit_code={returncode}"
            )

            return returncode

        except Exception as e:
            logger.error(f"[{session_id}] ERROR {tool_name} | exception={str(e)}", exc_info=True)
            print(f"Error executing {tool_name}: {e}", file=sys.stderr)
            return 1


def cmd_transform(args, cli: BioFMICLI) -> int:
    """Transform MSA/VCF/EDS to l-EDS format"""
    cpp_args = [
        "-i", str(args.input),
        "-l", str(args.context_length),
        "--method", args.method
    ]

    if args.output:
        cpp_args.extend(["-o", str(args.output)])

    if args.sources:
        cpp_args.extend(["-s", str(args.sources)])

    print(f"Transforming {args.input} to l-EDS (context length: {args.context_length}, method: {args.method})")
    return cli.run_cpp_tool("biofmi-transform", cpp_args)


def cmd_build(args, cli: BioFMICLI) -> int:
    """Build BIO-FMI index from l-EDS"""
    cpp_args = [
        "-i", str(args.input),
        "-l", str(args.context_length)
    ]

    if args.output:
        cpp_args.extend(["-o", str(args.output)])

    print(f"Building BIO-FMI index from {args.input}")
    return cli.run_cpp_tool("biofmi-build", cpp_args)


def cmd_locate(args, cli: BioFMICLI) -> int:
    """Locate pattern(s) in BIO-FMI index"""
    cpp_args = [
        "-i", str(args.index),
        "-l", str(args.context_length)
    ]

    if args.pattern:
        cpp_args.extend(["-p", args.pattern])

    if args.pattern_file:
        cpp_args.extend(["-P", str(args.pattern_file)])

    if args.benchmark:
        cpp_args.append("--benchmark")

    if args.output:
        cpp_args.extend(["-o", str(args.output)])

    print(f"Searching patterns in index {args.index}")
    return cli.run_cpp_tool("biofmi-locate", cpp_args)


def resolve_source_file(eds_file: Path, sources_arg: Optional[str]) -> Optional[Path]:
    """
    Resolve source file for a given EDS file.

    Args:
        eds_file: Path to EDS file
        sources_arg: Either "auto", a glob pattern, or a specific file path, or None

    Returns:
        Path to .seds file, or None if not found
    """
    if sources_arg is None:
        return None

    if sources_arg == "auto":
        # Try: same name with .seds extension
        candidate = eds_file.with_suffix('.seds')
        if candidate.exists():
            return candidate

        # Try: same name + .seds
        candidate = Path(str(eds_file) + '.seds')
        if candidate.exists():
            return candidate

        return None

    elif '*' in sources_arg or '?' in sources_arg:
        # Glob pattern - match by filename stem
        import glob
        matches = glob.glob(sources_arg)
        # Try to find matching .seds for this .eds
        eds_stem = eds_file.stem
        for match in matches:
            if Path(match).stem == eds_stem:
                return Path(match)
        return None

    else:
        # Specific file path
        path = Path(sources_arg)
        return path if path.exists() else None


def cmd_stats(args, cli: BioFMICLI) -> int:
    """Show statistics for EDS/l-EDS file(s)"""

    # Handle CSV output format (requires JSON from C++ tool)
    if args.csv:
        # Print CSV header
        print("file,file_size_bytes,storage_mode,n_symbols,N_characters,m_strings,"
              "degenerate_symbols,regular_symbols,min_context_length,max_context_length,"
              "avg_context_length,total_change_size,common_characters,empty_strings,"
              "has_sources,num_paths,max_paths_per_string,avg_paths_per_string,"
              "current_memory_bytes,estimated_full_memory_bytes,reduction_factor")

        # Process each file
        for input_file in args.input:
            cpp_args = ["-i", str(input_file), "--json"]

            # Resolve source file for this EDS file
            source_file = resolve_source_file(Path(input_file), args.sources)
            if source_file:
                cpp_args.extend(["-s", str(source_file)])

            if args.full:
                cpp_args.append("--full")

            # Run C++ tool and capture output
            result = subprocess.run(
                [str(cli.cpp_tools_dir / "biofmi-stats")] + cpp_args,
                capture_output=True,
                text=True
            )

            if result.returncode != 0:
                print(f"Error processing {input_file}: {result.stderr}", file=sys.stderr)
                continue

            # Parse JSON output
            try:
                data = json.loads(result.stdout)

                # Extract values for CSV
                file_path = data['file']['path']
                file_size = data['file']['size_bytes']
                storage_mode = data['file']['storage_mode']
                n_symbols = data['structure']['n_symbols']
                N_characters = data['structure']['N_characters']
                m_strings = data['structure']['m_strings']
                degenerate_symbols = data['structure']['degenerate_symbols']
                regular_symbols = data['structure']['regular_symbols']
                min_context = data['context_lengths']['min']
                max_context = data['context_lengths']['max']
                avg_context = data['context_lengths']['avg']
                total_change_size = data['variations']['total_change_size']
                common_chars = data['variations']['common_characters']
                empty_strings = data['variations']['empty_strings']
                has_sources = data['sources']['loaded'] or data['sources']['file_provided']
                num_paths = data['sources']['num_paths']
                max_paths_per_string = data['sources']['max_paths_per_string']
                avg_paths_per_string = data['sources']['avg_paths_per_string']
                current_mem = data['memory']['current_bytes']

                # Handle optional fields for METADATA_ONLY mode
                if 'estimated_full_bytes' in data['memory']:
                    estimated_full_mem = data['memory']['estimated_full_bytes']
                    reduction_factor = data['memory']['reduction_factor']
                else:
                    estimated_full_mem = current_mem
                    reduction_factor = 1.0

                # Print CSV row
                print(f"{file_path},{file_size},{storage_mode},{n_symbols},{N_characters},"
                      f"{m_strings},{degenerate_symbols},{regular_symbols},{min_context},"
                      f"{max_context},{avg_context:.2f},{total_change_size},{common_chars},"
                      f"{empty_strings},{has_sources},{num_paths},{max_paths_per_string},"
                      f"{avg_paths_per_string:.2f},{current_mem},{estimated_full_mem},"
                      f"{reduction_factor:.1f}")

            except (json.JSONDecodeError, KeyError) as e:
                print(f"Error parsing output for {input_file}: {e}", file=sys.stderr)
                continue

        return 0

    # Handle standard/JSON/verbose output (one file at a time)
    for idx, input_file in enumerate(args.input):
        cpp_args = ["-i", str(input_file)]

        # Resolve source file for this EDS file
        source_file = resolve_source_file(Path(input_file), args.sources)
        if source_file:
            cpp_args.extend(["-s", str(source_file)])

        if args.full:
            cpp_args.append("--full")
        if args.json:
            cpp_args.append("--json")
        if args.verbose:
            cpp_args.append("--verbose")

        if not args.json:
            print(f"Computing statistics for {input_file}", flush=True)

        result = cli.run_cpp_tool("biofmi-stats", cpp_args)

        if result != 0:
            return result

        # Print separator between files (if multiple files and not JSON mode)
        if len(args.input) > 1 and idx < len(args.input) - 1 and not args.json:
            print()  # Empty line between files

    return 0


def cmd_genpatterns(args, cli: BioFMICLI) -> int:
    """Generate random patterns from EDS for benchmarking"""
    cpp_args = [
        "-i", str(args.input),
        "-n", str(args.count),
        "-l", str(args.length),
        "-o", str(args.output)
    ]

    print(f"Generating {args.count} patterns of length {args.length}")
    return cli.run_cpp_tool("biofmi-genpatterns", cpp_args)


def cmd_clean(args, cli: BioFMICLI) -> int:
    """Clean log files and temporary data"""
    import os
    from pathlib import Path

    # Get project root directory
    project_dir = Path(__file__).parent
    log_file = project_dir / "log.log"

    # Track what was cleaned
    cleaned_items = []
    total_size = 0

    # Clean log file
    if log_file.exists():
        size = log_file.stat().st_size

        if args.show_content and size > 0:
            # Show log content before cleaning
            print("=== Current log.log content ===")
            with open(log_file, 'r') as f:
                content = f.read()
                if content:
                    print(content)
                else:
                    print("(empty)")
            print("=" * 50)
            print()

        if not args.dry_run:
            # Actually remove the file
            log_file.unlink()
            if size > 0:  # Only report if file had content
                cleaned_items.append(f"log.log ({size} bytes)")
                total_size += size
        else:
            if size > 0:
                print(f"[DRY RUN] Would remove: log.log ({size} bytes)")
            else:
                print(f"[DRY RUN] Would remove: log.log (empty file)")

    # Report results (only if we actually cleaned or if nothing to clean)
    if cleaned_items:
        print(f"✓ Cleaned {len(cleaned_items)} item(s):")
        for item in cleaned_items:
            print(f"  - {item}")
        print(f"\nTotal space freed: {total_size:,} bytes ({total_size / 1024:.2f} KB)")
    elif not args.dry_run and not cleaned_items:
        # Normal mode but nothing was cleaned
        print("✓ No log files to clean")

    return 0


def main():
    parser = argparse.ArgumentParser(
        description="BIO-FMI: Indexing and querying elastic-degenerate strings",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Transform MSA to l-EDS
  python run.py transform -i data.msa -l 5 --method linear

  # Build index from l-EDS
  python run.py build -i data.5.leds -l 5

  # Search for pattern
  python run.py locate -i data.5.leds.index -l 5 -p "ACGTACGT"

  # Show EDS statistics
  python run.py stats -i data.eds

For more information, see README.md
        """
    )

    subparsers = parser.add_subparsers(dest='command', help='Available commands')

    # Transform command
    parser_transform = subparsers.add_parser('transform',
        help='Transform MSA/VCF/EDS to l-EDS format')
    parser_transform.add_argument('-i', '--input', required=True, type=Path,
        help='Input file (.msa, .vcf, .eds, or .edz)')
    parser_transform.add_argument('-o', '--output', type=Path,
        help='Output file (default: input with .leds extension)')
    parser_transform.add_argument('-l', '--context-length', type=int, default=5,
        help='Context length parameter (default: 5)')
    parser_transform.add_argument('--method', choices=['linear', 'cartesian'], default='linear',
        help='Transformation method (default: linear)')
    parser_transform.add_argument('-s', '--sources', type=Path,
        help='Source file for EDS phasing information (.edp)')

    # Build command
    parser_build = subparsers.add_parser('build',
        help='Build BIO-FMI index from l-EDS')
    parser_build.add_argument('-i', '--input', required=True, type=Path,
        help='Input l-EDS file')
    parser_build.add_argument('-o', '--output', type=Path,
        help='Output index directory (default: input with .index extension)')
    parser_build.add_argument('-l', '--context-length', type=int, required=True,
        help='Context length parameter used in l-EDS')

    # Locate command
    parser_locate = subparsers.add_parser('locate',
        help='Locate pattern(s) in BIO-FMI index')
    parser_locate.add_argument('-i', '--index', required=True, type=Path,
        help='Index directory or l-EDS file')
    parser_locate.add_argument('-l', '--context-length', type=int, required=True,
        help='Context length parameter')
    parser_locate.add_argument('-p', '--pattern', type=str,
        help='Single pattern to search')
    parser_locate.add_argument('-P', '--pattern-file', type=Path,
        help='File containing patterns (one per line)')
    parser_locate.add_argument('-o', '--output', type=Path,
        help='Output file for results (default: stdout)')
    parser_locate.add_argument('--benchmark', action='store_true',
        help='Run in benchmark mode')

    # Stats command
    parser_stats = subparsers.add_parser('stats',
        help='Show statistics for EDS/l-EDS file(s)')
    parser_stats.add_argument('input', type=Path, nargs='+',
        help='Input EDS or l-EDS file(s) - can specify multiple')
    parser_stats.add_argument('-s', '--sources', type=str,
        help='Source file(s): specific path, glob pattern (e.g., "data/*.seds"), or "auto" to auto-discover')
    parser_stats.add_argument('-f', '--full', action='store_true',
        help='Use FULL mode (load all strings into RAM)')
    parser_stats.add_argument('-j', '--json', action='store_true',
        help='Output in JSON format (single file only)')
    parser_stats.add_argument('-c', '--csv', action='store_true',
        help='Output in CSV format (ideal for multiple files)')
    parser_stats.add_argument('-v', '--verbose', action='store_true',
        help='Show detailed statistics')

    # Generate patterns command
    parser_genpatterns = subparsers.add_parser('genpatterns',
        help='Generate random patterns from EDS for benchmarking')
    parser_genpatterns.add_argument('-i', '--input', required=True, type=Path,
        help='Input EDS file')
    parser_genpatterns.add_argument('-n', '--count', type=int, default=100,
        help='Number of patterns to generate (default: 100)')
    parser_genpatterns.add_argument('-l', '--length', type=int, default=10,
        help='Length of patterns (default: 10)')
    parser_genpatterns.add_argument('-o', '--output', required=True, type=Path,
        help='Output file for patterns')

    # Clean command
    parser_clean = subparsers.add_parser('clean',
        help='Clean log files and temporary data')
    parser_clean.add_argument('--dry-run', action='store_true',
        help='Show what would be removed without actually removing')
    parser_clean.add_argument('--show-content', action='store_true',
        help='Show log content before cleaning')

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return 1

    # Initialize CLI
    cli = BioFMICLI()

    # Dispatch to appropriate command handler
    commands = {
        'transform': cmd_transform,
        'build': cmd_build,
        'locate': cmd_locate,
        'stats': cmd_stats,
        'genpatterns': cmd_genpatterns,
        'clean': cmd_clean
    }

    return commands[args.command](args, cli)


if __name__ == "__main__":
    sys.exit(main())
