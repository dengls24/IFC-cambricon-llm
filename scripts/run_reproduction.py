#!/usr/bin/env python
"""Run the standalone Cambricon-LLM Figure 9 reproduction."""

from __future__ import annotations

import sys
from pathlib import Path


def main() -> int:
    project_root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(project_root / "src"))
    from ifc_cambricon_llm.cli import main as cli_main

    return cli_main(["--profiles", str(project_root / "data" / "cambricon_fig9_profiles.json"), "--output-dir", str(project_root / "results")])


if __name__ == "__main__":
    raise SystemExit(main())

