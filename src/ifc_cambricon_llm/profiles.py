"""Profile loading helpers."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PROFILE_PATH = PROJECT_ROOT / "data" / "cambricon_fig9_profiles.json"


def load_profiles(path: str | Path | None = None) -> dict[str, Any]:
    """Load the simulator profile bundle."""

    profile_path = Path(path) if path is not None else DEFAULT_PROFILE_PATH
    with profile_path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    _validate_payload(payload, profile_path)
    return payload


def _validate_payload(payload: dict[str, Any], path: Path) -> None:
    required = ["paper", "npu", "models", "platforms"]
    for section in required:
        if section not in payload:
            raise KeyError(f"Missing {section!r} in {path}")

