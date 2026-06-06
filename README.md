# IFC Cambricon-LLM

Standalone C simulator for a Cambricon-LLM style in-flash-computing decode-speed reproduction.

The repository focuses on the method used in "Cambricon-LLM: A Chiplet-Based Hybrid Architecture for On-Device Inference of 70B LLM" for Figure 9:

- Table II flash configurations for Cambricon-LLM-S/M/L.
- A 16x16, 1 GHz, 2 TOPS INT8 NPU with 40 GB/s DRAM bandwidth.
- Section V hardware-aware tiling and read-compute/read-request workload split.
- SSDsim-style C flash controller state with channel/chip/die/plane busy timelines.
- Extended flash opcodes: `READ`, `WRITE`, `READ_COMPUTE`, and `READ_SLICE`.
- Figure 9 W8A8 decode-speed comparison for OPT and LLaMA2 models at 1K context.

It intentionally keeps the scope narrow: model weights are flash-resident, in-flash computing handles the tiled weight stage, and the attention cache remains in DRAM as in the Cambricon-LLM setup.

## Quick Start

```bash
make run
```

Expected output:

```text
passed: Cambricon-LLM Figure 9 C reproduction
rows: 21
mean_abs_relative_error_pct: ...
max_abs_relative_error_pct: ...
```

The command writes:

- `results/figure9_reproduction.csv`
- `results/summary.json`
- `results/report.md`
- `results/request_trace.csv`
- `results/controller_timing_summary.csv`
- `results/npu_timing.csv`
- `results/controller_schedule.csv`
- `results/ablation_summary.csv`
- `results/figure12_read_slice_ablation.csv`
- `results/figure14_tiling_ablation.csv`

## Current Reproduction Quality

The checked simulator uses a compact per-platform calibration, not per-model fitting. Current outputs:

- Rows: 21 Figure 9 points.
- Mean absolute relative error: 8.341%.
- Max absolute relative error: 14.618%.
- Worst case: LLaMA2-70B on Cambricon-LLM-L.

Run tests:

```bash
make test
```

## Method

The simulator derives the optimal tile shape from the paper's Section V formulation:

```text
H_req = sqrt(cores_per_channel * page_size)
W_req = channel_count * H_req
```

For Cambricon-LLM-S this gives `256 x 2048`, matching the tile-size study in the paper. The simulator then computes:

- read-compute request time from array read latency and input-vector transfer;
- read-compute channel occupancy;
- sliced read request time from the remaining channel bandwidth;
- workload fraction `alpha = t_read / (t_read + t_read_compute)`;
- overlapped tiled weight-stage time;
- DRAM attention-cache traffic and NPU attention arithmetic.

More detail is in [docs/method.md](docs/method.md). Results are summarized in [docs/results.md](docs/results.md).

## Repository Layout

```text
data/      Paper references and hardware/model profiles
docs/      Method and result notes
include/   Public C header
src/       C simulator and CLI
tests/     C smoke tests for formulas and reproduction bounds
results/   Reproduction outputs
```
