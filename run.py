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
        """Execute a C++ tool with given arguments"""
        tool_path = self.cpp_tools_dir / tool_name

        if not tool_path.exists():
            print(f"Error: Tool '{tool_name}' not found at {tool_path}", file=sys.stderr)
            return 1

        try:
            result = subprocess.run([str(tool_path)] + args, check=False)
            return result.returncode
        except Exception as e:
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


def cmd_stats(args, cli: BioFMICLI) -> int:
    """Show statistics for EDS/l-EDS file"""
    cpp_args = ["-i", str(args.input)]

    if args.sources:
        cpp_args.extend(["-s", str(args.sources)])
    if args.full:
        cpp_args.append("--full")
    if args.json:
        cpp_args.append("--json")
    if args.verbose:
        cpp_args.append("--verbose")

    if not args.json:
        print(f"Computing statistics for {args.input}")
    return cli.run_cpp_tool("biofmi-stats", cpp_args)


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
        help='Show statistics for EDS/l-EDS file')
    parser_stats.add_argument('input', type=Path,
        help='Input EDS or l-EDS file')
    parser_stats.add_argument('-s', '--sources', type=Path,
        help='Source file (.seds) - optional')
    parser_stats.add_argument('-f', '--full', action='store_true',
        help='Use FULL mode (load all strings into RAM)')
    parser_stats.add_argument('-j', '--json', action='store_true',
        help='Output in JSON format')
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
        'genpatterns': cmd_genpatterns
    }

    return commands[args.command](args, cli)


if __name__ == "__main__":
    sys.exit(main())
