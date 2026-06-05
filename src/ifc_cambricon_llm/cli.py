"""Command line interface."""

from __future__ import annotations

import argparse
from pathlib import Path

from .profiles import DEFAULT_PROFILE_PATH, load_profiles
from .simulator import simulate_reproduction, write_outputs


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run Cambricon-LLM IFC Figure 9 reproduction.")
    parser.add_argument("--profiles", default=str(DEFAULT_PROFILE_PATH), help="Path to profile JSON.")
    parser.add_argument("--output-dir", default="results", help="Directory for reproduction outputs.")
    args = parser.parse_args(argv)

    profiles = load_profiles(args.profiles)
    rows, summary = simulate_reproduction(profiles)
    paths = write_outputs(rows, summary, Path(args.output_dir))

    print("passed: Cambricon-LLM Figure 9 reproduction")
    print(f"metrics_csv: {paths['metrics_csv']}")
    print(f"summary_json: {paths['summary_json']}")
    print(f"report_md: {paths['report_md']}")
    print(f"rows: {summary.row_count}")
    print(f"mean_abs_relative_error_pct: {summary.mean_abs_relative_error_pct:.3f}")
    print(f"max_abs_relative_error_pct: {summary.max_abs_relative_error_pct:.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

