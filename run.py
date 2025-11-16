#!/usr/bin/env python3
"""
BIO-FMI CLI - Command-line interface for BIO-FMI indexing and querying
Orchestrates C++ tools for building and searching FM-indexes.

Note: For EDS format transformations, use the EDSParser tools:
  - edsparser-transform: Transform MSA/VCF/EDS to EDS/l-EDS
  - edsparser-stats: Show EDS statistics
  - edsparser-genpatterns: Generate test patterns
"""

import argparse
import subprocess
import sys
import os
from pathlib import Path
from typing import List
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
        log_file = Path(__file__).parent / 'cmd_history.log'
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


def cmd_clean(args, cli: BioFMICLI) -> int:
    """Clean log files and temporary data"""
    import os
    from pathlib import Path

    # Get project root directory
    project_dir = Path(__file__).parent
    log_file = project_dir / "cmd_history.log"

    # Track what was cleaned
    cleaned_items = []
    total_size = 0

    # Clean log file
    if log_file.exists():
        size = log_file.stat().st_size

        if args.show_content and size > 0:
            # Show log content before cleaning
            print("=== Current cmd_history.log content ===")
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
                cleaned_items.append(f"cmd_history.log ({size} bytes)")
                total_size += size
        else:
            if size > 0:
                print(f"[DRY RUN] Would remove: cmd_history.log ({size} bytes)")

    # Print summary
    if not args.dry_run:
        if cleaned_items:
            print(f"Cleaned {len(cleaned_items)} item(s) ({total_size} bytes total)")
            for item in cleaned_items:
                print(f"  - {item}")
        else:
            print("Nothing to clean")
    else:
        if log_file.exists() and log_file.stat().st_size > 0:
            print(f"[DRY RUN] Total size to clean: {total_size} bytes")
        else:
            print("[DRY RUN] Nothing to clean")

    return 0


def main():
    parser = argparse.ArgumentParser(
        description='BIO-FMI - FM-index for Elastic-Degenerate Strings',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  # Build index from l-EDS:
  biofmi build -i data.5.leds -l 5

  # Search patterns:
  biofmi locate -i data.5.leds.index -l 5 -p "ACGT"
  biofmi locate -i data.5.leds.index -l 5 -P patterns.txt

  # Clean logs:
  biofmi clean

Note: For EDS transformations, use EDSParser tools:
  edsparser-transform -i data.msa -l 5       # MSA → l-EDS
  edsparser-stats -i data.eds --sources=auto # Show statistics
  edsparser-genpatterns -i data.eds -o patterns.txt
        '''
    )

    parser.add_argument('--version', action='version', version='BioFMI 1.0.0')

    subparsers = parser.add_subparsers(dest='command', help='Available commands')

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
        'build': cmd_build,
        'locate': cmd_locate,
        'clean': cmd_clean
    }

    return commands[args.command](args, cli)


if __name__ == "__main__":
    sys.exit(main())
