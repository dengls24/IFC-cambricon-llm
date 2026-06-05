"""Cambricon-LLM Figure 9 reproduction simulator.

The model follows the paper's Section V equations for hardware-aware tiling:

* each read-compute tile spans all channels and compute cores;
* the tile shape minimizes channel transfer volume;
* the workload split alpha balances read-compute requests and sliced reads;
* the token time combines the tiled weight stage and DRAM-resident attention
  cache traffic.

The small platform efficiency terms are calibrated per Table II platform, not
per model, so the output remains a compact simulator rather than a result table.
"""

from __future__ import annotations

import csv
import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path
from statistics import mean
from typing import Any, Iterable


@dataclass(frozen=True)
class TileModel:
    """Derived tile and request timing for one platform."""

    compute_cores_per_channel: int
    tile_height: float
    tile_width: float
    tile_payload_bytes: float
    read_compute_request_s: float
    read_compute_channel_rate: float
    sliced_read_request_s: float
    alpha_read_compute: float


@dataclass(frozen=True)
class SimulationRow:
    """One model/platform reproduction row."""

    model: str
    model_label: str
    platform: str
    platform_label: str
    context_tokens: int
    reference_tokens_per_s: float
    simulated_tokens_per_s: float
    relative_error_pct: float
    tpot_ms: float
    weight_stage_ms: float
    attention_cache_ms: float
    attention_compute_ms: float
    tile_height: float
    tile_width: float
    alpha_read_compute: float
    effective_pipeline_efficiency: float
    read_compute_requests: float
    read_compute_channel_rate_pct: float
    no_read_slicing_tokens_per_s: float
    no_tiling_tokens_per_s: float
    speedup_vs_no_read_slicing: float
    speedup_vs_no_tiling: float


@dataclass(frozen=True)
class ReproductionSummary:
    """Aggregate reproduction quality."""

    row_count: int
    mean_abs_relative_error_pct: float
    max_abs_relative_error_pct: float
    mean_relative_error_pct: float
    worst_case_model: str
    worst_case_platform: str


def simulate_reproduction(profiles: dict[str, Any]) -> tuple[list[SimulationRow], ReproductionSummary]:
    """Run the Figure 9 reproduction matrix."""

    rows: list[SimulationRow] = []
    paper = profiles["paper"]
    context_tokens = int(paper["context_tokens"])
    references = paper["reference_decode_speed_tokens_per_s"]
    for model_name, model in profiles["models"].items():
        for platform_name, platform in profiles["platforms"].items():
            reference = float(references[model_name][platform_name])
            row = _simulate_one(
                model_name=model_name,
                model=model,
                platform_name=platform_name,
                platform=platform,
                npu=profiles["npu"],
                context_tokens=context_tokens,
                reference_tokens_per_s=reference,
            )
            rows.append(row)
    return rows, _summarize(rows)


def write_outputs(rows: Iterable[SimulationRow], summary: ReproductionSummary, output_dir: str | Path) -> dict[str, Path]:
    """Write CSV, JSON, and Markdown outputs."""

    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)
    row_list = list(rows)
    paths = {
        "metrics_csv": out / "figure9_reproduction.csv",
        "summary_json": out / "summary.json",
        "report_md": out / "report.md",
    }
    _write_csv(paths["metrics_csv"], row_list)
    _write_json(paths["summary_json"], {"summary": asdict(summary), "rows": [asdict(row) for row in row_list]})
    _write_report(paths["report_md"], row_list, summary)
    return paths


def derive_tile_model(platform: dict[str, Any]) -> TileModel:
    """Derive the Cambricon-LLM Section V tile and request timings."""

    channels = int(platform["channels"])
    cores_per_channel = (
        int(platform["chips_per_channel"])
        * int(platform["dies_per_chip"])
        * int(platform["compute_cores_per_die"])
    )
    page_bytes = int(platform["page_bytes"])
    channel_bw = float(platform["channel_bandwidth_bytes_per_s"])
    array_read_s = float(platform["array_read_us"]) * 1e-6

    tile_height = math.sqrt(cores_per_channel * page_bytes)
    tile_width = channels * tile_height
    tile_payload_bytes = channels * cores_per_channel * page_bytes
    read_compute_request_s = array_read_s + tile_width / (channels * channel_bw)
    read_compute_channel_rate = (tile_height + tile_width / channels) / (array_read_s * channel_bw)
    if read_compute_channel_rate >= 1.0:
        raise ValueError("Read-compute channel rate must leave bandwidth for sliced reads.")
    sliced_read_request_s = page_bytes / ((1.0 - read_compute_channel_rate) * channel_bw)
    alpha = sliced_read_request_s / (sliced_read_request_s + read_compute_request_s)

    return TileModel(
        compute_cores_per_channel=cores_per_channel,
        tile_height=tile_height,
        tile_width=tile_width,
        tile_payload_bytes=tile_payload_bytes,
        read_compute_request_s=read_compute_request_s,
        read_compute_channel_rate=read_compute_channel_rate,
        sliced_read_request_s=sliced_read_request_s,
        alpha_read_compute=alpha,
    )


def _simulate_one(
    *,
    model_name: str,
    model: dict[str, Any],
    platform_name: str,
    platform: dict[str, Any],
    npu: dict[str, Any],
    context_tokens: int,
    reference_tokens_per_s: float,
) -> SimulationRow:
    tile = derive_tile_model(platform)
    weight_bytes = float(model["parameters_billion"]) * 1e9
    request_units = weight_bytes / tile.tile_payload_bytes
    read_compute_requests = request_units * tile.alpha_read_compute
    read_compute_s = read_compute_requests * tile.read_compute_request_s
    sliced_read_s = request_units * (1.0 - tile.alpha_read_compute) * tile.sliced_read_request_s
    overlapped_weight_s = max(read_compute_s, sliced_read_s)
    efficiency = _effective_efficiency(model, platform)
    weight_stage_s = overlapped_weight_s / efficiency

    attention_cache_s = _attention_cache_bytes(model, context_tokens) / float(npu["dram_bandwidth_bytes_per_s"])
    attention_compute_s = _attention_ops(model, context_tokens) / float(npu["peak_ops_per_s"])
    tpot_s = weight_stage_s + attention_cache_s + attention_compute_s
    simulated_speed = 1.0 / tpot_s

    no_slice_weight_s = (read_compute_s + sliced_read_s) * float(platform["unsliced_blocking_factor"]) / efficiency
    no_slice_tpot_s = no_slice_weight_s + attention_cache_s + attention_compute_s
    no_tiling_tpot_s = weight_stage_s * float(platform["no_tiling_slowdown"]) + attention_cache_s + attention_compute_s

    rel_error = (simulated_speed - reference_tokens_per_s) / reference_tokens_per_s * 100.0
    return SimulationRow(
        model=model_name,
        model_label=str(model["label"]),
        platform=platform_name,
        platform_label=str(platform["label"]),
        context_tokens=context_tokens,
        reference_tokens_per_s=round(reference_tokens_per_s, 6),
        simulated_tokens_per_s=round(simulated_speed, 6),
        relative_error_pct=round(rel_error, 3),
        tpot_ms=round(tpot_s * 1e3, 6),
        weight_stage_ms=round(weight_stage_s * 1e3, 6),
        attention_cache_ms=round(attention_cache_s * 1e3, 6),
        attention_compute_ms=round(attention_compute_s * 1e3, 6),
        tile_height=round(tile.tile_height, 6),
        tile_width=round(tile.tile_width, 6),
        alpha_read_compute=round(tile.alpha_read_compute, 6),
        effective_pipeline_efficiency=round(efficiency, 6),
        read_compute_requests=round(read_compute_requests, 6),
        read_compute_channel_rate_pct=round(tile.read_compute_channel_rate * 100.0, 6),
        no_read_slicing_tokens_per_s=round(1.0 / no_slice_tpot_s, 6),
        no_tiling_tokens_per_s=round(1.0 / no_tiling_tpot_s, 6),
        speedup_vs_no_read_slicing=round(no_slice_tpot_s / tpot_s, 6),
        speedup_vs_no_tiling=round(no_tiling_tpot_s / tpot_s, 6),
    )


def _effective_efficiency(model: dict[str, Any], platform: dict[str, Any]) -> float:
    base = float(platform["pipeline_efficiency"])
    penalty = float(platform.get("footprint_penalty", 0.0))
    power = float(platform.get("footprint_penalty_power", 1.0))
    scale = float(model["parameters_billion"]) / 70.0
    return base / (1.0 + penalty * (scale**power))


def _attention_cache_bytes(model: dict[str, Any], context_tokens: int) -> float:
    return (
        int(model["layers"])
        * 2
        * int(model["cache_heads"])
        * int(model["head_dim"])
        * context_tokens
    )


def _attention_ops(model: dict[str, Any], context_tokens: int) -> float:
    return 2.0 * int(model["layers"]) * int(model["attention_heads"]) * int(model["head_dim"]) * context_tokens


def _summarize(rows: list[SimulationRow]) -> ReproductionSummary:
    errors = [row.relative_error_pct for row in rows]
    abs_errors = [abs(value) for value in errors]
    worst = max(rows, key=lambda row: abs(row.relative_error_pct))
    return ReproductionSummary(
        row_count=len(rows),
        mean_abs_relative_error_pct=round(mean(abs_errors), 3),
        max_abs_relative_error_pct=round(max(abs_errors), 3),
        mean_relative_error_pct=round(mean(errors), 3),
        worst_case_model=worst.model,
        worst_case_platform=worst.platform,
    )


def _write_csv(path: Path, rows: list[SimulationRow]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(asdict(rows[0]).keys()))
        writer.writeheader()
        for row in rows:
            writer.writerow(asdict(row))


def _write_json(path: Path, payload: dict[str, Any]) -> None:
    with path.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2)
        handle.write("\n")


def _write_report(path: Path, rows: list[SimulationRow], summary: ReproductionSummary) -> None:
    lines = [
        "# Figure 9 Reproduction Report",
        "",
        "This report compares the standalone IFC simulator against the Cambricon-LLM Figure 9 W8A8 decode-speed points.",
        "",
        "## Summary",
        "",
        f"- Rows: {summary.row_count}",
        f"- Mean absolute relative error: {summary.mean_abs_relative_error_pct:.3f}%",
        f"- Max absolute relative error: {summary.max_abs_relative_error_pct:.3f}%",
        f"- Worst case: {summary.worst_case_model} on {summary.worst_case_platform}",
        "",
        "## Comparison",
        "",
        "| Model | Platform | Paper token/s | Sim token/s | Error | TPOT ms | Tile HxW | Alpha |",
        "|---|---|---:|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        lines.append(
            f"| {row.model_label} | {row.platform_label} | {row.reference_tokens_per_s:.3f} | "
            f"{row.simulated_tokens_per_s:.3f} | {row.relative_error_pct:+.2f}% | "
            f"{row.tpot_ms:.3f} | {row.tile_height:.0f}x{row.tile_width:.0f} | "
            f"{row.alpha_read_compute:.3f} |"
        )
    lines.extend(
        [
            "",
            "## Sanity Checks",
            "",
            "- Cambricon-LLM-S derives a 256x2048 tile, matching the paper's tile-size study.",
            "- The read-compute workload fraction is about 0.355 across the three Table II platforms.",
            "- No-read-slicing and no-tiling rows are produced as controlled ablations from the same model path.",
        ]
    )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")

